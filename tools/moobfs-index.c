/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs-index: user-space oracle for the kernel QUERY. Walks a directory,
 * reads every typed attribute with the shared encoding and answers the same
 * conjunctive queries, printing "<ino>\t<path>" lines sorted by inode number.
 *
 *   moobfs-index [--uid N] [--domain] [--json] [--order NAME TYPE asc|desc] [--limit N] <dir> [terms...]
 * --uid N drops privileges first, so the walk sees only what that user may reach.
 * --domain treats <dir> as a domain directory (docs/spec-query-v2.md §3): its own
 * attributes are left out, and an entry is listed only when it passes the homogeneity
 * rule, evaluated here independently of the kernel from mode bits, owner, group, link
 * count and the presence of an access ACL on <dir>, every directory below it on the way,
 * and the entry itself.
 * --order lists only entries whose NAME attribute has TYPE, sorted by (value, ino) ascending
 * or descending (the QUERY2 order); terms may then be empty. --limit keeps the first N.
 * An operator ending in '?' (for example "gt?") also matches files without the attribute.
 * --json prints one object with dir, count and ordered {ino, path} results.
 */
#define _GNU_SOURCE
#include <ftw.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <unistd.h>
#include <limits.h>
#include <grp.h>
#include "query-common.h"

static struct moob_query_term terms[MOOB_QUERY_MAX_TERMS];
static int nterms;
static size_t root_len;
static int domain_mode;
static struct stat dom_st;
static int dom_acl;

static int has_acl(const char *path)
{
	return getxattr(path, "system.posix_acl_access", NULL, 0) > 0;
}

/* Everyone who can traverse the domain can read (want_read) or traverse this inode. */
static int reach_ok(const char *path, const struct stat *st, int want_read)
{
	mode_t u = want_read ? S_IRUSR : S_IXUSR, g = want_read ? S_IRGRP : S_IXGRP;
	mode_t o = want_read ? S_IROTH : S_IXOTH;
	int acl = has_acl(path);

	/* every class: the kernel never judges owner or group by the bits of others */
	if (!acl && (st->st_mode & (u | g | o)) == (u | g | o))
		return 1;
	if (acl || dom_acl || st->st_uid != dom_st.st_uid || st->st_gid != dom_st.st_gid)
		return 0;
	if ((dom_st.st_mode & S_IXUSR) && !(st->st_mode & u))
		return 0;
	if ((dom_st.st_mode & S_IXGRP) && !(st->st_mode & g))
		return 0;
	if ((dom_st.st_mode & S_IXOTH) && !(st->st_mode & o))
		return 0;
	return 1;
}

static int homogeneous(const char *path, const struct stat *st)
{
	char buf[PATH_MAX];
	size_t i;

	if (S_ISREG(st->st_mode) && st->st_nlink > 1)
		return 0;
	snprintf(buf, sizeof buf, "%s", path);
	for (i = root_len + 1; buf[i]; i++) {		/* every directory strictly between */
		struct stat ds;

		if (buf[i] != '/')
			continue;
		buf[i] = 0;
		if (lstat(buf, &ds) || !reach_ok(buf, &ds, 0))
			return 0;
		buf[i] = '/';
	}
	return reach_ok(path, st, 1);
}

struct hit { uint64_t ino; char *path; ma_u8 *ov; size_t olen; };
static struct hit *hits;
static size_t nhits, cap;
static char order_name[MOOB_QUERY_NAME_MAX + 1];
static int order_type, order_desc;
static long out_limit = -1;
static int json_output;

static void json_str(const char *s)
{
	putchar('"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			putchar('\\');
		if ((unsigned char)*s < 0x20)
			printf("\\u%04x", (unsigned char)*s);
		else
			putchar(*s);
	}
	putchar('"');
}

static int value_cmp(int type, const ma_u8 *a, size_t alen, const ma_u8 *b, size_t blen)
{
	switch (type) {
	case MOOB_T_INT64: case MOOB_T_TIME: {
		int64_t x = (int64_t)moob_attr_get64(a), y = (int64_t)moob_attr_get64(b);
		return x < y ? -1 : x > y ? 1 : 0;
	}
	case MOOB_T_DOUBLE: {
		ma_u64 xb = moob_attr_get64(a), yb = moob_attr_get64(b);
		ma_u64 x = (xb >> 63) ? ~xb : (xb | (1ULL << 63));
		ma_u64 y = (yb >> 63) ? ~yb : (yb | (1ULL << 63));
		return x < y ? -1 : x > y ? 1 : 0;
	}
	case MOOB_T_BOOL:
		return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
	default: {
		int c = memcmp(a, b, alen < blen ? alen : blen);
		if (c) return c;
		return alen < blen ? -1 : alen > blen ? 1 : 0;
	}
	}
}

static int term_matches(const char *path, const struct moob_query_term *t)
{
	char full[MOOB_ATTR_PREFIX_LEN + MOOB_QUERY_NAME_MAX + 1];
	ma_u8 v[MOOB_ATTR_VALUE_MAX + 1];
	ssize_t n;
	int c;

	snprintf(full, sizeof full, "%s%s", MOOB_ATTR_PREFIX, t->name);
	n = lgetxattr(path, full, v, sizeof v);
	/* absent, invalid and raw values are not indexed: for the index they are missing */
	if (n <= 0 || moob_attr_validate(v, (size_t)n) != MOOB_ATTR_OK || v[0] == MOOB_T_RAW)
		return (t->flags & MOOB_TERM_MISSING_OK) ? 1 : 0;
	if (t->op == MOOB_OP_EXISTS)
		return 1;
	if (v[0] != t->type)
		return 0;
	if (t->op == MOOB_OP_PREFIX)
		return (size_t)n - 1 >= t->len && !memcmp(v + 1, t->value, t->len);
	c = value_cmp(t->type, v + 1, (size_t)n - 1, t->value, t->len);
	switch (t->op) {
	case MOOB_OP_EQ: return c == 0;
	case MOOB_OP_LT: return c < 0;
	case MOOB_OP_LE: return c <= 0;
	case MOOB_OP_GT: return c > 0;
	case MOOB_OP_GE: return c >= 0;
	}
	return 0;
}

static int visit(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
	int i;

	if (flag != FTW_F && flag != FTW_D)
		return 0;
	if (domain_mode && (ftw->level == 0 || !homogeneous(path, sb)))
		return 0;
	for (i = 0; i < nterms; i++)
		if (!term_matches(path, &terms[i]))
			return 0;
	ma_u8 ov[MOOB_ATTR_VALUE_MAX + 1];
	ssize_t on = 0;

	if (order_name[0]) {
		char full[MOOB_ATTR_PREFIX_LEN + MOOB_QUERY_NAME_MAX + 1];

		snprintf(full, sizeof full, "%s%s", MOOB_ATTR_PREFIX, order_name);
		on = lgetxattr(path, full, ov, sizeof ov);
		if (on <= 0 || moob_attr_validate(ov, (size_t)on) != MOOB_ATTR_OK || ov[0] != order_type)
			return 0;
	}
	if (nhits == cap) {
		cap = cap ? cap * 2 : 64;
		hits = realloc(hits, cap * sizeof *hits);
		if (!hits) return 1;
	}
	hits[nhits].ino = sb->st_ino;
	hits[nhits].path = strdup(path + root_len + (path[root_len] == '/'));
	hits[nhits].ov = NULL;
	hits[nhits].olen = 0;
	if (on > 0) {
		hits[nhits].olen = (size_t)on - 1;
		hits[nhits].ov = malloc((size_t)on);
		if (!hits[nhits].ov) return 1;
		memcpy(hits[nhits].ov, ov + 1, (size_t)on - 1);
	}
	nhits++;
	return 0;
}

static int by_ino(const void *a, const void *b)
{
	const struct hit *x = a, *y = b;
	return x->ino < y->ino ? -1 : x->ino > y->ino;
}

static int by_order(const void *a, const void *b)
{
	const struct hit *x = a, *y = b;
	int c = value_cmp(order_type, x->ov, x->olen, y->ov, y->olen);

	if (!c)
		c = x->ino < y->ino ? -1 : x->ino > y->ino;
	return order_desc ? -c : c;
}

int main(int argc, char **argv)
{
	size_t i;

	for (;;) {
		if (argc >= 3 && !strcmp(argv[1], "--uid")) {
			char *end;
			unsigned long v;
			uid_t u;

			errno = 0;
			v = strtoul(argv[2], &end, 10);
			u = (uid_t)v;
			if (errno || end == argv[2] || *end || (unsigned long)u != v || u == (uid_t)-1) {
				fprintf(stderr, "moobfs-index: --uid needs a numeric uid\n");
				return 64;
			}
			/* root's supplementary groups would let the walk see what this uid cannot */
			if ((!geteuid() && setgroups(0, NULL)) || setgid(u) || setuid(u)) {
				perror("setuid");
				return 1;
			}
			argv += 2; argc -= 2;
		} else if (argc >= 2 && !strcmp(argv[1], "--domain")) {
			domain_mode = 1;
			argv++; argc--;
		} else if (argc >= 2 && !strcmp(argv[1], "--json")) {
			json_output = 1;
			argv++; argc--;
		} else if (argc >= 5 && !strcmp(argv[1], "--order")) {
			size_t olen = strlen(argv[2]);

			order_type = parse_type(argv[3]);
			if (order_type < 0 || olen >= sizeof order_name) return 64;
			memcpy(order_name, argv[2], olen + 1);
			order_desc = !strcmp(argv[4], "desc");
			argv += 4; argc -= 4;
		} else if (argc >= 3 && !strcmp(argv[1], "--limit")) {
			out_limit = strtol(argv[2], NULL, 10);
			argv += 2; argc -= 2;
		} else {
			break;
		}
	}
	if (argc < (order_name[0] ? 2 : 6)) {
		fprintf(stderr, "usage: moobfs-index [--uid N] [--domain] [--json] [--order NAME TYPE asc|desc] [--limit N] <dir> [terms...]\n");
		return 64;
	}
	nterms = parse_terms(argc, argv, 2, terms);
	if (nterms < (order_name[0] ? 0 : 1)) {
		fprintf(stderr, "moobfs-index: invalid query terms\n");
		return 65;
	}
	root_len = strlen(argv[1]);
	if (domain_mode) {
		if (stat(argv[1], &dom_st)) {
			perror(argv[1]);
			return 1;
		}
		dom_acl = has_acl(argv[1]);
	}
	if (nftw(argv[1], visit, 32, FTW_PHYS | FTW_MOUNT)) {
		perror("nftw");
		return 1;
	}
	if (nhits)	/* hits is NULL when nothing matched, and qsort's base must not be NULL */
		qsort(hits, nhits, sizeof *hits, order_name[0] ? by_order : by_ino);
	if (out_limit >= 0 && (size_t)out_limit < nhits)
		nhits = (size_t)out_limit;
	if (json_output) {
		printf("{\"dir\": ");
		json_str(argv[1]);
		printf(", \"count\": %zu, \"results\": [", nhits);
		for (i = 0; i < nhits; i++) {
			printf("%s\n  {\"ino\": %" PRIu64 ", \"path\": ", i ? "," : "", hits[i].ino);
			json_str(hits[i].path);
			putchar('}');
		}
		printf("%s]}\n", nhits ? "\n" : "");
	} else {
		for (i = 0; i < nhits; i++)
			printf("%" PRIu64 "\t%s\n", hits[i].ino, hits[i].path);
	}
	return 0;
}
