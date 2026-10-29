/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rootpath-race: a domain directory renamed between the moment a build takes the domain's
 * name and the moment it records the generation of renames (issue #110, R2-5 of journal 0155).
 * QUERY2 takes the name of the domain directory before domain_ensure() waits for the domain's
 * build mutex. While another builder holds that mutex, the directory is renamed; the waiting
 * build then runs with the old name and must not leave a fresh domain whose recorded names
 * carry that old prefix. The mutex wait widens a window that otherwise lasts microseconds;
 * nothing in the module is changed for the test.
 *
 *   rootpath-race plant <dir> <files> <every>   creates f000000 ... in <dir> on the lower
 *                                               filesystem; every <every>-th file gets
 *                                               user.moob.kind = string "k"
 *   rootpath-race run <layer mount> <old> <new> <attributed files>
 *
 * run: process B issues the mount-wide QUERY (root), which builds <old> and holds its build
 * mutex; process A issues QUERY2 on <old> and waits for the mutex; <old> is renamed to <new>
 * through the layer while B is still building. Then the mount-wide QUERY must return every
 * attributed file, each under <new>/ and each present. Prints ROOTPATH_RACE_PASS, or
 * ROOTPATH_RACE_FAIL and exits 1; exits 3 (ROOTPATH_RACE_INCONCLUSIVE) when the interleaving
 * was not reached: A was not seen waiting, B had returned before the rename, or the build
 * counter shows no build by A after B's. B may fail with EAGAIN (see run()).
 */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void kind_exists(struct moob_query_term *t)
{
	memset(t, 0, sizeof *t);
	strcpy(t->name, "kind");
	t->op = MOOB_OP_EXISTS;
}

static int v1_query(int fd, struct moob_query *q)
{
	memset(q, 0, sizeof *q);
	q->version = MOOB_UAPI_VERSION;
	q->nterms = 1;
	q->limit = MOOB_QUERY_MAX_LIMIT;
	q->results = (__u64)(uintptr_t)res;
	kind_exists(&q->terms[0]);
	return ioctl(fd, MOOB_IOC_QUERY, q);
}

static int q2_query(int fd)
{
	struct moob_query2 q;

	memset(&q, 0, sizeof q);
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = MOOB_QUERY_MAX_LIMIT;
	q.results = (__u64)(uintptr_t)res;
	kind_exists(&q.terms[0]);
	return ioctl(fd, MOOB_IOC_QUERY2, &q);
}

/*
 * The scheduler state of pid from /proc/<pid>/stat: 'D' for the killable sleep of
 * mutex_lock_killable(). Before domain_ensure() takes the build mutex, QUERY2 reads nothing
 * from disk, so a 'D' there is the wait for the mutex (stack and wchan are not readable in the
 * guest kernels).
 */
static char state(pid_t pid)
{
	char path[64], buf[512], *p;
	ssize_t n;
	int fd;

	snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
	fd = open(path, O_RDONLY);
	n = fd < 0 ? -1 : read(fd, buf, sizeof buf - 1);
	if (fd >= 0)
		close(fd);
	if (n <= 0)
		return '?';
	buf[n] = 0;
	p = strrchr(buf, ')');
	return p && p[1] && p[2] ? p[2] : '?';
}

/* Whether pid has not exited yet; the child is left to be reaped by waitpid() later. */
static int alive(pid_t pid)
{
	siginfo_t si;

	memset(&si, 0, sizeof si);
	return !waitid(P_PID, (id_t)pid, &si, WEXITED | WNOHANG | WNOWAIT) && !si.si_pid;
}

static unsigned int builds(int fd)
{
	struct moob_info info;

	if (ioctl(fd, MOOB_IOC_GET_INFO, &info))
		return 0;
	return info.domain_builds;
}

static void show_info(int fd)
{
	struct moob_info info;

	if (!ioctl(fd, MOOB_IOC_GET_INFO, &info))
		printf("rootpath-race: domains_built=%u domain_builds=%u domain_releases=%u flags=%#x\n",
		       info.domains_built, info.domain_builds, info.domain_releases, info.flags);
}

static int plant(const char *dir, int files, int every)
{
	char path[4096];
	const char v[2] = { MOOB_T_STRING, 'k' };
	int i, fd;

	for (i = 0; i < files; i++) {
		snprintf(path, sizeof path, "%s/f%06d", dir, i);
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0 || close(fd)) {
			perror(path);
			return 1;
		}
		if (i % every == 0 && setxattr(path, "user.moob.kind", v, sizeof v, 0)) {
			perror("setxattr");
			return 1;
		}
	}
	return 0;
}

static int run(const char *mnt, const char *old, const char *new, unsigned int expect)
{
	char path[4096];
	struct moob_query q;
	unsigned int b0, b1, i, stale = 0, unknown = 0, missing = 0;
	int root, dom, status, n, a_waited = 0, b_building = 0;
	double t0, t_rename = 0;
	pid_t a, b;
	size_t olen = strlen(old), nlen = strlen(new);

	root = open(mnt, O_RDONLY | O_DIRECTORY);
	snprintf(path, sizeof path, "%s/%s", mnt, old);
	dom = open(path, O_RDONLY | O_DIRECTORY);
	if (root < 0 || dom < 0) {
		perror("open");
		return 1;
	}
	b0 = builds(root);
	t0 = now();
	b = fork();
	if (!b)
		_exit(v1_query(root, &q) ? 10 + errno % 100 : 0);
	/*
	 * B builds the outside part first, then <old>, the domain with the many files. A starts
	 * once the build counter shows the outside part done, so that B is in <old>'s build (or
	 * about to take its mutex, microseconds later) and not past it on a fast guest; a fixed
	 * 100 ms wait was longer than the whole build of B under KVM (journals 0225, 0228). The
	 * counter is read every millisecond for at most 2 s, then A starts anyway; 2 ms more let
	 * B take that mutex before A asks for it.
	 */
	for (i = 0; i < 2000 && builds(root) == b0 && alive(b); i++)
		usleep(1000);
	usleep(2000);
	printf("rootpath-race: A starts after %u ms, outside part %s\n", i,
	       builds(root) != b0 ? "built" : "not seen built");
	a = fork();
	if (!a)
		_exit(q2_query(dom) ? 10 + errno % 100 : 0);
	/* A has taken the domain's name and waits in domain_ensure() for B's build mutex */
	for (i = 0, n = 0; i < 5000; i++) {
		n = state(a) == 'D' ? n + 1 : 0;
		if (n == 5) {
			a_waited = 1;
			break;
		}
		if (!alive(a))
			break;
		usleep(1000);
	}
	b_building = alive(b);
	printf("rootpath-race: at the decision A state '%c' alive %d after %u ms, B alive %d\n",
	       state(a), alive(a), i, b_building);
	if (a_waited && b_building) {
		if (renameat(root, old, root, new)) {
			perror("rename");
			return 1;
		}
		t_rename = now() - t0;
		b_building = alive(b);	/* still building, or at least not returned, after it */
	}
	/*
	 * B only holds the mutex. A v1 QUERY whose domain is taken down while it checks results
	 * fails with EAGAIN (query_repin()); A takes the stale domain down, so that is allowed.
	 */
	if (waitpid(b, &status, 0) != b || !WIFEXITED(status) ||
	    (WEXITSTATUS(status) && WEXITSTATUS(status) != 10 + EAGAIN)) {
		show_info(root);
		printf("rootpath-race: builds since the start %u\n", builds(root) - b0);
		printf("ROOTPATH_RACE_FAIL: the mount-wide QUERY failed (status %d)\n", status);
		return 1;
	}
	printf("rootpath-race: B (mount-wide QUERY) took %.3f s, %s\n", now() - t0,
	       WEXITSTATUS(status) ? "EAGAIN" : "answered");
	if (waitpid(a, &status, 0) != a || !WIFEXITED(status) || WEXITSTATUS(status)) {
		printf("ROOTPATH_RACE_FAIL: QUERY2 on the domain failed (status %d)\n", status);
		return 1;
	}
	if (!a_waited || !b_building) {
		printf("ROOTPATH_RACE_INCONCLUSIVE: A waited on the build mutex %d, B building at the rename %d\n",
		       a_waited, b_building);
		return 3;
	}
	b1 = builds(root);
	printf("rootpath-race: rename at %.3f s, builds during the race %u\n", t_rename, b1 - b0);
	/* the outside part and <old> by B, then <old> again by A once B let the mutex go */
	if (b1 - b0 < 3) {
		printf("ROOTPATH_RACE_INCONCLUSIVE: A did not build after B\n");
		return 3;
	}
	close(dom);
	if (v1_query(root, &q)) {
		perror("MOOB_IOC_QUERY");
		return 1;
	}
	for (i = 0; i < q.count; i++) {
		struct stat st;

		if (res[i].flags & MOOB_RES_PATH_UNKNOWN || !res[i].path_len) {
			unknown++;
			continue;
		}
		if (!strncmp(res[i].path, old, olen) && res[i].path[olen] == '/')
			stale++;
		else if (strncmp(res[i].path, new, nlen) || res[i].path[nlen] != '/')
			stale++;
		snprintf(path, sizeof path, "%s/%s", mnt, res[i].path);
		if (stat(path, &st) || st.st_ino != res[i].ino)
			missing++;
	}
	printf("rootpath-race: QUERY after the race: %u results (expected %u), %u under the old name, "
	       "%u without a path, %u that do not resolve; builds by it %u\n",
	       q.count, expect, stale, unknown, missing, builds(root) - b1);
	if (q.count != expect || q.truncated || stale || unknown || missing) {
		if (q.count)
			printf("rootpath-race: first result path '%s'\n", res[0].path);
		printf("ROOTPATH_RACE_FAIL: a domain built under its old name was left fresh\n");
		return 1;
	}
	printf("ROOTPATH_RACE_PASS: every result under the new name after the rename during the build\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 5 && !strcmp(argv[1], "plant"))
		return plant(argv[2], atoi(argv[3]), atoi(argv[4]));
	if (argc == 6 && !strcmp(argv[1], "run"))
		return run(argv[2], argv[3], argv[4], (unsigned int)atoi(argv[5]));
	fprintf(stderr, "usage: rootpath-race plant <dir> <files> <every> | run <mount> <old> <new> <n>\n");
	return 2;
}
