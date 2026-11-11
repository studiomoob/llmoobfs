/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fallbackfault: finding E of journal 0083 (Part B of docs/review-findings.md; journal 0283).
 * A change of a directory's permissions inside a domain notes the domain for per-result checks
 * (moobl_index_perm_changed, xa_store into ix->fallback). When that store fails for lack of
 * memory, the layer must close, not open: it sets fallback_all, so every domain of the mount is
 * answered with per-result checks until the next mount.
 *
 *   fallbackfault <fresh directory below a domain_depth=1 layer> <0|1>
 *
 * The directory becomes a domain with a readable file (top) and a subdirectory (sub) holding a
 * second one. A first QUERY2 as uid 1001 builds the domain and returns both. Then root changes
 * sub to 0700 and uid 1001 queries again: sub's file must be gone, top must stay.
 * With 1, every slab allocation made by this task with moobl_index_perm_changed() on its stack
 * fails during the chmod (failslab, task filter, stack filter on that function's addresses, the
 * kernel's Documentation/fault-injection/fault-injection.rst), so both allocations of the
 * xa_store (GFP_NOWAIT, then GFP_KERNEL_ACCOUNT in __xas_nomem) fail. The chmod itself has
 * already reached the lower and must succeed; failslab's "times" counter must show at least one
 * injected failure, or the run proves nothing and fails.
 * Root in a dedicated VM only (KASAN guest: vm/kasan-guest.sh step fallbackfault). Each run
 * needs a fresh mount: fallback_all stays set until unmount.
 */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

#define FS "/sys/kernel/debug/failslab/"
#define TIMES 100000L

static void die(const char *s) { perror(s); exit(2); }

static void put(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	size_t len = strlen(value);

	if (fd < 0 || write(fd, value, len) != (ssize_t)len)
		die(path);
	close(fd);
}

static long get(const char *path)
{
	char b[32] = "";
	int fd = open(path, O_RDONLY);

	if (fd < 0 || read(fd, b, sizeof b - 1) < 0)
		die(path);
	close(fd);
	return strtol(b, NULL, 10);
}

static void file(const char *path)
{
	int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);

	if (fd < 0 || write(fd, "x", 1) != 1)
		die(path);
	close(fd);
	if (setxattr(path, "user.moob.kind", "\1note", 5, 0))
		die("attribute");
}

/* QUERY2 kind exists on dom as uid 1001; bit 0: top answered, bit 1: sub/f answered */
static int seen_by_1001(const char *dom, uint64_t top, uint64_t subf)
{
	pid_t pid = fork();
	int st;

	if (pid < 0)
		die("fork");
	if (!pid) {
		static struct moob_query_result r[16];
		struct moob_query2 q;
		int fd, seen = 0;
		unsigned int i;

		if (setgroups(0, NULL) || setresgid(1001, 1001, 1001) || setresuid(1001, 1001, 1001))
			_exit(100);
		fd = open(dom, O_RDONLY | O_DIRECTORY);
		if (fd < 0)
			_exit(101);
		memset(&q, 0, sizeof q);
		q.version = MOOB_QUERY2_VERSION;
		q.nterms = 1;
		q.limit = 16;
		q.results = (uint64_t)(uintptr_t)r;
		strcpy(q.terms[0].name, "kind");
		q.terms[0].op = MOOB_OP_EXISTS;
		if (ioctl(fd, MOOB_IOC_QUERY2, &q))
			_exit(102);
		for (i = 0; i < q.count; i++) {
			if (r[i].ino == top)
				seen |= 1;
			else if (r[i].ino == subf)
				seen |= 2;
			else
				_exit(103);
		}
		_exit(seen);
	}
	if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st) > 3) {
		fprintf(stderr, "query as uid 1001: status %d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
		exit(2);
	}
	return WEXITSTATUS(st);
}

/* [start, end) of moobl_index_perm_changed: its address and the next symbol's */
static void range(unsigned long long *start, unsigned long long *end)
{
	char line[512], symbol[256], type;
	unsigned long long addr;
	FILE *fp = fopen("/proc/kallsyms", "r");

	if (!fp)
		die("kallsyms");
	*start = 0;
	*end = ~0ULL;
	while (fgets(line, sizeof line, fp))
		if (sscanf(line, "%llx %c %255s", &addr, &type, symbol) == 3 &&
		    !strcmp(symbol, "moobl_index_perm_changed"))
			*start = addr;
	rewind(fp);
	while (fgets(line, sizeof line, fp))
		if (sscanf(line, "%llx %c %255s", &addr, &type, symbol) == 3 &&
		    *start && addr > *start && addr < *end)
			*end = addr;
	fclose(fp);
	if (!*start || *end == ~0ULL) {
		fprintf(stderr, "moobl_index_perm_changed not in /proc/kallsyms\n");
		exit(2);
	}
}

static void arm(unsigned long long start, unsigned long long end)
{
	char v[32];

	put(FS "probability", "0");
	snprintf(v, sizeof v, "%llu", start);
	put(FS "require-start", v);
	snprintf(v, sizeof v, "%llu", end);
	put(FS "require-end", v);
	put(FS "stacktrace-depth", "32");
	put(FS "ignore-gfp-wait", "N");
	put(FS "task-filter", "Y");
	put(FS "interval", "1");
	put(FS "verbose", "0");
	snprintf(v, sizeof v, "%ld", TIMES);
	put(FS "times", v);
	put(FS "probability", "100");
}

static void disarm(void)
{
	put(FS "probability", "0");
	put(FS "task-filter", "N");
	put(FS "require-start", "0");
	put(FS "require-end", "0");
}

int main(int argc, char **argv)
{
	char dom[1024], sub[1100], top[1100], subf[1100];
	unsigned long long start = 0, end = 0;
	struct stat a, b;
	long left = TIMES;
	int inject, seen, rc, e;

	if (argc != 3 || (strcmp(argv[2], "0") && strcmp(argv[2], "1"))) {
		fprintf(stderr, "usage: fallbackfault <fresh domain> <0|1>\n");
		return 64;
	}
	setbuf(stdout, NULL);
	inject = argv[2][0] == '1';
	snprintf(dom, sizeof dom, "%s", argv[1]);
	snprintf(sub, sizeof sub, "%s/sub", dom);
	snprintf(top, sizeof top, "%s/top", dom);
	snprintf(subf, sizeof subf, "%s/sub/f", dom);
	if (mkdir(dom, 0755) || chmod(dom, 0755) || mkdir(sub, 0755) || chmod(sub, 0755))
		die("directories");
	file(top);
	file(subf);
	if (stat(top, &a) || stat(subf, &b))
		die("stat");
	seen = seen_by_1001(dom, a.st_ino, b.st_ino);
	if (seen != 3) {
		fprintf(stderr, "before the chmod uid 1001 saw %d, not both files\n", seen);
		return 2;
	}
	if (inject) {
		range(&start, &end);
		arm(start, end);
		put("/proc/self/make-it-fail", "1");
	}
	rc = chmod(sub, 0700);
	e = errno;
	if (inject) {
		put("/proc/self/make-it-fail", "0");
		left = get(FS "times");
		disarm();
	}
	if (rc) {
		errno = e;
		die("chmod of the subdirectory (the lower change comes first and must succeed)");
	}
	if (inject && left >= TIMES) {
		fprintf(stderr, "no allocation failed under moobl_index_perm_changed: nothing tested\n");
		return 2;
	}
	seen = seen_by_1001(dom, a.st_ino, b.st_ino);
	printf("FALLBACKFAULT_RESULT injected=%d faults=%ld top=%d sub_file=%d\n", inject,
	       TIMES - left, seen & 1, (seen >> 1) & 1);
	if (!(seen & 1)) {
		fprintf(stderr, "the readable file is missing: the answer is not just checked\n");
		return 1;
	}
	if (seen & 2) {
		fprintf(stderr, "0083 E: a file below a directory uid 1001 cannot search was answered\n");
		return 1;
	}
	return 0;
}
