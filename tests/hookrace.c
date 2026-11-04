// SPDX-License-Identifier: GPL-2.0-only
/*
 * hookrace LAYER DIR rename|unlink
 *
 * Regression tests for the two findings of the TLA+ model of the index loader (formal/README.md,
 * journal 0266). Both are completeness defects: a QUERY2 left a file out.
 *
 * rename (F1): during the first build of the domain DIR, a typed attribute y is written to the
 *   file a, which already has x, and a is renamed to b. The scan finds the name changed and
 *   leaves the file to the rename hook, which is made to wait (rename_hook_delay_us) until the
 *   build has ended. The hook must still read the file whole: a query on x must return it.
 *
 * unlink (F2): a file has two names, n1 in DIR and n2 in DIR/sub, so that the scan reads n1 and
 *   then n2. While the scan reads it through n2 (scan_delay_us), n2 is unlinked. The record must
 *   not keep the removed name: once the dentry cache is dropped, the mount-wide QUERY, which
 *   checks each result by its recorded name, must return it. (With two names the file is
 *   heterogeneous, and QUERY2 leaves it out in any case.)
 *
 * LAYER is the mounted layer (domain_depth=1); DIR a directory directly under it that no query
 * has touched since the mount, and that does not exist yet. Runs as root: it writes the module
 * parameters and drops the dentry cache. Prints HOOKRACE_PASS and exits 0 only if the check
 * holds, HOOKRACE_FAIL: <why> and exits 1 otherwise.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

#define PARAMS "/sys/module/moobfs_layer/parameters/"

static const char *mode;
static const char *layer_root;

static void fail(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void param(const char *name, unsigned int v);

static void fail(const char *fmt, ...)
{
	va_list ap;
	int e = errno;

	param("scan_delay_us", 0);
	param("rename_hook_delay_us", 0);
	fprintf(stderr, "HOOKRACE_FAIL: [%s] ", mode);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, " (errno=%d %s)\n", e, strerror(e));
	exit(1);
}

static void param(const char *name, unsigned int v)
{
	char p[256];
	FILE *f;

	snprintf(p, sizeof p, PARAMS "%s", name);
	f = fopen(p, "w");
	if (!f || fprintf(f, "%u\n", v) < 0 || fclose(f)) {
		fprintf(stderr, "HOOKRACE_FAIL: [%s] cannot write %s\n", mode, p);
		exit(1);
	}
}

static void join(char *out, size_t n, const char *a, const char *b)
{
	int r = snprintf(out, n, "%s/%s", a, b);

	if (r < 0 || (size_t)r >= n)
		fail("path too long");
}

static void ms(unsigned int n)
{
	struct timespec ts = { n / 1000, (long)(n % 1000) * 1000000L };

	while (nanosleep(&ts, &ts) && errno == EINTR)
		;
}

static void set_str(const char *path, const char *attr, const char *val)
{
	unsigned char v[1 + 64];
	char name[128];
	size_t n = strlen(val);

	v[0] = MOOB_T_STRING;
	memcpy(v + 1, val, n);
	snprintf(name, sizeof name, MOOB_ATTR_PREFIX "%s", attr);
	if (setxattr(path, name, v, 1 + n, 0))
		fail("setxattr %s on %s", name, path);
}

static void make_file(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);

	if (fd < 0)
		fail("create %s", path);
	close(fd);
	if (chmod(path, 0644))
		fail("chmod %s", path);
}

static uint64_t ino_of(const char *path)
{
	struct stat st;

	if (lstat(path, &st))
		fail("lstat %s", path);
	return st.st_ino;
}

static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];

/* Whether a QUERY2 on DIR for "attr exists" returns ino. */
static int query_has(const char *dir, const char *attr, uint64_t ino)
{
	struct moob_query2 q;
	__u32 k;
	int fd, found = 0;

	fd = open(dir, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		fail("open %s", dir);
	memset(&q, 0, sizeof q);
	q.version = MOOB_QUERY2_VERSION;
	q.results = (__u64)(uintptr_t)res;
	q.limit = MOOB_QUERY_MAX_LIMIT;
	q.nterms = 1;
	strcpy(q.terms[0].name, attr);
	q.terms[0].op = MOOB_OP_EXISTS;
	if (ioctl(fd, MOOB_IOC_QUERY2, &q))
		fail("QUERY2 %s on %s", attr, dir);
	close(fd);
	for (k = 0; k < q.count; k++)
		found |= res[k].ino == ino;
	return found;
}

/*
 * Whether the mount-wide QUERY (v1, root only when domains are on) for "attr exists" returns
 * ino. A file with two names is heterogeneous and QUERY2 leaves it out by design (spec section
 * 3); QUERY checks every result for the caller by resolving its recorded name, and leaves the
 * file out when that name no longer exists. With show set, prints what it returned.
 */
static int query1_has(const char *attr, uint64_t ino, int show)
{
	struct moob_query q;
	__u32 k;
	int fd, found = 0;

	fd = open(layer_root, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		fail("open %s", layer_root);
	memset(&q, 0, sizeof q);
	q.version = MOOB_UAPI_VERSION;
	q.results = (__u64)(uintptr_t)res;
	q.limit = MOOB_QUERY_MAX_LIMIT;
	q.nterms = 1;
	strcpy(q.terms[0].name, attr);
	q.terms[0].op = MOOB_OP_EXISTS;
	if (ioctl(fd, MOOB_IOC_QUERY, &q))
		fail("QUERY %s on %s", attr, layer_root);
	close(fd);
	for (k = 0; k < q.count; k++) {
		found |= res[k].ino == ino;
		if (show)
			fprintf(stderr, "hookrace: result ino=%llu flags=%#x path=%.*s\n",
				(unsigned long long)res[k].ino, res[k].flags,
				(int)res[k].path_len, res[k].path);
	}
	if (show)
		fprintf(stderr, "hookrace: %u results, looking for ino=%llu\n", q.count,
			(unsigned long long)ino);
	return found;
}

/* A child process makes the first query on dir, which builds the domain. */
static pid_t build_in_child(const char *dir)
{
	pid_t pid = fork();

	if (pid < 0)
		fail("fork");
	if (!pid) {
		int fd = open(dir, O_RDONLY | O_DIRECTORY);
		struct moob_query2 q;

		memset(&q, 0, sizeof q);
		q.version = MOOB_QUERY2_VERSION;
		q.results = (__u64)(uintptr_t)res;
		q.limit = MOOB_QUERY_MAX_LIMIT;
		q.nterms = 1;
		strcpy(q.terms[0].name, "x");
		q.terms[0].op = MOOB_OP_EXISTS;
		_exit(fd < 0 || ioctl(fd, MOOB_IOC_QUERY2, &q) ? 1 : 0);
	}
	return pid;
}

static void reap(pid_t pid)
{
	int st;

	if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st))
		fail("the building query failed");
	param("scan_delay_us", 0);
	param("rename_hook_delay_us", 0);
}

static void drop_caches(void)
{
	FILE *f;

	sync();
	f = fopen("/proc/sys/vm/drop_caches", "w");
	if (!f || fprintf(f, "2\n") < 0 || fclose(f))
		fail("drop_caches");
}

static void test_rename(const char *dir)
{
	char a[4096], b[4096];
	uint64_t ino;
	pid_t pid;
	int st;

	join(a, sizeof a, dir, "a");
	join(b, sizeof b, dir, "b");
	make_file(a);
	set_str(a, "x", "1");	/* the domain is not live: only the lower holds it */
	ino = ino_of(a);
	param("scan_delay_us", 300000);
	param("rename_hook_delay_us", 1500000);
	pid = build_in_child(dir);
	ms(100);		/* the scan is reading a, and sleeps for 300 ms */
	set_str(a, "y", "1");	/* a record with y only; the scan will read again */
	if (rename(a, b))	/* the hook waits 1.5 s: the build ends meanwhile */
		fail("rename %s %s", a, b);
	/* otherwise the window was missed, and the test proves nothing */
	if (waitpid(pid, &st, WNOHANG) != pid)
		fail("the build had not ended when the rename hook ran");
	param("scan_delay_us", 0);
	param("rename_hook_delay_us", 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st))
		fail("the building query failed");
	if (!query_has(dir, "y", ino))
		fail("a query on y does not return the renamed file");
	if (!query_has(dir, "x", ino))
		fail("a query on x does not return the renamed file: its record holds only y");
}

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* dir/n1 with x, and a second name dir/sub/n2: the scan reads n1, then sub, then n2, last. */
static uint64_t make_pair(const char *dir, char *n1, char *n2)
{
	char sub[4096];

	join(sub, 4096, dir, "sub");
	join(n1, 4096, dir, "n1");
	join(n2, 4096, sub, "n2");
	if (mkdir(sub, 0755) || chmod(sub, 0755))
		fail("mkdir %s", sub);
	make_file(n1);
	set_str(n1, "x", "1");
	if (link(n1, n2))
		fail("link %s %s", n1, n2);
	return ino_of(n1);
}

#define UL_DELAY_MS 500

static void test_unlink(const char *dir)
{
	char cal[4096], n1[4096], n2[4096];
	long long t0, built;
	uint64_t ino;
	pid_t pid;

	/*
	 * A domain like dir, built first to time the build: the reading of n2 is the last one, so
	 * it takes the last UL_DELAY_MS of the build. The unlink then lands in the middle of it.
	 */
	if (snprintf(cal, sizeof cal, "%s-cal", dir) >= (int)sizeof cal)
		fail("path too long");
	if (mkdir(cal, 0755) || chmod(cal, 0755))
		fail("mkdir %s", cal);
	make_pair(cal, n1, n2);
	ino = make_pair(dir, n1, n2);
	param("scan_delay_us", UL_DELAY_MS * 1000);
	t0 = now_ms();
	reap(build_in_child(cal));
	built = now_ms() - t0;
	if (built < UL_DELAY_MS)
		fail("the timing build took %lld ms, less than one reading", built);
	param("scan_delay_us", UL_DELAY_MS * 1000);
	pid = build_in_child(dir);
	ms((unsigned int)(built - UL_DELAY_MS / 2));
	if (unlink(n2))		/* during the reading through n2 */
		fail("unlink %s", n2);
	reap(pid);
	if (!query1_has("x", ino, 0)) {
		query1_has("x", ino, 1);
		fail("QUERY on x does not return the file while it is cached");
	}
	drop_caches();		/* the answer now comes from the recorded name */
	if (!query1_has("x", ino, 0)) {
		query1_has("x", ino, 1);
		fail("QUERY on x does not return the file: its record names the removed n2");
	}
}

int main(int argc, char **argv)
{
	char dir[4096];

	if (argc != 4) {
		fprintf(stderr, "usage: hookrace LAYER DIR rename|unlink\n");
		return 2;
	}
	mode = argv[3];
	layer_root = argv[1];
	join(dir, sizeof dir, argv[1], argv[2]);
	if (mkdir(dir, 0755) || chmod(dir, 0755))
		fail("mkdir %s", dir);
	if (!strcmp(mode, "rename"))
		test_rename(dir);
	else if (!strcmp(mode, "unlink"))
		test_unlink(dir);
	else
		fail("unknown mode");
	printf("HOOKRACE_PASS: %s\n", mode);
	return 0;
}
