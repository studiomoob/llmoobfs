/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * permmatrix: QUERY2 against the kernel's own permission decision (journal 0116).
 *
 *   permmatrix plant <lower dir> <seed>       as root: <lower dir>/pm, a domain directory with
 *                                              files of random modes, owners and groups, some
 *                                              under an intermediate directory, each carrying
 *                                              user.moob.pm
 *   permmatrix check <layer dir> <lower dir>   for every identity of the matrix: QUERY2
 *                                              "pm exists" on <layer dir>/pm must return only
 *                                              files that identity can getxattr on the lower
 *                                              and reach by listing every directory on the
 *                                              way (and open the domain there), and every
 *                                              such file the rule calls homogeneous
 *
 * The domain is planted on the lower and the layer mounted after, so no permission changes
 * through the layer and the domain is answered from its partition, without per-result checks
 * (the check step verifies fallback_domains=0 on the caller's side through the shell; it runs
 * check again after a chmod through the layer, for the per-result checks). The oracle is the
 * lower kernel itself, not a copy of the rule: getxattr of the file, and opendir of the
 * directory between, since a search-only directory (0711) lets getxattr through but keeps its
 * names from those who cannot list it (journal 0215). check also counts the cases the rule
 * before journal 0116 ("others" bit alone) and the rule before journal 0215 (search alone for
 * a directory) would have answered wrongly, so a run proves the matrix reached them. Prints
 * PERMMATRIX_PASS with the counts; exits 1 on a mismatch.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

#define NFILES 48
#define NSUB 4
static const mode_t file_modes[] = { 0644, 0604, 0640, 0600, 0444, 0404, 0440, 0004, 0040, 0400,
				     0664, 0606 };
static const mode_t dir_modes[] = { 0755, 0705, 0750, 0711, 0701, 0710, 0700, 0751, 0715,
				    0754, 0745 };
static const uid_t uids[] = { 1001, 1002, 1003 };
static const gid_t gids[] = { 2001, 2002 };

struct ident { uid_t uid; gid_t gid; int ngroups; gid_t groups[2]; };
static const struct ident idents[] = {
	{ 1001, 2001, 0, { 0 } }, { 1001, 2002, 0, { 0 } }, { 1001, 2001, 1, { 2002 } },
	{ 1002, 2001, 0, { 0 } }, { 1002, 2002, 0, { 0 } }, { 1003, 2002, 1, { 2001 } },
	{ 1004, 3000, 0, { 0 } }, { 1004, 3000, 1, { 2001 } }, { 1004, 3000, 1, { 2002 } },
};

/*
 * An access ACL with a named user entry (journal 0215), written as the kernel's xattr format
 * (include/uapi/linux/posix_acl_xattr.h): version 2, then the entries in the required order.
 * The owner, group and other entries keep the mode's bits; the mask grants everything, so the
 * group class bits of the mode become rwx. Any named entry makes the ACL extended, so the rule
 * leaves the inode out unless condition 1 applies, which it never does with an ACL.
 */
static int set_named_acl(const char *path, mode_t mode, uid_t uid, unsigned int perm)
{
	unsigned char buf[4 + 5 * 8];
	static const unsigned short tags[] = { 0x01, 0x02, 0x04, 0x10, 0x20 };
	unsigned int perms[5] = { (mode >> 6) & 7, perm, (mode >> 3) & 7, 7, mode & 7 };
	unsigned int ids[5] = { ~0u, uid, ~0u, ~0u, ~0u };
	int i;

	buf[0] = 2; buf[1] = buf[2] = buf[3] = 0;
	for (i = 0; i < 5; i++) {
		unsigned char *e = buf + 4 + 8 * i;

		e[0] = (unsigned char)tags[i]; e[1] = 0;
		e[2] = (unsigned char)perms[i]; e[3] = 0;
		e[4] = (unsigned char)ids[i]; e[5] = (unsigned char)(ids[i] >> 8);
		e[6] = (unsigned char)(ids[i] >> 16); e[7] = (unsigned char)(ids[i] >> 24);
	}
	return setxattr(path, "system.posix_acl_access", buf, sizeof buf, 0);
}

static int has_acl(const char *path)
{
	return getxattr(path, "system.posix_acl_access", NULL, 0) > 0;
}

static unsigned long long rng;
static unsigned int pick(unsigned int n)
{
	rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
	return (unsigned int)(rng >> 33) % n;
}

static int plant(const char *lower, unsigned long long seed)
{
	char path[4096];
	const char val[] = { MOOB_T_STRING, 'v' };
	mode_t m;
	int i;

	rng = seed;
	snprintf(path, sizeof path, "%s/pm", lower);
	if (mkdir(path, 0755) || chown(path, uids[pick(3)], gids[pick(2)]) ||
	    chmod(path, dir_modes[pick(sizeof dir_modes / sizeof dir_modes[0])])) {
		perror(path);
		return 1;
	}
	for (i = 0; i < NSUB; i++) {
		mode_t dm = dir_modes[pick(sizeof dir_modes / sizeof dir_modes[0])];
		static const unsigned int dir_acl_perms[] = { 05, 01, 04 };

		snprintf(path, sizeof path, "%s/pm/s%d", lower, i);
		if (mkdir(path, 0755) || chown(path, uids[pick(3)], gids[pick(2)]) || chmod(path, dm) ||
		    (!pick(3) && set_named_acl(path, dm, 1004, dir_acl_perms[pick(3)]))) {
			perror(path);
			return 1;
		}
	}
	for (i = 0; i < NFILES; i++) {
		int fd, sub = (int)pick(NSUB + 2) - 2;	/* about a third directly in the domain */

		if (sub < 0)
			snprintf(path, sizeof path, "%s/pm/f%d", lower, i);
		else
			snprintf(path, sizeof path, "%s/pm/s%d/f%d", lower, sub, i);
		fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
		m = file_modes[pick(sizeof file_modes / sizeof file_modes[0])];
		if (fd < 0 || close(fd) || setxattr(path, MOOB_ATTR_PREFIX "pm", val, sizeof val, 0) ||
		    chown(path, uids[pick(3)], gids[pick(2)]) || chmod(path, m) ||
		    (!pick(4) && set_named_acl(path, m, 1002, pick(2) ? 04 : 0))) {
			perror(path);
			return 1;
		}
	}
	return 0;
}

/*
 * The homogeneity rule of docs/spec-query-v2.md §3 over lower modes; an inode with an
 * extended ACL (the only ACLs planted) is never admitted, by either condition. The class bits
 * need are 04 (read) for files, 05 (read and search) for directories, 01 for
 * directories under the rule before journal 0215. others_only is the condition 1 before
 * journal 0116, where the bits of others alone admitted the inode.
 */
static int rule(const struct stat *in, const struct stat *dom, mode_t need, int others_only)
{
	mode_t u = need << 6, g = need << 3, o = need;

	if (others_only ? (in->st_mode & o) == o : (in->st_mode & (u | g | o)) == (u | g | o))
		return 1;
	if (in->st_uid != dom->st_uid || in->st_gid != dom->st_gid)
		return 0;
	return !((dom->st_mode & 0100) && (in->st_mode & u) != u) &&
	       !((dom->st_mode & 0010) && (in->st_mode & g) != g) &&
	       !((dom->st_mode & 0001) && (in->st_mode & o) != o);
}

/* old_in: the rule before 0116; prev_in: before 0215; new_in: the rule now */
struct expect { char rel[64]; ino_t ino; int sub; int old_in, prev_in, new_in; };

static int collect(const char *lower, struct expect *ex)
{
	char path[4096];
	struct stat dom, st, sub;
	int n = 0, i, s, acl;

	snprintf(path, sizeof path, "%s/pm", lower);
	if (stat(path, &dom))
		return -1;
	for (i = 0; i < NFILES; i++) {
		for (s = -1; s < NSUB; s++) {
			if (s < 0)
				snprintf(ex[n].rel, sizeof ex[n].rel, "f%d", i);
			else
				snprintf(ex[n].rel, sizeof ex[n].rel, "s%d/f%d", s, i);
			snprintf(path, sizeof path, "%s/pm/%s", lower, ex[n].rel);
			if (stat(path, &st))
				continue;
			ex[n].ino = st.st_ino;
			ex[n].sub = s;
			acl = has_acl(path);
			ex[n].old_in = !acl && rule(&st, &dom, 04, 1);
			ex[n].prev_in = ex[n].new_in = !acl && rule(&st, &dom, 04, 0);
			if (s >= 0) {
				snprintf(path, sizeof path, "%s/pm/s%d", lower, s);
				if (stat(path, &sub))
					return -1;
				acl = has_acl(path);
				ex[n].old_in = ex[n].old_in && !acl && rule(&sub, &dom, 01, 1);
				ex[n].prev_in = ex[n].prev_in && !acl && rule(&sub, &dom, 01, 0);
				ex[n].new_in = ex[n].new_in && !acl && rule(&sub, &dom, 05, 0);
			}
			n++;
			break;
		}
	}
	return n;
}

/* In a child with the identity's credentials: 0 all equal, 1 mismatch, 2 error. */
static int check_one(const struct ident *id, const char *layer, const char *lower,
		     const struct expect *ex, int n, int *counts)
{
	static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];
	struct moob_query2 q;
	char path[4096];
	int fd, i, j, bad = 0, dom_ok, list_ok[NSUB];
	DIR *dir;

	if (setgroups((size_t)id->ngroups, id->groups) || setresgid(id->gid, id->gid, id->gid) ||
	    setresuid(id->uid, id->uid, id->uid)) {
		perror("credentials");
		return 2;
	}
	/* QUERY2 asks read and search on the domain directory (spec §4): the lower decides */
	snprintf(path, sizeof path, "%s/pm", lower);
	dom_ok = !faccessat(AT_FDCWD, path, R_OK | X_OK, AT_EACCESS);
	/* the directories between: can this identity list them on the lower */
	for (i = 0; i < NSUB; i++) {
		if (snprintf(path, sizeof path, "%s/pm/s%d", lower, i) >= (int)sizeof path)
			return 2;
		dir = opendir(path);
		list_ok[i] = dir != NULL;
		if (dir)
			closedir(dir);
		else if (errno != EACCES && errno != ENOENT) {
			perror(path);
			return 2;
		}
	}
	snprintf(path, sizeof path, "%s/pm", layer);
	fd = open(path, O_RDONLY | O_DIRECTORY);
	memset(&q, 0, sizeof q);
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = MOOB_QUERY_MAX_LIMIT;
	q.results = (unsigned long)res;
	strcpy(q.terms[0].name, "pm");
	q.terms[0].op = MOOB_OP_EXISTS;
	if (fd < 0 || ioctl(fd, MOOB_IOC_QUERY2, &q)) {
		/* a domain the identity cannot open or list answers nothing: every file must be
		 * unreadable for it through the domain, which the oracle below confirms */
		if (errno != EACCES) {
			perror("QUERY2");
			return 2;
		}
		q.count = 0;
	}
	if (q.truncated) {
		fprintf(stderr, "truncated answer\n");
		return 2;
	}
	counts[2] += (int)q.count;
	for (i = 0; i < n; i++) {
		int got = 0, can, readable;

		for (j = 0; j < (int)q.count; j++)
			if (res[j].ino == ex[i].ino)
				got = 1;
		if (snprintf(path, sizeof path, "%s/pm/%s", lower, ex[i].rel) >= (int)sizeof path)
			return 2;
		can = getxattr(path, MOOB_ATTR_PREFIX "pm", NULL, 0) >= 0;
		if (!can && errno != EACCES) {
			perror(path);
			return 2;
		}
		readable = can && dom_ok;
		can = readable && (ex[i].sub < 0 || list_ok[ex[i].sub]);
		/*
		 * Returned and not reachable is the leak (ADR-0011 promise 3), judged by the lower
		 * kernel alone. Reachable, homogeneous and not returned is a missing answer; a
		 * reachable file the rule leaves out (heterogeneous) is absent by design.
		 */
		if ((got && !can) || (!got && can && ex[i].new_in)) {
			printf("MISMATCH uid=%u gid=%u group=%d: %s returned=%d readable=%d homogeneous=%d\n",
			       id->uid, id->gid, id->ngroups ? (int)id->groups[0] : -1, ex[i].rel,
			       got, can, ex[i].new_in);
			bad = 1;
		}
		/* the old rule placed it in the partition and the domain opened, but the file is
		 * not readable: the kernel before journal 0116 returned it */
		if (ex[i].old_in && dom_ok && !readable)
			counts[0]++;
		/* and the rule before journal 0215 (search alone for a directory) */
		if (ex[i].prev_in && dom_ok && !can)
			counts[1]++;
	}
	return bad;
}

static int check(const char *layer, const char *lower)
{
	static struct expect ex[NFILES];
	int n = collect(lower, ex), k, mismatches = 0, old_wrong = 0, prev_wrong = 0, returned = 0;
	int shared[2];

	if (n <= 0) {
		fprintf(stderr, "nothing planted under %s/pm\n", lower);
		return 2;
	}
	for (k = 0; k < (int)(sizeof idents / sizeof idents[0]); k++) {
		pid_t pid;
		int st, counts[3] = { 0, 0, 0 };

		if (pipe(shared))
			return 2;
		pid = fork();
		if (pid == 0) {
			int r = check_one(&idents[k], layer, lower, ex, n, counts);

			if (write(shared[1], counts, sizeof counts) != sizeof counts)
				r = 2;
			fflush(stdout);	/* the MISMATCH lines: _exit() does not flush */
			_exit(r);
		}
		close(shared[1]);
		if (read(shared[0], counts, sizeof counts) != sizeof counts)
			counts[0] = counts[1] = counts[2] = 0;
		close(shared[0]);
		if (pid < 0 || waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) == 2)
			return 2;
		mismatches += WEXITSTATUS(st);
		old_wrong += counts[0];
		prev_wrong += counts[1];
		returned += counts[2];
	}
	printf("PERMMATRIX%s identities=%zu files=%d returned=%d old_rule_wrong=%d prev_rule_wrong=%d mismatches=%d\n",
	       mismatches ? "_FAIL" : "_PASS", sizeof idents / sizeof idents[0], n, returned,
	       old_wrong, prev_wrong, mismatches);
	return mismatches ? 1 : 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "plant"))
		return plant(argv[2], strtoull(argv[3], NULL, 10));
	if (argc == 4 && !strcmp(argv[1], "check"))
		return check(argv[2], argv[3]);
	fprintf(stderr, "usage: permmatrix plant <lower> <seed> | check <layer> <lower>\n");
	return 2;
}
