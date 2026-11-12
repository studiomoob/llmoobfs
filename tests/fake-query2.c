/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fake-query2.so: an LD_PRELOAD stand-in for the layer, for finding 0213-3 (journal 0213; test
 * added in journal 0283). The module never answers QUERY2 with more results than the request's
 * limit, nor with a path_len that does not fit path or lacks its NUL, but the UAPI header
 * promises only the length; the moobfs CLI must not trust more than that. This library makes
 * any directory look like a layer (statfs f_type, /proc/self/mountinfo unreadable, as in a
 * sandbox, issue #167) and answers MOOB_IOC_QUERY2 as MOOB_FAKE says:
 *   count   count = limit + 1            (the CLI must fail with EPROTO, print nothing)
 *   pathlen one result, path_len = MOOB_QUERY_PATH_MAX, path without NUL  (unknown path)
 *   nonul   one result, path_len = 3, path "abcabc..." without NUL at 3   (unknown path)
 *   good    one result, path_len = 3, path "abc"                          (the control)
 * Every other call goes to the C library. Linux and glibc only (RTLD_NEXT).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/statfs.h>
#include "moobfs_ioctl.h"

#define LAYER_FS_TYPE 0x4d4f4f42	/* tools/moobfs-cli.h */

static void *next(const char *name)
{
	void *p = dlsym(RTLD_NEXT, name);

	if (!p)
		abort();
	return p;
}

FILE *fopen(const char *path, const char *mode)
{
	FILE *(*real)(const char *, const char *) = next("fopen");

	if (!strcmp(path, "/proc/self/mountinfo")) {
		errno = EACCES;
		return NULL;
	}
	return real(path, mode);
}

FILE *fopen64(const char *path, const char *mode)
{
	FILE *(*real)(const char *, const char *) = next("fopen64");

	if (!strcmp(path, "/proc/self/mountinfo")) {
		errno = EACCES;
		return NULL;
	}
	return real(path, mode);
}

int statfs(const char *path, struct statfs *buf)
{
	int (*real)(const char *, struct statfs *) = next("statfs");
	int r = real(path, buf);

	if (!r)
		buf->f_type = LAYER_FS_TYPE;
	return r;
}

int fstatfs(int fd, struct statfs *buf)
{
	int (*real)(int, struct statfs *) = next("fstatfs");
	int r = real(fd, buf);

	if (!r)
		buf->f_type = LAYER_FS_TYPE;
	return r;
}

int statfs64(const char *path, struct statfs64 *buf)
{
	int (*real)(const char *, struct statfs64 *) = next("statfs64");
	int r = real(path, buf);

	if (!r)
		buf->f_type = LAYER_FS_TYPE;
	return r;
}

int fstatfs64(int fd, struct statfs64 *buf)
{
	int (*real)(int, struct statfs64 *) = next("fstatfs64");
	int r = real(fd, buf);

	if (!r)
		buf->f_type = LAYER_FS_TYPE;
	return r;
}

int ioctl(int fd, unsigned long req, ...)
{
	int (*real)(int, unsigned long, void *) = next("ioctl");
	const char *mode = getenv("MOOB_FAKE");
	struct moob_query2 *q;
	struct moob_query_result *r;
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (req != MOOB_IOC_QUERY2 || !mode)
		return real(fd, req, arg);
	q = arg;
	r = (struct moob_query_result *)(unsigned long)q->results;
	q->truncated = 0;
	if (!strcmp(mode, "count")) {
		q->count = q->limit + 1;	/* the buffer holds limit records: none is written */
		return 0;
	}
	if (!q->limit) {
		errno = EINVAL;
		return -1;
	}
	memset(r, 0, sizeof *r);
	r->ino = 7;
	q->count = 1;
	if (!strcmp(mode, "pathlen")) {
		memset(r->path, 'a', sizeof r->path);
		r->path_len = MOOB_QUERY_PATH_MAX;
	} else if (!strcmp(mode, "nonul")) {
		memset(r->path, 'b', sizeof r->path);
		r->path_len = 3;
	} else {
		memcpy(r->path, "abc", 4);
		r->path_len = 3;
	}
	return 0;
}
