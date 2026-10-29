/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs-check: validate every typed attribute under a lower directory (or a
 * mounted layer) and the format marker on its root. Diagnoses only; never
 * repairs. Output is one JSON object. Exit: 0 clean, 2 problems found, 1 error.
 *
 * With --domain-depth N (the mount option of the layer), it also lists under "left_out"
 * the files that carry typed attributes and that QUERY2 does not return, with the reason:
 * they fail the homogeneity rule of docs/spec-query-v2.md section 3, or sit above every
 * domain. These are not problems and do not change the exit status.
 *
 *   moobfs-check [--domain-depth N] <directory>
 *   moobfs-check --help
 *
 * Exit status (docs/tools.md): 0 clean; 2 problems found; 1 the tree could not be walked;
 * 64 usage.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/xattr.h>
#include <sys/stat.h>
#include <limits.h>
#include "moobfs_attr.h"
#include "walk.h"

static unsigned long files, attrs, problems;
/* identity records for the (namespace, memory_id) uniqueness check */
struct ident { char ns[1025]; char id[1025]; dev_t dev; ino_t ino; char *path; };
static struct ident *idents; static size_t nidents, cidents;
static unsigned long ident_conflicts, ident_cross_namespace;
static int first_problem = 1;
static int domain_depth;	/* 0: do not look for files left out of QUERY2 */
struct left { char *path; char *why; };
static struct left *lefts; static size_t nlefts, clefts;

/* The rule of tools/moobfs-index.c (reach_ok), with the reason when it fails. */
static const char *reach_why(const struct stat *st, int acl, const struct stat *dom,
			     int dom_acl, int want_read)
{
	/* an intermediate directory needs read as well as search (journal 0215) */
	mode_t u = want_read ? S_IRUSR : S_IRUSR | S_IXUSR;
	mode_t g = want_read ? S_IRGRP : S_IRGRP | S_IXGRP;
	mode_t o = want_read ? S_IROTH : S_IROTH | S_IXOTH;

	/* every class: the kernel never judges owner or group by the bits of others */
	if (!acl && (st->st_mode & (u | g | o)) == (u | g | o))
		return NULL;
	if (acl)
		return "has an extended access ACL";
	if (dom_acl)
		return "the domain directory has an extended access ACL";
	if (st->st_uid != dom->st_uid || st->st_gid != dom->st_gid)
		return "owner or group differs from the domain directory";
	if (((dom->st_mode & S_IXUSR) && (st->st_mode & u) != u) ||
	    ((dom->st_mode & S_IXGRP) && (st->st_mode & g) != g) ||
	    ((dom->st_mode & S_IXOTH) && (st->st_mode & o) != o))
		return "more private than the domain directory";
	return NULL;
}

/* rel: the file, relative to the root; where (or NULL): the directory between, likewise */
static int left_out(const char *rel, const char *where, const char *why)
{
	char buf[PATH_MAX + 96];

	if (nlefts == clefts) {
		clefts = clefts ? clefts * 2 : 64;
		lefts = realloc(lefts, clefts * sizeof *lefts);
		if (!lefts)
			return 1;
	}
	if (where)
		snprintf(buf, sizeof buf, "directory %s: %s", where, why);
	else
		snprintf(buf, sizeof buf, "%s", why);
	lefts[nlefts].path = strdup(rel);
	lefts[nlefts].why = strdup(buf);
	if (!lefts[nlefts].path || !lefts[nlefts].why)
		return 1;
	nlefts++;
	return 0;
}

/*
 * Is this inode, which carries typed attributes, in its domain's answers? The domain directory
 * and every directory between are the walk's frames: the inodes it listed to reach this one.
 */
static int check_domain(struct moob_walk *w, const char *rel, const struct moob_node *n, int level)
{
	const struct stat *st = &n->st, *dom;
	const char *why;
	int i, dom_acl;

	if (level < domain_depth || (level == domain_depth && !S_ISDIR(st->st_mode)))
		return left_out(rel, NULL, "above every domain directory");
	if (level == domain_depth)
		return 0;	/* a domain directory itself */
	dom = &w->frames[domain_depth].node.st;
	dom_acl = w->frames[domain_depth].ext_acl;
	if (S_ISREG(st->st_mode) && st->st_nlink > 1)
		return left_out(rel, NULL, "has more than one hard link");
	for (i = domain_depth + 1; i < level; i++) {	/* every directory strictly between */
		const struct moob_walk_frame *f = &w->frames[i];

		why = reach_why(&f->node.st, f->ext_acl, dom, dom_acl, 0);
		if (why) {
			char where[PATH_MAX];

			snprintf(where, sizeof where, "%.*s", (int)f->rel_len, rel);
			return left_out(rel, where, why);
		}
	}
	why = reach_why(st, moob_node_ext_acl(n), dom, dom_acl, 1);
	return why ? left_out(rel, NULL, why) : 0;
}

/*
 * Every string of the document goes through here: an attribute name that fails validation, a
 * directory name inside a left-out reason or a path can hold any byte but NUL.
 */
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

/* rel: the path relative to the root, "" for the root itself */
static void report(const char *rel, const char *name, const char *why)
{
	printf("%s\n    {\"path\": ", first_problem ? "" : ",");
	json_str(rel);
	printf(", \"attribute\": ");
	json_str(name ? name : "");
	printf(", \"problem\": ");
	json_str(why);
	putchar('}');
	first_problem = 0;
	problems++;
}

/* Every read goes through the descriptor the walk opened: the inode judged is the one reported. */
static int visit(struct moob_walk *w, const char *path, const struct moob_node *node, int level)
{
	const struct stat *sb = &node->st;
	char *list, *p;
	ssize_t len;
	unsigned long per_file = 0;

	files++;
	len = moob_node_listxattr(node, NULL, 0);
	if (len < 0) {
		if (errno == ENOTSUP || errno == EOPNOTSUPP)
			return 0;
		report(path, NULL, "cannot list attributes");
		return 0;
	}
	if (!len)
		return 0;
	if (len > XATTR_LIST_MAX)	/* the kernel's index reads at most this much (R2-12, journal 0156) */
		report(path, NULL, "attribute list larger than 64 KiB: left out of the layer's index");
	list = malloc((size_t)len);
	if (!list)
		return 1;
	len = moob_node_listxattr(node, list, (size_t)len);
	if (len < 0) {
		free(list);
		report(path, NULL, "cannot list attributes");
		return 0;
	}
	for (p = list; p < list + len; p += strlen(p) + 1) {
		ma_u8 value[MOOB_ATTR_VALUE_MAX + 1];
		const char *name = p + MOOB_ATTR_PREFIX_LEN;
		size_t nlen;
		ssize_t n;
		int err;

		if (strncmp(p, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN))
			continue;
		nlen = strlen(name);
		if (moob_attr_name_reserved(name, nlen)) {
			if (level != 0)
				report(path, name, "format marker outside the root");
			continue;
		}
		attrs++;
		per_file++;
		if (!moob_attr_name_ok(name, nlen)) {
			report(path, name, moob_attr_strerror(MOOB_ATTR_EBADNAME));
			continue;
		}
		n = moob_node_getxattr(node, p, value, sizeof value);
		if (n < 0) {
			report(path, name, errno == ERANGE ? "value larger than the maximum" : "cannot read value");
			continue;
		}
		err = moob_attr_validate(value, (size_t)n);
		if (err)
			report(path, name, moob_attr_strerror(err));
	}
	if (per_file > MOOB_ATTR_MAX_PER_FILE)
		report(path, NULL, "more typed attributes than the per-file limit");
	free(list);
	if (domain_depth && per_file && (S_ISREG(sb->st_mode) || S_ISDIR(sb->st_mode)) &&
	    check_domain(w, path, node, level))
		return 1;
	{	/* collect (namespace, memory_id) when both are well-formed strings */
		ma_u8 ns[MOOB_ATTR_VALUE_MAX + 1], id[MOOB_ATTR_VALUE_MAX + 1];
		ssize_t a = moob_node_getxattr(node, MOOB_ATTR_PREFIX "namespace", ns, sizeof ns);
		ssize_t b = moob_node_getxattr(node, MOOB_ATTR_PREFIX "memory_id", id, sizeof id);
		if (a > 1 && b > 1 && ns[0] == MOOB_T_STRING && id[0] == MOOB_T_STRING &&
		    !moob_attr_validate(ns, (size_t)a) && !moob_attr_validate(id, (size_t)b)) {
			if (nidents == cidents) {
				cidents = cidents ? cidents * 2 : 256;
				idents = realloc(idents, cidents * sizeof *idents);
				if (!idents) return 1;
			}
			memcpy(idents[nidents].ns, ns + 1, (size_t)a - 1); idents[nidents].ns[a - 1] = 0;
			memcpy(idents[nidents].id, id + 1, (size_t)b - 1); idents[nidents].id[b - 1] = 0;
			idents[nidents].dev = sb->st_dev; idents[nidents].ino = sb->st_ino;
			idents[nidents].path = strdup(path);
			nidents++;
		}
	}
	return 0;
}

static int ident_cmp(const void *x, const void *y)
{
	const struct ident *a = x, *b = y; int c = strcmp(a->id, b->id);
	if (c) return c;
	c = strcmp(a->ns, b->ns);
	if (c) return c;
	return a->ino < b->ino ? -1 : a->ino > b->ino;
}

/* Two distinct inodes with the same (namespace, memory_id) are a conflict; the same inode
 * reached by several paths (hard links) is not; the same id in another namespace is normal. */
static void check_identities(void)
{
	size_t i;

	if (nidents < 2)
		return;		/* nothing to compare; qsort() must not get a null array */
	qsort(idents, nidents, sizeof *idents, ident_cmp);
	for (i = 1; i < nidents; i++) {
		if (strcmp(idents[i].id, idents[i - 1].id)) continue;
		if (strcmp(idents[i].ns, idents[i - 1].ns)) { ident_cross_namespace++; continue; }
		if (idents[i].ino == idents[i - 1].ino && idents[i].dev == idents[i - 1].dev) continue;
		report(idents[i].path, "memory_id", "duplicate (namespace, memory_id) on a different inode");
		ident_conflicts++;
	}
}

int main(int argc, char **argv)
{
	ma_u8 value[16];
	ssize_t n;
	unsigned int version = 0;
	const char *format_state;

	if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
		fputs("usage: moobfs-check [--domain-depth N] <directory>\n"
		      "       moobfs-check --help\n", stdout);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "--domain-depth")) {
		domain_depth = atoi(argv[2]);
		if (domain_depth < 1 || domain_depth > 32)
			argc = 0;
		argv += 2;
		argc -= 2;
	}
	if (argc != 2) {
		fputs("usage: moobfs-check [--domain-depth N] <directory>\n"
		      "       moobfs-check --help\n", stderr);
		return 64;
	}
	n = lgetxattr(argv[1], MOOB_ATTR_PREFIX MOOB_ATTR_FORMAT_NAME, value, sizeof value);
	if (n < 0 && errno == ENODATA) {
		format_state = "missing";
	} else if (n < 0) {
		format_state = "unreadable";
	} else {
		version = moob_attr_parse_format(value, (size_t)n);
		format_state = !version ? "invalid" : version > MOOB_ATTR_FORMAT_VERSION ? "newer" : "ok";
	}
	printf("{\n  \"format\": {\"state\": \"%s\", \"version\": %u, \"supported\": %u},\n  \"problems\": [",
	       format_state, version, MOOB_ATTR_FORMAT_VERSION);
	if (strcmp(format_state, "ok") && strcmp(format_state, "missing"))
		report("", MOOB_ATTR_FORMAT_NAME, format_state);
	if (moob_walk(argv[1], visit)) {
		fprintf(stderr, "moobfs-check: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	check_identities();
	printf("%s],\n", first_problem ? "" : "\n  ");
	first_problem = 1;
	if (domain_depth) {
		size_t k;

		printf("  \"domain_depth\": %d,\n  \"left_out\": [", domain_depth);
		for (k = 0; k < nlefts; k++) {
			printf("%s\n    {\"path\": ", k ? "," : "");
			json_str(lefts[k].path);
			printf(", \"reason\": ");
			json_str(lefts[k].why);
			putchar('}');
		}
		printf("%s],\n  \"left_out_count\": %zu,\n", nlefts ? "\n  " : "", nlefts);
	}
	printf("%s  \"files\": %lu,\n  \"typed_attributes\": %lu,\n  \"identities\": %zu,\n"
	       "  \"identity_conflicts\": %lu,\n  \"same_id_other_namespace\": %lu,\n  \"problem_count\": %lu\n}\n",
	       "", files, attrs, nidents, ident_conflicts, ident_cross_namespace, problems);
	return problems ? 2 : 0;
}
