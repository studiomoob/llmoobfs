/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The parts of `moobfs workspace`, `moobfs mount` and `moobfs status` (ADR-0035) that make no
 * system call, so that tests/workspace_test.c checks them on macOS and Linux (make test):
 *
 *   - names: users, agents and namespaces are [a-z_][a-z0-9_-]{0,31}; a namespace is at most
 *     27 bytes, so that its group moob-<ns> fits the 32 bytes shadow-utils accepts by default;
 *   - POSIX ACLs in the VFS's xattr form (include/uapi/linux/posix_acl_xattr.h: a little-endian
 *     version 2 header, then 8-byte entries {tag, perm, id} in the order posix_acl_valid() of
 *     Linux 6.12.116 fs/posix_acl.c requires), with the mask recomputed as setfacl(1) does: the
 *     union of the owning group's and every named entry's permissions, and no mask when no named
 *     entry is left;
 *   - the plan of `workspace create` (the steps it prints with --dry-run and runs otherwise);
 *   - the option parser of the commands, the mount data string, the supported-kernels list
 *     and the test of a lower's ancestors.
 *
 * Every function is static inline, so that a file that includes this header and uses only part
 * of it compiles under -Werror.
 */
#ifndef MOOBFS_WORKSPACE_H
#define MOOBFS_WORKSPACE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WS_NAME_MAX 32U			/* bytes of a user, agent or namespace name */
#define WS_NS_MAX 27U			/* "moob-" + 27 bytes = 32 */
#define WS_GROUP_PREFIX "moob-"
#define WS_GROUP_MAX (WS_NS_MAX + 5U)
#define WS_MAX_AGENTS 64
#define WS_SHARED "shared"

/* ---- names ---------------------------------------------------------------------------- */

static inline int ws_name_ok(const char *s)
{
	size_t i, n = strlen(s);

	if (!n || n > WS_NAME_MAX || !(s[0] == '_' || (s[0] >= 'a' && s[0] <= 'z')))
		return 0;
	for (i = 1; i < n; i++)
		if (!(s[i] == '_' || s[i] == '-' || (s[i] >= 'a' && s[i] <= 'z') ||
		      (s[i] >= '0' && s[i] <= '9')))
			return 0;
	return 1;
}

static inline int ws_ns_ok(const char *s)
{
	return ws_name_ok(s) && strlen(s) <= WS_NS_MAX;
}

/* The group of a namespace; ns must be ws_ns_ok(). */
static inline void ws_group_of(const char *ns, char *buf, size_t len)
{
	snprintf(buf, len, WS_GROUP_PREFIX "%s", ns);
}

struct ws_agents {
	int n;
	char name[WS_MAX_AGENTS][WS_NAME_MAX + 1];
};

/*
 * Splits "a1,a2" into a. Returns 0, or -1 with *why set: an empty element, an invalid name,
 * the reserved name "shared", a name given twice, or more than WS_MAX_AGENTS names.
 */
static inline int ws_parse_agents(const char *list, struct ws_agents *a, const char **why)
{
	const char *p = list;

	a->n = 0;
	for (;;) {
		const char *comma = strchr(p, ',');
		size_t len = comma ? (size_t)(comma - p) : strlen(p);
		char name[WS_NAME_MAX + 2];
		int i;

		if (!len) {
			*why = "an empty agent name";
			return -1;
		}
		if (len > WS_NAME_MAX) {
			*why = "an agent name longer than 32 bytes";
			return -1;
		}
		memcpy(name, p, len);
		name[len] = 0;
		if (!ws_name_ok(name)) {
			*why = "an agent name that is not [a-z_][a-z0-9_-]{0,31}";
			return -1;
		}
		if (!strcmp(name, WS_SHARED)) {
			*why = "the agent name \"shared\", which is the shared area";
			return -1;
		}
		for (i = 0; i < a->n; i++)
			if (!strcmp(a->name[i], name)) {
				*why = "an agent name given twice";
				return -1;
			}
		if (a->n == WS_MAX_AGENTS) {
			*why = "more than 64 agents";
			return -1;
		}
		memcpy(a->name[a->n++], name, len + 1);
		if (!comma)
			return 0;
		p = comma + 1;
	}
}

/* ---- POSIX ACLs ----------------------------------------------------------------------- */

#define WS_ACL_USER_OBJ 0x01U
#define WS_ACL_USER 0x02U
#define WS_ACL_GROUP_OBJ 0x04U
#define WS_ACL_GROUP 0x08U
#define WS_ACL_MASK 0x10U
#define WS_ACL_OTHER 0x20U
#define WS_ACL_UNDEFINED_ID 0xffffffffU
#define WS_ACL_VERSION 2U
#define WS_ACL_MAX 64
#define WS_ACL_BYTES (4 + 8 * WS_ACL_MAX)
#define WS_XATTR_ACCESS "system.posix_acl_access"
#define WS_XATTR_DEFAULT "system.posix_acl_default"

struct ws_acl_entry {
	unsigned int tag, perm, id;
};

struct ws_acl {
	int n;
	struct ws_acl_entry e[WS_ACL_MAX];
};

/* The three base entries of mode's permission bits. */
static inline void ws_acl_from_mode(struct ws_acl *a, unsigned int mode)
{
	a->n = 3;
	a->e[0] = (struct ws_acl_entry){ WS_ACL_USER_OBJ, (mode >> 6) & 7U, WS_ACL_UNDEFINED_ID };
	a->e[1] = (struct ws_acl_entry){ WS_ACL_GROUP_OBJ, (mode >> 3) & 7U, WS_ACL_UNDEFINED_ID };
	a->e[2] = (struct ws_acl_entry){ WS_ACL_OTHER, mode & 7U, WS_ACL_UNDEFINED_ID };
}

/* The kernel's order: by tag (the tag values ascend in that order), then by id. */
static inline void ws_acl_sort(struct ws_acl *a)
{
	int i, j;

	for (i = 1; i < a->n; i++) {
		struct ws_acl_entry x = a->e[i];

		for (j = i; j > 0 && (a->e[j - 1].tag > x.tag ||
				      (a->e[j - 1].tag == x.tag && a->e[j - 1].id > x.id)); j--)
			a->e[j] = a->e[j - 1];
		a->e[j] = x;
	}
}

/*
 * posix_acl_valid() of Linux 6.12.116: one owner entry, named users, one owning group entry,
 * named groups, a mask (required when there is a named entry), one other entry, in that order;
 * permissions within rwx. Named ids must also differ, which setfacl(1) guarantees.
 */
static inline int ws_acl_valid(const struct ws_acl *a)
{
	static const unsigned int order[] = { WS_ACL_USER_OBJ, WS_ACL_USER, WS_ACL_GROUP_OBJ,
					      WS_ACL_GROUP, WS_ACL_MASK, WS_ACL_OTHER };
	int i, k = 0, named = 0, mask = 0, counts[6] = { 0 };

	for (i = 0; i < a->n; i++) {
		const struct ws_acl_entry *e = &a->e[i];

		if (e->perm & ~7U)
			return 0;
		while (k < 6 && order[k] != e->tag)
			k++;
		if (k == 6)
			return 0;	/* unknown tag, or out of order */
		counts[k]++;
		if (e->tag == WS_ACL_USER || e->tag == WS_ACL_GROUP) {
			named = 1;
			if (i && a->e[i - 1].tag == e->tag && a->e[i - 1].id >= e->id)
				return 0;
		}
		mask += e->tag == WS_ACL_MASK;
	}
	return counts[0] == 1 && counts[2] == 1 && counts[5] == 1 && counts[4] <= 1 &&
	       (!named || mask);
}

static inline unsigned int ws_le16(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8;
}

static inline unsigned int ws_le32(const unsigned char *p)
{
	return (unsigned int)p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 |
	       (unsigned int)p[3] << 24;
}

/* The bytes of a getxattr into a; 0, or -1 when they are not a valid ACL. */
static inline int ws_acl_decode(const unsigned char *b, size_t len, struct ws_acl *a)
{
	size_t i;

	a->n = 0;
	if (len < 4 || (len - 4) % 8 || (len - 4) / 8 > WS_ACL_MAX || ws_le32(b) != WS_ACL_VERSION)
		return -1;
	for (i = 4; i < len; i += 8) {
		struct ws_acl_entry *e = &a->e[a->n++];

		e->tag = ws_le16(b + i);
		e->perm = ws_le16(b + i + 2);
		e->id = ws_le32(b + i + 4);
		if (e->tag != WS_ACL_USER && e->tag != WS_ACL_GROUP)
			e->id = WS_ACL_UNDEFINED_ID;
	}
	return ws_acl_valid(a) ? 0 : -1;
}

/* a into b (len bytes); the number of bytes, or -1 when a is invalid or b too short. */
static inline long ws_acl_encode(const struct ws_acl *a, unsigned char *b, size_t len)
{
	size_t need = 4 + 8 * (size_t)a->n, i;

	if (!ws_acl_valid(a) || len < need)
		return -1;
	b[0] = (unsigned char)WS_ACL_VERSION;
	b[1] = b[2] = b[3] = 0;
	for (i = 0; i < (size_t)a->n; i++) {
		const struct ws_acl_entry *e = &a->e[i];
		unsigned char *p = b + 4 + 8 * i;

		p[0] = (unsigned char)e->tag;
		p[1] = (unsigned char)(e->tag >> 8);
		p[2] = (unsigned char)e->perm;
		p[3] = 0;
		p[4] = (unsigned char)e->id;
		p[5] = (unsigned char)(e->id >> 8);
		p[6] = (unsigned char)(e->id >> 16);
		p[7] = (unsigned char)(e->id >> 24);
	}
	return (long)need;
}

static inline int ws_acl_find(const struct ws_acl *a, unsigned int tag, unsigned int id)
{
	int i;

	for (i = 0; i < a->n; i++)
		if (a->e[i].tag == tag && (tag != WS_ACL_USER && tag != WS_ACL_GROUP ? 1 : a->e[i].id == id))
			return i;
	return -1;
}

static inline void ws_acl_drop(struct ws_acl *a, int i)
{
	memmove(&a->e[i], &a->e[i + 1], (size_t)(a->n - i - 1) * sizeof a->e[0]);
	a->n--;
}

static inline int ws_acl_named(const struct ws_acl *a)
{
	int i, n = 0;

	for (i = 0; i < a->n; i++)
		n += a->e[i].tag == WS_ACL_USER || a->e[i].tag == WS_ACL_GROUP;
	return n;
}

/*
 * setfacl's mask: the union of the owning group entry and every named entry; removed when no
 * named entry is left, so that the ACL is minimal again. 0, or -1 when there is no room.
 */
static inline int ws_acl_fix_mask(struct ws_acl *a)
{
	unsigned int perm = 0;
	int i, m = ws_acl_find(a, WS_ACL_MASK, 0);

	if (!ws_acl_named(a)) {
		if (m >= 0)
			ws_acl_drop(a, m);
		return 0;
	}
	for (i = 0; i < a->n; i++)
		if (a->e[i].tag == WS_ACL_USER || a->e[i].tag == WS_ACL_GROUP ||
		    a->e[i].tag == WS_ACL_GROUP_OBJ)
			perm |= a->e[i].perm;
	if (m >= 0) {
		a->e[m].perm = perm;
		return 0;
	}
	if (a->n == WS_ACL_MAX)
		return -1;
	a->e[a->n++] = (struct ws_acl_entry){ WS_ACL_MASK, perm, WS_ACL_UNDEFINED_ID };
	ws_acl_sort(a);
	return 0;
}

/* Adds or replaces u:<uid>:<perm>, then the mask. 0, or -1 when there is no room. */
static inline int ws_acl_set_user(struct ws_acl *a, unsigned int uid, unsigned int perm)
{
	int i = ws_acl_find(a, WS_ACL_USER, uid);

	if (i >= 0) {
		a->e[i].perm = perm & 7U;
	} else {
		if (a->n >= WS_ACL_MAX - 1)	/* room for the mask as well */
			return -1;
		a->e[a->n++] = (struct ws_acl_entry){ WS_ACL_USER, perm & 7U, uid };
		ws_acl_sort(a);
	}
	return ws_acl_fix_mask(a);
}

/* Removes u:<uid>, then fixes the mask. 1 when an entry was removed, else 0. */
static inline int ws_acl_del_user(struct ws_acl *a, unsigned int uid)
{
	int i = ws_acl_find(a, WS_ACL_USER, uid);

	if (i < 0)
		return 0;
	ws_acl_drop(a, i);
	(void)ws_acl_fix_mask(a);	/* only removes or updates: cannot fail */
	return 1;
}

/* The permission bits the ACL gives the mode: the mask stands for the group class. */
static inline unsigned int ws_acl_mode(const struct ws_acl *a)
{
	int u = ws_acl_find(a, WS_ACL_USER_OBJ, 0), g = ws_acl_find(a, WS_ACL_GROUP_OBJ, 0);
	int o = ws_acl_find(a, WS_ACL_OTHER, 0), m = ws_acl_find(a, WS_ACL_MASK, 0);
	unsigned int mode = 0;

	if (u >= 0)
		mode |= a->e[u].perm << 6;
	if (m >= 0)
		mode |= a->e[m].perm << 3;
	else if (g >= 0)
		mode |= a->e[g].perm << 3;
	if (o >= 0)
		mode |= a->e[o].perm;
	return mode;
}

/* "u::rwx,u:1001:r-x,g::rwx,m::rwx,o::---" into buf, for messages and --dry-run. */
static inline void ws_acl_text(const struct ws_acl *a, char *buf, size_t len)
{
	size_t off = 0;
	int i;

	buf[0] = 0;
	for (i = 0; i < a->n && off < len; i++) {
		const struct ws_acl_entry *e = &a->e[i];
		const char *t = e->tag == WS_ACL_USER_OBJ || e->tag == WS_ACL_USER ? "u" :
				e->tag == WS_ACL_MASK ? "m" : e->tag == WS_ACL_OTHER ? "o" : "g";
		char id[16] = "";
		int w;

		if (e->tag == WS_ACL_USER || e->tag == WS_ACL_GROUP)
			snprintf(id, sizeof id, "%u", e->id);
		w = snprintf(buf + off, len - off, "%s%s:%s:%c%c%c", i ? "," : "", t, id,
			     (e->perm & 4U) ? 'r' : '-', (e->perm & 2U) ? 'w' : '-',
			     (e->perm & 1U) ? 'x' : '-');
		if (w < 0)
			return;
		off += (size_t)w;
	}
}

/* ---- the plan of workspace create ----------------------------------------------------- */

enum ws_step_kind { WS_GROUPADD, WS_USERADD, WS_GPASSWD_ADD, WS_GPASSWD_DEL, WS_MKDIR };

struct ws_step {
	enum ws_step_kind kind;
	char user[WS_NAME_MAX + 1];		/* useradd, gpasswd; the owner for mkdir */
	char group[WS_NAME_MAX + 1];		/* groupadd, gpasswd; the group for mkdir */
	char path[WS_NS_MAX + WS_NAME_MAX + 2];	/* mkdir, relative to the mount root */
	unsigned int mode;			/* mkdir, with the setgid bit */
	int default_acl;			/* mkdir: u::rwx,g::rwx,o::--- as the default ACL */
};

#define WS_PLAN_MAX (3 + 3 * WS_MAX_AGENTS)

struct ws_plan {
	int n;
	struct ws_step s[WS_PLAN_MAX];
};

/* What workspace create found before planning; the caller looks the names up. */
struct ws_create_in {
	const char *ns;
	const struct ws_agents *agents;
	int make_group;				/* moob-<ns> does not exist: groupadd */
	int make_user[WS_MAX_AGENTS];		/* the agent does not exist: useradd */
	int is_member[WS_MAX_AGENTS];		/* already in moob-<ns>: no gpasswd */
	char agent_group[WS_MAX_AGENTS][WS_NAME_MAX + 1];	/* primary group of the agent */
};

static inline struct ws_step *ws_add(struct ws_plan *p, enum ws_step_kind kind)
{
	struct ws_step *s = &p->s[p->n++];

	memset(s, 0, sizeof *s);
	s->kind = kind;
	return s;
}

/* groupadd, useradd, gpasswd -a, then the directories, in the order of ADR-0035 item 1. */
static inline void ws_plan_create(const struct ws_create_in *in, struct ws_plan *p)
{
	char group[WS_GROUP_MAX + 1];
	struct ws_step *s;
	int i;

	ws_group_of(in->ns, group, sizeof group);
	p->n = 0;
	if (in->make_group)
		snprintf(ws_add(p, WS_GROUPADD)->group, sizeof p->s[0].group, "%s", group);
	for (i = 0; i < in->agents->n; i++)
		if (in->make_user[i])
			snprintf(ws_add(p, WS_USERADD)->user, sizeof p->s[0].user, "%s",
				 in->agents->name[i]);
	for (i = 0; i < in->agents->n; i++) {
		if (in->is_member[i])
			continue;
		s = ws_add(p, WS_GPASSWD_ADD);
		snprintf(s->user, sizeof s->user, "%s", in->agents->name[i]);
		snprintf(s->group, sizeof s->group, "%s", group);
	}
	s = ws_add(p, WS_MKDIR);
	snprintf(s->path, sizeof s->path, "%s", in->ns);
	snprintf(s->user, sizeof s->user, "root");
	snprintf(s->group, sizeof s->group, "%s", group);
	s->mode = 0750;
	s = ws_add(p, WS_MKDIR);
	snprintf(s->path, sizeof s->path, "%s/" WS_SHARED, in->ns);
	snprintf(s->user, sizeof s->user, "root");
	snprintf(s->group, sizeof s->group, "%s", group);
	s->mode = 02770;
	s->default_acl = 1;
	for (i = 0; i < in->agents->n; i++) {
		s = ws_add(p, WS_MKDIR);
		snprintf(s->path, sizeof s->path, "%s/%s", in->ns, in->agents->name[i]);
		snprintf(s->user, sizeof s->user, "%s", in->agents->name[i]);
		snprintf(s->group, sizeof s->group, "%s", in->agent_group[i]);
		s->mode = 0700;
	}
}

/*
 * The argument vector of a step that runs a shadow-utils command (argv[0] the command name),
 * into argv (at least 9 slots); the number of arguments, or 0 for mkdir.
 */
static inline int ws_step_argv(struct ws_step *s, char **argv)
{
	static char groupadd[] = "groupadd", useradd[] = "useradd", gpasswd[] = "gpasswd";
	static char sys[] = "--system", ug[] = "--user-group", nohome[] = "--no-create-home";
	static char shell[] = "--shell", nologin[] = "/usr/sbin/nologin", add[] = "-a", del[] = "-d";
	int n = 0;

	switch (s->kind) {
	case WS_GROUPADD:
		argv[n++] = groupadd;
		argv[n++] = sys;
		argv[n++] = s->group;
		break;
	case WS_USERADD:
		argv[n++] = useradd;
		argv[n++] = sys;
		argv[n++] = ug;
		argv[n++] = nohome;
		argv[n++] = shell;
		argv[n++] = nologin;
		argv[n++] = s->user;
		break;
	case WS_GPASSWD_ADD:
	case WS_GPASSWD_DEL:
		argv[n++] = gpasswd;
		argv[n++] = s->kind == WS_GPASSWD_ADD ? add : del;
		argv[n++] = s->user;
		argv[n++] = s->group;
		break;
	case WS_MKDIR:
		break;
	}
	argv[n] = NULL;
	return n;
}

/* One line describing the step: the command, or "mkdir <path> <owner>:<group> <mode> ...". */
static inline void ws_step_text(struct ws_step *s, char *buf, size_t len)
{
	char *argv[9];
	size_t off = 0;
	int i, n = ws_step_argv(s, argv);

	buf[0] = 0;
	if (s->kind == WS_MKDIR) {
		snprintf(buf, len, "mkdir %s, owner %s:%s, mode %04o%s", s->path, s->user, s->group,
			 s->mode, s->default_acl ? ", default ACL u::rwx,g::rwx,o::---" : "");
		return;
	}
	for (i = 0; i < n && off < len; i++) {
		int w = snprintf(buf + off, len - off, "%s%s", i ? " " : "", argv[i]);

		if (w < 0)
			return;
		off += (size_t)w;
	}
}

/* ---- options of the commands ---------------------------------------------------------- */

#define WS_OPT_AGENTS 0x001U
#define WS_OPT_CREATE_USERS 0x002U
#define WS_OPT_DRY_RUN 0x004U
#define WS_OPT_JSON 0x008U
#define WS_OPT_COUNT 0x010U
#define WS_OPT_DEPTH 0x020U
#define WS_OPT_NOLINKS 0x040U
#define WS_OPT_NOINDEX 0x080U
#define WS_OPT_BUDGET 0x100U
#define WS_OPT_UNSUPPORTED 0x200U
#define WS_OPT_OPEN_LOWER 0x400U
#define WS_MAX_POS 4

struct ws_args {
	int npos;
	const char *pos[WS_MAX_POS];
	unsigned int seen;		/* WS_OPT_* given */
	const char *agents, *count, *budget;
	int depth;
};

/* Is s a valid index_budget: digits with an optional K, M or G, from 1M to 4G (memparse). */
static inline int ws_budget_ok(const char *s)
{
	unsigned long long v = 0, shift = 0;
	const char *p = s;

	if (*p < '0' || *p > '9')
		return 0;
	for (; *p >= '0' && *p <= '9'; p++) {
		v = v * 10 + (unsigned long long)(*p - '0');
		if (v > (4ULL << 30))
			return 0;
	}
	if (*p == 'k' || *p == 'K')
		shift = 10;
	else if (*p == 'm' || *p == 'M')
		shift = 20;
	else if (*p == 'g' || *p == 'G')
		shift = 30;
	if (shift)
		p++;
	if (*p || (shift && v > ((4ULL << 30) >> shift)))
		return 0;
	v <<= shift;
	return v >= (1ULL << 20) && v <= (4ULL << 30);
}

/*
 * Options anywhere among the positional arguments; allowed is the set of WS_OPT_* the command
 * takes. 0, or -1 for an unknown, repeated or invalid option, a missing value or too many
 * positional arguments. The caller checks npos.
 */
static inline int ws_parse_args(int argc, char **argv, unsigned int allowed, struct ws_args *a)
{
	static const struct { const char *name; unsigned int bit; int value; } opts[] = {
		{ "--agents", WS_OPT_AGENTS, 1 }, { "--create-users", WS_OPT_CREATE_USERS, 0 },
		{ "--dry-run", WS_OPT_DRY_RUN, 0 }, { "--json", WS_OPT_JSON, 0 },
		{ "--count", WS_OPT_COUNT, 1 }, { "--domain-depth", WS_OPT_DEPTH, 1 },
		{ "--nolinks", WS_OPT_NOLINKS, 0 }, { "--noindex", WS_OPT_NOINDEX, 0 },
		{ "--index-budget", WS_OPT_BUDGET, 1 }, { "--unsupported", WS_OPT_UNSUPPORTED, 0 },
		{ "--allow-open-lower", WS_OPT_OPEN_LOWER, 0 },
	};
	int k;

	memset(a, 0, sizeof *a);
	a->depth = 2;
	for (k = 0; k < argc; k++) {
		size_t i;

		if (strncmp(argv[k], "--", 2)) {
			if (a->npos == WS_MAX_POS)
				return -1;
			a->pos[a->npos++] = argv[k];
			continue;
		}
		for (i = 0; i < sizeof opts / sizeof opts[0]; i++)
			if (!strcmp(argv[k], opts[i].name))
				break;
		if (i == sizeof opts / sizeof opts[0] || !(allowed & opts[i].bit) ||
		    (a->seen & opts[i].bit) || (opts[i].value && k + 1 >= argc))
			return -1;
		a->seen |= opts[i].bit;
		if (!opts[i].value)
			continue;
		k++;
		if (opts[i].bit == WS_OPT_AGENTS) {
			a->agents = argv[k];
		} else if (opts[i].bit == WS_OPT_COUNT) {
			size_t n = strlen(argv[k]);

			if (!n || n > 64)	/* MOOB_QUERY_NAME_MAX */
				return -1;
			a->count = argv[k];
		} else if (opts[i].bit == WS_OPT_BUDGET) {
			if (!ws_budget_ok(argv[k]))
				return -1;
			a->budget = argv[k];
		} else {
			if (argv[k][0] < '0' || argv[k][0] > '8' || argv[k][1])
				return -1;
			a->depth = argv[k][0] - '0';
		}
	}
	return 0;
}

/* ---- mount ---------------------------------------------------------------------------- */

/*
 * The data string of mount(2) for the layer (docs/operations.md, section 3). -1 when lower
 * holds a comma, which the option parser of the old mount API would split, or buf is short.
 */
static inline int ws_mount_data(const char *lower, const struct ws_args *a, char *buf, size_t len)
{
	int w;

	if (strchr(lower, ','))
		return -1;
	w = snprintf(buf, len, "lower=%s,domain_depth=%d%s%s%s%s", lower, a->depth,
		     (a->seen & WS_OPT_NOLINKS) ? ",nolinks" : "",
		     (a->seen & WS_OPT_NOINDEX) ? ",noindex" : "",
		     a->budget ? ",index_budget=" : "", a->budget ? a->budget : "");
	return w < 0 || (size_t)w >= len ? -1 : 0;
}

/* Whether release is a line of the supported-kernels text (comments and blanks ignored). */
static inline int ws_kernel_listed(const char *text, const char *release)
{
	size_t rlen = strlen(release);
	const char *p = text;

	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);

		while (len && (p[len - 1] == ' ' || p[len - 1] == '\t' || p[len - 1] == '\r'))
			len--;
		if (p[0] != '#' && len == rlen && rlen && !memcmp(p, release, rlen))
			return 1;
		if (!nl)
			break;
		p = nl + 1;
	}
	return 0;
}

/*
 * Does a directory on the way to the lower keep everyone but root out (distribution strategy
 * section 2.3, item 1)? Owned by root, no search bit for others, none for its group unless the
 * group is root's, and no access ACL that could grant one.
 */
static inline int ws_dir_blocks_others(unsigned int mode, unsigned int uid, unsigned int gid,
				       int has_acl)
{
	return uid == 0 && !has_acl && !(mode & 01U) && (!(mode & 010U) || gid == 0);
}

/* ---- JSON ----------------------------------------------------------------------------- */

static inline void ws_json_str(FILE *f, const char *s)
{
	fputc('"', f);
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			fputc('\\', f);
		if ((unsigned char)*s < 0x20)
			fprintf(f, "\\u%04x", (unsigned char)*s);
		else
			fputc(*s, f);
	}
	fputc('"', f);
}

#endif
