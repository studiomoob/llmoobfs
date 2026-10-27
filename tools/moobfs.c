/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs: one command for the things a user does with a mounted MoobFS layer (roadmap item C4,
 * ADR-0022). User space only, over the existing ioctls and tools; no new interface.
 *
 *   moobfs info [--json] <path>
 *   moobfs query [--json] [--ino] [--order NAME TYPE asc|desc] [--limit N] [--page N]
 *                <dir> [terms...]
 *   moobfs check [moobfs-check arguments]
 *   moobfs --help
 *
 * info finds the layer mount that holds <path> (/proc/self/mountinfo) and prints GET_INFO of
 * its root. query issues QUERY2 on a domain directory and follows the cursor until the answer
 * is complete or --limit results were printed; on the mount root of a mount without domains it
 * issues the mount-wide QUERY, which has no cursor. Paths are relative to <dir>. Without
 * --order the results are sorted by inode number, as moobfs-index prints them. Terms are those
 * of moobfs-index: "name op type value", up to four. check runs moobfs-check.
 *
 * Exit status (docs/tools.md): 0 success; 1 a system call failed; 64 usage; 65 invalid terms or
 * a query the kernel refused as invalid; 69 no layer at <path> (module not loaded, another
 * filesystem, or a directory that cannot be queried); 75 temporary (the domain does not fit the
 * index budget, a build limit, or the domain kept changing); 77 permission denied; 3 the answer
 * is incomplete (the mount-wide QUERY had more results than it can return).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>
#include "query-common.h"

#define EX_INCOMPLETE 3
#define EX_USAGE 64
#define EX_DATAERR 65
#define EX_UNAVAILABLE 69
#define EX_TEMPFAIL 75
#define EX_NOPERM 77

static const char synopsis[] =
	"usage: moobfs info [--json] <path>\n"
	"       moobfs query [--json] [--ino] [--order NAME TYPE asc|desc] [--limit N] [--page N]\n"
	"                    <dir> [name op type value]...\n"
	"       moobfs check [moobfs-check arguments]\n"
	"       moobfs --help\n"
	"Terms: op is eq lt le gt ge prefix exists (exists takes - - as type and value); an op\n"
	"ending in ? also matches files without the attribute. Type: string int64 double bool time.\n"
	"See moobfs(1) and docs/operations.md.\n";

static int usage(void)
{
	fputs(synopsis, stderr);
	return EX_USAGE;
}

/* The layer mount that holds a path, from /proc/self/mountinfo. */
struct mnt {
	char point[PATH_MAX];	/* mount point */
	char root[PATH_MAX];	/* directory of the filesystem mounted there; "/" for the whole */
	char opts[1024];	/* superblock options: lower=, domain_depth=, ... */
};

/* mountinfo escapes space, tab, newline and backslash as \ooo */
static void unescape(char *s)
{
	char *d = s;

	while (*s) {
		if (s[0] == '\\' && s[1] >= '0' && s[1] <= '3' && s[2] >= '0' && s[2] <= '7' &&
		    s[3] >= '0' && s[3] <= '7') {
			*d++ = (char)((s[1] - '0') * 64 + (s[2] - '0') * 8 + (s[3] - '0'));
			s += 4;
		} else {
			*d++ = *s++;
		}
	}
	*d = 0;
}

static int module_loaded(void)
{
	FILE *f = fopen("/proc/filesystems", "r");
	char line[256];
	int found = 0;

	if (!f)
		return 1;	/* unknown: do not claim it is missing */
	while (!found && fgets(line, sizeof line, f))
		found = strstr(line, "\tmoobfs_layer\n") != NULL;
	fclose(f);
	return found;
}

/*
 * Fills m with the moobfs_layer mount whose device is that of path and whose mount point is the
 * longest prefix of its real path. Returns 0, or an exit status after printing why.
 */
static int find_mount(const char *path, struct mnt *m)
{
	char real[PATH_MAX], line[4 * PATH_MAX];
	struct stat st;
	size_t best = 0;
	FILE *f;

	if (!realpath(path, real) || stat(real, &st)) {
		int err = errno;

		fprintf(stderr, "moobfs: %s: %s\n", path,
			err == EACCES ? "permission denied" : strerror(err));
		return err == EACCES ? EX_NOPERM : 1;
	}
	f = fopen("/proc/self/mountinfo", "r");
	if (!f) {
		perror("moobfs: /proc/self/mountinfo");
		return 1;
	}
	while (fgets(line, sizeof line, f)) {
		char point[PATH_MAX], root[PATH_MAX], fstype[64], src[PATH_MAX], opts[1024];
		unsigned int maj, min;
		char *sep = strstr(line, " - ");
		size_t plen;

		if (!sep || sscanf(line, "%*s %*s %u:%u %4095s %4095s", &maj, &min, root, point) != 4 ||
		    sscanf(sep + 3, "%63s %4095s %1023s", fstype, src, opts) != 3)
			continue;
		if (strcmp(fstype, "moobfs_layer") || makedev(maj, min) != st.st_dev)
			continue;
		unescape(point);
		unescape(root);
		plen = strlen(point);
		if (strncmp(real, point, plen) || (real[plen] && real[plen] != '/' && plen > 1))
			continue;
		if (plen >= best) {
			best = plen + 1;
			snprintf(m->point, sizeof m->point, "%s", point);
			snprintf(m->root, sizeof m->root, "%s", root);
			snprintf(m->opts, sizeof m->opts, "%s", opts);
		}
	}
	fclose(f);
	if (best)
		return 0;
	if (!module_loaded())
		fprintf(stderr, "moobfs: the moobfs_layer module is not loaded "
			"(sudo modprobe moobfs_layer; docs/operations.md)\n");
	else
		fprintf(stderr, "moobfs: %s is not on a moobfs_layer mount (findmnt -t moobfs_layer)\n",
			path);
	return EX_UNAVAILABLE;
}

/* A numeric superblock option; the kernel shows it only when it is not the default. */
static unsigned int opt_uint(const char *opts, const char *key, unsigned int dflt)
{
	size_t klen = strlen(key);
	const char *p = opts;

	while ((p = strstr(p, key))) {
		if ((p == opts || p[-1] == ',') && p[klen] == '=')
			return (unsigned int)strtoul(p + klen + 1, NULL, 10);
		p += klen;
	}
	return dflt;
}

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

static int cmd_info(int argc, char **argv)
{
	struct moob_info info;
	struct mnt m;
	int json = 0, fd, rc;
	unsigned int f;

	if (argc >= 2 && !strcmp(argv[1], "--json")) {
		json = 1;
		argv++;
		argc--;
	}
	if (argc != 2)
		return usage();
	rc = find_mount(argv[1], &m);
	if (rc)
		return rc;
	if (strcmp(m.root, "/")) {
		fprintf(stderr, "moobfs: %s is a bind mount of %s inside a layer; GET_INFO needs the "
			"mount root, which is not reachable through it (docs/client-contract.md, 7)\n",
			m.point, m.root);
		return EX_UNAVAILABLE;
	}
	fd = open(m.point, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		fprintf(stderr, "moobfs: %s: %s\n", m.point, strerror(errno));
		return errno == EACCES ? EX_NOPERM : 1;
	}
	memset(&info, 0, sizeof info);
	if (ioctl(fd, MOOB_IOC_GET_INFO, &info)) {
		fprintf(stderr, "moobfs: GET_INFO on %s: %s\n", m.point, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	f = info.flags;
	if (json) {
		printf("{\"mount\": ");
		json_str(m.point);
		printf(", \"lower_fs\": ");
		json_str(info.lower_fs);
		printf(", \"uapi_version\": %u, \"format_version\": %u, \"domain_depth\": %u,"
		       " \"index\": %s, \"nolinks\": %s, \"on_demand\": %s, \"paths_stale\": %s,"
		       " \"index_budget\": %" PRIu64 ", \"index_build_work\": %u, \"index_idle\": %u,"
		       " \"attr_name_max\": %u, \"attr_string_max\": %u, \"attr_max_per_file\": %u",
		       info.uapi_version, info.format_version, info.domain_depth,
		       (f & MOOB_FLAG_INDEX_OFF) ? "false" : "true",
		       (f & MOOB_FLAG_NOLINKS) ? "true" : "false",
		       (f & MOOB_FLAG_ON_DEMAND) ? "true" : "false",
		       (f & MOOB_FLAG_PATHS_STALE) ? "true" : "false",
		       (uint64_t)info.index_budget, opt_uint(m.opts, "index_build_work", 32768),
		       opt_uint(m.opts, "index_idle", 60), info.attr_name_max, info.attr_string_max,
		       info.attr_max_per_file);
		if (f & MOOB_FLAG_COUNTERS)
			printf(", \"counters\": {\"index_used\": %" PRIu64 ", \"index_entries\": %u,"
			       " \"index_inodes\": %u, \"heterogeneous\": %u, \"fallback_domains\": %u,"
			       " \"domains_built\": %u, \"domain_builds\": %u, \"domain_releases\": %u}",
			       (uint64_t)info.index_used, info.index_entries, info.index_inodes,
			       info.heterogeneous, info.fallback_domains, info.domains_built,
			       info.domain_builds, info.domain_releases);
		else
			printf(", \"counters\": null");
		printf("}\n");
		return 0;
	}
	printf("mount:            %s\n", m.point);
	printf("lower filesystem: %s\n", info.lower_fs);
	printf("interface:        UAPI %u, attribute format %u\n", info.uapi_version,
	       info.format_version);
	printf("index:            %s\n", (f & MOOB_FLAG_INDEX_OFF) ? "off (noindex): no queries" :
	       (f & MOOB_FLAG_ON_DEMAND) ? "on, built per domain by its first query" : "on");
	if (info.domain_depth)
		printf("domains:          directories %u level%s below %s; query them with "
		       "moobfs query\n", info.domain_depth, info.domain_depth > 1 ? "s" : "", m.point);
	else
		printf("domains:          off; the mount-wide query is the only one\n");
	printf("links:            %s\n", (f & MOOB_FLAG_NOLINKS) ? "refused (nolinks)" : "allowed");
	printf("index budget:     %" PRIu64 " MiB\n", (uint64_t)info.index_budget >> 20);
	printf("build work:       %u units per first query of a domain\n",
	       opt_uint(m.opts, "index_build_work", 32768));
	printf("attributes:       up to %u per file, names up to %u bytes, strings up to %u bytes\n",
	       info.attr_max_per_file, info.attr_name_max, info.attr_string_max);
	if (f & MOOB_FLAG_COUNTERS)
		printf("counters:         %u domains built (%u builds, %u releases), %u inodes, "
		       "%u entries, %" PRIu64 " KiB used, %u heterogeneous, %u fallback\n",
		       info.domains_built, info.domain_builds, info.domain_releases, info.index_inodes,
		       info.index_entries, (uint64_t)info.index_used >> 10, info.heterogeneous,
		       info.fallback_domains);
	else
		printf("counters:         shown to administrators only (they sum every user's memories)\n");
	return 0;
}

static int by_ino(const void *a, const void *b)
{
	const struct moob_query_result *x = a, *y = b;

	return x->ino < y->ino ? -1 : x->ino > y->ino;
}

/* An errno from QUERY or QUERY2 as a sentence and an exit status. */
static int query_error(int err, const char *dir, const char *op, unsigned int depth)
{
	switch (err) {
	case EACCES:
		fprintf(stderr, "moobfs: %s: permission denied; a query needs read and search "
			"permission on the directory\n", dir);
		return EX_NOPERM;
	case EPERM:
		fprintf(stderr, "moobfs: %s: the mount-wide query is for administrators when domains "
			"are on; query a domain directory\n", dir);
		return EX_NOPERM;
	case ENOTTY:
		if (depth)
			fprintf(stderr, "moobfs: %s cannot be queried: give a domain directory (%u "
				"level%s below the mount root)\n", dir, depth, depth == 1 ? "" : "s");
		else
			fprintf(stderr, "moobfs: %s cannot be queried: domains are off on this mount, "
				"so only its root answers\n", dir);
		return EX_UNAVAILABLE;
	case EOPNOTSUPP:
		fprintf(stderr, "moobfs: %s: the index of this mount is off (noindex)\n", dir);
		return EX_UNAVAILABLE;
	case ENOMEM:
		fprintf(stderr, "moobfs: %s: the domain does not fit the index budget (index_budget), "
			"or the kernel is out of memory\n", dir);
		return EX_TEMPFAIL;
	case EDQUOT:
		fprintf(stderr, "moobfs: %s: too many first queries of domains in the last minute; "
			"retry later\n", dir);
		return EX_TEMPFAIL;
	case E2BIG:
		fprintf(stderr, "moobfs: %s: building this domain's index needs more work than the "
			"mount allows (index_build_work); the administrator can raise it\n", dir);
		return EX_TEMPFAIL;
	case ETIMEDOUT:
		fprintf(stderr, "moobfs: %s: building this domain's index took longer than the "
			"deadline; retry later\n", dir);
		return EX_TEMPFAIL;
	case EAGAIN:
		fprintf(stderr, "moobfs: %s: the domain kept changing while its index was built; "
			"retry\n", dir);
		return EX_TEMPFAIL;
	case EINVAL:
		fprintf(stderr, "moobfs: %s: the kernel refused the query as invalid (%s)\n", dir, op);
		return EX_DATAERR;
	default:
		fprintf(stderr, "moobfs: %s on %s: %s\n", op, dir, strerror(err));
		return 1;
	}
}

struct results {
	struct moob_query_result *v;
	size_t n, cap;
};

/*
 * A record whose path_len does not fit the path field, or with no NUL after its path, is kept
 * as a result with an unknown path: the header promises the length, not the terminator.
 */
static void check_record(struct moob_query_result *res)
{
	if (res->path_len >= MOOB_QUERY_PATH_MAX || res->path[res->path_len]) {
		res->flags |= MOOB_RES_PATH_UNKNOWN;
		res->path_len = 0;
		res->path[0] = '\0';
	}
}

static int append(struct results *r, const struct moob_query_result *page, size_t n)
{
	size_t i;

	if (r->n + n > r->cap) {
		size_t cap = r->cap ? r->cap : 256;
		struct moob_query_result *v;

		while (cap < r->n + n)
			cap *= 2;
		v = realloc(r->v, cap * sizeof *v);
		if (!v)
			return -1;
		r->v = v;
		r->cap = cap;
	}
	memcpy(r->v + r->n, page, n * sizeof *page);
	for (i = 0; i < n; i++)
		check_record(&r->v[r->n + i]);
	r->n += n;
	return 0;
}

/* Retries a call that failed with EAGAIN, as spec-query-v2 allows: three tries, 50 ms apart. */
static int ioctl_retry(int fd, unsigned long cmd, void *arg)
{
	const struct timespec pause = { 0, 50 * 1000 * 1000 };
	int i, r = -1;

	for (i = 0; i < 3; i++) {
		r = ioctl(fd, cmd, arg);
		if (!r || errno != EAGAIN)
			break;
		nanosleep(&pause, NULL);
	}
	return r;
}

/*
 * QUERY2 with the cursor until done or max results (0: all). ESTALE means the domain changed
 * how it answers between pages; the answer then starts again, at most three times.
 */
static int run_query2(int fd, const struct moob_query2 *proto, size_t max, __u32 page,
		      struct moob_query_result *buf, struct results *out)
{
	int restarts = 0;

	for (;;) {
		struct moob_query2 q = *proto;

		out->n = 0;
		for (;;) {
			size_t want = max ? max - out->n : page;

			q.limit = want < page ? (__u32)want : page;
			q.results = (__u64)(uintptr_t)buf;
			if (ioctl_retry(fd, MOOB_IOC_QUERY2, &q))
				break;
			if (q.count > q.limit)
				return EPROTO;
			if (append(out, buf, q.count))
				return ENOMEM;
			if (!q.truncated || (max && out->n >= max))
				return 0;
		}
		if (errno != ESTALE || ++restarts > 3)
			return errno;
	}
}

static int cmd_query(int argc, char **argv)
{
	struct moob_query2 q2;
	struct moob_query_result *buf;
	struct results res = { NULL, 0, 0 };
	struct mnt m;
	int json = 0, ino = 0, n, k = 1, fd, rc, truncated = 0, v1 = 0;
	unsigned long max = 0, page = MOOB_QUERY_MAX_LIMIT;
	unsigned int depth;
	size_t i;
	char *end;

	memset(&q2, 0, sizeof q2);
	while (k < argc && !strncmp(argv[k], "--", 2)) {
		if (!strcmp(argv[k], "--json")) {
			json = 1;
			k++;
		} else if (!strcmp(argv[k], "--ino")) {
			ino = 1;
			k++;
		} else if (!strcmp(argv[k], "--order") && k + 3 < argc) {
			int t = parse_type(argv[k + 2]);
			size_t olen = strlen(argv[k + 1]);

			if (t < 0 || !olen || olen >= sizeof q2.order_by ||
			    (strcmp(argv[k + 3], "asc") && strcmp(argv[k + 3], "desc")))
				return usage();
			memcpy(q2.order_by, argv[k + 1], olen + 1);
			q2.order_type = (__u8)t;
			q2.order = !strcmp(argv[k + 3], "desc") ? MOOB_ORDER_DESC : MOOB_ORDER_ASC;
			k += 4;
		} else if (!strcmp(argv[k], "--limit") && k + 1 < argc) {
			errno = 0;
			max = strtoul(argv[k + 1], &end, 10);
			if (errno || *end || !max)
				return usage();
			k += 2;
		} else if (!strcmp(argv[k], "--page") && k + 1 < argc) {
			errno = 0;
			page = strtoul(argv[k + 1], &end, 10);
			if (errno || *end || !page || page > MOOB_QUERY_MAX_LIMIT)
				return usage();
			k += 2;
		} else {
			return usage();
		}
	}
	if (k >= argc)
		return usage();
	n = parse_terms(argc, argv, k + 1, q2.terms);
	if (n < 0 || (n == 0 && !q2.order_by[0])) {
		fprintf(stderr, "moobfs: invalid query terms (moobfs --help)\n");
		return EX_DATAERR;
	}
	rc = find_mount(argv[k], &m);
	if (rc)
		return rc;
	depth = opt_uint(m.opts, "domain_depth", 1);
	fd = open(argv[k], O_RDONLY | O_DIRECTORY);
	if (fd < 0 && errno == EACCES)
		return query_error(EACCES, argv[k], "open", depth);
	if (fd < 0) {
		fprintf(stderr, "moobfs: %s: %s\n", argv[k], strerror(errno));
		return 1;
	}
	buf = calloc(MOOB_QUERY_MAX_LIMIT, sizeof *buf);
	if (!buf)
		return 1;
	q2.version = MOOB_QUERY2_VERSION;
	q2.nterms = (__u32)n;
	rc = run_query2(fd, &q2, max, (__u32)page, buf, &res);
	if ((rc == ENOTTY || rc == EOPNOTSUPP) && !depth && !q2.order_by[0]) {
		/* no domains: the mount root answers the mount-wide QUERY, without a cursor */
		struct moob_query q;

		memset(&q, 0, sizeof q);
		q.version = MOOB_UAPI_VERSION;
		q.nterms = (__u32)n;
		memcpy(q.terms, q2.terms, sizeof q.terms);
		q.limit = max && max < MOOB_QUERY_MAX_LIMIT ? (__u32)max : MOOB_QUERY_MAX_LIMIT;
		q.results = (__u64)(uintptr_t)buf;
		v1 = 1;
		rc = ioctl_retry(fd, MOOB_IOC_QUERY, &q) ? errno : 0;
		if (!rc && q.count > q.limit)
			rc = EPROTO;
		if (!rc && append(&res, buf, q.count))
			rc = ENOMEM;
		truncated = q.truncated && !(max && res.n >= max);
	}
	close(fd);
	if (rc)
		return query_error(rc, argv[k], v1 ? "QUERY" : "QUERY2", depth);
	if (!q2.order_by[0] && res.n)
		qsort(res.v, res.n, sizeof *res.v, by_ino);
	if (json) {
		printf("{\"dir\": ");
		json_str(argv[k]);
		printf(", \"ioctl\": \"%s\", \"complete\": %s, \"count\": %zu, \"results\": [",
		       v1 ? "QUERY" : "QUERY2", truncated ? "false" : "true", res.n);
		for (i = 0; i < res.n; i++) {
			printf("%s\n  {\"ino\": %" PRIu64 ", \"path\": ", i ? "," : "",
			       (uint64_t)res.v[i].ino);
			json_str(res.v[i].path);
			printf(", \"path_unknown\": %s, \"path_truncated\": %s}",
			       (res.v[i].flags & MOOB_RES_PATH_UNKNOWN) ? "true" : "false",
			       (res.v[i].flags & MOOB_RES_PATH_TRUNCATED) ? "true" : "false");
		}
		printf("%s]}\n", res.n ? "\n" : "");
	} else {
		for (i = 0; i < res.n; i++) {
			if (ino)
				printf("%" PRIu64 "\t", (uint64_t)res.v[i].ino);
			printf("%s%s\n", res.v[i].path,
			       (res.v[i].flags & MOOB_RES_PATH_UNKNOWN) ? "?" : "");
		}
	}
	if (truncated) {
		fprintf(stderr, "moobfs: more than %u results; the mount-wide query cannot return "
			"the rest (use domains)\n", MOOB_QUERY_MAX_LIMIT);
		return EX_INCOMPLETE;
	}
	return 0;
}

/* moobfs-check from the directory of this program, else from PATH. */
static int cmd_check(char **argv)
{
	char self[PATH_MAX], tool[PATH_MAX + 16];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);

	argv[0] = "moobfs-check";
	if (n > 0) {
		char *slash;

		self[n] = 0;
		slash = strrchr(self, '/');
		if (slash) {
			*slash = 0;
			snprintf(tool, sizeof tool, "%s/moobfs-check", self);
			if (!access(tool, X_OK))
				execv(tool, argv);
		}
	}
	execvp("moobfs-check", argv);
	fprintf(stderr, "moobfs: cannot run moobfs-check: %s\n", strerror(errno));
	return 1;
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return usage();
	if (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "help")) {
		fputs(synopsis, stdout);
		return 0;
	}
	if (!strcmp(argv[1], "info"))
		return cmd_info(argc - 1, argv + 1);
	if (!strcmp(argv[1], "query"))
		return cmd_query(argc - 1, argv + 1);
	if (!strcmp(argv[1], "check"))
		return cmd_check(argv + 1);
	return usage();
}
