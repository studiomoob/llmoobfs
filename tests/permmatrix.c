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
 *
 *   permmatrix churn <layer dir> <lower dir> <seed> <rounds> <steps> <delay us>
 *                                              as root, on a tree planted by plant and with
 *                                              the layer not mounted: <rounds> mounts of the
 *                                              layer over <lower dir>, each with <steps>
 *                                              seeded operations through the layer (chmod,
 *                                              chown, setfacl, rename, link, unlink), done as
 *                                              root, as the owner concerned or as another
 *                                              identity, and after each one QUERY2 for every
 *                                              identity of the matrix (security roadmap S15,
 *                                              journal 0250). In the odd rounds, scan_delay_us
 *                                              is set to <delay us> while a build runs (at the
 *                                              mount and after a directory rename) and
 *                                              operations land during it.
 *
 * churn's oracle is again the lower kernel, as the identity: every returned path must name the
 * returned inode on the lower and be reachable (the domain opened, each directory on the way
 * listed, the file's attribute read); a result without a path (a renamed directory's, by
 * contract) needs one reachable name of its inode. A reachable file the rule admits must be
 * returned, unless its answer depends on history the rule leaves open: a file seen with more
 * than one name in the mount (it may stay out once back to one), a file below a directory
 * whose permissions changed or that was renamed (the domain falls back without re-evaluating),
 * a file an operation touched during a build. Those are only checked for leaks. Prints
 * PERMCHURN_PASS with the counts.
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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <time.h>
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

/* ---- churn: seeded operations between QUERY2 calls (security roadmap S15, journal 0250) ---- */

#define CH_MAX 256		/* names under pm */
#define CH_MINFILES 16		/* unlink keeps at least this many files with one name */
#define SCAN_DELAY "/sys/module/moobfs_layer/parameters/scan_delay_us"

struct node {
	char rel[200];		/* path below pm */
	struct stat st;
	int parent;		/* index of the parent directory, -1 for pm */
	int depth;		/* 1 directly in pm */
	int acl;
};
struct tree {
	int n;
	struct node v[CH_MAX];
	struct stat dom;
	int domacl;
};

/* what churn changed in the round: inodes whose answer is history-dependent (see the header) */
static ino_t tainted[4 * CH_MAX];
static int ntainted, taint_all;

struct churn_stats {
	int ops, ok, denied, refused, by_uid_ok, during_build, builds, checks, returned, expected,
	    leak_checked, unknown_path, mismatches, kind_ok[6];
};
static const char *const op_names[] = { "chmod", "chown", "setfacl", "rename", "link", "unlink" };

static int node_cmp(const void *a, const void *b)
{
	return strcmp(((const struct node *)a)->rel, ((const struct node *)b)->rel);
}

static int walk(const char *lower, const char *rel, int depth, struct tree *t)
{
	char path[4096];
	struct dirent *e;
	DIR *d;

	snprintf(path, sizeof path, "%s/pm%s%s", lower, *rel ? "/" : "", rel);
	d = opendir(path);
	if (!d)
		return -1;
	while ((e = readdir(d))) {
		struct node *nd;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (t->n >= CH_MAX)
			goto fail;
		nd = &t->v[t->n++];
		if (snprintf(nd->rel, sizeof nd->rel, "%s%s%s", rel, *rel ? "/" : "", e->d_name) >=
		    (int)sizeof nd->rel)
			goto fail;
		snprintf(path, sizeof path, "%s/pm/%s", lower, nd->rel);
		if (lstat(path, &nd->st))
			goto fail;
		nd->acl = has_acl(path);
		nd->depth = depth;
		if (S_ISDIR(nd->st.st_mode) && walk(lower, nd->rel, depth + 1, t))
			goto fail;
	}
	closedir(d);
	return 0;
fail:
	closedir(d);
	return -1;
}

/* the tree on the lower, as root, sorted by name so that a seed picks the same names */
static int load_tree(const char *lower, struct tree *t)
{
	char path[4096];
	int i, j;

	t->n = 0;
	snprintf(path, sizeof path, "%s/pm", lower);
	if (stat(path, &t->dom) || walk(lower, "", 1, t)) {
		fprintf(stderr, "cannot read the tree under %s\n", path);
		return -1;
	}
	t->domacl = has_acl(path);
	qsort(t->v, (size_t)t->n, sizeof t->v[0], node_cmp);
	for (i = 0; i < t->n; i++) {
		const char *slash = strrchr(t->v[i].rel, '/');

		t->v[i].parent = -1;
		for (j = 0; slash && j < t->n; j++)
			if (!strncmp(t->v[j].rel, t->v[i].rel, (size_t)(slash - t->v[i].rel)) &&
			    t->v[j].rel[slash - t->v[i].rel] == '\0')
				t->v[i].parent = j;
	}
	return 0;
}

static int is_file(const struct tree *t, int i)
{
	return S_ISREG(t->v[i].st.st_mode);
}

/* is i (a node) below directory d (a node, or -1 for pm) */
static int below(const struct tree *t, int i, int d)
{
	for (i = t->v[i].parent; i >= 0; i = t->v[i].parent)
		if (i == d)
			return 1;
	return d < 0;
}

/* the homogeneity rule of spec §3 over the lower, for a file at its one name */
static int homogeneous(const struct tree *t, int i)
{
	const struct node *f = &t->v[i];
	int p;

	if (f->st.st_nlink > 1 || f->acl)
		return 0;
	if (t->domacl ? (f->st.st_mode & 0444) != 0444 : !rule(&f->st, &t->dom, 04, 0))
		return 0;
	for (p = f->parent; p >= 0; p = t->v[p].parent) {
		const struct node *d = &t->v[p];

		if (d->acl)
			return 0;
		if (t->domacl ? (d->st.st_mode & 0555) != 0555 : !rule(&d->st, &t->dom, 05, 0))
			return 0;
	}
	return 1;
}

static void taint(ino_t ino)
{
	if (ntainted < (int)(sizeof tainted / sizeof tainted[0]))
		tainted[ntainted++] = ino;
	else
		taint_all = 1;
}

static int is_tainted(ino_t ino)
{
	int i;

	for (i = 0; i < ntainted; i++)
		if (tainted[i] == ino)
			return 1;
	return taint_all;
}

/* Remember multiple names before an operation can remove the penultimate one, including
 * names kept from the preceding mount and names changed during the first build. */
static void taint_links(const struct tree *t)
{
	int i;

	for (i = 0; i < t->n; i++)
		if (is_file(t, i) && t->v[i].st.st_nlink > 1 && !is_tainted(t->v[i].st.st_ino))
			taint(t->v[i].st.st_ino);
}

/* every file below directory d (-1: the domain directory itself) */
static void taint_under(const struct tree *t, int d)
{
	int i;

	if (d < 0) {
		taint_all = 1;
		return;
	}
	for (i = 0; i < t->n; i++)
		if (is_file(t, i) && below(t, i, d))
			taint(t->v[i].st.st_ino);
}

static void set_creds(const struct ident *id)
{
	if (setgroups((size_t)id->ngroups, id->groups) || setresgid(id->gid, id->gid, id->gid) ||
	    setresuid(id->uid, id->uid, id->uid)) {
		perror("credentials");
		_exit(2);
	}
}

static int write_delay(unsigned int us)
{
	FILE *f = fopen(SCAN_DELAY, "w");
	int bad;

	if (!f) {
		perror(SCAN_DELAY);
		return -1;
	}
	bad = fprintf(f, "%u\n", us) < 0;
	if (fclose(f) || bad) {
		perror(SCAN_DELAY);
		return -1;
	}
	return 0;
}

/*
 * One seeded operation through the layer, in a child with the actor's credentials. Returns 0
 * done, 1 denied (EACCES, EPERM), 3 refused for another reason (EXDEV, EINVAL, ENOTEMPTY, ...),
 * 2 error of the test itself. *kind and *target say what was tried, for the taint.
 */
static int churn_op(const char *layer, const struct tree *t, int *kind, int *target, int *by_uid,
		    unsigned int *serial)
{
	char a[4096], b[4096];
	struct ident actor = { 0, 0, 0, { 0 } };
	int who = (int)pick(10), i, d = -1, k, st, r = 0, nfiles = 0;
	const struct node *n;
	pid_t pid;

	for (i = 0; i < t->n; i++)
		nfiles += is_file(t, i) && t->v[i].st.st_nlink == 1;
	k = (int)pick(20);
	*kind = k < 5 ? 0 : k < 8 ? 1 : k < 11 ? 2 : k < 15 ? 3 : k < 17 ? 4 : 5;
	/* the target: any name for chmod, chown and setfacl (pm itself as -1), else a file or a
	 * directory below pm; unlink prefers a file with several names */
	i = (int)pick((unsigned int)t->n + 1) - 1;
	if (*kind == 4 || *kind == 5)
		for (k = 0; k < 8 && (i < 0 || !is_file(t, i)); k++)
			i = (int)pick((unsigned int)t->n);
	if (*kind == 5)
		for (k = 0; k < 4 && i >= 0 && t->v[i].st.st_nlink == 1; k++)
			i = (int)pick((unsigned int)t->n);
	if (*kind >= 3 && i < 0)
		i = (int)pick((unsigned int)t->n);
	if ((*kind >= 4 && !is_file(t, i)) ||
	    (*kind == 5 && t->v[i].st.st_nlink == 1 && nfiles <= CH_MINFILES))
		*kind = 0;	/* nothing fit: a chmod of what was picked */
	*target = i;
	n = i >= 0 ? &t->v[i] : NULL;
	snprintf(a, sizeof a, "%s/pm%s%s", layer, n ? "/" : "", n ? n->rel : "");
	if (*kind == 3 || *kind == 4) {
		/*
		 * A destination directory: any for a file; for a directory, one at its parent's
		 * depth, since a directory keeps its depth across a rename (spec §5, EXDEV)
		 */
		int dsrc = S_ISDIR(n->st.st_mode), j;

		for (k = 0; k < 8; k++) {
			d = (int)pick((unsigned int)t->n + 1) - 1;
			if (d < 0 ? !dsrc || n->depth == 1 :
			    S_ISDIR(t->v[d].st.st_mode) && (!dsrc || t->v[d].depth == n->depth - 1))
				break;
		}
		if (k == 8)
			d = dsrc ? n->parent : -1;
		if (*kind == 3 && pick(8)) {
			const char *base = strrchr(n->rel, '/');
			char fresh[16];

			snprintf(fresh, sizeof fresh, "r%u", (*serial)++);
			snprintf(b, sizeof b, "%s/pm%s%s/%s", layer, d >= 0 ? "/" : "",
				 d >= 0 ? t->v[d].rel : "", pick(4) ? (base ? base + 1 : n->rel) : fresh);
		} else if (*kind == 3) {	/* over another name: replaces a file, refused for a directory */
			j = (int)pick((unsigned int)t->n);
			snprintf(b, sizeof b, "%s/pm/%s", layer, t->v[j].rel);
		} else {
			snprintf(b, sizeof b, "%s/pm%s%s/l%u", layer, d >= 0 ? "/" : "",
				 d >= 0 ? t->v[d].rel : "", (*serial)++);
		}
	}
	/* the actor: root, the owner concerned (of the target, or of the directory a name is
	 * changed in), or another identity of the matrix */
	if (who >= 4 && who < 9) {
		const struct stat *o = n ? &n->st : &t->dom;

		if (*kind == 3 || *kind == 5)
			o = n->parent >= 0 ? &t->v[n->parent].st : &t->dom;
		actor.uid = o->st_uid;
		actor.gid = o->st_gid;
	} else if (who == 9) {
		actor = idents[pick(sizeof idents / sizeof idents[0])];
	}
	*by_uid = actor.uid != 0;
	pid = fork();
	if (pid < 0)
		return 2;
	if (pid == 0) {
		const struct stat *o = n ? &n->st : &t->dom;
		static const unsigned int acl_perms[] = { 0, 04, 05, 07 };

		if (actor.uid)
			set_creds(&actor);
		switch (*kind) {
		case 0:
			r = chmod(a, S_ISDIR(o->st_mode) ?
				  dir_modes[pick(sizeof dir_modes / sizeof dir_modes[0])] :
				  file_modes[pick(sizeof file_modes / sizeof file_modes[0])]);
			break;
		case 1:
			r = chown(a, pick(4) ? uids[pick(3)] : (uid_t)-1, gids[pick(2)]);
			break;
		case 2:
			if (pick(3))
				r = set_named_acl(a, o->st_mode & 0777, pick(2) ? 1002 : 1004,
						  acl_perms[pick(4)]);
			else if ((r = removexattr(a, "system.posix_acl_access")) && errno == ENODATA)
				r = 0;
			break;
		case 3:
			r = rename(a, b);
			break;
		case 4:
			r = link(a, b);
			break;
		default:
			r = unlink(a);
			break;
		}
		if (!r)
			_exit(0);
		if (errno == EACCES || errno == EPERM)
			_exit(1);
		if (errno == EXDEV || errno == EINVAL || errno == ENOTEMPTY || errno == EISDIR ||
		    errno == ENOTDIR || errno == EEXIST || errno == EBUSY || errno == ENOENT)
			_exit(3);
		fprintf(stderr, "%s %s: %s\n", op_names[*kind], a, strerror(errno));
		_exit(2);
	}
	if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st))
		return 2;
	return WEXITSTATUS(st);
}

/* what a done operation makes history-dependent until the next mount */
static void churn_taint(const struct tree *t, int kind, int target, int during_build)
{
	int dir = target < 0 || S_ISDIR(t->v[target].st.st_mode);

	if (dir && kind != 4 && kind != 5)	/* a directory's permissions changed or it moved */
		taint_under(t, target);
	else if (target >= 0 && (kind == 4 || during_build))
		taint(t->v[target].st.st_ino);	/* a second name, or a change during a build */
}

/* In a child with the identity's credentials: 0 all equal, 1 mismatch, 2 error. */
static int churn_check_one(const struct ident *id, const char *layer, const char *lower,
			   const struct tree *t, const int *expect, int *counts)
{
	static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];
	char path[4096], rel[MOOB_QUERY_PATH_MAX + 1];
	int can[CH_MAX], i, j, p, fd, bad = 0, dom_ok;
	struct moob_query2 q;
	DIR *dir;

	set_creds(id);
	snprintf(path, sizeof path, "%s/pm", lower);
	dom_ok = !faccessat(AT_FDCWD, path, R_OK | X_OK, AT_EACCESS);
	for (i = 0; i < t->n; i++) {	/* parents sort before their children */
		snprintf(path, sizeof path, "%s/pm/%s", lower, t->v[i].rel);
		p = t->v[i].parent;
		if (!dom_ok || (p >= 0 && !can[p])) {
			can[i] = 0;
		} else if (S_ISDIR(t->v[i].st.st_mode)) {
			dir = opendir(path);
			can[i] = dir != NULL;
			if (dir)
				closedir(dir);
			else if (errno != EACCES)
				goto error;
		} else {
			can[i] = getxattr(path, MOOB_ATTR_PREFIX "pm", NULL, 0) >= 0;
			if (!can[i] && errno != EACCES)
				goto error;
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
		if (errno != EACCES) {
			perror("QUERY2");
			return 2;
		}
		q.count = 0;
	}
	if (fd >= 0)
		close(fd);
	if (q.truncated) {
		fprintf(stderr, "truncated answer\n");
		return 2;
	}
	counts[0] += (int)q.count;
	for (j = 0; j < (int)q.count; j++) {
		const char *why = NULL;

		counts[2]++;
		if (res[j].flags & MOOB_RES_PATH_UNKNOWN) {
			counts[3]++;
			for (i = 0; i < t->n; i++)
				if (is_file(t, i) && t->v[i].st.st_ino == res[j].ino && can[i])
					break;
			if (i == t->n)
				why = "no reachable name of the inode";
			snprintf(rel, sizeof rel, "?");
		} else {
			memcpy(rel, res[j].path, res[j].path_len < MOOB_QUERY_PATH_MAX ?
			       res[j].path_len : MOOB_QUERY_PATH_MAX);
			rel[res[j].path_len < MOOB_QUERY_PATH_MAX ? res[j].path_len : MOOB_QUERY_PATH_MAX] = 0;
			for (i = 0; i < t->n; i++)
				if (!strcmp(t->v[i].rel, rel))
					break;
			if (i == t->n || !is_file(t, i))
				why = "the path names no file";
			else if (t->v[i].st.st_ino != res[j].ino)
				why = "the path names another inode";
			else if (!can[i])
				why = "not reachable";
		}
		if (why) {
			printf("MISMATCH uid=%u gid=%u group=%d: returned %s ino=%llu: %s\n", id->uid,
			       id->gid, id->ngroups ? (int)id->groups[0] : -1, rel,
			       (unsigned long long)res[j].ino, why);
			bad = 1;
		}
	}
	for (i = 0; i < t->n; i++) {
		if (!expect[i] || !can[i])
			continue;
		counts[1]++;
		for (j = 0; j < (int)q.count; j++)
			if (res[j].ino == t->v[i].st.st_ino)
				break;
		if (j == (int)q.count) {
			printf("MISMATCH uid=%u gid=%u group=%d: %s reachable and homogeneous, not returned\n",
			       id->uid, id->gid, id->ngroups ? (int)id->groups[0] : -1, t->v[i].rel);
			bad = 1;
		}
	}
	return bad;
error:
	perror(path);
	return 2;
}

/* QUERY2 for every identity against the lower as it is now; 0 equal, 1 mismatch, 2 error */
static int churn_check(const char *layer, const char *lower, struct churn_stats *s)
{
	static struct tree t;
	int expect[CH_MAX], i, k, bad = 0;

	if (load_tree(lower, &t))
		return 2;
	/* a file seen with several names stays out after going back to one (spec §3, Q2a) */
	taint_links(&t);
	for (i = 0; i < t.n; i++)
		expect[i] = is_file(&t, i) && !is_tainted(t.v[i].st.st_ino) && homogeneous(&t, i);
	for (k = 0; k < (int)(sizeof idents / sizeof idents[0]); k++) {
		int shared[2], st, counts[4] = { 0, 0, 0, 0 };
		pid_t pid;

		if (pipe(shared))
			return 2;
		fflush(stdout);
		pid = fork();
		if (pid == 0) {
			int r = churn_check_one(&idents[k], layer, lower, &t, expect, counts);

			if (write(shared[1], counts, sizeof counts) != sizeof counts)
				r = 2;
			fflush(stdout);
			_exit(r);
		}
		close(shared[1]);
		if (read(shared[0], counts, sizeof counts) != sizeof counts)
			memset(counts, 0, sizeof counts);
		close(shared[0]);
		if (pid < 0 || waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) == 2)
			return 2;
		bad |= WEXITSTATUS(st);
		s->checks++;
		s->returned += counts[0];
		s->expected += counts[1];
		s->leak_checked += counts[2];
		s->unknown_path += counts[3];
	}
	return bad;
}

static int record_op(const struct tree *t, int r, int kind, int target, int by_uid, int during,
		     struct churn_stats *s)
{
	if (r == 2)
		return -1;
	s->ops++;
	if (r == 1)
		s->denied++;
	else if (r == 3)
		s->refused++;
	else {
		s->ok++;
		s->kind_ok[kind]++;
		s->by_uid_ok += by_uid;
		s->during_build += during;
		churn_taint(t, kind, target, during);
	}
	return 0;
}

/*
 * A build of the domain in progress: a root child sends QUERY2 with scan_delay_us set while
 * the parent applies up to nops operations. An operation counts as during the build when the
 * child had not answered when it returned.
 */
static int build_phase(const char *layer, const char *lower, unsigned int delay, int nops,
		       unsigned int *serial, struct churn_stats *s)
{
	static struct tree t;
	int sync[2], st = 0, k, kind, target, by_uid, r, built = 0;
	pid_t pid;
	char c;

	if (write_delay(delay) || pipe(sync))
		return -1;
	pid = fork();
	if (pid == 0) {
		static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];
		struct moob_query2 q;
		struct timespec a, b;
		char path[4096];
		int fd;

		close(sync[0]);
		snprintf(path, sizeof path, "%s/pm", layer);
		fd = open(path, O_RDONLY | O_DIRECTORY);
		memset(&q, 0, sizeof q);
		q.version = MOOB_QUERY2_VERSION;
		q.nterms = 1;
		q.limit = MOOB_QUERY_MAX_LIMIT;
		q.results = (unsigned long)res;
		strcpy(q.terms[0].name, "pm");
		q.terms[0].op = MOOB_OP_EXISTS;
		if (write(sync[1], "x", 1) != 1)
			_exit(2);
		clock_gettime(CLOCK_MONOTONIC, &a);
		if (fd < 0 || ioctl(fd, MOOB_IOC_QUERY2, &q))
			_exit(2);
		clock_gettime(CLOCK_MONOTONIC, &b);
		/* slow enough to be a build: half of the delays of the files planted */
		_exit((b.tv_sec - a.tv_sec) * 1000000 + (b.tv_nsec - a.tv_nsec) / 1000 >
		      (long)delay * NFILES / 2 ? 1 : 0);
	}
	close(sync[1]);
	k = pid < 0 ? 0 : (int)read(sync[0], &c, 1);
	close(sync[0]);
	if (k != 1)
		return -1;
	usleep(1000);
	for (k = 0; k < nops; k++) {
		int during;

		if (load_tree(lower, &t))
			return -1;
		taint_links(&t);
		r = churn_op(layer, &t, &kind, &target, &by_uid, serial);
		during = waitpid(pid, &st, WNOHANG) == 0;
		if (record_op(&t, r, kind, target, by_uid, during, s))
			return -1;
		if (!during) {
			built = 1;
			break;
		}
		usleep(500);
	}
	if (!built && waitpid(pid, &st, 0) < 0)
		return -1;
	if (write_delay(0) || !WIFEXITED(st) || WEXITSTATUS(st) == 2) {
		fprintf(stderr, "the query of the build phase failed\n");
		return -1;
	}
	s->builds += WEXITSTATUS(st);
	return 0;
}

/*
 * Before the first mount, on the lower: a directory below two of the planted ones, with a
 * random mode and owner, and some files moved into it, so that a path crosses two directories
 * between the domain and the file.
 */
static int deepen(const char *lower)
{
	char a[4096], b[4096];
	int i, k, f;

	for (i = 0; i < 2; i++) {
		snprintf(a, sizeof a, "%s/pm/s%d/t%d", lower, i, i);
		if (mkdir(a, 0755) || chown(a, uids[pick(3)], gids[pick(2)]) ||
		    chmod(a, dir_modes[pick(sizeof dir_modes / sizeof dir_modes[0])]))
			goto fail;
		for (k = 0; k < 4; k++) {
			f = (int)pick(NFILES);
			snprintf(a, sizeof a, "%s/pm/f%d", lower, f);
			snprintf(b, sizeof b, "%s/pm/s%d/t%d/f%d", lower, i, i, f);
			if (rename(a, b) && errno != ENOENT)
				goto fail;
		}
	}
	return 0;
fail:
	perror(a);
	return -1;
}

static int churn(const char *layer, const char *lower, unsigned long long seed, int rounds,
		 int steps, unsigned int delay)
{
	static struct tree t;
	struct churn_stats s;
	char opts[4200];
	unsigned int serial = 0;
	int round, step, r, kind, target, by_uid;

	memset(&s, 0, sizeof s);
	rng = seed * 7919 + 17;
	snprintf(opts, sizeof opts, "lower=%s,domain_depth=1", lower);
	if (deepen(lower))
		return 2;
	for (round = 0; round < rounds; round++) {
		int slow = delay && (round & 1);

		ntainted = taint_all = 0;
		if (mount("none", layer, "moobfs_layer", 0, opts)) {
			perror("mount");
			return 2;
		}
		if (load_tree(lower, &t))
			goto error;
		taint_links(&t);
		if (slow && build_phase(layer, lower, delay, 8, &serial, &s))
			goto error;
		for (step = 0; step < steps; step++) {
			if (load_tree(lower, &t))
				goto error;
			taint_links(&t);
			r = churn_op(layer, &t, &kind, &target, &by_uid, &serial);
			if (record_op(&t, r, kind, target, by_uid, 0, &s))
				goto error;
			/* a directory renamed: the next query builds the domain again (domain_fresh) */
			if (slow && !r && kind == 3 && S_ISDIR(t.v[target].st.st_mode) &&
			    build_phase(layer, lower, delay, 4, &serial, &s))
				goto error;
			r = churn_check(layer, lower, &s);
			if (r == 2)
				goto error;
			if (r) {
				printf("after %s of %s (round %d, step %d)\n", op_names[kind],
				       target >= 0 ? t.v[target].rel : "the domain directory", round, step);
				s.mismatches++;
			}
		}
		if (umount(layer)) {
			perror("umount");
			return 2;
		}
	}
	printf("PERMCHURN%s seed=%llu rounds=%d steps=%d ops=%d done=%d denied=%d refused=%d "
	       "done_by_uid=%d during_build=%d builds=%d checks=%d returned=%d path_unknown=%d "
	       "expected_checked=%d chmod=%d chown=%d setfacl=%d rename=%d link=%d unlink=%d "
	       "mismatching_steps=%d\n",
	       s.mismatches ? "_FAIL" : "_PASS", seed, rounds, steps, s.ops, s.ok, s.denied,
	       s.refused, s.by_uid_ok, s.during_build, s.builds, s.checks, s.returned,
	       s.unknown_path, s.expected, s.kind_ok[0], s.kind_ok[1], s.kind_ok[2], s.kind_ok[3],
	       s.kind_ok[4], s.kind_ok[5], s.mismatches);
	return s.mismatches ? 1 : 0;
error:
	write_delay(0);
	umount(layer);
	return 2;
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "plant"))
		return plant(argv[2], strtoull(argv[3], NULL, 10));
	if (argc == 4 && !strcmp(argv[1], "check"))
		return check(argv[2], argv[3]);
	if (argc == 8 && !strcmp(argv[1], "churn"))
		return churn(argv[2], argv[3], strtoull(argv[4], NULL, 10), atoi(argv[5]),
			     atoi(argv[6]), (unsigned int)strtoul(argv[7], NULL, 10));
	fprintf(stderr, "usage: permmatrix plant <lower> <seed> | check <layer> <lower> |\n"
		"       churn <layer> <lower> <seed> <rounds> <steps> <delay us>\n");
	return 2;
}
