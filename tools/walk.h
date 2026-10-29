/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A walk by directory descriptor, shared by moobfs-check and moobfs-index (journal 0232).
 *
 * It replaces nftw(FTW_PHYS | FTW_MOUNT) and the reads by path that followed it. Below the root
 * no name is resolved through a path: each entry is looked at with fstatat(AT_SYMLINK_NOFOLLOW)
 * relative to its directory's descriptor, opened relative to that descriptor without following
 * a link, and every decision and every read is taken on the descriptor (fstat, the access ACL,
 * the typed attributes). A directory is listed through the descriptor that was judged. So a
 * directory or file swapped for a symbolic link between two steps (renameat2(RENAME_EXCHANGE)
 * by another user of the tree) cannot make a tool judge one inode and report another, nor walk
 * outside the root.
 *
 * What the callback sees, for a tree that does not change, is what nftw(FTW_PHYS | FTW_MOUNT)
 * gave the tools: entries in readdir order, a directory before what it contains, the root at
 * level 0; symbolic links, entries that cannot be stat'ed, entries on another filesystem and
 * directories that cannot be opened are not passed (the tools ignored FTW_SL, FTW_SLN, FTW_NS
 * and FTW_DNR); a FIFO, socket or device is passed like a file. The walk fails, as nftw did, when
 * the root cannot be stat'ed or an entry cannot be stat'ed for a reason other than EACCES or
 * ENOENT. Paths given to the callback are relative to the root ("" for the root itself).
 *
 * Descriptors:
 * - A directory or regular file is opened O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY (a FIFO
 *   swapped in must not block the walk). Its typed attributes are read with fgetxattr and
 *   flistxattr.
 * - An entry of another type, or a regular file the caller may not open for reading (a tool
 *   run by a user other than root), gets an O_PATH descriptor, which opens nothing; its
 *   attributes are read through /proc/self/fd/N, which reaches that same inode and applies the
 *   same permission check as a read by path (an fgetxattr on an O_PATH descriptor fails with
 *   EBADF). Without /proc those reads fail, and the tools say so as for any unreadable entry.
 * - The walk keeps one descriptor per level up to MOOB_WALK_OPEN levels; deeper, it closes the
 *   shallowest one and reopens it, when it is needed again, from the root down by name with
 *   O_NOFOLLOW, checking at each level that the (st_dev, st_ino) are the ones judged. A
 *   directory that is no longer there, or no longer the same, ends its listing with a line on
 *   stderr. nftw(..., 32, ...) had the same bound; the walk therefore needs about
 *   MOOB_WALK_OPEN + 4 descriptors whatever the depth.
 */
#ifndef MOOBFS_WALK_H
#define MOOBFS_WALK_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include "ext-acl.h"

#define MOOB_WALK_OPEN 32

struct moob_node {
	int fd;			/* O_RDONLY, or O_PATH when proc[0] */
	char proc[32];		/* "/proc/self/fd/N" for an O_PATH descriptor, else "" */
	struct stat st;		/* fstat of fd: the inode that is judged and reported */
};

static inline ssize_t moob_node_getxattr(const struct moob_node *n, const char *name, void *buf, size_t size)
{
	return n->proc[0] ? getxattr(n->proc, name, buf, size) : fgetxattr(n->fd, name, buf, size);
}

static inline ssize_t moob_node_listxattr(const struct moob_node *n, char *buf, size_t size)
{
	return n->proc[0] ? listxattr(n->proc, buf, size) : flistxattr(n->fd, buf, size);
}

/* moob_ext_acl() of tools/ext-acl.h on the inode this descriptor holds */
static inline int moob_node_ext_acl(const struct moob_node *n)
{
	static unsigned char buf[65536];	/* XATTR_SIZE_MAX: getxattr cannot return ERANGE */
	ssize_t len = moob_node_getxattr(n, "system.posix_acl_access", buf, sizeof buf);

	return moob_ext_acl_bytes(buf, len, len < 0 ? errno : 0, n->st.st_mode);
}

struct moob_walk_frame {
	struct moob_node node;	/* node.fd is -1 while closed (deeper than MOOB_WALK_OPEN) */
	int ext_acl;		/* moob_node_ext_acl() of this directory */
	size_t rel_len;		/* its path relative to the root is rel[0 .. rel_len) */
	char *names;		/* its entries, read once: NUL-separated, in readdir order */
	size_t names_len, next;	/* the next entry to visit is names + next */
	size_t name_off;	/* offset of its own name in the parent's names */
};

struct moob_walk;
/* level 0 is the root; a nonzero return stops the walk and is returned by moob_walk() */
typedef int (*moob_walk_fn)(struct moob_walk *w, const char *rel, const struct moob_node *n, int level);

struct moob_walk {
	moob_walk_fn visit;
	dev_t dev;			/* entries on another filesystem are not walked (FTW_MOUNT) */
	struct moob_walk_frame *frames;	/* frames[0 .. depth): the directories above the entry */
	int depth, cap, nopen;
	char *rel;			/* path of the current entry relative to the root */
	size_t rel_cap;
};

static inline void moob_node_close(struct moob_node *n)
{
	if (n->fd >= 0)
		close(n->fd);
	n->fd = -1;
	n->proc[0] = 0;
}

/*
 * Open name under dirfd as the inode fstatat() described (expect), never through a link.
 * Returns 0, 1 when the entry is to be left out (gone, swapped, closed to the caller), or -1.
 */
static inline int moob_node_open(int dirfd, const char *name, const struct stat *expect, struct moob_node *n)
{
	int reg = S_ISREG(expect->st_mode), dir = S_ISDIR(expect->st_mode);
	int flags = O_NOFOLLOW | O_CLOEXEC;

	n->proc[0] = 0;
	if (reg || dir)
		n->fd = openat(dirfd, name, flags | O_RDONLY | O_NONBLOCK | O_NOCTTY | (dir ? O_DIRECTORY : 0));
	else
		n->fd = -1, errno = EACCES;
	/* a regular file the caller cannot read is still listed, as a read by path listed it */
	if (n->fd < 0 && errno == EACCES && !dir) {
		n->fd = openat(dirfd, name, flags | O_PATH);
		if (n->fd >= 0)
			snprintf(n->proc, sizeof n->proc, "/proc/self/fd/%d", n->fd);
	}
	if (n->fd < 0)
		return errno == ENOMEM || errno == EMFILE || errno == ENFILE ? -1 : 1;
	if (fstat(n->fd, &n->st)) {
		moob_node_close(n);
		return -1;
	}
	/* another inode, or a link swapped in for the O_PATH case, is not the entry judged */
	if (n->st.st_dev != expect->st_dev || n->st.st_ino != expect->st_ino ||
	    (n->st.st_mode & S_IFMT) != (expect->st_mode & S_IFMT)) {
		moob_node_close(n);
		return 1;
	}
	return 0;
}

static inline int moob_walk_rel(struct moob_walk *w, size_t at, const char *name)
{
	size_t nl = strlen(name), need = at + 1 + nl + 1;

	if (need > w->rel_cap) {
		char *p = realloc(w->rel, need * 2);

		if (!p)
			return -1;
		w->rel = p;
		w->rel_cap = need * 2;
	}
	if (at)
		w->rel[at++] = '/';
	memcpy(w->rel + at, name, nl + 1);
	return 0;
}

/* Read every name of the directory n into f->names; returns 0, 1 (cannot be listed) or -1. */
static inline int moob_walk_list(struct moob_walk_frame *f)
{
	struct dirent *e;
	size_t cap = 0;
	int fd = dup(f->node.fd);
	DIR *dp;

	if (fd < 0)
		return -1;
	dp = fdopendir(fd);
	if (!dp) {
		close(fd);
		return -1;
	}
	f->names_len = f->next = 0;
	for (;;) {
		size_t nl;

		errno = 0;
		e = readdir(dp);
		if (!e)
			break;
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		nl = strlen(e->d_name) + 1;
		if (f->names_len + nl > cap) {
			char *p = realloc(f->names, (f->names_len + nl) * 2);

			if (!p) {
				closedir(dp);
				return -1;
			}
			f->names = p;
			cap = (f->names_len + nl) * 2;
		}
		memcpy(f->names + f->names_len, e->d_name, nl);
		f->names_len += nl;
	}
	if (errno) {
		int err = errno;

		closedir(dp);
		errno = err;
		return -1;
	}
	closedir(dp);
	return 0;
}

/*
 * The descriptor of frames[k], reopened from the deepest open frame above it when the walk had
 * closed it. Returns 0, 1 when the directory is no longer the one judged, or -1.
 */
static inline int moob_walk_reopen(struct moob_walk *w, int k)
{
	int j = k, i;

	while (w->frames[j].node.fd < 0)
		j--;
	for (i = j + 1; i <= k; i++) {
		struct moob_walk_frame *p = &w->frames[i - 1], *f = &w->frames[i];
		struct stat st;
		int fd = openat(p->node.fd, w->frames[i - 1].names + f->name_off,
				O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);

		if (i - 1 > j)		/* opened only to reach this one */
			moob_node_close(&p->node);
		if (fd < 0)
			return errno == ENOMEM || errno == EMFILE || errno == ENFILE ? -1 : 1;
		if (fstat(fd, &st) || st.st_dev != f->node.st.st_dev || st.st_ino != f->node.st.st_ino) {
			close(fd);
			return 1;
		}
		f->node.fd = fd;
	}
	w->nopen++;
	return 0;
}

/* Make frames[depth] the directory n (whose name is at name_off in the parent's names). */
static inline int moob_walk_push(struct moob_walk *w, struct moob_node *n, size_t rel_len, size_t name_off)
{
	struct moob_walk_frame *f;
	int r;

	if (w->depth == w->cap) {
		struct moob_walk_frame *p = realloc(w->frames, (size_t)(w->cap ? w->cap * 2 : 16) * sizeof *p);

		if (!p)
			return -1;
		w->frames = p;
		w->cap = w->cap ? w->cap * 2 : 16;
	}
	f = &w->frames[w->depth];
	memset(f, 0, sizeof *f);
	f->node = *n;
	f->rel_len = rel_len;
	f->name_off = name_off;
	n->fd = -1;	/* owned by the frame now */
	r = moob_walk_list(f);
	if (r) {
		moob_node_close(&f->node);
		free(f->names);
		return r;
	}
	f->ext_acl = moob_node_ext_acl(&f->node);
	w->depth++;
	/* keep the root and at most MOOB_WALK_OPEN levels open */
	if (++w->nopen > MOOB_WALK_OPEN) {
		int i;

		for (i = 1; i < w->depth && w->frames[i].node.fd < 0; i++)
			;
		moob_node_close(&w->frames[i].node);
		w->nopen--;
	}
	return 0;
}

static inline void moob_walk_pop(struct moob_walk *w)
{
	struct moob_walk_frame *f = &w->frames[--w->depth];

	if (f->node.fd >= 0)
		w->nopen--;
	moob_node_close(&f->node);
	free(f->names);
}

static inline int moob_walk_entries(struct moob_walk *w)
{
	while (w->depth > 1 || (w->depth == 1 && w->frames[0].next < w->frames[0].names_len)) {
		int k = w->depth - 1, r;
		struct moob_walk_frame *f = &w->frames[k];
		struct moob_node n;
		struct stat st;
		const char *name;
		size_t off;

		if (f->next >= f->names_len) {
			moob_walk_pop(w);
			continue;
		}
		off = f->next;
		name = f->names + off;
		f->next += strlen(name) + 1;
		if (f->node.fd < 0) {
			r = moob_walk_reopen(w, k);
			if (r < 0)
				return -1;
			if (r) {
				w->rel[f->rel_len] = 0;
				fprintf(stderr, "moob walk: %s: changed during the walk; its other entries were not read\n",
					f->rel_len ? w->rel : ".");
				f->next = f->names_len;
				continue;
			}
		}
		if (fstatat(f->node.fd, name, &st, AT_SYMLINK_NOFOLLOW)) {
			if (errno == EACCES || errno == ENOENT)
				continue;	/* FTW_NS */
			return -1;
		}
		if (S_ISLNK(st.st_mode) || st.st_dev != w->dev)
			continue;		/* FTW_SL; FTW_MOUNT */
		r = moob_node_open(f->node.fd, name, &st, &n);
		if (r < 0)
			return -1;
		if (r)
			continue;
		w->rel[f->rel_len] = 0;
		if (moob_walk_rel(w, f->rel_len, name)) {
			moob_node_close(&n);
			return -1;
		}
		r = w->visit(w, w->rel, &n, w->depth);
		if (r) {
			moob_node_close(&n);
			return r;
		}
		if (S_ISDIR(n.st.st_mode)) {
			r = moob_walk_push(w, &n, strlen(w->rel), off);
			if (r < 0)
				return -1;	/* r > 0: cannot be listed, FTW_DNR after the fact */
		}
		moob_node_close(&n);	/* f may have moved: frames can be reallocated by the push */
	}
	return 0;
}

/*
 * Walk root as nftw(root, visit, 32, FTW_PHYS | FTW_MOUNT) walked it for the tools. A root that
 * is a symbolic link, even written with a trailing slash, is not walked (nftw reported it as
 * FTW_SL). Returns 0, the callback's
 * nonzero value, or -1 with errno set.
 */
static inline int moob_walk(const char *root, moob_walk_fn visit)
{
	struct moob_walk w;
	struct moob_node n;
	struct stat st;
	int r;

	char *top;
	size_t len = strlen(root);

	memset(&w, 0, sizeof w);
	w.visit = visit;
	/* nftw stripped trailing slashes first: "link/" names the link, not where it points */
	while (len > 1 && root[len - 1] == '/')
		len--;
	top = strndup(root, len);
	if (!top)
		return -1;
	if (fstatat(AT_FDCWD, top, &st, AT_SYMLINK_NOFOLLOW)) {
		free(top);
		return -1;
	}
	if (S_ISLNK(st.st_mode)) {
		free(top);
		return 0;
	}
	w.dev = st.st_dev;
	r = moob_node_open(AT_FDCWD, top, &st, &n);
	free(top);
	if (r < 0)
		return -1;
	if (r)
		return 0;	/* FTW_DNR, or the root changed between the two calls */
	if (moob_walk_rel(&w, 0, "")) {
		moob_node_close(&n);
		return -1;
	}
	r = visit(&w, w.rel, &n, 0);
	if (!r && S_ISDIR(n.st.st_mode)) {
		r = moob_walk_push(&w, &n, 0, 0);
		if (r > 0)
			r = 0;
		if (!r)
			r = moob_walk_entries(&w);
	}
	moob_node_close(&n);
	while (w.depth)
		moob_walk_pop(&w);
	free(w.frames);
	free(w.rel);
	return r;
}
#endif
