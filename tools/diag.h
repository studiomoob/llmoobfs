/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Read-only diagnostics of the limits that apply to a caller of the layer (roadmap item G2,
 * journal 0243), for the moobfs CLI. Only files the kernel already exposes are read; nothing is
 * written and no kernel interface is added (ADR-0022):
 *
 *   /proc/self/cgroup                 the caller's cgroup v2 path, the "0::" line
 *   <cgroup root><path>/memory.max    the limit of each cgroup from the caller's up to the root
 *   .../memory.current                its usage
 *   .../memory.events.local           "max" and "oom": times this cgroup itself was at its limit
 *   .../memory.events                 "oom_kill": processes below it killed by an OOM killer
 *   /sys/module/<mod>/parameters/<p>  the layer's module parameters
 *
 * The kernel raises a cgroup's "max" event on the cgroup whose limit a charge would exceed and
 * on its ancestors, and its local count on that cgroup only (Linux 6.12.112
 * include/linux/memcontrol.h, memcg_memory_event(); mm/memcontrol.c try_charge_memcg()). So
 * the local count of the cgroup that holds the limit (for a systemd user, user-UID.slice, not
 * the session scope the process is in) tells that its limit, and not the index budget, refused
 * a charge. Paths are arguments, so that tests/diag_test.c runs it over a fake tree. POSIX only.
 */
#ifndef MOOBFS_DIAG_H
#define MOOBFS_DIAG_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DIAG_LEVELS 32
#define DIAG_PATH 1024
#define DIAG_NOLIMIT (~0ULL)

struct diag_level {
	char path[DIAG_PATH];		/* cgroup path below the root: "/user.slice/user-1001.slice" */
	int has_max;			/* memory.max was read (the memory controller is on here) */
	unsigned long long max;		/* bytes; DIAG_NOLIMIT for "max" */
	int has_current;
	unsigned long long current;	/* bytes */
	unsigned long long ev_max;	/* memory.events.local "max" (memory.events if no .local) */
	unsigned long long ev_oom;	/* memory.events.local "oom" */
	unsigned long long ev_oom_kill;	/* memory.events "oom_kill", hierarchical */
};

struct diag_chain {
	int n;				/* levels read, the caller's cgroup first; 0 in the root */
	struct diag_level lv[DIAG_LEVELS];
};

/* The first line of file, without its newline, into buf. 0 or -1 (errno set). */
static int diag_line(const char *file, char *buf, size_t len)
{
	FILE *f = fopen(file, "r");
	size_t n;

	if (!f)
		return -1;
	if (!fgets(buf, (int)len, f)) {
		fclose(f);
		errno = EIO;
		return -1;
	}
	fclose(f);
	n = strlen(buf);
	if (n && buf[n - 1] == '\n')
		buf[n - 1] = 0;
	return 0;
}

/* A decimal number, or "max" as DIAG_NOLIMIT. 0 or -1. */
static int diag_number(const char *s, unsigned long long *v)
{
	char *end;

	if (!strcmp(s, "max")) {
		*v = DIAG_NOLIMIT;
		return 0;
	}
	if (*s < '0' || *s > '9')
		return -1;
	errno = 0;
	*v = strtoull(s, &end, 10);
	return errno || *end ? -1 : 0;
}

/* The value of key in a flat-keyed file ("key value" lines). 0, or -1 if absent/unreadable. */
static int diag_key(const char *file, const char *key, unsigned long long *v)
{
	FILE *f = fopen(file, "r");
	char line[256];
	size_t klen = strlen(key);
	int rc = -1;

	if (!f)
		return -1;
	while (rc && fgets(line, sizeof line, f)) {
		size_t n = strlen(line);

		if (n && line[n - 1] == '\n')
			line[--n] = 0;
		if (!strncmp(line, key, klen) && line[klen] == ' ')
			rc = diag_number(line + klen + 1, v);
	}
	fclose(f);
	return rc;
}

/*
 * The caller's cgroup v2 path from a /proc/<pid>/cgroup file: the "0::" line. A path the kernel
 * marks " (deleted)" loses the mark. 0, or -1 when the file cannot be read or has no such line
 * (cgroup v1 only).
 */
static int diag_cgroup_path(const char *proc_cgroup, char *out, size_t len)
{
	FILE *f = fopen(proc_cgroup, "r");
	char line[DIAG_PATH + 16];
	int rc = -1;

	if (!f)
		return -1;
	while (rc && fgets(line, sizeof line, f)) {
		size_t n = strlen(line);
		static const char deleted[] = " (deleted)";

		if (strncmp(line, "0::/", 4))
			continue;
		if (n && line[n - 1] == '\n')
			line[--n] = 0;
		if (n > 3 + sizeof deleted - 1 && !strcmp(line + n - (sizeof deleted - 1), deleted))
			line[n - (sizeof deleted - 1)] = 0;
		if (strlen(line + 3) < len) {
			snprintf(out, len, "%s", line + 3);
			rc = 0;
		}
	}
	fclose(f);
	if (rc)
		errno = ENOENT;
	return rc;
}

static void diag_file(char *buf, size_t len, const char *root, const char *path, const char *name)
{
	snprintf(buf, len, "%s%s/%s", root, path, name);
}

/*
 * Reads the memory files of the caller's cgroup and of each ancestor below the root, the
 * caller's first. 0, or -1 when the caller's cgroup is unknown (no cgroup v2 line, its
 * directory, cgroup.procs included, not visible under root, or a sandbox).
 * Levels without the memory controller have has_max 0 and count as no limit.
 */
static int diag_chain_read(const char *proc_cgroup, const char *root, struct diag_chain *c)
{
	char path[DIAG_PATH], file[DIAG_PATH * 2], val[64];

	memset(c, 0, sizeof *c);
	if (diag_cgroup_path(proc_cgroup, path, sizeof path))
		return -1;
	/* a cgroup the root does not show (no cgroup v2 mounted there): unknown, not unlimited */
	diag_file(file, sizeof file, root, strcmp(path, "/") ? path : "", "cgroup.procs");
	if (access(file, F_OK))
		return -1;
	while (strcmp(path, "/") && c->n < DIAG_LEVELS) {
		struct diag_level *l = &c->lv[c->n++];
		char *slash;

		snprintf(l->path, sizeof l->path, "%s", path);
		diag_file(file, sizeof file, root, path, "memory.max");
		l->has_max = !diag_line(file, val, sizeof val) && !diag_number(val, &l->max);
		diag_file(file, sizeof file, root, path, "memory.current");
		l->has_current = !diag_line(file, val, sizeof val) && !diag_number(val, &l->current);
		diag_file(file, sizeof file, root, path, "memory.events.local");
		if (diag_key(file, "max", &l->ev_max)) {
			diag_file(file, sizeof file, root, path, "memory.events");
			(void)diag_key(file, "max", &l->ev_max);
		}
		(void)diag_key(file, "oom", &l->ev_oom);
		diag_file(file, sizeof file, root, path, "memory.events");
		(void)diag_key(file, "oom_kill", &l->ev_oom_kill);
		slash = strrchr(path, '/');
		if (slash == path)
			slash[1] = 0;
		else if (slash)
			*slash = 0;
	}
	return 0;
}

/* The level whose memory.max is the smallest limit, or -1 when no level has one. */
static int diag_effective(const struct diag_chain *c)
{
	int i, best = -1;

	for (i = 0; i < c->n; i++)
		if (c->lv[i].has_max && c->lv[i].max != DIAG_NOLIMIT &&
		    (best < 0 || c->lv[i].max < c->lv[best].max))
			best = i;
	return best;
}

/*
 * The level whose own limit was reached between two readings of the same chain: its local
 * "max" or "oom" count grew. -1 when none did, or the chains are not of the same cgroup.
 */
static int diag_hit(const struct diag_chain *before, const struct diag_chain *after)
{
	int i;

	if (before->n != after->n)
		return -1;
	for (i = 0; i < after->n; i++) {
		if (strcmp(before->lv[i].path, after->lv[i].path))
			return -1;
		if (after->lv[i].has_max && after->lv[i].max != DIAG_NOLIMIT &&
		    (after->lv[i].ev_max > before->lv[i].ev_max ||
		     after->lv[i].ev_oom > before->lv[i].ev_oom))
			return i;
	}
	return -1;
}

/* An integer module parameter, /sys/module/<module>/parameters/<name> under sysmod. 0 or -1. */
static int diag_param(const char *sysmod, const char *name, long long *v)
{
	char file[DIAG_PATH], val[64], *end;

	snprintf(file, sizeof file, "%s/parameters/%s", sysmod, name);
	if (diag_line(file, val, sizeof val))
		return -1;
	errno = 0;
	*v = strtoll(val, &end, 10);
	return errno || end == val || *end ? -1 : 0;
}

#endif
