/* SPDX-License-Identifier: GPL-2.0-only */
/* Issue 114 T5/T6/T8/T9/T10. Dedicated VM only; no shared attribute validator.
 * plant LOWER before mounting; check LAYER LOWER q2|v1 afterwards.
 * The faults mode runs only under the pinned fault-injection KASAN guest.
 * Fixed bytes/expected membership come from spec-m2, not common/attr.c.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

#define PATHCAP 4096
#define HIDDEN 70
#define MUST(c, why) do { if (!(c)) { fprintf(stderr, "PERMISSION_GAPS_FAIL: %s (errno=%d)\n", why, errno); exit(1); } } while (0)
static const char *layer, *lower;
static int legacy, phase;
static struct moob_query_result results[128];

static void path(char *out, const char *root, const char *rel)
{
	int n = snprintf(out, PATHCAP, "%s/%s", root, rel);

	MUST(n > 0 && n < PATHCAP, "fixture path length");
}

static void directory(const char *root, const char *rel, mode_t mode)
{
	char p[PATHCAP];

	path(p, root, rel);
	MUST(!mkdir(p, mode), "exclusive fixture directory");
}

static void attribute(const char *root, const char *rel, const char *name,
		      const unsigned char *value, size_t size)
{
	char p[PATHCAP];

	path(p, root, rel);
	MUST(!setxattr(p, name, value, size, 0), "fixture attribute");
}

static void file(const char *root, const char *rel)
{
	char p[PATHCAP];
	int fd;

	path(p, root, rel);
	fd = open(p, O_CREAT | O_EXCL | O_WRONLY, 0644);
	MUST(fd >= 0 && !close(fd), "exclusive fixture file");
}

static void mode(const char *root, const char *rel, mode_t bits)
{
	char p[PATHCAP];

	path(p, root, rel);
	MUST(!chmod(p, bits), "fixture chmod");
}

static int open_dir(const char *root, const char *rel)
{
	char p[PATHCAP];
	int fd;

	path(p, root, rel);
	fd = open(p, O_RDONLY | O_DIRECTORY);
	MUST(fd >= 0, "open directory as actual caller");
	return fd;
}

static void fill(struct moob_query2 *q, const char *name, unsigned int limit)
{
	memset(q, 0, sizeof(*q));
	q->version = MOOB_QUERY2_VERSION;
	q->nterms = 1;
	q->limit = limit;
	q->results = (uint64_t)(uintptr_t)results;
	MUST(strlen(name) < sizeof(q->terms[0].name), "query name length");
	strcpy(q->terms[0].name, name);
	q->terms[0].op = MOOB_OP_EXISTS;
}

/* shape 0: plain; 1: equality intersection; 2/3: ordered in both directions. */
static unsigned int query(int fd, const char *name, int shape, unsigned int limit,
			  unsigned int *truncated)
{
	struct moob_query2 q;
	unsigned int i;

	fill(&q, name, limit);
	if (legacy) {
		struct moob_query old = {0};

		old.version = MOOB_UAPI_VERSION;
		old.nterms = 1;
		old.limit = limit;
		old.results = q.results;
		old.terms[0] = q.terms[0];
		MUST(!ioctl(fd, MOOB_IOC_QUERY, &old), "v1 query");
		q.count = old.count;
		q.truncated = old.truncated;
	} else {
		if (shape == 1) {
			q.nterms = 2;
			q.terms[0].op = MOOB_OP_EQ;
			q.terms[0].type = 4;
			q.terms[0].len = 1;
			q.terms[0].value[0] = 1;
			q.terms[1] = q.terms[0];
			strcpy(q.terms[1].name, "tag");
		} else if (shape >= 2) {
			strcpy(q.order_by, "rank");
			q.order_type = 2;
			q.order = shape == 2 ? MOOB_ORDER_ASC : MOOB_ORDER_DESC;
		}
		MUST(!ioctl(fd, MOOB_IOC_QUERY2, &q), "QUERY2");
	}
	MUST(q.count <= limit && q.truncated <= 1, "reply bounds");
	for (i = 0; i < q.count; i++)
		MUST(!results[i].flags && memchr(results[i].path, 0, sizeof(results[i].path)),
		     "complete terminated result path");
	*truncated = q.truncated;
	return q.count;
}

static void child(void (*check)(void))
{
	pid_t pid = fork();
	int status;

	MUST(pid >= 0, "fork");
	if (!pid) {
		MUST(!setgroups(0, NULL) && !setresgid(1001, 1001, 1001) &&
		     !setresuid(1001, 1001, 1001), "drop credentials");
		MUST(geteuid() == 1001 && getegid() == 1001, "actual unprivileged identity");
		check();
		_exit(0);
	}
	MUST(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status),
	     "unprivileged child verdict");
}

static void target(void)
{
	int noexec;

	for (noexec = 0; noexec < 2; noexec++) {
		const char *d = noexec ? "noexec" : "control";
		struct moob_query2 q;
		struct moob_query2_batch batch = {0};
		char p[PATHCAP];
		int fd = open_dir(layer, d), lowfd = open_dir(lower, d), r;

		path(p, lower, d);
		MUST(!faccessat(AT_FDCWD, p, R_OK, AT_EACCESS), "T5 lower readable");
		r = faccessat(AT_FDCWD, p, X_OK, AT_EACCESS);
		MUST(noexec ? r == -1 && errno == EACCES : r == 0, "T5 lower search control");
		fill(&q, "test", 8);
		errno = 0;
		r = ioctl(fd, MOOB_IOC_QUERY2, &q);
		MUST(noexec ? r == -1 && errno == EACCES : !r && q.count == 1,
		     "T5 QUERY2 execute check decisive after successful open");
		fill(&q, "test", 8);
		batch.version = MOOB_QUERY2_VERSION;
		batch.nqueries = 1;
		batch.queries = (uint64_t)(uintptr_t)&q;
		errno = 0;
		r = ioctl(fd, MOOB_IOC_QUERY2_BATCH, &batch);
		MUST(noexec ? r == -1 && errno == EACCES : !r && q.count == 1,
		     "T5 QUERY2_BATCH execute check decisive");
		MUST(!close(fd) && !close(lowfd), "close T5 descriptors");
	}
}

static void count_channel(void)
{
	unsigned int truncated, count;
	int shape, fd = open_dir(layer, legacy ? "" : "count");
	char p[PATHCAP];
	unsigned char value[8];

	path(p, lower, "count/private/h00");
	if (phase == 1)
		MUST(getxattr(p, "user.moob.count", value, sizeof(value)) == 2, "T6 readable control");
	else
		MUST(getxattr(p, "user.moob.count", value, sizeof(value)) < 0 && errno == EACCES,
		     "T6 lower denies hidden candidate");
	for (shape = 0; shape < (legacy ? 1 : 4); shape++) {
		count = query(fd, "count", shape, 1, &truncated);
		MUST(count == (phase == 2 ? 0U : 1U), "T6 visible count");
		MUST(truncated == (phase == 1 ? 1U : 0U), "T6 hidden candidates do not set truncated");
	}
	MUST(!close(fd), "close T6 descriptor");
}

static void counts(void)
{
	unsigned int truncated;
	int fd;

	if (legacy) {
		mode(layer, "count/public", 0755);
		mode(layer, "count/private", 0755);
	}
	fd = open_dir(layer, legacy ? "" : "count");
	MUST(query(fd, "count", 0, 128, &truncated) == HIDDEN + 1 && !truncated,
	     "T6 warm index includes all future hidden candidates");
	MUST(!close(fd), "close warm descriptor");
	mode(layer, "count/private", 0700);
	for (phase = 0; phase < 3; phase++) {
		if (phase == 1)
			mode(layer, "count/private", 0755);
		if (phase == 2) {
			mode(layer, "count/private", 0700);
			mode(layer, "count/public", 0700);
		}
		child(count_channel);
	}
}

static const char *const dirs[] = { "public", "read-no-search", "no-read", "private", "branch/leaf" };
static void directory_answers(void)
{
	unsigned int count, truncated, i, j, expected = 0;
	int fd = open_dir(layer, "dirs");

	count = query(fd, "test", 0, 128, &truncated);
	MUST(!truncated, "T8 complete directory reply");
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		char p[PATHCAP], rel[128];
		unsigned char value[8];
		int readable, seen = 0;

		snprintf(rel, sizeof(rel), "dirs/%s", dirs[i]);
		path(p, lower, rel);
		readable = getxattr(p, "user.moob.test", value, sizeof(value)) == 2;
		MUST(readable || errno == EACCES, "T8 lower permission decision");
		for (j = 0; j < count; j++)
			seen += !strcmp(results[j].path, dirs[i]);
		MUST(seen == readable, "T8 attributed directory differs from lower getxattr");
		expected += readable;
	}
	MUST(count == expected, "T8 no extra or duplicate directories");
	MUST(!close(fd), "close T8 descriptor");
}

struct encoding { const char *rel; unsigned char bytes[10]; size_t size; int indexed; mode_t mode; uid_t uid; gid_t gid; };
static const struct encoding encodings[] = {
	{ "empty-string", {1}, 1, 1, 0644, 0, 0 },
	{ "unicode", {1, 0xc3, 0xa9}, 3, 1, 0644, 0, 0 },
	{ "true", {4, 1}, 2, 1, 0644, 0, 0 },
	{ "false", {4, 0}, 2, 1, 0644, 0, 0 },
	{ "integer", {2, 1, 0, 0, 0, 0, 0, 0, 0}, 9, 1, 0644, 0, 0 },
	{ "time", {5, 0, 0, 0, 0, 0, 0, 0, 0}, 9, 1, 0644, 0, 0 },
	{ "bad-bool", {4, 2}, 2, 0, 0644, 0, 0 },
	{ "bad-utf8", {1, 0xff}, 2, 0, 0644, 0, 0 },
	{ "nul-string", {1, 0}, 2, 0, 0644, 0, 0 },
	{ "short-int", {2, 0}, 2, 0, 0644, 0, 0 },
	{ "nan", {3, 0, 0, 0, 0, 0, 0, 0xf8, 0x7f}, 9, 0, 0644, 0, 0 },
	{ "raw", {6, 0}, 2, 0, 0644, 0, 0 },
	{ "no-type", {0}, 0, 0, 0644, 0, 0 },
	{ "unknown-type", {255}, 1, 0, 0644, 0, 0 },
	{ "private", {4, 1}, 2, 1, 0600, 0, 0 },
	{ "owner-denied", {4, 1}, 2, 1, 0004, 1001, 1001 },
	{ "group-denied", {4, 1}, 2, 1, 0404, 0, 1001 },
	{ "closed/valid", {4, 1}, 2, 1, 0644, 0, 0 },
};

static void encoding_answers(void)
{
	unsigned int i, j, count, truncated, expected = 0;
	int fd = open_dir(layer, legacy ? "" : "enc");

	count = query(fd, "probe", 0, 128, &truncated);
	MUST(!truncated, "T10 complete encoding reply");
	for (i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++) {
		const struct encoding *e = &encodings[i];
		char p[PATHCAP], rel[128];
		unsigned char value[16];
		ssize_t size;
		int readable, wanted, seen = 0;

		snprintf(rel, sizeof(rel), "enc/%s", e->rel);
		path(p, lower, rel);
		size = getxattr(p, "user.moob.probe", value, sizeof(value));
		readable = size >= 0;
		MUST(readable || errno == EACCES, "T10 lower permission decision");
		if (readable)
			MUST((size_t)size == e->size && !memcmp(value, e->bytes, e->size), "T10 fixed bytes retained");
		wanted = e->indexed && readable;
		for (j = 0; j < count; j++)
			seen += !strcmp(results[j].path, legacy ? rel : e->rel);
		MUST(seen == wanted, "T10 independent encoding/permission membership");
		expected += wanted;
	}
	MUST(count == expected && expected == 6, "T10 known positive controls and no extras");
	MUST(!close(fd), "close T10 descriptor");
}

static void plant(void)
{
	static const unsigned char text[] = {1, 'v'}, boolean[] = {4, 1};
	unsigned int i;
	char rel[128], p[PATHCAP];

	umask(0);
	directory(lower, "control", 0755);
	directory(lower, "noexec", 0444);
	file(lower, "control/f"); file(lower, "noexec/f");
	attribute(lower, "control/f", "user.moob.test", text, sizeof(text));
	attribute(lower, "noexec/f", "user.moob.test", text, sizeof(text));
	directory(lower, "count", 0755);
	directory(lower, "count/public", 0755);
	directory(lower, "count/private", 0755);
	for (i = 0; i <= HIDDEN; i++) {
		unsigned char rank[] = {2, (unsigned char)i, 0, 0, 0, 0, 0, 0, 0};

		if (!i) strcpy(rel, "count/public/visible");
		else snprintf(rel, sizeof(rel), "count/private/h%02u", i - 1);
		file(lower, rel);
		attribute(lower, rel, "user.moob.count", boolean, sizeof(boolean));
		attribute(lower, rel, "user.moob.tag", boolean, sizeof(boolean));
		attribute(lower, rel, "user.moob.rank", rank, sizeof(rank));
	}
	directory(lower, "dirs", 0755);
	directory(lower, "dirs/branch", 0755);
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		static const mode_t bits[] = {0755, 0444, 0111, 0700, 0755};

		snprintf(rel, sizeof(rel), "dirs/%s", dirs[i]);
		directory(lower, rel, bits[i]);
		attribute(lower, rel, "user.moob.test", text, sizeof(text));
	}
	directory(lower, "enc", 0755);
	directory(lower, "enc/closed", 0700);
	for (i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++) {
		const struct encoding *e = &encodings[i];

		snprintf(rel, sizeof(rel), "enc/%s", e->rel);
		file(lower, rel);
		attribute(lower, rel, "user.moob.probe", e->bytes, e->size);
		path(p, lower, rel);
		MUST(!chown(p, e->uid, e->gid), "T10 fixture ownership");
		mode(lower, rel, e->mode);
	}
}

static unsigned long long release_count(void)
{
	struct moob_info info = {0};
	int fd = open_dir(layer, "");

	MUST(!ioctl(fd, MOOB_IOC_GET_INFO, &info) && !close(fd), "release counters");
	return info.domain_releases;
}

static void release_permissions(void)
{
	unsigned char value[8];
	unsigned int count, truncated;
	char p[PATHCAP];
	int fd = open_dir(layer, "r000");

	path(p, lower, "r000/release-perm/visible");
	MUST(getxattr(p, "user.moob.t9", value, sizeof(value)) == 2, "T9 release visible lower control");
	path(p, lower, "r000/release-perm/private/hidden");
	MUST(getxattr(p, "user.moob.t9", value, sizeof(value)) < 0 && errno == EACCES,
	     "T9 release hidden lower control");
	count = query(fd, "t9", 0, 8, &truncated);
	MUST(count == 1 && !truncated && !strcmp(results[0].path, "release-perm/visible"),
	     "T9 unprivileged release/rebuild permission answer");
	MUST(!close(fd), "close release query");
}

/* Called after the existing release step's root-only assertions, on its r000 domain. */
static void release_check(void)
{
	static const unsigned char boolean[] = {4, 1};
	unsigned int truncated;
	unsigned long long before, after;
	int fd;

	directory(layer, "r000/release-perm", 0755);
	directory(layer, "r000/release-perm/private", 0755);
	file(layer, "r000/release-perm/visible");
	file(layer, "r000/release-perm/private/hidden");
	attribute(layer, "r000/release-perm/visible", "user.moob.t9", boolean, sizeof(boolean));
	attribute(layer, "r000/release-perm/private/hidden", "user.moob.t9", boolean, sizeof(boolean));
	fd = open_dir(layer, "r000");
	MUST(query(fd, "t9", 0, 8, &truncated) == 2 && !truncated && !close(fd), "T9 release warm control");
	mode(layer, "r000/release-perm/private", 0700);
	child(release_permissions);
	before = release_count();
	sync();
	fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
	MUST(fd >= 0 && write(fd, "2\n", 2) == 2 && !close(fd), "T9 invoke shrinkers");
	after = release_count();
	MUST(after > before, "T9 a real index release occurred");
	child(release_permissions);
	child(release_permissions);
	printf("UNPRIV_RELEASE_PASS: uid=1001, before release and after rebuild, releases=%llu\n", after - before);
}

struct fault_result { int hit, error; };

/* Each child owns its fail-nth control descriptor. Opening it before dropping uid does not
 * retain privilege for QUERY2: the domain fd is opened after all uid/gid slots are dropped.
 * Linux 6.12.112 fs/proc/base.c proc_fail_nth_write has no write-time capability requirement.
 */
static struct fault_result fault_trial(const char *domain, int nth, int shape)
{
	int pipefd[2], status;
	struct fault_result result;
	pid_t pid;

	MUST(!pipe(pipefd), "fault result pipe");
	pid = fork();
	MUST(pid >= 0, "fault fork");
	if (!pid) {
		struct moob_query2 q;
		struct moob_query2_batch batch = {0};
		char control[32], p[PATHCAP], rel[128];
		unsigned char value[8];
		int ctl = open("/proc/thread-self/fail-nth", O_RDWR), fd, n, r, saved;
		unsigned int truncated;

		close(pipefd[0]);
		MUST(ctl >= 0, "own thread fail-nth descriptor");
		MUST(!setgroups(0, NULL) && !setresgid(1001, 1001, 1001) &&
		     !setresuid(1001, 1001, 1001), "fault child drop credentials");
		MUST(geteuid() == 1001 && getegid() == 1001, "fault query actual identity");
		fd = open_dir(layer, domain);
		fill(&q, "test", 1);
		if (shape == 1) {
			strcpy(q.order_by, "rank"); q.order_type = 2; q.order = MOOB_ORDER_ASC;
		}
		batch.version = MOOB_QUERY2_VERSION;
		batch.nqueries = 1;
		batch.queries = (uint64_t)(uintptr_t)&q;
		n = snprintf(control, sizeof(control), "%d", nth);
		MUST(pwrite(ctl, control, (size_t)n, 0) == n, "arm child fail-nth");
		errno = 0;
		r = ioctl(fd, shape == 2 ? MOOB_IOC_QUERY2_BATCH : MOOB_IOC_QUERY2,
			  shape == 2 ? (void *)&batch : (void *)&q);
		saved = errno;
		memset(control, 0, sizeof(control));
		MUST(pread(ctl, control, sizeof(control) - 1, 0) > 0, "read child fail-nth");
		result.hit = !strtoul(control, NULL, 10);
		MUST(pwrite(ctl, "0", 1, 0) == 1, "disarm child fail-nth");
		result.error = r ? saved : 0;
		if (r)
			MUST(result.hit && (saved == ENOMEM || saved == ENOSPC || saved == EIO ||
			     saved == EAGAIN || saved == EINTR || saved == ENOBUFS), "explained unprivileged fault error");
		else {
			if (q.count != 1 || q.truncated || strcmp(results[0].path, "visible"))
				fprintf(stderr, "T9_DIAG: domain=%s nth=%d shape=%d hit=%d count=%u truncated=%u path=%s\n",
					domain, nth, shape, result.hit, q.count, q.truncated, results[0].path);
			MUST(q.count == 1 && !q.truncated && !strcmp(results[0].path, "visible"),
			     "T9 faulted success respects permissions and truncation");
		}
		snprintf(rel, sizeof(rel), "%s/visible", domain); path(p, lower, rel);
		MUST(getxattr(p, "user.moob.test", value, sizeof(value)) == 2, "fault visible lower control");
		snprintf(rel, sizeof(rel), "%s/private/hidden", domain); path(p, lower, rel);
		MUST(getxattr(p, "user.moob.test", value, sizeof(value)) < 0 && errno == EACCES,
		     "fault hidden lower control");
		MUST(query(fd, "test", 0, 1, &truncated) == 1 && !truncated &&
		     !strcmp(results[0].path, "visible"), "T9 unprivileged retry after fault");
		MUST(!close(fd) && !close(ctl), "close fault descriptors");
		MUST(write(pipefd[1], &result, sizeof(result)) == sizeof(result), "fault result write");
		_exit(0);
	}
	close(pipefd[1]);
	MUST(read(pipefd[0], &result, sizeof(result)) == sizeof(result), "fault result read");
	MUST(!close(pipefd[0]) && waitpid(pid, &status, 0) == pid &&
	     WIFEXITED(status) && !WEXITSTATUS(status), "fault child verdict");
	return result;
}

static void faults(void)
{
	static const unsigned char text[] = {1, 'v'}, rank[] = {2, 0, 0, 0, 0, 0, 0, 0, 0};
	int fresh, shape, calls = 0, total = 0;

	umask(0);
	for (fresh = 0; fresh < 2; fresh++) for (shape = 0; shape < 3; shape++) {
		int n, hits = 0;

		for (n = 1; n <= 256; n++) {
			char domain[64], rel[128];
			struct fault_result result;
			unsigned int truncated;
			int fd;

			snprintf(domain, sizeof(domain), "uf-%d-%d-%03d", fresh, shape, n);
			directory(layer, domain, 0755);
			snprintf(rel, sizeof(rel), "%s/private", domain); directory(layer, rel, 0755);
			snprintf(rel, sizeof(rel), "%s/visible", domain); file(layer, rel);
			attribute(layer, rel, "user.moob.test", text, sizeof(text));
			attribute(layer, rel, "user.moob.rank", rank, sizeof(rank));
			snprintf(rel, sizeof(rel), "%s/private/hidden", domain); file(layer, rel);
			attribute(layer, rel, "user.moob.test", text, sizeof(text));
			attribute(layer, rel, "user.moob.rank", rank, sizeof(rank));
			if (!fresh) {
				fd = open_dir(layer, domain);
				MUST(query(fd, "test", 0, 8, &truncated) == 2 && !truncated && !close(fd), "fault warm control");
			}
			snprintf(rel, sizeof(rel), "%s/private", domain); mode(layer, rel, 0700);
			result = fault_trial(domain, n, shape);
			calls++;
			if (!result.hit) break;
			hits++; total++;
		}
		MUST(n <= 256 && hits > 0, "unprivileged fault sweep terminates and injects real faults");
		printf("UNPRIV_FAULT_CASE_PASS: uid=1001 fresh=%d shape=%d calls=%d faults=%d\n", fresh, shape, n, hits);
	}
	printf("UNPRIV_FAULT_PASS: uid=1001 operations=6 calls=%d faults=%d unexpected=0\n", calls, total);
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	if (argc == 3 && !strcmp(argv[1], "plant")) {
		lower = argv[2]; plant(); return 0;
	}
	if (argc == 4 && (!strcmp(argv[1], "release") || !strcmp(argv[1], "faults"))) {
		layer = argv[2]; lower = argv[3];
		if (!strcmp(argv[1], "release")) release_check(); else faults();
		return 0;
	}
	MUST(argc == 5 && !strcmp(argv[1], "check"), "usage: permission-gaps plant LOWER | check LAYER LOWER q2|v1 | release|faults LAYER LOWER");
	layer = argv[2]; lower = argv[3]; legacy = !strcmp(argv[4], "v1");
	MUST(legacy || !strcmp(argv[4], "q2"), "known query mode");
	if (!legacy) {
		child(target);
		puts("TARGET_EXEC_PASS: uid=1001 opens readable domains; QUERY2 and BATCH enforce search");
	}
	counts();
	printf("TRUNCATED_PERMISSION_PASS: %s, uid=1001, 70 hidden candidates, 0/1/many visible controls\n", legacy ? "v1" : "QUERY2 plain/intersection/ordered asc/desc");
	if (!legacy) {
		child(directory_answers);
		mode(layer, "dirs/branch", 0700);
		child(directory_answers);
		puts("ATTRIBUTED_DIRECTORY_PASS: uid=1001, lower getxattr decisions before/after fallback");
	}
	child(encoding_answers);
	printf("INDEPENDENT_ENCODING_PASS: %s, 18 fixed fixtures, uid=1001, no shared validator\n", legacy ? "v1" : "QUERY2");
	return 0;
}
