/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ioctl client for the MoobFS layer, used by the VM tests.
 *   query [--uid N] <mount> <name> <op> <type> <value> [...]   prints "<ino>\t<path>" sorted by ino
 *   --uid N drops privileges to uid/gid N before opening, to test isolation.
 *   query <mount> info                               prints GET_INFO fields
 *   query <mount> invalid                            checks that malformed requests are refused
 *   query [--uid N] <domain-dir> q2 [opts] <terms...>  QUERY2 on a domain directory (paths relative
 *                                                    to it); sorted by ino unless --order is given
 *     opts: --order NAME TYPE asc|desc   order by an attribute (terms may then be empty)
 *           --limit N                    results per call (default 4096)
 *           --pages                      follow the cursor until the answer is complete
 *           --cursor-in FILE             start from a cursor saved by --cursor-out
 *           --cursor-out FILE            save the cursor returned by the (first) call
 *           --raw                        print in the kernel's order (no sort by ino)
 *           --bad-results                an unmapped results address (EFAULT path, journal 0113)
 *   query [--uid N] <domain-dir> q2batch [--limit N] -- <terms> [-- <terms>]...
 *                                                    one QUERY2_BATCH; prints "q<k>\t<ino>\t<path>"
 *                                                    per request, sorted by ino within each
 *   query [--uid N] <dir> q2batcherrno -- <terms>    prints the errno of QUERY2_BATCH
 *   An operator ending in '?' (gt?, eq?, ...) sets MOOB_TERM_MISSING_OK on that term.
 *   query <domain-dir> q2invalid                     malformed QUERY2 requests are refused
 *   query [--uid N] <dir> q2errno [opts] <terms...>  prints the errno of QUERY2 (0 on success)
 *   query [--uid N] <x> rename <old> <new>           prints the errno of rename(2) (0 on success)
 *   query [--uid N] <x> xset <file> <name> <string>  sets user.moob.<name> to a typed string; prints
 *                                                    the errno of setxattr(2) (0 on success)
 *   query [--uid N] <mount> v1bad                    a v1 QUERY with a bad version; prints its errno
 *   query [--uid N] <domain-dir> race2 <seconds>     QUERY2 under churn (spec test 8): cycles the
 *                                                    plain, equality and ordered plans with small
 *                                                    pages; results must be well formed and openable
 *                                                    unless changed after the answer; ESTALE (fallback
 *                                                    switched) restarts paging, EACCES (domain closed
 *                                                    by chmod) is counted
 *   query [--uid N] <mount> race <seconds>           repeats QUERY kind eq note while another
 *                                                    process changes modes, names and inodes;
 *                                                    every result must be well formed and
 *                                                    openable unless it was concurrently removed
 */
#include <time.h>
#include <sys/syscall.h>
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <grp.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <inttypes.h>
#include "../tools/query-common.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s (errno=%d %s)\n", __LINE__, #c, errno, strerror(errno)); return 1; } } while (0)

static int by_ino(const void *a, const void *b)
{
	const struct moob_query_result *x = a, *y = b;
	return x->ino < y->ino ? -1 : x->ino > y->ino;
}

static int run_query(int fd, struct moob_query *q, struct moob_query_result *res, __u32 limit)
{
	q->version = MOOB_UAPI_VERSION;
	q->limit = limit;
	q->results = (__u64)(uintptr_t)res;
	return ioctl(fd, MOOB_IOC_QUERY, q);
}

int main(int argc, char **argv)
{
	int fd;
	struct moob_query q;
	struct moob_query_result *res;

	if (argc >= 3 && !strcmp(argv[1], "--uid")) {
		uid_t u = (uid_t)strtoul(argv[2], NULL, 10);
		/* the same identity as moobfs-index --uid, which the suite compares it with */
		if ((!geteuid() && setgroups(0, NULL)) || setgid(u) || setuid(u)) { perror("setuid"); return 1; }
		argv += 2; argc -= 2;
	}
	if (argc >= 4 && !strcmp(argv[2], "open")) {	/* query [--uid N] <mount> open <relpath> */
		char full[8192]; char c;
		snprintf(full, sizeof full, "%s/%s", argv[1], argv[3]);
		fd = open(full, O_RDONLY);
		if (fd < 0) { printf("open %s: %s\n", argv[3], strerror(errno)); return errno == EACCES ? 3 : 2; }
		if (read(fd, &c, 1) < 0) { printf("read %s: %s\n", argv[3], strerror(errno)); return 2; }
		printf("open %s: ok\n", argv[3]);
		return 0;
	}
	if (argc >= 5 && !strcmp(argv[2], "rename")) {
		printf("rename %d\n", rename(argv[3], argv[4]) ? errno : 0);
		return 0;
	}
	if (argc >= 5 && !strcmp(argv[2], "whiteout")) {	/* refused by the layer (V6, 0155) */
		printf("whiteout %d\n", syscall(SYS_renameat2, AT_FDCWD, argv[3], AT_FDCWD, argv[4], 1u << 2 /* RENAME_WHITEOUT */) ? errno : 0);
		return 0;
	}
	if (argc >= 6 && !strcmp(argv[2], "xset")) {	/* typed string: type byte, then UTF-8 */
		char name[MOOB_QUERY_NAME_MAX + 16], val[MOOB_QUERY_VALUE_MAX + 1];
		size_t n = strlen(argv[5]);

		if (n > MOOB_QUERY_VALUE_MAX)
			return 64;
		snprintf(name, sizeof(name), "user.moob.%s", argv[4]);
		val[0] = MOOB_T_STRING;
		memcpy(val + 1, argv[5], n);
		printf("xset %d\n", setxattr(argv[3], name, val, n + 1, 0) ? errno : 0);
		return 0;
	}
	if (argc < 3)
		return 64;
	fd = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (fd < 0 && argc >= 3 && !strcmp(argv[2], "q2errno")) {
		printf("q2 %d\n", errno);
		return 0;
	}
	if (fd < 0 && argc >= 3 && !strcmp(argv[2], "q2batcherrno")) {
		printf("q2batch %d\n", errno);
		return 0;
	}
	if (fd < 0) { perror("open"); return 1; }
	if (!strcmp(argv[2], "v1bad")) {	/* refused before anything is built (journal 0105) */
		memset(&q, 0, sizeof q);
		q.version = 99;
		q.nterms = 1;
		q.limit = 1;
		printf("q1 %d\n", ioctl(fd, MOOB_IOC_QUERY, &q) ? errno : 0);
		return 0;
	}
	res = calloc(MOOB_QUERY_MAX_LIMIT, sizeof *res);
	if (!res) return 1;

	if (!strcmp(argv[2], "q2") || !strcmp(argv[2], "q2errno")) {
		struct moob_query2 q2;
		struct moob_query_result *all = NULL;
		const char *cin = NULL, *cout = NULL;
		size_t nall = 0;
		int n, r, k = 3, pages = 0, first = 1, raw = 0, bad = 0, errmode = !strcmp(argv[2], "q2errno");
		__u32 i, limit = MOOB_QUERY_MAX_LIMIT;

		memset(&q2, 0, sizeof q2);
		while (k < argc && !strncmp(argv[k], "--", 2)) {
			if (!strcmp(argv[k], "--order") && k + 3 < argc) {
				int t = parse_type(argv[k + 2]);
				size_t olen = strlen(argv[k + 1]);

				if (t < 0 || olen >= sizeof q2.order_by) return 65;
				memcpy(q2.order_by, argv[k + 1], olen + 1);
				q2.order_type = (__u8)t;
				q2.order = !strcmp(argv[k + 3], "desc") ? MOOB_ORDER_DESC : MOOB_ORDER_ASC;
				k += 4;
			} else if (!strcmp(argv[k], "--limit") && k + 1 < argc) {
				limit = (__u32)strtoul(argv[k + 1], NULL, 10); k += 2;
			} else if (!strcmp(argv[k], "--pages")) {
				pages = 1; k++;
			} else if (!strcmp(argv[k], "--raw")) {
				raw = 1; k++;
			} else if (!strcmp(argv[k], "--bad-results")) {
				bad = 1; k++;
			} else if (!strcmp(argv[k], "--cursor-in") && k + 1 < argc) {
				cin = argv[k + 1]; k += 2;
			} else if (!strcmp(argv[k], "--cursor-out") && k + 1 < argc) {
				cout = argv[k + 1]; k += 2;
			} else {
				fprintf(stderr, "query: bad q2 option %s\n", argv[k]); return 64;
			}
		}
		n = parse_terms(argc, argv, k, q2.terms);
		if (n < 0) { fprintf(stderr, "query: invalid terms\n"); return 65; }
		if (cin) {
			FILE *f = fopen(cin, "rb");
			if (!f || fread(q2.cursor, 1, sizeof q2.cursor, f) != sizeof q2.cursor) { perror(cin); return 1; }
			fclose(f);
		}
		q2.version = MOOB_QUERY2_VERSION;
		q2.nterms = (__u32)n;
		q2.limit = limit;
		q2.results = bad ? (__u64)8 : (__u64)(uintptr_t)res;	/* page 0 is never mapped */
		for (;;) {
			r = ioctl(fd, MOOB_IOC_QUERY2, &q2);
			if (errmode) { printf("q2 %d\n", r ? errno : 0); return 0; }
			if (r) { perror("MOOB_IOC_QUERY2"); return 1; }
			if (first && cout) {
				FILE *f = fopen(cout, "wb");
				if (!f || fwrite(q2.cursor, 1, sizeof q2.cursor, f) != sizeof q2.cursor) { perror(cout); return 1; }
				fclose(f);
			}
			first = 0;
			all = realloc(all, (nall + q2.count + 1) * sizeof *all);
			if (!all) return 1;
			memcpy(all + nall, res, q2.count * sizeof *res);
			nall += q2.count;
			if (!q2.truncated || !pages)
				break;
		}
		if (!q2.order_by[0] && !raw)
			qsort(all, nall, sizeof *all, by_ino);
		for (i = 0; i < nall; i++)
			printf("%" PRIu64 "\t%s%s\n", (uint64_t)all[i].ino, all[i].path,
			       (all[i].flags & MOOB_RES_PATH_UNKNOWN) ? "?" : "");
		if (q2.truncated)
			fprintf(stderr, "query: results truncated\n");
		return 0;
	}
	if (!strcmp(argv[2], "q2batch") || !strcmp(argv[2], "q2batcherrno")) {
		struct moob_query2 *bq = calloc(MOOB_QUERY2_BATCH_MAX + 1, sizeof *bq);
		struct moob_query_result *rs = calloc((size_t)(MOOB_QUERY2_BATCH_MAX + 1) * MOOB_QUERY_MAX_LIMIT, sizeof *rs);
		struct moob_query2_batch b;
		__u32 limit = MOOB_QUERY_MAX_LIMIT, k, i;
		int a = 3, nq = 0, r;

		if (!bq || !rs) return 1;
		if (a + 1 < argc && !strcmp(argv[a], "--limit")) { limit = (__u32)strtoul(argv[a + 1], NULL, 10); a += 2; }
		while (a < argc && !strcmp(argv[a], "--")) {
			int e = a + 1, n;

			while (e < argc && strcmp(argv[e], "--")) e++;
			if (nq > (int)MOOB_QUERY2_BATCH_MAX) return 64;
			n = parse_terms(e, argv, a + 1, bq[nq].terms);
			if (n < 1) { fprintf(stderr, "query: invalid terms in request %d\n", nq); return 65; }
			bq[nq].version = MOOB_QUERY2_VERSION; bq[nq].nterms = (__u32)n; bq[nq].limit = limit;
			bq[nq].results = (__u64)(uintptr_t)(rs + (size_t)nq * MOOB_QUERY_MAX_LIMIT);
			nq++;
			a = e;
		}
		if (a != argc || !nq) return 64;
		memset(&b, 0, sizeof b); b.version = MOOB_QUERY2_VERSION; b.nqueries = (__u32)nq;
		b.queries = (__u64)(uintptr_t)bq;
		r = ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b);
		if (!strcmp(argv[2], "q2batcherrno")) { printf("q2batch %d\n", r ? errno : 0); return 0; }
		if (r) { perror("MOOB_IOC_QUERY2_BATCH"); return 1; }
		for (k = 0; k < (__u32)nq; k++) {
			struct moob_query_result *rk = rs + (size_t)k * MOOB_QUERY_MAX_LIMIT;

			qsort(rk, bq[k].count, sizeof *rk, by_ino);
			for (i = 0; i < bq[k].count; i++)
				printf("q%u\t%" PRIu64 "\t%s\n", k, (uint64_t)rk[i].ino, rk[i].path);
			if (bq[k].truncated)
				fprintf(stderr, "query: request %u truncated\n", k);
		}
		return 0;
	}
	if (!strcmp(argv[2], "q2invalid")) {
		struct moob_query2 q2;
		int sub;
#define Q2BAD(stmt) do { memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.nterms = 1; \
	q2.limit = 10; q2.results = (__u64)(uintptr_t)res; strcpy(q2.terms[0].name, "kind"); \
	q2.terms[0].op = MOOB_OP_EXISTS; stmt; errno = 0; \
	CHECK(ioctl(fd, MOOB_IOC_QUERY2, &q2) == -1 && errno == EINVAL); } while (0)
		memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.nterms = 1; q2.limit = 10;
		q2.results = (__u64)(uintptr_t)res; strcpy(q2.terms[0].name, "kind"); q2.terms[0].op = MOOB_OP_EXISTS;
		CHECK(!ioctl(fd, MOOB_IOC_QUERY2, &q2));		/* baseline is valid */
		Q2BAD(q2.version = 1);
		Q2BAD(q2.version = 3);
		Q2BAD(q2.nterms = 0);
		Q2BAD(q2.nterms = 5);
		Q2BAD(q2.limit = 0);
		Q2BAD(q2.limit = MOOB_QUERY_MAX_LIMIT + 1);
		Q2BAD(q2.flags = 1);
		Q2BAD(q2.order = MOOB_ORDER_ASC);			/* order without order_by */
		Q2BAD(q2.order_type = MOOB_T_INT64);
		Q2BAD(strcpy(q2.order_by, "created"));			/* order_by without order */
		Q2BAD(strcpy(q2.order_by, "created"); q2.order = 3; q2.order_type = MOOB_T_TIME);
		Q2BAD(strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_BOOL);
		Q2BAD(strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_RAW);
		Q2BAD(strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = 0);
		Q2BAD(strcpy(q2.order_by, "Created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_TIME);
		Q2BAD(memset(q2.order_by, 'a', sizeof q2.order_by); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_TIME);
		Q2BAD(q2.pad[3] = 1);
		Q2BAD(q2.reserved[3] = 1);
		Q2BAD(q2.terms[0].flags = 2);				/* unknown term flag */
		Q2BAD(q2.terms[0].flags = MOOB_TERM_MISSING_OK);	/* EXISTS cannot be "or missing" */
		Q2BAD(q2.terms[0].op = MOOB_OP_EQ; q2.terms[0].type = MOOB_T_STRING; q2.terms[0].len = 1;
		      q2.terms[0].value[0] = 'x'; q2.terms[0].flags = MOOB_TERM_MISSING_OK);	/* only "or missing" terms */
		/* "or missing" terms alone are fine when an order attribute drives the scan */
		memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.nterms = 1; q2.limit = 10;
		q2.results = (__u64)(uintptr_t)res; strcpy(q2.terms[0].name, "kind");
		q2.terms[0].op = MOOB_OP_EQ; q2.terms[0].type = MOOB_T_STRING; q2.terms[0].len = 1;
		q2.terms[0].value[0] = 'x'; q2.terms[0].flags = MOOB_TERM_MISSING_OK;
		strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_DESC; q2.order_type = MOOB_T_TIME;
		CHECK(!ioctl(fd, MOOB_IOC_QUERY2, &q2));
		/* batch: count limits, header fields, and one bad request refuses the whole batch */
		{
			struct moob_query2 *bq = calloc(MOOB_QUERY2_BATCH_MAX + 1, sizeof *bq);
			struct moob_query2_batch b;
			__u32 k;

			CHECK(bq != NULL);
			for (k = 0; k <= MOOB_QUERY2_BATCH_MAX; k++) {
				bq[k].version = MOOB_QUERY2_VERSION; bq[k].nterms = 1; bq[k].limit = 10;
				bq[k].results = (__u64)(uintptr_t)res; strcpy(bq[k].terms[0].name, "kind");
				bq[k].terms[0].op = MOOB_OP_EXISTS; bq[k].count = 777;
			}
			memset(&b, 0, sizeof b); b.version = MOOB_QUERY2_VERSION; b.queries = (__u64)(uintptr_t)bq;
			b.nqueries = MOOB_QUERY2_BATCH_MAX; CHECK(!ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b));
			CHECK(bq[MOOB_QUERY2_BATCH_MAX - 1].count != 777);
			b.nqueries = 0; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EINVAL);
			b.nqueries = MOOB_QUERY2_BATCH_MAX + 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EINVAL);
			b.nqueries = 2; b.version = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EINVAL);
			b.version = MOOB_QUERY2_VERSION; b.reserved[1] = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EINVAL);
			b.reserved[1] = 0; bq[0].count = 777; bq[1].limit = 0; errno = 0;
			CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EINVAL);
			CHECK(bq[0].count == 777);				/* nothing ran */
			bq[1].limit = 10; b.queries = 0; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EFAULT);
			free(bq);
		}
		Q2BAD(strcpy(q2.terms[0].name, "Kind"));
		/*
		 * term_check and the header, case by case (journal 0141): each refusal next to the valid
		 * request it differs from by one field. Q2EQ sets an equality term of the given type.
		 */
#define Q2OK(stmt) do { memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.nterms = 1; \
	q2.limit = 10; q2.results = (__u64)(uintptr_t)res; strcpy(q2.terms[0].name, "kind"); \
	q2.terms[0].op = MOOB_OP_EXISTS; stmt; errno = 0; CHECK(!ioctl(fd, MOOB_IOC_QUERY2, &q2)); } while (0)
#define Q2EQ(ty, n, bytes) (q2.terms[0].op = MOOB_OP_EQ, q2.terms[0].type = (ty), q2.terms[0].len = (n), \
	memcpy(q2.terms[0].value, bytes, n))
		Q2BAD(q2.terms[0].type = MOOB_T_STRING);		/* EXISTS carries no value */
		Q2BAD(q2.terms[0].len = 1);
		Q2BAD(q2.terms[0].op = 0);
		Q2BAD(q2.terms[0].op = MOOB_OP_EXISTS + 1);
		Q2BAD(q2.terms[0].name[0] = 0);			/* empty name */
		Q2BAD(memset(q2.terms[0].name, 'a', sizeof q2.terms[0].name));	/* not NUL-terminated */
		Q2OK(memset(q2.terms[0].name, 'a', MOOB_QUERY_NAME_MAX); q2.terms[0].name[MOOB_QUERY_NAME_MAX] = 0);
		Q2BAD(strcpy(q2.terms[0].name, "a..b"));
		Q2BAD(strcpy(q2.terms[0].name, ".kind"));
		Q2BAD(strcpy(q2.terms[0].name, "kind."));
		Q2BAD(strcpy(q2.terms[0].name, "a-b"));
		Q2BAD(q2.terms[0].pad[1] = 1);
		Q2BAD(q2.terms[0].pad[2] = 1);
		Q2BAD(q2.pad[0] = 1);
		Q2BAD(q2.reserved[0] = 1);
		Q2BAD(q2.nterms = 2; strcpy(q2.terms[1].name, "Kind"); q2.terms[1].op = MOOB_OP_EXISTS);	/* second term */
		Q2BAD(q2.nterms = 2; strcpy(q2.terms[1].name, "kind"); q2.terms[1].op = 42);
		Q2OK(q2.nterms = 2; strcpy(q2.terms[1].name, "kind"); q2.terms[1].op = MOOB_OP_EXISTS);
		Q2BAD(q2.nterms = 4; strcpy(q2.terms[3].name, "kind"); q2.terms[3].op = MOOB_OP_EXISTS;
		      strcpy(q2.terms[1].name, "kind"); q2.terms[1].op = MOOB_OP_EXISTS;
		      strcpy(q2.terms[2].name, "kind"); q2.terms[2].op = MOOB_OP_EXISTS; q2.terms[3].pad[0] = 1);	/* last term */
		/* types: raw and unknown types cannot be queried; fixed sizes; PREFIX on strings only */
		Q2BAD(Q2EQ(MOOB_T_RAW, 1, "x"));
		Q2BAD(Q2EQ(0, 1, "x"));
		Q2BAD(Q2EQ(MOOB_T_RAW + 1, 1, "x"));
		Q2OK(Q2EQ(MOOB_T_INT64, 8, "\0\0\0\0\0\0\0\0"));
		Q2BAD(Q2EQ(MOOB_T_INT64, 7, "\0\0\0\0\0\0\0"));
		Q2BAD(Q2EQ(MOOB_T_INT64, 9, "\0\0\0\0\0\0\0\0\0"));
		Q2OK(Q2EQ(MOOB_T_TIME, 8, "\0\0\0\0\0\0\0\0"); q2.terms[0].op = MOOB_OP_GE);
		Q2BAD(Q2EQ(MOOB_T_TIME, 0, ""));
		Q2BAD(Q2EQ(MOOB_T_TIME, 8, "\0\0\0\0\0\0\0\0"); q2.terms[0].op = MOOB_OP_PREFIX);
		Q2OK(Q2EQ(MOOB_T_DOUBLE, 8, "\0\0\0\0\0\0\xf0\x7f"));		/* +infinity */
		Q2BAD(Q2EQ(MOOB_T_DOUBLE, 8, "\0\0\0\0\0\0\xf8\x7f"));		/* NaN */
		Q2BAD(Q2EQ(MOOB_T_DOUBLE, 8, "\x01\0\0\0\0\0\xf0\xff"));		/* negative signalling NaN */
		Q2BAD(Q2EQ(MOOB_T_DOUBLE, 4, "\0\0\x80\x3f"));
		Q2BAD(Q2EQ(MOOB_T_DOUBLE, 8, "\0\0\0\0\0\0\0\0"); q2.terms[0].op = MOOB_OP_PREFIX);
		Q2OK(Q2EQ(MOOB_T_BOOL, 1, "\x01"));
		Q2BAD(Q2EQ(MOOB_T_BOOL, 1, "\x02"));
		Q2BAD(Q2EQ(MOOB_T_BOOL, 0, ""));
		Q2BAD(Q2EQ(MOOB_T_BOOL, 2, "\x01\x00"));
		Q2BAD(Q2EQ(MOOB_T_BOOL, 1, "\x01"); q2.terms[0].op = MOOB_OP_PREFIX);
		Q2OK(Q2EQ(MOOB_T_STRING, 1, "n"); q2.terms[0].op = MOOB_OP_PREFIX);
		Q2OK(Q2EQ(MOOB_T_STRING, 0, ""));
		Q2OK(Q2EQ(MOOB_T_STRING, 0, ""); memset(q2.terms[0].value, 'a', MOOB_QUERY_VALUE_MAX);
		     q2.terms[0].len = MOOB_QUERY_VALUE_MAX);
		Q2BAD(Q2EQ(MOOB_T_STRING, 0, ""); memset(q2.terms[0].value, 'a', MOOB_QUERY_VALUE_MAX);
		      q2.terms[0].len = MOOB_QUERY_VALUE_MAX + 1);		/* longer than value[] */
		Q2BAD(Q2EQ(MOOB_T_STRING, 0, ""); q2.terms[0].len = 0xffff);
		Q2BAD(Q2EQ(MOOB_T_STRING, 3, "a\0b"));				/* NUL */
		Q2BAD(Q2EQ(MOOB_T_STRING, 2, "\xc0\x80"));			/* overlong */
		Q2BAD(Q2EQ(MOOB_T_STRING, 3, "\xed\xa0\x80"));			/* surrogate */
		Q2BAD(Q2EQ(MOOB_T_STRING, 4, "\xf4\x90\x80\x80"));		/* above U+10FFFF */
		Q2BAD(Q2EQ(MOOB_T_STRING, 2, "a\xe2"));				/* cut by the end */
		Q2BAD(Q2EQ(MOOB_T_STRING, 1, "\x80"));
		/* an "or missing" term on a type check that fails is refused like a plain one */
		Q2BAD(q2.nterms = 2; strcpy(q2.terms[1].name, "kind"); q2.terms[1].op = MOOB_OP_LT;
		      q2.terms[1].type = MOOB_T_BOOL; q2.terms[1].len = 1; q2.terms[1].value[0] = 2;
		      q2.terms[1].flags = MOOB_TERM_MISSING_OK);
		Q2OK(q2.nterms = 2; strcpy(q2.terms[1].name, "kind"); q2.terms[1].op = MOOB_OP_EQ;
		     q2.terms[1].type = MOOB_T_BOOL; q2.terms[1].len = 1; q2.terms[1].value[0] = 1;
		     q2.terms[1].flags = MOOB_TERM_MISSING_OK);
		/* order fields: the name follows the attribute alphabet; nterms 0 needs order_by */
		Q2BAD(strcpy(q2.order_by, "a..b"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_TIME);
		Q2BAD(strcpy(q2.order_by, "created."); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_TIME);
		Q2BAD(strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_DESC; q2.order_type = MOOB_T_RAW + 1);
		Q2OK(q2.nterms = 0; strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_STRING);
		Q2OK(q2.nterms = 0; strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_INT64);
		Q2OK(q2.nterms = 0; strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_DOUBLE);
		Q2BAD(q2.nterms = 0; strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_ASC; q2.order_type = MOOB_T_STRING;
		      q2.limit = 0);
		Q2BAD(q2.limit = 0xffffffffU);
#undef Q2OK
#undef Q2EQ
		/* a cursor not issued by this mount for this query is refused with ESTALE */
		memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.nterms = 1; q2.limit = 10;
		q2.results = (__u64)(uintptr_t)res; strcpy(q2.terms[0].name, "kind"); q2.terms[0].op = MOOB_OP_EXISTS;
		q2.cursor[0] = 0xc2; q2.cursor[127] = 1;
		errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2, &q2) == -1 && errno == ESTALE);
		memset(q2.cursor, 0xff, sizeof q2.cursor);
		errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY2, &q2) == -1 && errno == ESTALE);
		/* an order attribute that no file carries gives an empty answer, not an error */
		memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.limit = 10;
		q2.results = (__u64)(uintptr_t)res; strcpy(q2.order_by, "no_such_attr");
		q2.order = MOOB_ORDER_DESC; q2.order_type = MOOB_T_TIME;
		CHECK(!ioctl(fd, MOOB_IOC_QUERY2, &q2) && q2.count == 0 && !q2.truncated);
		/* QUERY2 answers only on a domain directory, never on a subdirectory of it */
		{
			char sub_path[4096];
			snprintf(sub_path, sizeof sub_path, "%s/q2sub", argv[1]);
			mkdir(sub_path, 0700);
			sub = open(sub_path, O_RDONLY | O_DIRECTORY); CHECK(sub >= 0);
			memset(&q2, 0, sizeof q2); q2.version = MOOB_QUERY2_VERSION; q2.nterms = 1; q2.limit = 10;
			q2.results = (__u64)(uintptr_t)res; strcpy(q2.terms[0].name, "kind"); q2.terms[0].op = MOOB_OP_EXISTS;
			errno = 0; CHECK(ioctl(sub, MOOB_IOC_QUERY2, &q2) == -1 && errno == ENOTTY);
			close(sub); rmdir(sub_path);
		}
		puts("QUERY2_INVALID_REQUESTS_REFUSED");
		return 0;
	}
	if (!strcmp(argv[2], "build")) {
		/* the mount-wide QUERY reads every partition, so it builds every domain (ADR-0012) */
		static struct moob_query_result one[1];
		struct moob_query bq;

		memset(&bq, 0, sizeof bq); bq.nterms = 1;
		strcpy(bq.terms[0].name, "kind"); bq.terms[0].op = MOOB_OP_EXISTS;
		if (run_query(fd, &bq, one, 1)) { perror("MOOB_IOC_QUERY (build)"); return 1; }
		return 0;
	}
	if (!strcmp(argv[2], "info")) {
		struct moob_info info;
		CHECK(!ioctl(fd, MOOB_IOC_GET_INFO, &info));
		printf("uapi=%u format=%u name_max=%u string_max=%u raw_max=%u per_file=%u\n",
		       info.uapi_version, info.format_version, info.attr_name_max, info.attr_string_max,
		       info.attr_raw_max, info.attr_max_per_file);
		printf("domain_depth=%u heterogeneous=%u fallback_domains=%u counters=%s\n", info.domain_depth,
		       info.heterogeneous, info.fallback_domains,
		       (info.flags & MOOB_FLAG_COUNTERS) ? "shown" : "hidden");
		printf("domains_built=%u domain_builds=%u domain_releases=%u on_demand=%d\n",
		       info.domains_built, info.domain_builds, info.domain_releases,
		       (info.flags & MOOB_FLAG_ON_DEMAND) != 0);
		printf("lower=%s caps=%#x flags=%#x budget=%" PRIu64 " used=%" PRIu64 " entries=%u inodes=%u\n",
		       info.lower_fs, info.caps, info.flags, (uint64_t)info.index_budget,
		       (uint64_t)info.index_used, info.index_entries, info.index_inodes);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[2], "race2")) {
		struct timespec t0, t1;
		unsigned long iters = 0, pages = 0, hits = 0, gone = 0, denied = 0, replaced = 0;
		unsigned long stale = 0, closed = 0, closed_tries = 0;
		int was_closed = 0;
		double secs = atof(argv[3]);
		/* MOOB_RACE2_FORK: a second process queries the same open file at the same time, so
		 * one of them finds the file's result buffer lent (moobl_qbuf_get, journal 0139) */
		pid_t child = getenv("MOOB_RACE2_FORK") ? fork() : -1;

		CHECK(child != -1 || !getenv("MOOB_RACE2_FORK"));
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (;;) {
			struct moob_query2 q2;
			int plan = (int)(iters % 3), r;

			memset(&q2, 0, sizeof q2);
			q2.version = MOOB_QUERY2_VERSION;
			q2.nterms = 1;
			strcpy(q2.terms[0].name, "kind"); q2.terms[0].op = MOOB_OP_EQ;
			q2.terms[0].type = MOOB_T_STRING; q2.terms[0].len = 4; memcpy(q2.terms[0].value, "note", 4);
			if (plan == 1) {	/* equality plan: two EQ terms */
				q2.nterms = 2;
				strcpy(q2.terms[1].name, "ns"); q2.terms[1].op = MOOB_OP_EQ;
				q2.terms[1].type = MOOB_T_STRING; q2.terms[1].len = 1; q2.terms[1].value[0] = 'r';
			} else if (plan == 2) {	/* ordered plan */
				strcpy(q2.order_by, "created"); q2.order = MOOB_ORDER_DESC; q2.order_type = MOOB_T_TIME;
			}
			q2.limit = 7;
			q2.results = (__u64)(uintptr_t)res;
			for (;;) {	/* one full pagination */
				__u32 i;

				r = ioctl(fd, MOOB_IOC_QUERY2, &q2);
				if (r && errno == ESTALE) {	/* the domain switched fallback mode: start over */
					stale++;
					memset(q2.cursor, 0, sizeof q2.cursor);
					continue;
				}
				if (r && errno == EACCES) {	/* domain directory closed by chmod: count episodes */
					closed_tries++;
					if (!was_closed) closed++;
					was_closed = 1;
					break;
				}
				was_closed = 0;
				if (r) { perror("MOOB_IOC_QUERY2"); return 1; }
				pages++;
				CHECK(q2.count <= q2.limit);
				for (i = 0; i < q2.count; i++) {
					struct stat st;
					int rfd;

					CHECK(memchr(res[i].path, 0, sizeof res[i].path) != NULL);
					if (res[i].flags & MOOB_RES_PATH_UNKNOWN) continue;
					CHECK(res[i].ino != 0 && res[i].path[0] && res[i].path[0] != '/' && !strstr(res[i].path, ".."));
					CHECK(strlen(res[i].path) == res[i].path_len);
					rfd = openat(fd, res[i].path, O_RDONLY);
					if (rfd >= 0) {
						CHECK(!fstat(rfd, &st));
						if ((__u64)st.st_ino == res[i].ino) hits++;
						else replaced++;	/* name recreated after the answer */
						close(rfd);
						continue;
					}
					if (errno == ENOENT) { gone++; continue; }
					if (errno == EACCES) { denied++; continue; }
					fprintf(stderr, "race2: open %s: %s\n", res[i].path, strerror(errno));
					return 1;
				}
				if (!q2.truncated) {
					iters++;	/* a complete pagination */
					break;
				}
			}
			clock_gettime(CLOCK_MONOTONIC, &t1);
			if ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9 >= secs) break;
		}
		if (child == 0)
			_exit(pages ? 0 : 1);
		if (child > 0) {
			int st;

			CHECK(waitpid(child, &st, 0) == child);
			CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
		}
		CHECK(pages > 0);
		printf("race2: %lu complete paginations, %lu pages, %lu openable, %lu replaced, %lu vanished, %lu denied, %lu stale restarts, %lu domain-closed episodes (%lu refused calls)\n",
		       iters, pages, hits, replaced, gone, denied, stale, closed, closed_tries);
		fflush(stdout);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[2], "race")) {
		struct timespec t0, t1;
		unsigned long iters = 0, hits = 0, gone = 0, denied = 0;
		double secs = atof(argv[3]);
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (;;) {
			__u32 i;
			memset(&q, 0, sizeof q);
			q.nterms = 1; strcpy(q.terms[0].name, "kind"); q.terms[0].op = MOOB_OP_EQ;
			q.terms[0].type = MOOB_T_STRING; q.terms[0].len = 4; memcpy(q.terms[0].value, "note", 4);
			if (run_query(fd, &q, res, MOOB_QUERY_MAX_LIMIT)) { perror("MOOB_IOC_QUERY"); return 1; }
			CHECK(q.count <= MOOB_QUERY_MAX_LIMIT);
			for (i = 0; i < q.count; i++) {
				char full[8192]; int rfd;
				CHECK(memchr(res[i].path, 0, sizeof res[i].path) != NULL);
				if (res[i].flags & MOOB_RES_PATH_UNKNOWN) continue;	/* renamed directory: path unknown by contract */
				CHECK(res[i].path[0] != '/' && !strstr(res[i].path, ".."));
				snprintf(full, sizeof full, "%s/%s", argv[1], res[i].path);
				rfd = open(full, O_RDONLY);
				if (rfd >= 0) { hits++; close(rfd); continue; }
				if (errno == ENOENT) { gone++; continue; }	/* removed or renamed after the answer */
				if (errno == EACCES) { denied++; continue; }	/* chmod after the answer (TOCTOU contract) */
				fprintf(stderr, "race: open %s: %s\n", res[i].path, strerror(errno));
				return 1;
			}
			iters++;
			clock_gettime(CLOCK_MONOTONIC, &t1);
			if ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9 >= secs) break;
		}
		printf("race: %lu queries, %lu openable results, %lu vanished, %lu denied after answer\n", iters, hits, gone, denied);
		return iters ? 0 : 1;
	}
	if (!strcmp(argv[2], "invalid")) {
		int sub;
		memset(&q, 0, sizeof q);
		q.nterms = 1; strcpy(q.terms[0].name, "kind"); q.terms[0].op = MOOB_OP_EXISTS;
		CHECK(!run_query(fd, &q, res, 10));				/* baseline is valid */
		q.version = 99; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.version = MOOB_UAPI_VERSION; q.nterms = 5; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.nterms = 1; q.limit = 0; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.limit = 10; q.flags = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.flags = 0; q.reserved[1] = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.reserved[1] = 0; strcpy(q.terms[0].name, "Kind"); errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		strcpy(q.terms[0].name, "kind"); q.terms[0].op = 42; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].op = MOOB_OP_EXISTS; q.terms[0].flags = MOOB_TERM_MISSING_OK; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].flags = 0; q.terms[0].op = 42;
		q.terms[0].op = MOOB_OP_EQ; q.terms[0].type = MOOB_T_RAW; q.terms[0].len = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].type = MOOB_T_INT64; q.terms[0].len = 4; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].len = 8; q.terms[0].op = MOOB_OP_PREFIX; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].op = MOOB_OP_EQ; q.terms[0].type = MOOB_T_STRING; q.terms[0].len = 1; q.terms[0].value[0] = 0xff; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].value[0] = 'x'; q.terms[0].pad[0] = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].pad[0] = 0; CHECK(!run_query(fd, &q, res, 10));
		/* more of do_query's header and term checks (journal 0141); each restores the baseline */
		q.nterms = 0; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.nterms = 1; q.limit = MOOB_QUERY_MAX_LIMIT + 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.limit = 10; q.reserved[0] = 1; errno = 0; CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.reserved[0] = 0; q.terms[0].flags = MOOB_TERM_MISSING_OK; errno = 0;	/* a QUERY2 flag in v1 */
		CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[0].flags = 0; q.nterms = 2; strcpy(q.terms[1].name, "kind"); q.terms[1].op = MOOB_OP_EQ;
		q.terms[1].type = MOOB_T_BOOL; q.terms[1].len = 1; q.terms[1].value[0] = 2; errno = 0;	/* second term */
		CHECK(ioctl(fd, MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL);
		q.terms[1].value[0] = 1; CHECK(!run_query(fd, &q, res, 10));
		q.nterms = 1; CHECK(!run_query(fd, &q, res, 10));
		errno = 0; CHECK(ioctl(fd, _IOR(MOOB_IOC_MAGIC, 9, int), &q) == -1 && errno == ENOTTY);
		/* the ioctls only answer on the mount root */
		{
			char sub_path[4096];
			snprintf(sub_path, sizeof sub_path, "%s/subdir", argv[1]);
			mkdir(sub_path, 0700);
			sub = open(sub_path, O_RDONLY | O_DIRECTORY); CHECK(sub >= 0);
			errno = 0; CHECK(ioctl(sub, MOOB_IOC_QUERY, &q) == -1 && errno == ENOTTY);
			close(sub); rmdir(sub_path);
		}
		puts("QUERY_INVALID_REQUESTS_REFUSED");
		return 0;
	}
	{
		int n;
		__u32 i;
		memset(&q, 0, sizeof q);
		n = parse_terms(argc, argv, 2, q.terms);
		if (n < 1) { fprintf(stderr, "query: invalid terms\n"); return 65; }
		q.nterms = (__u32)n;
		if (run_query(fd, &q, res, MOOB_QUERY_MAX_LIMIT)) { perror("MOOB_IOC_QUERY"); return 1; }
		qsort(res, q.count, sizeof *res, by_ino);
		for (i = 0; i < q.count; i++)
			printf("%" PRIu64 "\t%s%s%s\n", (uint64_t)res[i].ino, res[i].path,
			       (res[i].flags & MOOB_RES_PATH_UNKNOWN) ? "?" : "",
			       (res[i].flags & MOOB_RES_PATH_TRUNCATED) ? "\t(truncated)" : "");
		if (q.truncated)
			fprintf(stderr, "query: results truncated\n");
	}
	return 0;
}
