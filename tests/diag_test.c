/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Unit tests of tools/diag.h (journal 0243) over a fake /proc/self/cgroup and cgroup tree in a
 * temporary directory: the path line, the walk up to the root, the effective limit, which
 * level's limit was reached between two readings, and module parameters. POSIX only, run by
 * make test on macOS and Linux.
 *   diag_test [TMPDIR]
 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE	/* mkdtemp() on macOS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../tools/diag.h"

static int failures;
static char top[1024];

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "diag_test:%d: %s\n", __LINE__, #c); \
	failures++; } } while (0)

static void put(const char *rel, const char *text)
{
	char file[2048];
	FILE *f;

	snprintf(file, sizeof file, "%s/%s", top, rel);
	f = fopen(file, "w");
	if (!f || fputs(text, f) < 0 || fclose(f)) {
		perror(file);
		exit(1);
	}
}

static void dir(const char *rel)
{
	char d[2048];

	snprintf(d, sizeof d, "%s/%s", top, rel);
	if (mkdir(d, 0755)) {
		perror(d);
		exit(1);
	}
}

static void path(char *buf, size_t len, const char *rel)
{
	snprintf(buf, len, "%s/%s", top, rel);
}

static void events(const char *cg, unsigned int max, unsigned int oom, unsigned int kill)
{
	char rel[512], text[256];

	snprintf(text, sizeof text, "low 0\nhigh 0\nmax %u\noom %u\noom_kill %u\noom_group_kill 0\n",
		 max, oom, kill);
	snprintf(rel, sizeof rel, "cg%s/memory.events.local", cg);
	put(rel, text);
	snprintf(text, sizeof text, "low 0\nhigh 0\nmax %u\noom %u\noom_kill %u\noom_group_kill 0\n",
		 max + 100, oom + 100, kill);
	snprintf(rel, sizeof rel, "cg%s/memory.events", cg);
	put(rel, text);
}

int main(int argc, char **argv)
{
	struct diag_chain a, b;
	char proc[1200], root[1200], mod[1200], out[1200];
	long long v;
	const char *tmp = argc > 1 ? argv[1] : getenv("TMPDIR");

	snprintf(top, sizeof top, "%s/moob-diag.XXXXXX", tmp && *tmp ? tmp : "/tmp");
	if (!mkdtemp(top)) {
		perror(top);
		return 1;
	}
	path(proc, sizeof proc, "cgroup");
	path(root, sizeof root, "cg");
	path(mod, sizeof mod, "mod");

	/* the 0:: line among v1 lines, and the " (deleted)" mark */
	put("cgroup", "12:memory:/legacy\n0::/user.slice/user-1001.slice/session-3.scope\n");
	CHECK(!diag_cgroup_path(proc, out, sizeof out));
	CHECK(!strcmp(out, "/user.slice/user-1001.slice/session-3.scope"));
	put("cgroup", "0::/gone (deleted)\n");
	CHECK(!diag_cgroup_path(proc, out, sizeof out) && !strcmp(out, "/gone"));
	put("cgroup", "4:memory:/only-v1\n");
	CHECK(diag_cgroup_path(proc, out, sizeof out) == -1);
	CHECK(diag_chain_read(proc, root, &a) == -1);
	path(out, sizeof out, "absent");
	CHECK(diag_chain_read(out, root, &a) == -1);

	/* no cgroup v2 hierarchy at the root: unknown, not unlimited */
	put("cgroup", "0::/\n");
	CHECK(diag_chain_read(proc, root, &a) == -1);
	/* the root cgroup itself: no level, no limit */
	dir("cg");
	put("cg/cgroup.procs", "1\n");
	CHECK(!diag_chain_read(proc, root, &a) && a.n == 0 && diag_effective(&a) == -1);
	/* a cgroup the root does not show (another namespace, or not mounted): unknown */
	put("cgroup", "0::/elsewhere/x.scope\n");
	CHECK(diag_chain_read(proc, root, &a) == -1);

	/*
	 * A systemd user: the limit is on user-1001.slice, the process in a session scope below it,
	 * and user.slice has a larger one.
	 */
	dir("cg/user.slice");
	dir("cg/user.slice/user-1001.slice");
	dir("cg/user.slice/user-1001.slice/session-3.scope");
	put("cg/user.slice/user-1001.slice/session-3.scope/cgroup.procs", "42\n");
	put("cg/user.slice/user-1001.slice/cgroup.procs", "");
	put("cg/user.slice/memory.max", "8589934592\n");
	put("cg/user.slice/memory.current", "123\n");
	put("cg/user.slice/user-1001.slice/memory.max", "2147483648\n");
	put("cg/user.slice/user-1001.slice/memory.current", "1048576\n");
	put("cg/user.slice/user-1001.slice/session-3.scope/memory.max", "max\n");
	put("cg/user.slice/user-1001.slice/session-3.scope/memory.current", "4096\n");
	events("/user.slice", 0, 0, 0);
	events("/user.slice/user-1001.slice", 2, 1, 0);
	events("/user.slice/user-1001.slice/session-3.scope", 0, 0, 0);
	put("cgroup", "0::/user.slice/user-1001.slice/session-3.scope\n");
	CHECK(!diag_chain_read(proc, root, &a));
	CHECK(a.n == 3);
	CHECK(!strcmp(a.lv[0].path, "/user.slice/user-1001.slice/session-3.scope"));
	CHECK(!strcmp(a.lv[1].path, "/user.slice/user-1001.slice"));
	CHECK(!strcmp(a.lv[2].path, "/user.slice"));
	CHECK(a.lv[0].has_max && a.lv[0].max == DIAG_NOLIMIT);
	CHECK(a.lv[1].max == 2147483648ULL && a.lv[1].current == 1048576ULL);
	CHECK(a.lv[1].ev_max == 2 && a.lv[1].ev_oom == 1);	/* the local counts */
	CHECK(a.lv[2].ev_max == 0);
	CHECK(diag_effective(&a) == 1);

	/* the same chain read again, nothing reached: no hit */
	CHECK(!diag_chain_read(proc, root, &b) && diag_hit(&a, &b) == -1);
	/* a limit reached in the slice: its local count grows, and the scope's hierarchical one */
	events("/user.slice/user-1001.slice", 3, 1, 0);
	CHECK(!diag_chain_read(proc, root, &b) && diag_hit(&a, &b) == 1);
	/* a level without a limit never counts, whatever its events say */
	events("/user.slice/user-1001.slice", 2, 1, 0);
	events("/user.slice/user-1001.slice/session-3.scope", 5, 5, 0);
	CHECK(!diag_chain_read(proc, root, &b) && diag_hit(&a, &b) == -1);
	/* oom_kill is read from the hierarchical file */
	events("/user.slice/user-1001.slice", 2, 1, 4);
	CHECK(!diag_chain_read(proc, root, &b) && b.lv[1].ev_oom_kill == 4);
	/* another cgroup (the process moved): not comparable */
	put("cgroup", "0::/user.slice/user-1001.slice\n");
	CHECK(!diag_chain_read(proc, root, &b) && b.n == 2 && diag_hit(&a, &b) == -1);

	/* no memory controller below user.slice: those levels count as no limit */
	dir("cg/system.slice");
	dir("cg/system.slice/x.service");
	put("cg/system.slice/x.service/cgroup.procs", "7\n");
	put("cgroup", "0::/system.slice/x.service\n");
	CHECK(!diag_chain_read(proc, root, &a) && a.n == 2 && !a.lv[0].has_max &&
	      diag_effective(&a) == -1);
	/* a limit at the top level only, and a malformed value elsewhere */
	put("cg/system.slice/memory.max", "4194304\n");
	put("cg/system.slice/x.service/memory.max", "12abc\n");
	CHECK(!diag_chain_read(proc, root, &a) && !a.lv[0].has_max && diag_effective(&a) == 1);

	/* without memory.events.local (older kernels) the hierarchical "max" is used */
	put("cg/system.slice/memory.events", "max 7\noom 0\noom_kill 0\n");
	CHECK(!diag_chain_read(proc, root, &a) && a.lv[1].ev_max == 7);

	/* module parameters */
	dir("mod");
	dir("mod/parameters");
	put("mod/parameters/scan_delay_us", "0\n");
	put("mod/parameters/scan_getxattr_errno_once", "-5\n");
	put("mod/parameters/bad", "x\n");
	CHECK(!diag_param(mod, "scan_delay_us", &v) && v == 0);
	CHECK(!diag_param(mod, "scan_getxattr_errno_once", &v) && v == -5);
	CHECK(diag_param(mod, "bad", &v) == -1);
	CHECK(diag_param(mod, "absent", &v) == -1);

	/* flat keys: a key that is a prefix of another is not taken for it */
	put("keys", "oom_kill 3\noom 9\n");
	{
		unsigned long long x = 0;
		char f[1200];

		path(f, sizeof f, "keys");
		CHECK(!diag_key(f, "oom", &x) && x == 9);
		CHECK(!diag_key(f, "oom_kill", &x) && x == 3);
		CHECK(diag_key(f, "max", &x) == -1);
	}

	if (failures) {
		fprintf(stderr, "DIAG_TEST_FAIL: %d (tree left in %s)\n", failures, top);
		return 1;
	}
	{
		char cmd[1100];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", top);
		if (system(cmd))
			fprintf(stderr, "diag_test: could not remove %s\n", top);
	}
	printf("DIAG_TEST_PASS\n");
	return 0;
}
