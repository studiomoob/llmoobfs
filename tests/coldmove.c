// SPDX-License-Identifier: GPL-2.0-only
/*
 * coldmove LAYER LOWERDIR
 *
 * Regression test: a permission change (chmod) on one name of a file that has hard links in
 * two domains, A (built by a query) and B (cold: no query touched it since mount), must not
 * build B, release any domain, or leave A's answers out of step with the homogeneity rule
 * (spec-query-v2.md sections 2, 3, 4.2, 5 and 9). Written from the specification only.
 *
 * LAYER    the mounted layer (domain_depth=1), root of the passthrough view
 * LOWERDIR the lower directory behind it (used to read back what the lower decides)
 *
 * Must run as root (GET_INFO counters are CAP_SYS_ADMIN only). Prints COLDMOVE_PASS and exits 0
 * only if every check holds; otherwise prints "COLDMOVE_FAIL: <which check>" and exits 1.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <inttypes.h>

#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

#define MODE_OK 0644	/* consistent with a 0755 root:root domain directory (section 3, cond. 2) */
#define MODE_BAD 0600	/* group and others cannot read: heterogeneous (section 3) */
#define NOBODY 65534U

static const char *layer, *lower;
static char scen[96];		/* current scenario label, for messages */

static void fail(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
static void fail(const char *fmt, ...)
{
	va_list ap;
	int e = errno;

	fprintf(stderr, "COLDMOVE_FAIL: [%s] ", scen);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, " (errno=%d %s)\n", e, strerror(e));
	exit(1);
}

#define CK(c, ...) do { if (!(c)) { fail(__VA_ARGS__); } } while (0)

static void path2(char *out, size_t n, const char *root, const char *a, const char *b)
{
	int r = snprintf(out, n, "%s/%s%s%s", root, a, b ? "/" : "", b ? b : "");

	if (r < 0 || (size_t)r >= n)
		fail("path too long");
}

/* ---- GET_INFO ---- */

static struct moob_info get_info(void)
{
	struct moob_info in;
	int fd = open(layer, O_RDONLY | O_DIRECTORY);

	if (fd < 0)
		fail("open layer root %s", layer);
	memset(&in, 0, sizeof in);
	if (ioctl(fd, MOOB_IOC_GET_INFO, &in))
		fail("MOOB_IOC_GET_INFO");
	close(fd);
	return in;
}

/* B (and everything else) must be exactly as it was after A was built: no extra build, no
 * release, no fallback domain. */
static void same_domains(const char *step, const struct moob_info *ref)
{
	struct moob_info n = get_info();

	CK(n.domains_built == ref->domains_built,
	   "%s: domains_built %u, expected %u (a cold domain was built, or one was dropped)",
	   step, n.domains_built, ref->domains_built);
	/* section 9 lets A be taken down as stale and built again by its next query, so
	 * domain_builds may grow; it must never fall. A build of B shows in domains_built. */
	CK(n.domain_builds >= ref->domain_builds,
	   "%s: domain_builds %u fell below %u", step, n.domain_builds, ref->domain_builds);
	CK(n.domain_releases == ref->domain_releases,
	   "%s: domain_releases %u, expected %u (a domain was released unexpectedly)",
	   step, n.domain_releases, ref->domain_releases);
	CK(n.fallback_domains == ref->fallback_domains,
	   "%s: fallback_domains %u, expected %u", step, n.fallback_domains, ref->fallback_domains);
}

/* ---- files ---- */

enum { F0, F1, CTL, NFILES };
static const char *const fname[NFILES] = { "f0", "f1", "c" };
static const char *const aname[2] = { "l0", "l1" };	/* names in B */
static uint64_t fino[NFILES];
static int ctl_present = 1;	/* the control file is expected in answers */

static void set_kind(const char *path)
{
	unsigned char v[1 + 4];

	v[0] = MOOB_T_STRING;
	memcpy(v + 1, "note", 4);
	if (setxattr(path, MOOB_ATTR_PREFIX "kind", v, sizeof v, 0))
		fail("setxattr user.moob.kind on %s", path);
}

static void make_file(const char *dom, const char *name, uint64_t *ino)
{
	char p[4096];
	struct stat st;
	int fd;

	path2(p, sizeof p, layer, dom, name);
	fd = open(p, O_WRONLY | O_CREAT | O_EXCL, MODE_OK);
	if (fd < 0)
		fail("create %s", p);
	if (write(fd, "x", 1) != 1)
		fail("write %s", p);
	close(fd);
	if (chmod(p, MODE_OK))
		fail("chmod %s", p);
	set_kind(p);
	if (lstat(p, &st))
		fail("lstat %s", p);
	*ino = st.st_ino;
}

static void do_chmod(const char *dom, const char *name, mode_t m)
{
	char p[4096];

	path2(p, sizeof p, layer, dom, name);
	if (chmod(p, m))
		fail("chmod %o %s", (unsigned)m, p);
}

static void do_link(const char *d1, const char *n1, const char *d2, const char *n2)
{
	char a[4096], b[4096];

	path2(a, sizeof a, layer, d1, n1);
	path2(b, sizeof b, layer, d2, n2);
	if (link(a, b))
		fail("link %s %s", a, b);
}

static void do_unlink(const char *dom, const char *name)
{
	char p[4096];

	path2(p, sizeof p, layer, dom, name);
	if (unlink(p))
		fail("unlink %s", p);
}

static void do_rename(const char *d1, const char *n1, const char *d2, const char *n2)
{
	char a[4096], b[4096];

	path2(a, sizeof a, layer, d1, n1);
	path2(b, sizeof b, layer, d2, n2);
	if (rename(a, b))
		fail("rename %s %s", a, b);
}

/* What the lower filesystem itself says about the file named dom/name. */
static void lower_is(const char *dom, const char *name, mode_t mode, nlink_t nlink)
{
	char p[4096];
	struct stat st;

	path2(p, sizeof p, lower, dom, name);
	if (lstat(p, &st))
		fail("lower lstat %s", p);
	CK((st.st_mode & 07777) == mode, "lower %s mode %o, expected %o", p,
	   (unsigned)(st.st_mode & 07777), (unsigned)mode);
	CK(st.st_nlink == nlink, "lower %s nlink %u, expected %u", p, (unsigned)st.st_nlink,
	   (unsigned)nlink);
}

/* ---- QUERY2 ---- */

static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];

static void set_plan(struct moob_query2 *q, int plan)
{
	memset(q, 0, sizeof *q);
	q->version = MOOB_QUERY2_VERSION;
	q->results = (__u64)(uintptr_t)res;
	q->limit = MOOB_QUERY_MAX_LIMIT;
	switch (plan) {
	case 0:		/* equality term: kind == "note" */
		q->nterms = 1;
		strcpy(q->terms[0].name, "kind");
		q->terms[0].op = MOOB_OP_EQ;
		q->terms[0].type = MOOB_T_STRING;
		q->terms[0].len = 4;
		memcpy(q->terms[0].value, "note", 4);
		break;
	case 1:		/* existence term */
		q->nterms = 1;
		strcpy(q->terms[0].name, "kind");
		q->terms[0].op = MOOB_OP_EXISTS;
		break;
	default:	/* ordered plan, no terms */
		q->nterms = 0;
		strcpy(q->order_by, "kind");
		q->order = MOOB_ORDER_ASC;
		q->order_type = MOOB_T_STRING;
		break;
	}
}

/*
 * Query domain dom with the given plan and compare the inode set with the expected one:
 * the control file (unless ctl_present is 0), f0/f1 when their bit is set in mask. Not truncated, no duplicates,
 * nothing else. If check_open is set, every named result must be openable by the caller.
 */
static void check_set(const char *step, const char *dom, int plan, unsigned mask, int check_open)
{
	char dp[4096];
	struct moob_query2 q;
	uint64_t want[NFILES];
	int nwant = 0, fd, i, j;
	__u32 k;

	path2(dp, sizeof dp, layer, dom, NULL);
	fd = open(dp, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		fail("%s: open domain %s", step, dp);
	for (i = 0; i < NFILES; i++)
		if ((i == CTL && ctl_present) || (i != CTL && (mask & (1u << i))))
			want[nwant++] = fino[i];
	set_plan(&q, plan);
	if (ioctl(fd, MOOB_IOC_QUERY2, &q))
		fail("%s: QUERY2 plan %d on %s", step, plan, dom);
	CK(!q.truncated, "%s: plan %d truncated with limit %u", step, plan, q.limit);
	for (k = 0; k < q.count; k++) {
		int known = 0;

		for (i = 0; i < nwant; i++)
			known |= want[i] == res[k].ino;
		if (!known) {
			const char *nm = "?";

			for (j = 0; j < NFILES; j++)
				if (fino[j] == res[k].ino)
					nm = fname[j];
			fail("%s: plan %d returned %s (ino %" PRIu64 ", path '%.*s') which must not be a "
			     "member (mask %#x)", step, plan, nm, (uint64_t)res[k].ino,
			     (int)res[k].path_len, res[k].path, mask);
		}
		for (j = 0; j < (int)k; j++)
			CK(res[j].ino != res[k].ino, "%s: plan %d returned ino %" PRIu64 " twice",
			   step, plan, (uint64_t)res[k].ino);
		if (check_open && !(res[k].flags & MOOB_RES_PATH_UNKNOWN)) {
			int rfd = openat(fd, res[k].path, O_RDONLY);

			if (rfd < 0)
				fail("%s: plan %d result '%s' is not readable by the caller",
				     step, plan, res[k].path);
			close(rfd);
		}
	}
	if ((int)q.count != nwant) {
		for (i = 0; i < nwant; i++) {
			int found = 0;

			for (k = 0; k < q.count; k++)
				found |= want[i] == res[k].ino;
			if (!found) {
				for (j = 0; j < NFILES; j++)
					if (fino[j] == want[i])
						fail("%s: plan %d lacks %s (answer of %u, expected %d, "
						     "mask %#x)", step, plan, fname[j], q.count, nwant,
						     mask);
			}
		}
		fail("%s: plan %d count %u, expected %d", step, plan, q.count, nwant);
	}
	close(fd);
}

/* Run check_set as an unprivileged user in a child; the results must also be openable. */
static void check_set_nobody(const char *step, const char *dom, unsigned mask)
{
	pid_t p;
	int st;

	fflush(NULL);
	p = fork();
	if (p < 0)
		fail("%s: fork", step);
	if (!p) {
		if (setgroups(0, NULL) || setgid(NOBODY) || setuid(NOBODY)) {
			fprintf(stderr, "COLDMOVE_FAIL: [%s] %s: cannot drop privileges\n", scen, step);
			_exit(2);
		}
		check_set(step, dom, 0, mask, 1);	/* fail() exits 1 with its message */
		_exit(0);
	}
	if (waitpid(p, &st, 0) != p)
		fail("%s: waitpid", step);
	CK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "%s: unprivileged view of %s failed", step, dom);
}

static void check_all(const char *step, const char *dom, unsigned mask)
{
	int plan;

	for (plan = 0; plan < 3; plan++)
		check_set(step, dom, plan, mask, 0);
	check_set_nobody(step, dom, mask);
}

/* A short page must say truncated: an answer is never falsely complete. */
static void check_short_page(const char *step, const char *dom, int expect_members)
{
	char dp[4096];
	struct moob_query2 q;
	int fd;

	path2(dp, sizeof dp, layer, dom, NULL);
	fd = open(dp, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		fail("%s: open %s", step, dp);
	set_plan(&q, 0);
	q.limit = 1;
	if (ioctl(fd, MOOB_IOC_QUERY2, &q))
		fail("%s: QUERY2 limit 1", step);
	CK(q.count == 1, "%s: limit 1 returned %u results", step, q.count);
	CK(q.truncated == (expect_members > 1), "%s: truncated=%u with %d members and limit 1",
	   step, q.truncated, expect_members);
	close(fd);
}

/* ---- one scenario ---- */

#define BIT(i) (1u << (i))
#define BOTH (BIT(F0) | BIT(F1))

/*
 * link_first   hard links into B exist before A is built (else they are added afterwards)
 * rem_primary  remove the names in A (the files then live only in B and are renamed back into
 *              A) instead of removing the names in B
 * restore_early mode is restored to MODE_OK while both names still exist
 *
 * f0 is chmod-ed through its B name, f1 through its A name, so both paths are exercised in
 * every scenario.
 */
static void scenario(int id, int link_first, int rem_primary, int restore_early)
{
	char A[32], B[32], Z[32], step[96];
	struct moob_info pre, snap, fin, zb, za;
	uint64_t zino[NFILES];
	int i;

	snprintf(scen, sizeof scen, "s%d link_first=%d rem_primary=%d restore_early=%d", id,
		 link_first, rem_primary, restore_early);
	snprintf(A, sizeof A, "A%d", id);
	snprintf(B, sizeof B, "B%d", id);
	snprintf(Z, sizeof Z, "Z%d", id);

	/* two domains; the files exist in the lower before any query */
	for (i = 0; i < 2; i++) {
		char d[4096];

		path2(d, sizeof d, layer, i ? B : A, NULL);
		if (mkdir(d, 0755) || chmod(d, 0755))
			fail("mkdir %s", d);
	}
	pre = get_info();
	CK(pre.flags & MOOB_FLAG_COUNTERS, "GET_INFO counters hidden: run as root");
	CK(pre.flags & MOOB_FLAG_DOMAINS, "domains are off: mount with domain_depth=1");
	CK(pre.flags & MOOB_FLAG_ON_DEMAND, "index is off or not on demand");
	for (i = 0; i < NFILES; i++)
		make_file(A, fname[i], &fino[i]);
	if (link_first) {
		do_link(A, "f0", B, aname[0]);
		do_link(A, "f1", B, aname[1]);
		lower_is(A, "f0", MODE_OK, 2);
		lower_is(A, "f1", MODE_OK, 2);
	}
	same_domains("setup", &pre);	/* creating and linking built nothing */

	/* first query builds A (and only A) */
	snprintf(step, sizeof step, "first query on A");
	if (link_first)
		check_set(step, A, 0, 0, 0);	/* names in two domains: not members (section 3) */
	else
		check_set(step, A, 0, BOTH, 0);	/* control query: the normal case works */
	snap = get_info();
	CK(snap.domains_built == pre.domains_built + 1 && snap.domain_builds == pre.domain_builds + 1,
	   "first query built %u domains (%u builds), expected exactly A",
	   snap.domains_built - pre.domains_built, snap.domain_builds - pre.domain_builds);
	CK(snap.domain_releases == pre.domain_releases, "first query released a domain");
	same_domains("after build", &snap);

	if (!link_first) {
		/* A is built, B is cold: the new names land in the cold domain */
		do_link(A, "f0", B, aname[0]);
		do_link(A, "f1", B, aname[1]);
		lower_is(A, "f0", MODE_OK, 2);
		lower_is(A, "f1", MODE_OK, 2);
		same_domains("link into cold B", &snap);
		/* section 3 leaves the answer right after a link open: no membership check here */
	}

	/* the change under test: chmod makes the file inconsistent with A's rule */
	do_chmod(B, aname[0], MODE_BAD);	/* through the cold B path */
	do_chmod(A, "f1", MODE_BAD);		/* through the A path */
	lower_is(A, "f0", MODE_BAD, 2);
	lower_is(A, "f1", MODE_BAD, 2);
	same_domains("chmod 0600", &snap);
	check_all("chmod 0600 with two names", A, 0);
	same_domains("query after chmod 0600", &snap);

	if (restore_early) {
		do_chmod(B, aname[0], MODE_OK);
		do_chmod(A, "f1", MODE_OK);
		lower_is(A, "f0", MODE_OK, 2);
		same_domains("early restore", &snap);
		/* mode is fine again but the file still has a name in another domain */
		check_all("restored while linked in B", A, 0);
		same_domains("query after early restore", &snap);
	}

	if (!rem_primary) {
		do_unlink(B, aname[0]);
		do_unlink(B, aname[1]);
		lower_is(A, "f0", restore_early ? MODE_OK : MODE_BAD, 1);
		lower_is(A, "f1", restore_early ? MODE_OK : MODE_BAD, 1);
		same_domains("unlink alias", &snap);
		if (restore_early) {
			/* one name again, mode fine: may stay out until the next change (section 3) */
			do_chmod(A, "f0", MODE_BAD);
			do_chmod(A, "f1", MODE_BAD);
			same_domains("chmod 0600 after unlink", &snap);
		}
		check_all("alias removed, mode still 0600", A, 0);
		do_chmod(A, "f0", MODE_OK);
		do_chmod(A, "f1", MODE_OK);
		lower_is(A, "f0", MODE_OK, 1);
		same_domains("restore after unlink", &snap);
		check_all("alias removed, mode restored", A, BOTH);
	} else {
		do_unlink(A, "f0");
		do_unlink(A, "f1");
		lower_is(B, aname[0], restore_early ? MODE_OK : MODE_BAD, 1);
		same_domains("unlink primary", &snap);
		check_all("no name left in A", A, 0);
		if (restore_early) {
			do_chmod(B, aname[0], MODE_BAD);	/* cold path again */
			same_domains("chmod 0600 via B alone", &snap);
		} else {
			do_chmod(B, aname[1], MODE_OK);		/* cold path */
			same_domains("chmod 0644 via B alone", &snap);
		}
		check_all("chmod through B with no name in A", A, 0);
		same_domains("query with no name in A", &snap);

		/* f0 comes back into A first: with its current mode */
		do_rename(B, aname[0], A, "f0");
		same_domains("rename f0 into A", &snap);
		check_all("f0 renamed in at 0600", A, 0);
		do_chmod(A, "f0", MODE_OK);
		same_domains("restore f0", &snap);
		check_all("f0 back and consistent", A, BIT(F0));
		/* f1 is consistent when it arrives: section 5 says the rule is evaluated there */
		do_rename(B, aname[1], A, "f1");
		lower_is(A, "f1", MODE_OK, 1);
		same_domains("rename f1 into A", &snap);
		check_all("f1 renamed in, consistent", A, BOTH);
	}
	check_short_page("short page", A, 3);
	same_domains("end of scenario", &snap);

	/* cleanup, then the counters must be what they were before the files existed */
	for (i = 0; i < NFILES; i++)
		do_unlink(A, fname[i]);
	same_domains("cleanup", &snap);
	ctl_present = 0;
	check_all("empty A", A, 0);	/* control file gone too: nothing may remain */
	ctl_present = 1;
	fin = get_info();
	CK(fin.index_entries == pre.index_entries, "index_entries %u after cleanup, was %u",
	   fin.index_entries, pre.index_entries);
	CK(fin.index_inodes == pre.index_inodes, "index_inodes %u after cleanup, was %u",
	   fin.index_inodes, pre.index_inodes);
	CK(fin.heterogeneous == pre.heterogeneous, "heterogeneous %u after cleanup, was %u",
	   fin.heterogeneous, pre.heterogeneous);
	CK(fin.fallback_domains == pre.fallback_domains, "fallback_domains changed");
	CK(fin.domains_built == pre.domains_built + 1, "domains_built %u after cleanup, expected %u",
	   fin.domains_built, pre.domains_built + 1);
	CK(fin.domain_builds >= pre.domain_builds + 1, "domain_builds %u after cleanup, expected at least %u",
	   fin.domain_builds, pre.domain_builds + 1);
	CK(fin.domain_releases == pre.domain_releases, "domain_releases changed during the scenario");

	/*
	 * Bytes: an emptied A must cost what another built domain costs after the same life
	 * cycle (files with the attribute created, queried, removed). Z is that reference.
	 */
	{
		char d[4096];

		path2(d, sizeof d, layer, Z, NULL);
		if (mkdir(d, 0755) || chmod(d, 0755))
			fail("mkdir %s", d);
	}
	zb = get_info();
	for (i = 0; i < NFILES; i++)
		make_file(Z, fname[i], &zino[i]);
	{
		uint64_t keep[NFILES];

		memcpy(keep, fino, sizeof keep);
		memcpy(fino, zino, sizeof fino);
		check_set("reference domain", Z, 0, BOTH, 0);
		memcpy(fino, keep, sizeof fino);
	}
	for (i = 0; i < NFILES; i++)
		do_unlink(Z, fname[i]);
	za = get_info();
	CK(za.index_entries == pre.index_entries && za.index_inodes == pre.index_inodes,
	   "reference domain did not return to %u entries / %u inodes", pre.index_entries,
	   pre.index_inodes);
	CK(fin.index_used - pre.index_used == za.index_used - zb.index_used,
	   "index_used: emptied A holds %" PRIu64 " bytes over the start, an emptied reference domain "
	   "%" PRIu64, (uint64_t)(fin.index_used - pre.index_used),
	   (uint64_t)(za.index_used - zb.index_used));

	for (i = 0; i < 3; i++) {
		char d[4096];

		path2(d, sizeof d, layer, i == 0 ? A : i == 1 ? B : Z, NULL);
		rmdir(d);
	}
}

int main(int argc, char **argv)
{
	int id = 0, link_first, rem_primary, restore_early;

	if (argc != 3) {
		fprintf(stderr, "usage: %s LAYER LOWERDIR\n", argv[0]);
		return 64;
	}
	if (geteuid()) {
		fprintf(stderr, "COLDMOVE_FAIL: must run as root\n");
		return 64;
	}
	layer = argv[1];
	lower = argv[2];
	umask(0);
	for (link_first = 0; link_first < 2; link_first++)
		for (rem_primary = 0; rem_primary < 2; rem_primary++)
			for (restore_early = 0; restore_early < 2; restore_early++)
				scenario(id++, link_first, rem_primary, restore_early);
	puts("COLDMOVE_PASS");
	return 0;
}
