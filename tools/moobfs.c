/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs: one command for the things a user does with a mounted MoobFS layer (roadmap item C4,
 * ADR-0022). User space only, over the existing ioctls and tools; no new interface.
 *
 *   moobfs info [--json] <path>
 *   moobfs query [--json] [--ino] [--order NAME TYPE asc|desc] [--limit N] [--page N]
 *                <dir> [terms...]
 *   moobfs check [moobfs-check arguments]
 *   moobfs diagnose [--json] <path>
 *   moobfs --help
 *
 * info finds the layer mount that holds <path> (/proc/self/mountinfo) and prints GET_INFO of
 * its root. query issues QUERY2 on a domain directory and follows the cursor until the answer
 * is complete or --limit results were printed; on the mount root of a mount without domains it
 * issues the mount-wide QUERY, which has no cursor. Paths are relative to <dir>. Without
 * --order the results are sorted by inode number, as moobfs-index prints them. Terms are those
 * of moobfs-index: "name op type value", up to four. check runs moobfs-check. diagnose reports,
 * without changing anything, the limits that apply to the caller of a query on <path>: the
 * mount's index settings, the memory cgroup limits above the calling process and how often they
 * were reached, the descriptor limit, test-only module parameters left set and, for an
 * administrator, the index counters and the tracepoints (roadmap item G2, journal 0243). When a
 * query fails with ENOMEM, query reads the caller's memory cgroups again and says whether one of
 * their limits or the index budget refused it (tools/diag.h).
 *
 * Exit status (docs/tools.md): 0 success; 1 a system call failed; 64 usage; 65 invalid terms or
 * a query the kernel refused as invalid; 69 no layer at <path> (module not loaded, another
 * filesystem, or a directory that cannot be queried); 75 temporary (the domain does not fit the
 * index budget, a build limit, or the domain kept changing); 77 permission denied; 3 the answer
 * is incomplete (the mount-wide QUERY had more results than it can return). diagnose exits 0, or
 * 2 when it printed a warning, besides the find_mount statuses above.
 *
 * /proc/self/mountinfo is optional (issue #167): a sandbox such as Landlock may deny reading it.
 * Without it the layer is recognised by statfs, query still issues QUERY2 and learns the domain
 * depth from GET_INFO when <dir> is the mount root, and info takes the highest directory above
 * <path> on the same device as the mount root; only the build work and idle options (shown in
 * the mount record alone) and the domain depth in some error messages are then unknown.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include "query-common.h"
#include "diag.h"

#define EX_INCOMPLETE 3
#define EX_USAGE 64
#define EX_DATAERR 65
#define EX_UNAVAILABLE 69
#define EX_TEMPFAIL 75
#define EX_NOPERM 77

/* statfs f_type of a moobfs_layer mount (docs/client-contract.md, section 7) */
#define LAYER_FS_TYPE 0x4d4f4f42

static const char synopsis[] =
	"usage: moobfs info [--json] <path>\n"
	"       moobfs query [--json] [--ino] [--order NAME TYPE asc|desc] [--limit N] [--page N]\n"
	"                    <dir> [name op type value]...\n"
	"       moobfs check [moobfs-check arguments]\n"
	"       moobfs diagnose [--json] <path>\n"
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
	int record;		/* 0: mountinfo was not readable; point is a guess, root and opts
				 * are unknown (see top_of_device) */
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
 * The highest directory of real (a real path, shortened in place) on the device dev: the mount
 * point of the layer when real is inside it, unless a bind mount of a directory of the same
 * layer is above it. Only stat is used, which Landlock does not restrict.
 */
static void top_of_device(char *real, dev_t dev)
{
	struct stat st;
	char *slash;

	while ((slash = strrchr(real, '/')) && slash != real) {
		*slash = 0;
		if (stat(real, &st) || st.st_dev != dev) {
			*slash = '/';
			return;
		}
	}
	if (slash == real && real[1]) {	/* the parent is "/" */
		if (!stat("/", &st) && st.st_dev == dev)
			real[1] = 0;
	}
}

/*
 * Fills m with the moobfs_layer mount whose device is that of path and whose mount point is the
 * longest prefix of its real path. When /proc/self/mountinfo cannot be read (a sandbox, issue
 * #167), recognises the layer by statfs and leaves m->record 0. Returns 0, or an exit status
 * after printing why.
 */
static int find_mount(const char *path, struct mnt *m)
{
	char real[PATH_MAX], line[4 * PATH_MAX];
	struct stat st;
	size_t best = 0;
	FILE *f;

	memset(m, 0, sizeof *m);

	if (!realpath(path, real) || stat(real, &st)) {
		int err = errno;

		fprintf(stderr, "moobfs: %s: %s\n", path,
			err == EACCES ? "permission denied" : strerror(err));
		return err == EACCES ? EX_NOPERM : 1;
	}
	f = fopen("/proc/self/mountinfo", "r");
	if (!f) {
		struct statfs sf;

		if (statfs(real, &sf)) {
			fprintf(stderr, "moobfs: %s: %s\n", path, strerror(errno));
			return 1;
		}
		if (sf.f_type == LAYER_FS_TYPE) {
			top_of_device(real, st.st_dev);
			snprintf(m->point, sizeof m->point, "%s", real);
			return 0;
		}
	}
	while (f && fgets(line, sizeof line, f)) {
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
			m->record = 1;
			snprintf(m->point, sizeof m->point, "%s", point);
			snprintf(m->root, sizeof m->root, "%s", root);
			snprintf(m->opts, sizeof m->opts, "%s", opts);
		}
	}
	if (f)
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
	/* only the mount record shows these two options; without it they are unknown */
	char work[16] = "null", idle[16] = "null";

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
	if (m.record && strcmp(m.root, "/")) {
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
		if (!m.record && errno == ENOTTY) {
			fprintf(stderr, "moobfs: %s is not the root of its layer mount (a bind mount of a "
				"directory inside a layer?); /proc/self/mountinfo, which would tell, is not "
				"readable, and GET_INFO needs the mount root\n", m.point);
			close(fd);
			return EX_UNAVAILABLE;
		}
		fprintf(stderr, "moobfs: GET_INFO on %s: %s\n", m.point, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	f = info.flags;
	if (m.record) {
		snprintf(work, sizeof work, "%u", opt_uint(m.opts, "index_build_work", 32768));
		snprintf(idle, sizeof idle, "%u", opt_uint(m.opts, "index_idle", 60));
	}
	if (json) {
		printf("{\"mount\": ");
		json_str(m.point);
		printf(", \"lower_fs\": ");
		json_str(info.lower_fs);
		printf(", \"uapi_version\": %u, \"format_version\": %u, \"domain_depth\": %u,"
		       " \"index\": %s, \"nolinks\": %s, \"on_demand\": %s, \"paths_stale\": %s,"
		       " \"index_budget\": %" PRIu64 ", \"index_build_work\": %s, \"index_idle\": %s,"
		       " \"attr_name_max\": %u, \"attr_string_max\": %u, \"attr_max_per_file\": %u",
		       info.uapi_version, info.format_version, info.domain_depth,
		       (f & MOOB_FLAG_INDEX_OFF) ? "false" : "true",
		       (f & MOOB_FLAG_NOLINKS) ? "true" : "false",
		       (f & MOOB_FLAG_ON_DEMAND) ? "true" : "false",
		       (f & MOOB_FLAG_PATHS_STALE) ? "true" : "false",
		       (uint64_t)info.index_budget, work, idle, info.attr_name_max, info.attr_string_max,
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
	if (m.record)
		printf("build work:       %s units per first query of a domain\n", work);
	else
		printf("build work:       unknown (/proc/self/mountinfo is not readable)\n");
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
static int query_error(int err, const char *dir, const char *op, int depth)
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
		if (depth < 0)
			fprintf(stderr, "moobfs: %s cannot be queried: give a domain directory (the "
				"domain depth is unknown: /proc/self/mountinfo is not readable)\n", dir);
		else if (depth)
			fprintf(stderr, "moobfs: %s cannot be queried: give a domain directory (%d "
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
			"or a memory limit or the kernel's memory refused it; moobfs diagnose %s shows "
			"the limits that apply\n", dir, dir);
		return EX_TEMPFAIL;
	case EDQUOT:
		fprintf(stderr, "moobfs: %s: too many first queries of domains in the last minute, by "
			"this user or on this mount; retry in a minute\n", dir);
		return EX_TEMPFAIL;
	case E2BIG:
		fprintf(stderr, "moobfs: %s: building this domain's index needs more work than the "
			"mount allows (index_build_work, at most 65536); the administrator can raise it, "
			"or mount with a larger domain_depth so that domains are smaller\n", dir);
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

#define PROC_CGROUP "/proc/self/cgroup"
#define CGROUP_ROOT "/sys/fs/cgroup"
#define LAYER_SYSMOD "/sys/module/moobfs_layer"

static unsigned long long mib(unsigned long long bytes)
{
	return bytes >> 20;
}

/*
 * After ENOMEM: was it the memory cgroup of the caller (a limit whose local "max" or "oom"
 * count grew during the query), or the index budget? before is the chain read before the
 * query, NULL if it could not be read. Returns the exit status, after printing the cause.
 */
static int enomem_error(const char *dir, const char *point, const struct diag_chain *before)
{
	struct diag_chain *after = before ? malloc(sizeof *after) : NULL;
	struct moob_info info;
	char budget[48] = "";
	int fd, hit = -1, lim = -1;

	memset(&info, 0, sizeof info);
	fd = open(point, O_RDONLY | O_DIRECTORY);
	if (fd >= 0 && !ioctl(fd, MOOB_IOC_GET_INFO, &info))
		snprintf(budget, sizeof budget, ", %llu MiB",
			 mib((unsigned long long)info.index_budget));
	if (fd >= 0)
		close(fd);
	if (after && !diag_chain_read(PROC_CGROUP, CGROUP_ROOT, after)) {
		hit = diag_hit(before, after);
		lim = diag_effective(after);
	}
	if (hit >= 0) {
		fprintf(stderr, "moobfs: %s: refused (ENOMEM): the memory cgroup %s reached its "
			"memory.max (%llu MiB) during the query; the index a query builds and the "
			"buffer it keeps are charged to the caller's cgroup. Close idle descriptors "
			"that queried, or ask the administrator for a larger MemoryMax= "
			"(docs/operations.md, section 5)\n", dir, after->lv[hit].path,
			mib(after->lv[hit].max));
	} else if (after && before) {
		fprintf(stderr, "moobfs: %s: refused (ENOMEM): the domain does not fit the mount's "
			"index budget (index_budget%s), or this user's fair share of it while other "
			"users hold theirs; ", dir, budget);
		if (lim >= 0)
			fprintf(stderr, "the memory cgroup %s (memory.max %llu MiB) did not reach "
				"its limit. ", after->lv[lim].path, mib(after->lv[lim].max));
		else
			fprintf(stderr, "no memory cgroup limit applies to this process. ");
		fprintf(stderr, "Retry later, or ask the administrator to raise index_budget\n");
	} else {
		free(after);
		return query_error(ENOMEM, dir, "QUERY2", 0);
	}
	free(after);
	return EX_TEMPFAIL;
}

static int cmd_query(int argc, char **argv)
{
	struct moob_query2 q2;
	struct moob_query_result *buf;
	struct results res = { NULL, 0, 0 };
	struct mnt m;
	struct diag_chain *cg;
	int json = 0, ino = 0, n, k = 1, fd, rc, truncated = 0, v1 = 0;
	unsigned long max = 0, page = MOOB_QUERY_MAX_LIMIT;
	int depth;
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
	/* -1: unknown without the mount record; GET_INFO tells it below if <dir> is the root */
	depth = m.record ? (int)opt_uint(m.opts, "domain_depth", 1) : -1;
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
	/* the caller's memory cgroups before the query, to tell an ENOMEM's cause afterwards */
	cg = malloc(sizeof *cg);
	if (cg && diag_chain_read(PROC_CGROUP, CGROUP_ROOT, cg)) {
		free(cg);
		cg = NULL;
	}
	rc = run_query2(fd, &q2, max, (__u32)page, buf, &res);
	if ((rc == ENOTTY || rc == EOPNOTSUPP) && depth < 0) {
		struct moob_info info;

		memset(&info, 0, sizeof info);
		if (!ioctl(fd, MOOB_IOC_GET_INFO, &info))	/* only the mount root answers it */
			depth = (int)info.domain_depth;
	}
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
	if (rc == ENOMEM) {
		rc = enomem_error(argv[k], m.point, cg);
		free(cg);
		return rc;
	}
	free(cg);
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

/* One finding of diagnose: a warning makes the exit status 2. */
struct finding {
	int warning;
	const char *code;
	char text[640];
};

#define MAX_FINDINGS 16

struct findings {
	int n, warnings;
	struct finding v[MAX_FINDINGS];
};

__attribute__((format(printf, 4, 5)))
static void add(struct findings *f, int warning, const char *code, const char *fmt, ...)
{
	va_list ap;

	if (f->n == MAX_FINDINGS)
		return;
	f->v[f->n].warning = warning;
	f->v[f->n].code = code;
	va_start(ap, fmt);
	vsnprintf(f->v[f->n].text, sizeof f->v[f->n].text, fmt, ap);
	va_end(ap);
	f->warnings += warning;
	f->n++;
}

/*
 * Depth of real below the root of the layer filesystem, given its mount record (a bind mount
 * of a subdirectory adds the components of m->root). -1 when unknown.
 */
static int depth_below_root(const char *real, const struct mnt *m)
{
	const char *p = real + strlen(m->point);
	int d = 0;

	if (!m->record)
		return -1;
	if (strcmp(m->point, "/") && *p && *p != '/')
		return -1;
	if (!strcmp(m->point, "/"))
		p = real;
	for (; *p; p++)
		if (*p == '/' && p[1] && p[1] != '/')
			d++;
	for (p = m->root; *p; p++)
		if (*p == '/' && p[1] && p[1] != '/')
			d++;
	return d;
}

/* Test-only parameters of the module (layer/index.c); not 0 means a build is slowed or fails. */
static const char *const test_params[] = { "scan_delay_us", "scan_hold_us",
					   "scan_getxattr_errno_once" };
#define NTEST_PARAMS (sizeof test_params / sizeof test_params[0])

static void json_ull_or_null(int has, unsigned long long v)
{
	if (has && v != DIAG_NOLIMIT)
		printf("%llu", v);
	else
		printf("null");
}

static int cmd_diagnose(int argc, char **argv)
{
	struct moob_info info;
	struct mnt m;
	struct diag_chain *cg = malloc(sizeof *cg);
	struct findings *fs = calloc(1, sizeof *fs);
	struct rlimit nofile = { 0, 0 };
	char real[PATH_MAX], tracing[16] = "";
	long long params[NTEST_PARAMS];
	int have_params[NTEST_PARAMS];
	int json = 0, fd, rc, have_info = 0, have_cg = 0, lim = -1, depth, role_depth, admin;
	unsigned int work = 0;
	size_t i;
	const char *role = "unknown";

	if (!cg || !fs)
		return 1;
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
	if (!realpath(argv[1], real))
		snprintf(real, sizeof real, "%s", argv[1]);
	admin = geteuid() == 0;

	/* the mount: GET_INFO answers on its root only */
	memset(&info, 0, sizeof info);
	fd = open(m.point, O_RDONLY | O_DIRECTORY);
	if (fd >= 0) {
		have_info = !ioctl(fd, MOOB_IOC_GET_INFO, &info);
		close(fd);
	}
	if (m.record)
		work = opt_uint(m.opts, "index_build_work", 32768);
	depth = have_info ? (int)info.domain_depth :
		m.record ? (int)opt_uint(m.opts, "domain_depth", 1) : -1;
	role_depth = depth_below_root(real, &m);
	if (depth >= 0 && role_depth >= 0) {
		if (!depth)
			role = role_depth ? "inside" : "mount-root";
		else
			role = role_depth == depth ? "domain" : role_depth < depth ? "above" : "inside";
	}
	if (!have_info)
		add(fs, 0, "info-unknown", "GET_INFO on %s failed, so the index settings are unknown "
		    "(a bind mount of a directory inside a layer, or the mount root not readable)",
		    m.point);
	else if (info.flags & MOOB_FLAG_INDEX_OFF)
		add(fs, 0, "index-off", "the index of this mount is off (noindex): QUERY and QUERY2 "
		    "answer EOPNOTSUPP; moobfs-index scans the lower tree in user space instead");
	if (!strcmp(role, "above"))
		add(fs, 0, "not-a-domain", "%s is above the domains: queries go to a directory %d "
		    "level%s below the mount root", argv[1], depth, depth == 1 ? "" : "s");
	else if (!strcmp(role, "inside") && depth)
		add(fs, 0, "not-a-domain", "%s is inside a domain: query its domain directory, %d "
		    "level%s below the mount root", argv[1], depth, depth == 1 ? "" : "s");

	/* the caller's memory cgroups (ADR-0018, ADR-0029) */
	have_cg = !diag_chain_read(PROC_CGROUP, CGROUP_ROOT, cg);
	(void)getrlimit(RLIMIT_NOFILE, &nofile);
	if (have_cg)
		lim = diag_effective(cg);
	if (!have_cg) {
		add(fs, 0, "cgroup-unknown", "the memory cgroup of this process cannot be read (no "
		    "cgroup v2 hierarchy at " CGROUP_ROOT ", or a sandbox denies " PROC_CGROUP ")");
	} else if (lim < 0) {
		add(fs, 1, "no-memory-limit", "no memory.max applies to this process (cgroup %s): the "
		    "index its queries build and the buffer each descriptor that queried keeps (about "
		    "2 MiB at the largest page, up to RLIMIT_NOFILE=%llu descriptors per process) are "
		    "bounded only by the machine's memory (ADR-0029). The deployment recipe sets "
		    "MemoryMax= for the users of a shared mount (docs/operations.md, section 5)",
		    cg->n ? cg->lv[0].path : "/", (unsigned long long)nofile.rlim_cur);
	} else {
		const struct diag_level *l = &cg->lv[lim];

		if (l->ev_oom_kill)
			add(fs, 1, "oom-killed", "%llu process%s in the memory cgroup %s %s killed at "
			    "its memory.max (%llu MiB); a query near the limit can be killed instead of "
			    "refused", l->ev_oom_kill, l->ev_oom_kill == 1 ? "" : "es", l->path,
			    l->ev_oom_kill == 1 ? "was" : "were", mib(l->max));
		if (l->ev_max || l->ev_oom)
			add(fs, 0, "limit-reached", "the memory cgroup %s reached its memory.max "
			    "(%llu MiB) %llu time%s (%llu without room after reclaim); a query that "
			    "needs memory there then fails with ENOMEM", l->path, mib(l->max),
			    l->ev_max, l->ev_max == 1 ? "" : "s", l->ev_oom);
		if (l->has_current && l->max && l->current >= l->max - l->max / 10)
			add(fs, 1, "near-limit", "the memory cgroup %s uses %llu of its %llu MiB",
			    l->path, mib(l->current), mib(l->max));
	}

	/* the module */
	for (i = 0; i < NTEST_PARAMS; i++) {
		have_params[i] = !diag_param(LAYER_SYSMOD, test_params[i], &params[i]);
		if (have_params[i] && params[i])
			add(fs, 1, "test-parameter", "the test-only module parameter %s is %lld: index "
			    "builds are slowed or made to fail (echo 0 > " LAYER_SYSMOD
			    "/parameters/%s)", test_params[i], params[i], test_params[i]);
	}
	if (have_info && (info.flags & MOOB_FLAG_COUNTERS) && info.index_budget &&
	    info.index_used >= info.index_budget - info.index_budget / 10)
		add(fs, 1, "budget-near-full", "the index holds %llu of its %llu MiB budget: first "
		    "queries of other domains can be refused with ENOMEM until a domain is released",
		    mib((unsigned long long)info.index_used), mib((unsigned long long)info.index_budget));
	if (admin) {
		static const char *const roots[] = { "/sys/kernel/tracing", "/sys/kernel/debug/tracing" };
		char file[PATH_MAX];

		for (i = 0; i < 2 && !tracing[0]; i++) {
			snprintf(file, sizeof file, "%s/events/moobfs/enable", roots[i]);
			if (!diag_line(file, tracing, sizeof tracing))
				add(fs, 0, "tracepoints", "the moobfs tracepoints are %s: each refused "
				    "build is a moobfs_domain_build line with err=<-errno> (enable: echo 1 > "
				    "%s; read %s/trace_pipe; docs/operations.md, section 8)",
				    !strcmp(tracing, "0") ? "off" : "on", file, roots[i]);
		}
		if (!tracing[0])
			add(fs, 0, "tracepoints", "no moobfs tracepoints found (tracefs not mounted at "
			    "/sys/kernel/tracing, or the module not loaded)");
	} else {
		add(fs, 0, "administrator-only", "the index counters and the tracepoints are shown to "
		    "administrators only (they cover every user's memories)");
	}

	if (json) {
		printf("{\"path\": ");
		json_str(argv[1]);
		printf(", \"mount\": ");
		json_str(m.point);
		printf(", \"role\": \"%s\", \"uid\": %u", role, (unsigned int)getuid());
		if (have_info) {
			printf(", \"index\": {\"on\": %s, \"domain_depth\": %u, \"index_budget\": %" PRIu64
			       ", \"index_build_work\": ",
			       (info.flags & MOOB_FLAG_INDEX_OFF) ? "false" : "true", info.domain_depth,
			       (uint64_t)info.index_budget);
			if (m.record)
				printf("%u", work);
			else
				printf("null");
			printf(", \"index_used\": ");
			if (info.flags & MOOB_FLAG_COUNTERS)
				printf("%" PRIu64, (uint64_t)info.index_used);
			else
				printf("null");
			printf("}");
		} else {
			printf(", \"index\": null");
		}
		printf(", \"memory_cgroup\": ");
		if (have_cg) {
			int j;

			printf("{\"cgroup\": ");
			json_str(cg->n ? cg->lv[0].path : "/");
			printf(", \"limit\": ");
			if (lim >= 0) {
				printf("{\"cgroup\": ");
				json_str(cg->lv[lim].path);
				printf(", \"memory_max\": %llu}", cg->lv[lim].max);
			} else {
				printf("null");
			}
			printf(", \"levels\": [");
			for (j = 0; j < cg->n; j++) {
				const struct diag_level *l = &cg->lv[j];

				printf("%s{\"cgroup\": ", j ? ", " : "");
				json_str(l->path);
				printf(", \"memory_max\": ");
				json_ull_or_null(l->has_max, l->max);
				printf(", \"memory_current\": ");
				json_ull_or_null(l->has_current, l->current);
				printf(", \"events_max\": %llu, \"events_oom\": %llu, \"events_oom_kill\": "
				       "%llu}", l->ev_max, l->ev_oom, l->ev_oom_kill);
			}
			printf("]}");
		} else {
			printf("null");
		}
		printf(", \"nofile\": %llu, \"module_parameters\": {", (unsigned long long)nofile.rlim_cur);
		for (i = 0; i < NTEST_PARAMS; i++) {
			printf("%s\"%s\": ", i ? ", " : "", test_params[i]);
			if (have_params[i])
				printf("%lld", params[i]);
			else
				printf("null");
		}
		printf("}, \"tracepoints\": ");
		if (!admin)
			printf("null");
		else if (!tracing[0])
			printf("\"unavailable\"");
		else
			printf("\"%s\"", !strcmp(tracing, "0") ? "off" : "on");
		printf(", \"warnings\": %d, \"findings\": [", fs->warnings);
		for (rc = 0; rc < fs->n; rc++) {
			printf("%s\n  {\"level\": \"%s\", \"code\": \"%s\", \"text\": ", rc ? "," : "",
			       fs->v[rc].warning ? "warning" : "note", fs->v[rc].code);
			json_str(fs->v[rc].text);
			printf("}");
		}
		printf("%s]}\n", fs->n ? "\n" : "");
	} else {
		printf("path:             %s (%s)\n", argv[1], !strcmp(role, "domain") ?
		       "a domain directory" : !strcmp(role, "above") ? "above the domains" :
		       !strcmp(role, "inside") ? "inside a domain" : !strcmp(role, "mount-root") ?
		       "the mount root" : "role unknown");
		printf("mount:            %s\n", m.point);
		if (have_info) {
			printf("index:            %s, budget %llu MiB", (info.flags & MOOB_FLAG_INDEX_OFF) ?
			       "off" : "on", mib((unsigned long long)info.index_budget));
			if (info.flags & MOOB_FLAG_COUNTERS)
				printf(" (%llu MiB used)", mib((unsigned long long)info.index_used));
			if (m.record)
				printf(", build work %u", work);
			printf("\n");
		}
		if (have_cg) {
			printf("memory cgroup:    %s\n", cg->n ? cg->lv[0].path : "/");
			if (lim >= 0)
				printf("memory limit:     %llu MiB at %s (%llu MiB used)\n",
				       mib(cg->lv[lim].max), cg->lv[lim].path,
				       cg->lv[lim].has_current ? mib(cg->lv[lim].current) : 0ULL);
			else
				printf("memory limit:     none\n");
		} else {
			printf("memory cgroup:    unknown\n");
		}
		printf("descriptors:      up to %llu per process\n", (unsigned long long)nofile.rlim_cur);
		for (rc = 0; rc < fs->n; rc++)
			printf("%s %s\n", fs->v[rc].warning ? "warning:" : "note:   ", fs->v[rc].text);
	}
	rc = fs->warnings ? 2 : 0;
	free(cg);
	free(fs);
	return rc;
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
	if (!strcmp(argv[1], "diagnose"))
		return cmd_diagnose(argc - 1, argv + 1);
	return usage();
}
