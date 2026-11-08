/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs workspace: the namespaces of ADR-0035 on a layer mounted with domain_depth=2. User
 * space only (ADR-0022); the kernel decides access from the owners, modes and ACLs set here.
 *
 *   moobfs workspace create <mount> <ns> [--agents a,b] [--create-users] [--dry-run] [--json]
 *   moobfs workspace grant|revoke <mount> <ns> <user> rw|ro
 *   moobfs workspace list <mount> [--count NAME] [--json]
 *
 * Layout (ADR-0035 item 1): <ns> root:moob-<ns> 0750; <ns>/shared root:moob-<ns> 2770 with the
 * default ACL u::rwx,g::rwx,o::r-x (base entries only, so new files get the group's write
 * access without an extended ACL: posix_acl_create() of Linux 6.12.116 fs/posix_acl.c stores no
 * access ACL when posix_acl_create_masq() returns 0; and they are 0664, readable by every class,
 * so they stay in the domain's partition); <ns>/<agent> <agent>:<its group> 0700.
 *
 * create checks everything first (names, the mount's domain depth from GET_INFO, that <ns> does
 * not exist, that users and the group exist unless --create-users), then runs its steps in
 * order: groupadd, useradd, gpasswd -a, and the directories, made with mkdirat at mode 0700 and
 * then given their owner, mode and default ACL through a descriptor opened with O_NOFOLLOW. On
 * a failure it stops and says which steps were done; it never deletes anything. The
 * shadow-utils commands run with posix_spawn and an argument vector, never a shell, from
 * /usr/sbin, /sbin, /usr/bin or /bin (not the caller's PATH, since root runs them), with their
 * standard output sent to standard error so that --json stays one object.
 *
 * grant rw and revoke rw add the user to moob-<ns> or remove it (gpasswd); ro adds or removes
 * the named entry u:<user>:r-x in the access ACL of <ns> and of <ns>/shared, read and written as
 * system.posix_acl_access with the mask recomputed; the default ACL of shared is left alone, so
 * files created during a grant are not extended. An access ACL left without named entries is
 * removed and the mode set from its base entries.
 *
 * list prints the namespaces (directories one level below the mount root whose group is
 * moob-<name>), their areas, owners, modes and ACLs, and with --count NAME the files of each
 * area carrying the typed attribute NAME, counted by QUERY2 as the caller.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "moobfs-cli.h"
#include "workspace.h"

#define DIR_FLAGS (O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)

static int need_root(const char *what)
{
	if (!geteuid())
		return 0;
	fprintf(stderr, "moobfs: workspace %s changes owners, groups and ACLs: run it as root\n",
		what);
	return EX_NOPERM;
}

static int bad_name(const char *kind, const char *name)
{
	fprintf(stderr, "moobfs: invalid %s name \"%s\": names are [a-z_][a-z0-9_-]{0,31}%s\n",
		kind, name, !strcmp(kind, "namespace") ? ", at most 27 bytes for a namespace" : "");
	return EX_DATAERR;
}

/* The mount root, opened after moobfs_layer_root() checked it. -1 after printing why. */
static int open_mount(const char *mount)
{
	int fd = open(mount, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (fd < 0)
		fprintf(stderr, "moobfs: %s: %s\n", mount, strerror(errno));
	return fd;
}

/* Is user a member of the group (a supplementary member, or its primary group)? */
static int in_group(const char *user, const char *group)
{
	struct passwd *pw = getpwnam(user);
	gid_t primary = pw ? pw->pw_gid : (gid_t)-1;
	struct group *gr = getgrnam(group);
	char **m;

	if (!pw || !gr)
		return 0;
	if (gr->gr_gid == primary)
		return 1;
	for (m = gr->gr_mem; m && *m; m++)
		if (!strcmp(*m, user))
			return 1;
	return 0;
}

/* Runs a shadow-utils command; 0, or -1 with a reason in err. */
static int run_cmd(char **argv, char *err, size_t errlen)
{
	static const char *const dirs[] = { "/usr/sbin", "/sbin", "/usr/bin", "/bin" };
	static char path_env[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin", lc_env[] = "LC_ALL=C";
	char *const envp[] = { path_env, lc_env, NULL };
	posix_spawn_file_actions_t fa;
	char prog[64];
	size_t i;
	pid_t pid;
	int st, rc;

	for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
		snprintf(prog, sizeof prog, "%s/%s", dirs[i], argv[0]);
		if (!access(prog, X_OK))
			break;
	}
	if (i == sizeof dirs / sizeof dirs[0]) {
		snprintf(err, errlen, "%s not found in /usr/sbin, /sbin, /usr/bin or /bin (Debian: "
			 "apt install passwd)", argv[0]);
		return -1;
	}
	if ((rc = posix_spawn_file_actions_init(&fa)) ||
	    (rc = posix_spawn_file_actions_adddup2(&fa, STDERR_FILENO, STDOUT_FILENO))) {
		snprintf(err, errlen, "posix_spawn: %s", strerror(rc));
		return -1;
	}
	rc = posix_spawn(&pid, prog, &fa, NULL, argv, envp);
	posix_spawn_file_actions_destroy(&fa);
	if (rc) {
		snprintf(err, errlen, "cannot run %s: %s", prog, strerror(rc));
		return -1;
	}
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) {
			snprintf(err, errlen, "waitpid: %s", strerror(errno));
			return -1;
		}
	}
	if (WIFEXITED(st) && !WEXITSTATUS(st))
		return 0;
	if (WIFEXITED(st))
		snprintf(err, errlen, "%s exited %d", argv[0], WEXITSTATUS(st));
	else
		snprintf(err, errlen, "%s was killed by signal %d", argv[0],
			 WIFSIGNALED(st) ? WTERMSIG(st) : 0);
	return -1;
}

/* ---- create ------------------------------------------------------------------------- */

/* mkdirat at 0700, then owner, mode and the default ACL through an O_NOFOLLOW descriptor. */
static int make_dir(int mfd, const struct ws_step *s, char *err, size_t errlen)
{
	char parent[WS_NS_MAX + 1];
	const char *slash = strchr(s->path, '/'), *name = slash ? slash + 1 : s->path;
	struct passwd *pw = getpwnam(s->user);
	uid_t uid = pw ? pw->pw_uid : 0;
	struct group *gr = getgrnam(s->group);
	gid_t gid = gr ? gr->gr_gid : 0;
	int pfd = mfd, fd, rc = -1;

	if (!pw || !gr) {
		snprintf(err, errlen, "no %s %s", pw ? "group" : "user", pw ? s->group : s->user);
		return -1;
	}
	if (slash) {
		snprintf(parent, sizeof parent, "%.*s", (int)(slash - s->path), s->path);
		pfd = openat(mfd, parent, DIR_FLAGS);
		if (pfd < 0) {
			snprintf(err, errlen, "open %s: %s", parent, strerror(errno));
			return -1;
		}
	}
	if (mkdirat(pfd, name, 0700)) {
		snprintf(err, errlen, "mkdir: %s", strerror(errno));
		goto out;
	}
	fd = openat(pfd, name, DIR_FLAGS);
	if (fd < 0) {
		snprintf(err, errlen, "open: %s", strerror(errno));
		goto out;
	}
	if (fchown(fd, uid, gid))
		snprintf(err, errlen, "chown: %s", strerror(errno));
	else if (fchmod(fd, (mode_t)s->mode))
		snprintf(err, errlen, "chmod: %s", strerror(errno));
	else
		rc = 0;
	if (!rc && s->default_acl) {
		struct ws_acl acl;
		unsigned char buf[WS_ACL_BYTES];
		long n;

		ws_acl_from_mode(&acl, WS_SHARED_DEFAULT);
		n = ws_acl_encode(&acl, buf, sizeof buf);
		if (n < 0 || fsetxattr(fd, WS_XATTR_DEFAULT, buf, (size_t)n, 0)) {
			snprintf(err, errlen, "default ACL: %s", n < 0 ? "encoding" : strerror(errno));
			rc = -1;
		}
	}
	close(fd);
out:
	if (pfd != mfd)
		close(pfd);
	return rc;
}

static int run_step(int mfd, struct ws_step *s, char *err, size_t errlen)
{
	char *argv[9];

	if (s->kind == WS_MKDIR)
		return make_dir(mfd, s, err, errlen);
	ws_step_argv(s, argv);
	return run_cmd(argv, err, errlen);
}

/* What create found: the group, each agent, and whether --create-users may make them. */
static int lookup_create(const char *ns, const struct ws_agents *ag, int create_users,
			 struct ws_create_in *in)
{
	char group[WS_GROUP_MAX + 1];
	int i;

	ws_group_of(ns, group, sizeof group);
	in->make_group = !getgrnam(group);
	if (in->make_group && !create_users) {
		fprintf(stderr, "moobfs: the group %s does not exist; create it (groupadd --system %s) "
			"or pass --create-users\n", group, group);
		return EX_DATAERR;
	}
	for (i = 0; i < ag->n; i++) {
		const char *a = ag->name[i];
		struct passwd *pw = getpwnam(a);
		struct group *gr;

		if (!pw) {
			if (!create_users) {
				fprintf(stderr, "moobfs: no user %s; create it or pass --create-users\n", a);
				return EX_DATAERR;
			}
			if (getgrnam(a)) {
				fprintf(stderr, "moobfs: a group %s exists without a user %s; useradd "
					"--user-group would fail: create the user by hand\n", a, a);
				return EX_DATAERR;
			}
			in->make_user[i] = 1;
			snprintf(in->agent_group[i], sizeof in->agent_group[i], "%s", a);
			continue;
		}
		gr = getgrgid(pw->pw_gid);
		if (!gr || strlen(gr->gr_name) > WS_NAME_MAX) {
			fprintf(stderr, "moobfs: the primary group of %s has no name of at most 32 "
				"bytes\n", a);
			return EX_DATAERR;
		}
		snprintf(in->agent_group[i], sizeof in->agent_group[i], "%s", gr->gr_name);
		in->is_member[i] = !in->make_group && in_group(a, group);
	}
	return 0;
}

/* create's checks of the mount and of the namespace; 0 or an exit status. */
static int check_create_target(const char *mount, const char *ns, int *mfd)
{
	struct moob_info info;
	struct stat st;
	int rc = moobfs_layer_root(mount, &info);

	if (rc)
		return rc;
	if (info.domain_depth != 2) {
		fprintf(stderr, "moobfs: %s is mounted with domain_depth=%u; a workspace needs "
			"domain_depth=2, so that each area is a domain (ADR-0035; moobfs mount "
			"--domain-depth 2)\n", mount, info.domain_depth);
		return EX_UNAVAILABLE;
	}
	*mfd = open_mount(mount);
	if (*mfd < 0)
		return 1;
	if (!fstatat(*mfd, ns, &st, AT_SYMLINK_NOFOLLOW)) {
		fprintf(stderr, "moobfs: %s/%s exists; nothing was changed\n", mount, ns);
	} else if (errno != ENOENT) {
		fprintf(stderr, "moobfs: %s/%s: %s; nothing was changed\n", mount, ns,
			strerror(errno));
	} else {
		return 0;
	}
	close(*mfd);
	return 1;
}

static void print_plan_json(const char *mount, const char *ns, struct ws_plan *p, int dry,
			    int done, const char *err)
{
	char text[256];
	int i;

	printf("{\"mount\": ");
	ws_json_str(stdout, mount);
	printf(", \"namespace\": ");
	ws_json_str(stdout, ns);
	printf(", \"dry_run\": %s, \"complete\": %s, \"steps\": [", dry ? "true" : "false",
	       !dry && done == p->n ? "true" : "false");
	for (i = 0; i < p->n; i++) {
		ws_step_text(&p->s[i], text, sizeof text);
		printf("%s\n  {\"step\": ", i ? "," : "");
		ws_json_str(stdout, text);
		printf(", \"done\": %s}", !dry && i < done ? "true" : "false");
	}
	printf("%s], \"error\": ", p->n ? "\n" : "");
	if (err)
		ws_json_str(stdout, err);
	else
		printf("null");
	printf("}\n");
}

/* Runs the plan; the number of steps done (p->n when all were). */
static int run_plan(int mfd, struct ws_plan *p, int json, char *err, size_t errlen)
{
	char text[256];
	int i;

	for (i = 0; i < p->n; i++) {
		ws_step_text(&p->s[i], text, sizeof text);
		if (run_step(mfd, &p->s[i], err, errlen)) {
			fprintf(stderr, "moobfs: failed: %s: %s\n", text, err);
			fprintf(stderr, "moobfs: %d of %d steps were done%s; nothing was undone\n", i,
				p->n, i ? " (printed above)" : "");
			return i;
		}
		if (!json)
			printf("done: %s\n", text);
	}
	return i;
}

static int ws_create(int argc, char **argv)
{
	static struct ws_agents ag;
	static struct ws_create_in in;
	static struct ws_plan plan;
	struct ws_args a;
	const char *why = NULL, *ns;
	char err[512] = "";
	int mfd = -1, rc, dry, json, done, i;

	if (ws_parse_args(argc - 1, argv + 1, WS_OPT_AGENTS | WS_OPT_CREATE_USERS | WS_OPT_DRY_RUN |
			  WS_OPT_JSON, &a) || a.npos != 2)
		return moobfs_usage();
	ns = a.pos[1];
	dry = !!(a.seen & WS_OPT_DRY_RUN);
	json = !!(a.seen & WS_OPT_JSON);
	if (!ws_ns_ok(ns))
		return bad_name("namespace", ns);
	if (a.agents && ws_parse_agents(a.agents, &ag, &why)) {
		fprintf(stderr, "moobfs: --agents %s: %s\n", a.agents, why);
		return EX_DATAERR;
	}
	if (!dry && (rc = need_root("create")))
		return rc;
	if ((rc = check_create_target(a.pos[0], ns, &mfd)))
		return rc;
	memset(&in, 0, sizeof in);
	in.ns = ns;
	in.agents = &ag;
	if ((rc = lookup_create(ns, &ag, !!(a.seen & WS_OPT_CREATE_USERS), &in))) {
		close(mfd);
		return rc;
	}
	ws_plan_create(&in, &plan);
	if (dry) {
		char text[256];

		for (i = 0; !json && i < plan.n; i++) {
			ws_step_text(&plan.s[i], text, sizeof text);
			printf("would: %s\n", text);
		}
		if (json)
			print_plan_json(a.pos[0], ns, &plan, 1, 0, NULL);
		close(mfd);
		return 0;
	}
	done = run_plan(mfd, &plan, json, err, sizeof err);
	close(mfd);
	if (json)
		print_plan_json(a.pos[0], ns, &plan, 0, done, done == plan.n ? NULL : err);
	if (done < plan.n)
		return 1;
	if (!json && ag.n)
		printf("note: group membership takes effect at each agent's next login\n");
	return 0;
}

/* ---- grant and revoke ------------------------------------------------------------- */

/* The ACL name of fd, or the base entries of its mode when it has none. */
static int acl_read(int fd, const char *name, struct ws_acl *a, mode_t *mode)
{
	unsigned char buf[WS_ACL_BYTES];
	struct stat st;
	ssize_t n;

	if (fstat(fd, &st))
		return -1;
	*mode = st.st_mode;
	n = fgetxattr(fd, name, buf, sizeof buf);
	if (n < 0 && errno == ENODATA) {
		ws_acl_from_mode(a, (unsigned int)st.st_mode & 0777U);
		return 0;
	}
	if (n < 0)
		return -1;
	if (ws_acl_decode(buf, (size_t)n, a)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/*
 * Writes a back. An access ACL without named entries is removed and the mode set from its
 * base entries (the special bits kept); a default ACL is always written.
 */
static int acl_write(int fd, const char *name, const struct ws_acl *a, mode_t mode)
{
	unsigned char buf[WS_ACL_BYTES];
	long n;

	if (!strcmp(name, WS_XATTR_ACCESS) && a->n == 3) {
		if (fremovexattr(fd, name) && errno != ENODATA)
			return -1;
		return fchmod(fd, (mode & 07000) | (mode_t)ws_acl_mode(a));
	}
	n = ws_acl_encode(a, buf, sizeof buf);
	if (n < 0) {
		errno = EINVAL;
		return -1;
	}
	return fsetxattr(fd, name, buf, (size_t)n, 0);
}

/* Adds or removes u:<uid>:r-x in one ACL; prints what changed. 0 or -1. */
static int acl_edit(int fd, const char *path, const char *name, uid_t uid, const char *user,
		    int grant)
{
	struct ws_acl a;
	mode_t mode;
	char text[WS_ACL_MAX * 24];
	int access_acl = !strcmp(name, WS_XATTR_ACCESS);
	const char *kind = access_acl ? "ACL" : "default ACL";

	if (acl_read(fd, name, &a, &mode)) {
		fprintf(stderr, "moobfs: %s: reading the %s: %s\n", path, kind, strerror(errno));
		return -1;
	}
	if (grant ? ws_acl_set_user(&a, (unsigned int)uid, 5) : !ws_acl_del_user(&a, (unsigned int)uid)) {
		if (!grant) {
			printf("%s: %s has no entry for %s; unchanged\n", path, kind, user);
			return 0;
		}
		fprintf(stderr, "moobfs: %s: the %s is full\n", path, kind);
		return -1;
	}
	if (acl_write(fd, name, &a, mode)) {
		fprintf(stderr, "moobfs: %s: writing the %s: %s\n", path, kind, strerror(errno));
		return -1;
	}
	ws_acl_text(&a, text, sizeof text);
	printf("%s: %s %s\n", path, kind, access_acl && a.n == 3 ? "removed (no named entry "
	       "left)" : text);
	return 0;
}

static int access_ro(int nsfd, int sfd, const char *ns, const char *user, uid_t uid, int grant)
{
	char shared[WS_NS_MAX + 8];

	snprintf(shared, sizeof shared, "%s/" WS_SHARED, ns);
	if (acl_edit(nsfd, ns, WS_XATTR_ACCESS, uid, user, grant) ||
	    acl_edit(sfd, shared, WS_XATTR_ACCESS, uid, user, grant))
		return 1;
	if (grant)
		printf("note: %s reads the files of %s that others may read (0664, the default); a "
		       "file made narrower stays closed to it\n", user, shared);
	else
		printf("note: %s can no longer enter %s\n", user, ns);
	return 0;
}

static int access_rw(const char *user, const char *group, int grant)
{
	struct ws_step s;
	char *argv[9], err[256];

	if (in_group(user, group) == grant) {
		printf("%s is %s a member of %s; unchanged\n", user, grant ? "already" : "not", group);
		return 0;
	}
	memset(&s, 0, sizeof s);
	s.kind = grant ? WS_GPASSWD_ADD : WS_GPASSWD_DEL;
	snprintf(s.user, sizeof s.user, "%s", user);
	snprintf(s.group, sizeof s.group, "%s", group);
	ws_step_argv(&s, argv);
	if (run_cmd(argv, err, sizeof err)) {
		fprintf(stderr, "moobfs: gpasswd %s %s %s: %s\n", argv[1], user, group, err);
		return 1;
	}
	printf("%s %s %s %s\nnote: a group change takes effect at the user's next login\n",
	       grant ? "added" : "removed", user, grant ? "to" : "from", group);
	return 0;
}

/* Opens <ns> and <ns>/shared and checks that <ns> belongs to moob-<ns>. */
static int open_workspace(const char *mount, const char *ns, int *nsfd, int *sfd)
{
	char group[WS_GROUP_MAX + 1];
	struct moob_info info;
	struct group *gr;
	struct stat st;
	int mfd, rc = moobfs_layer_root(mount, &info);

	if (rc)
		return rc;
	ws_group_of(ns, group, sizeof group);
	gr = getgrnam(group);
	if ((mfd = open_mount(mount)) < 0)
		return 1;
	*nsfd = openat(mfd, ns, DIR_FLAGS);
	close(mfd);
	if (*nsfd < 0 || fstat(*nsfd, &st) || !gr || st.st_gid != gr->gr_gid) {
		fprintf(stderr, "moobfs: %s/%s is not a workspace (a directory of the group %s)%s%s\n",
			mount, ns, group, *nsfd < 0 ? ": " : "", *nsfd < 0 ? strerror(errno) : "");
		if (*nsfd >= 0)
			close(*nsfd);
		return EX_DATAERR;
	}
	*sfd = openat(*nsfd, WS_SHARED, DIR_FLAGS);
	if (*sfd < 0) {
		fprintf(stderr, "moobfs: %s/%s/" WS_SHARED ": %s\n", mount, ns, strerror(errno));
		close(*nsfd);
		return 1;
	}
	return 0;
}

static int ws_access(int argc, char **argv, int grant)
{
	struct ws_args a;
	struct passwd *pw;
	char group[WS_GROUP_MAX + 1];
	const char *ns, *user, *how;
	int rc, nsfd, sfd;
	uid_t uid;

	if (ws_parse_args(argc - 1, argv + 1, 0, &a) || a.npos != 4)
		return moobfs_usage();
	ns = a.pos[1];
	user = a.pos[2];
	how = a.pos[3];
	if (strcmp(how, "rw") && strcmp(how, "ro"))
		return moobfs_usage();
	if (!ws_ns_ok(ns))
		return bad_name("namespace", ns);
	if (!ws_name_ok(user))
		return bad_name("user", user);
	if ((rc = need_root(grant ? "grant" : "revoke")))
		return rc;
	pw = getpwnam(user);
	if (!pw) {
		fprintf(stderr, "moobfs: no user %s\n", user);
		return EX_DATAERR;
	}
	uid = pw->pw_uid;
	if ((rc = open_workspace(a.pos[0], ns, &nsfd, &sfd)))
		return rc;
	ws_group_of(ns, group, sizeof group);
	rc = !strcmp(how, "rw") ? access_rw(user, group, grant) :
	     access_ro(nsfd, sfd, ns, user, uid, grant);
	close(sfd);
	close(nsfd);
	return rc;
}

/* ---- list ------------------------------------------------------------------------------ */

static int by_name(const void *x, const void *y)
{
	return strcmp(*(char *const *)x, *(char *const *)y);
}

/* The subdirectory names of fd, sorted; NULL after an error (errno set). */
static char **subdirs(int fd, size_t *n)
{
	int dfd = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	DIR *d = dfd < 0 ? NULL : fdopendir(dfd);
	char **v = NULL;
	size_t cap = 0;
	struct dirent *e;
	int nomem = 0;

	*n = 0;
	if (!d) {
		if (dfd >= 0)
			close(dfd);
		return NULL;
	}
	while ((e = readdir(d))) {
		struct stat st;
		char **nv;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") ||
		    fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) || !S_ISDIR(st.st_mode))
			continue;
		if (*n == cap) {
			cap = cap ? cap * 2 : 16;
			nv = realloc(v, cap * sizeof *v);
			if (!nv) {
				nomem = 1;
				break;
			}
			v = nv;
		}
		if (!(v[*n] = strdup(e->d_name))) {
			nomem = 1;
			break;
		}
		(*n)++;
	}
	closedir(d);
	if (nomem) {
		while (*n)
			free(v[--*n]);
		free(v);
		errno = ENOMEM;
		return NULL;
	}
	if (!v)
		v = calloc(1, sizeof *v);
	if (v && *n)
		qsort(v, *n, sizeof *v, by_name);
	return v;
}

static void free_names(char **v, size_t n)
{
	while (n)
		free(v[--n]);
	free(v);
}

static void id_names(const struct stat *st, char *owner, char *group, size_t len)
{
	struct passwd *pw = getpwuid(st->st_uid);
	struct group *gr;

	if (pw)
		snprintf(owner, len, "%s", pw->pw_name);
	else
		snprintf(owner, len, "%u", (unsigned int)st->st_uid);
	gr = getgrgid(st->st_gid);
	if (gr)
		snprintf(group, len, "%s", gr->gr_name);
	else
		snprintf(group, len, "%u", (unsigned int)st->st_gid);
}

static int has_xattr(int fd, const char *name)
{
	return fgetxattr(fd, name, NULL, 0) > 0;
}

/* One area: owner, mode, ACLs and, with count set, its files carrying that attribute. */
static void list_area(int nsfd, const char *name, const char *count, int json, int first)
{
	char owner[64], group[64], cerr[128] = "";
	int fd = openat(nsfd, name, DIR_FLAGS);
	struct stat st;
	size_t n = 0;
	int acl = 0, dacl = 0, have = fd >= 0 && !fstat(fd, &st);

	if (!have) {
		snprintf(cerr, sizeof cerr, "%s", strerror(errno));
		memset(&st, 0, sizeof st);
	} else {
		acl = has_xattr(fd, WS_XATTR_ACCESS);
		dacl = has_xattr(fd, WS_XATTR_DEFAULT);
		if (count) {
			int e = moobfs_count_exists(fd, count, &n);

			if (e)
				snprintf(cerr, sizeof cerr, "%s", e == ENOTTY ? "not a domain directory" :
					 strerror(e));
		}
	}
	if (fd >= 0)
		close(fd);
	id_names(&st, owner, group, sizeof owner);
	if (json) {
		printf("%s{\"name\": ", first ? "" : ", ");
		ws_json_str(stdout, name);
		printf(", \"owner\": ");
		ws_json_str(stdout, owner);
		printf(", \"group\": ");
		ws_json_str(stdout, group);
		printf(", \"mode\": \"%04o\", \"acl\": %s, \"default_acl\": %s, \"count\": ",
		       (unsigned int)st.st_mode & 07777U, acl ? "true" : "false", dacl ? "true" : "false");
		if (count && !cerr[0])
			printf("%zu", n);
		else
			printf("null");
		printf(", \"error\": ");
		if (cerr[0])
			ws_json_str(stdout, cerr);
		else
			printf("null");
		printf("}");
		return;
	}
	printf("  %-24s %s:%s %04o%s%s", name, owner, group, (unsigned int)st.st_mode & 07777U,
	       acl ? " acl" : "", dacl ? " default-acl" : "");
	if (cerr[0])
		printf("  (%s)", cerr);
	else if (count)
		printf("  %zu file%s with %s", n, n == 1 ? "" : "s", count);
	printf("\n");
}

/* One namespace and its areas. */
static void list_ns(int mfd, const char *name, const struct stat *st, const char *count, int json,
		    int first)
{
	char owner[64], group[64];
	int nsfd = openat(mfd, name, DIR_FLAGS), err = 0;
	size_t n = 0, i;
	char **areas = nsfd < 0 ? NULL : subdirs(nsfd, &n);

	if (!areas)
		err = errno;
	id_names(st, owner, group, sizeof owner);
	if (json) {
		printf("%s\n  {\"name\": ", first ? "" : ",");
		ws_json_str(stdout, name);
		printf(", \"owner\": ");
		ws_json_str(stdout, owner);
		printf(", \"group\": ");
		ws_json_str(stdout, group);
		printf(", \"mode\": \"%04o\", \"acl\": %s, \"areas\": ", (unsigned int)st->st_mode & 07777U,
		       nsfd >= 0 && has_xattr(nsfd, WS_XATTR_ACCESS) ? "true" : "false");
	} else {
		printf("%s/ %s:%s %04o%s\n", name, owner, group, (unsigned int)st->st_mode & 07777U,
		       nsfd >= 0 && has_xattr(nsfd, WS_XATTR_ACCESS) ? " acl" : "");
	}
	if (!areas) {
		if (json) {
			printf("null, \"error\": ");
			ws_json_str(stdout, strerror(err));
			printf("}");
		} else {
			printf("  (areas not listed: %s)\n", strerror(err));
		}
		if (nsfd >= 0)
			close(nsfd);
		return;
	}
	if (json)
		printf("[");
	for (i = 0; i < n; i++)
		list_area(nsfd, areas[i], count, json, !i);
	if (json)
		printf("], \"error\": null}");
	free_names(areas, n);
	close(nsfd);
}

static int ws_list(int argc, char **argv)
{
	struct moob_info info;
	struct ws_args a;
	char **names;
	size_t n = 0, i;
	int mfd, rc, json, shown = 0;

	if (ws_parse_args(argc - 1, argv + 1, WS_OPT_COUNT | WS_OPT_JSON, &a) || a.npos != 1)
		return moobfs_usage();
	json = !!(a.seen & WS_OPT_JSON);
	if ((rc = moobfs_layer_root(a.pos[0], &info)))
		return rc;
	if ((mfd = open_mount(a.pos[0])) < 0)
		return 1;
	names = subdirs(mfd, &n);
	if (!names) {
		fprintf(stderr, "moobfs: %s: %s\n", a.pos[0], strerror(errno));
		close(mfd);
		return errno == EACCES ? EX_NOPERM : 1;
	}
	if (json) {
		printf("{\"mount\": ");
		ws_json_str(stdout, a.pos[0]);
		printf(", \"domain_depth\": %u, \"count_attribute\": ", info.domain_depth);
		if (a.count)
			ws_json_str(stdout, a.count);
		else
			printf("null");
		printf(", \"namespaces\": [");
	}
	for (i = 0; i < n; i++) {
		struct stat st;
		struct group *gr;
		char want[WS_GROUP_MAX + 1];

		if (!ws_ns_ok(names[i]) || fstatat(mfd, names[i], &st, AT_SYMLINK_NOFOLLOW))
			continue;
		ws_group_of(names[i], want, sizeof want);
		gr = getgrgid(st.st_gid);
		if (!gr || strcmp(gr->gr_name, want))
			continue;
		list_ns(mfd, names[i], &st, a.count, json, !shown);
		shown++;
	}
	if (json)
		printf("%s]}\n", shown ? "\n" : "");
	else if (!shown)
		printf("no workspace on %s\n", a.pos[0]);
	free_names(names, n);
	close(mfd);
	return 0;
}

int moobfs_workspace(int argc, char **argv)
{
	if (argc < 2)
		return moobfs_usage();
	if (!strcmp(argv[1], "create"))
		return ws_create(argc - 1, argv + 1);
	if (!strcmp(argv[1], "grant"))
		return ws_access(argc - 1, argv + 1, 1);
	if (!strcmp(argv[1], "revoke"))
		return ws_access(argc - 1, argv + 1, 0);
	if (!strcmp(argv[1], "list"))
		return ws_list(argc - 1, argv + 1);
	return moobfs_usage();
}
