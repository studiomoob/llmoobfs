/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Phase 2a/2b acceptance test for the MoobFS memory layer: exercises the
 * pass-through operations on a mounted layer and checks that the lower
 * directory reflects every change. Usage: layer <mount> <lower> <reflink: yes|no> <links|nolinks>
 *   layer subvol <dir> <name>       create a btrfs subvolume <name> in <dir>
 *   layer syncfs <path>             syncfs(2) on the filesystem that holds <path>
 *   layer reserve <mount> <lower>   an ordinary user fills the layer; the blocks the lower
 *                                   reserves for its administrator must stay free
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#include <sys/wait.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <linux/btrfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <grp.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s (errno=%d %s)\n", __LINE__, #c, errno, strerror(errno)); return 1; } } while (0)

static char mnt[4096], low[4096];
/* Rotating buffers so two calls can appear in one expression. */
static const char *join(const char *base, const char *n)
{
	static char ring[8][8192]; static unsigned next;
	char *b = ring[next++ % 8];
	snprintf(b, sizeof ring[0], "%s/%s", base, n);
	return b;
}
static const char *up(const char *n) { return join(mnt, n); }
static const char *lo(const char *n) { return join(low, n); }

static int same_content(const char *a, const char *b)
{
	char x[65536], y[65536];
	int fa = open(a, O_RDONLY), fb = open(b, O_RDONLY);
	ssize_t na, nb;
	if (fa < 0 || fb < 0) return 0;
	na = read(fa, x, sizeof x); nb = read(fb, y, sizeof y);
	close(fa); close(fb);
	return na == nb && na >= 0 && !memcmp(x, y, (size_t)na);
}

static int shared_extent(const char *path)
{
	struct fiemap *map = calloc(1, sizeof(*map) + 4 * sizeof(struct fiemap_extent));
	int fd = open(path, O_RDONLY), shared = 0;
	unsigned i;
	if (!map || fd < 0) return -1;
	map->fm_length = ~0ULL; map->fm_extent_count = 4;
	if (ioctl(fd, FS_IOC_FIEMAP, map) == 0) {
		for (i = 0; i < map->fm_mapped_extents; i++)
			if (map->fm_extents[i].fe_flags & FIEMAP_EXTENT_SHARED) shared = 1;
	} else {
		shared = -1;
	}
	close(fd); free(map); return shared;
}

static int count_entries(const char *dir)
{
	DIR *d = opendir(dir); struct dirent *e; int n = 0;
	if (!d) return -1;
	while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
	closedir(d); return n;
}

/*
 * As uid and gid 1001, on a descriptor that root opened: writes to it (how 0), truncates it
 * (how 1), fills it until ENOSPC (how 2), copies into it with copy_file_range (how 3), clones
 * into it (how 4) or preallocates in it (how 5). src_fd is the source of 3 and 4.
 * Without CAP_FSETID the VFS then clears set-user-ID and set-group-ID through ->setattr, the
 * path that reached BUG() in notify_change (journal 0069). Returns the child's exit status.
 */
static int src_fd = -1;

static int as_user(int fd, int how)
{
	pid_t pid = fork();
	int status;

	if (pid < 0) return -1;
	if (!pid) {
		static char block[1 << 16];

		if (setgroups(0, NULL) || setgid(1001) || setuid(1001)) _exit(2);
		if (how == 0) _exit(write(fd, "y", 1) == 1 ? 0 : 3);
		if (how == 1) _exit(ftruncate(fd, 2) ? 4 : 0);
		if (how == 3) {
			loff_t in = 0, out = 0;

			_exit(copy_file_range(src_fd, &in, fd, &out, 4096, 0) == 4096 ? 0 : 6);
		}
		if (how == 4) _exit(ioctl(fd, FICLONE, src_fd) ? 7 : 0);
		if (how == 5) _exit(fallocate(fd, 0, 0, 65536) ? 8 : 0);
		if (how == 6) {
			/* dedupe into a descriptor this user could not open for writing */
			struct file_dedupe_range *dr = calloc(1, sizeof(*dr) + sizeof(struct file_dedupe_range_info));

			if (!dr) _exit(9);
			dr->src_length = 8192; dr->dest_count = 1; dr->info[0].dest_fd = fd;
			if (ioctl(src_fd, FIDEDUPERANGE, dr)) _exit(9);
			_exit(dr->info[0].status == -EPERM ? 0 : 10);
		}
		if (how == 7) {
			/* toggling append-only needs CAP_LINUX_IMMUTABLE, and any flag the ownership */
			int f;

			if (ioctl(fd, FS_IOC_GETFLAGS, &f)) _exit(11);
			f ^= FS_APPEND_FL;
			errno = 0;
			_exit(ioctl(fd, FS_IOC_SETFLAGS, &f) == -1 && errno == EPERM ? 0 : 12);
		}
		while (write(fd, block, sizeof block) > 0)
			;
		_exit(errno == ENOSPC ? 0 : 5);
	}
	if (waitpid(pid, &status, 0) != pid) return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

int main(int argc, char **argv)
{
	struct stat st, lst;
	struct statfs sfs;
	char buf[8192], *map;
	int fd, i;
	ssize_t n;

	int reflink, links;
	if (argc == 4 && !strcmp(argv[1], "subvol")) {
		struct btrfs_ioctl_vol_args va;

		memset(&va, 0, sizeof va);
		fd = open(argv[2], O_RDONLY | O_DIRECTORY); CHECK(fd >= 0);
		snprintf(va.name, sizeof va.name, "%s", argv[3]);
		CHECK(!ioctl(fd, BTRFS_IOC_SUBVOL_CREATE, &va));
		close(fd);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "syncfs")) {
		fd = open(argv[2], O_RDONLY); CHECK(fd >= 0);
		CHECK(!syncfs(fd));
		close(fd);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "reserve")) {
		/* an ordinary user fills the lower through the layer: whatever the reserve
		 * owner (vm/suite.sh, step reserve), at least most of the reserve is left */
		unsigned long long reserve;

		snprintf(mnt, sizeof mnt, "%s", argv[2]);
		snprintf(low, sizeof low, "%s", argv[3]);
		CHECK(!statfs(low, &sfs) && sfs.f_bfree > sfs.f_bavail);
		reserve = sfs.f_bfree - sfs.f_bavail;
		fd = open(up("fill"), O_WRONLY | O_CREAT | O_EXCL, 0644); CHECK(fd >= 0);
		CHECK(!fchown(fd, 1001, 1001));
		CHECK(as_user(fd, 2) == 0);
		CHECK(!fsync(fd));
		CHECK(!statfs(low, &sfs) && sfs.f_bfree >= reserve * 3 / 4);
		close(fd);
		CHECK(!unlink(up("fill")));
		puts("LAYER_RESERVE_PASS");
		return 0;
	}
	CHECK(argc == 5);
	reflink = !strcmp(argv[3], "yes");
	links = !strcmp(argv[4], "links");
	snprintf(mnt, sizeof mnt, "%s", argv[1]);
	snprintf(low, sizeof low, "%s", argv[2]);

	/* statfs passes through, with the layer's own magic */
	CHECK(!statfs(mnt, &sfs) && sfs.f_type == 0x4d4f4f42);

	/* create + write + read, visible in lower with the same bytes */
	fd = open(up("a.txt"), O_WRONLY | O_CREAT | O_EXCL, 0640); CHECK(fd >= 0);
	for (i = 0; i < (int)sizeof buf; i++) buf[i] = (char)('a' + i % 26);
	CHECK(write(fd, buf, sizeof buf) == (ssize_t)sizeof buf);
	CHECK(!fsync(fd));
	CHECK(!fstat(fd, &st) && st.st_size == (off_t)sizeof buf);
	close(fd);
	CHECK(!stat(lo("a.txt"), &lst) && lst.st_size == (off_t)sizeof buf);
	CHECK(!stat(up("a.txt"), &st) && st.st_ino == lst.st_ino && st.st_mode == lst.st_mode);
	CHECK(same_content(up("a.txt"), lo("a.txt")));
	errno = 0; CHECK(open(up("a.txt"), O_WRONLY | O_CREAT | O_EXCL, 0640) == -1 && errno == EEXIST);

	/* pread across a page boundary and EOF */
	fd = open(up("a.txt"), O_RDONLY); CHECK(fd >= 0);
	CHECK(pread(fd, buf, 100, 4050) == 100);
	for (i = 0; i < 100; i++) CHECK(buf[i] == (char)('a' + (4050 + i) % 26));
	CHECK(pread(fd, buf, 10, 8192) == 0);
	CHECK(lseek(fd, 8000, SEEK_SET) == 8000 && read(fd, buf, 1000) == 192);
	CHECK(lseek(fd, 0, SEEK_END) == 8192);
	close(fd);

	/* append, truncate, ftruncate */
	fd = open(up("a.txt"), O_WRONLY | O_APPEND); CHECK(fd >= 0);
	CHECK(write(fd, "tail", 4) == 4); close(fd);
	CHECK(!stat(lo("a.txt"), &lst) && lst.st_size == 8196);
	CHECK(!truncate(up("a.txt"), 100));
	CHECK(!stat(lo("a.txt"), &lst) && lst.st_size == 100);
	CHECK(!stat(up("a.txt"), &st) && st.st_size == 100);
	fd = open(up("a.txt"), O_RDWR); CHECK(fd >= 0);
	CHECK(!ftruncate(fd, 12000));
	CHECK(!fstat(fd, &st) && st.st_size == 12000);
	CHECK(pread(fd, buf, 16, 11990) == 10);
	for (i = 0; i < 10; i++) CHECK(buf[i] == 0);
	close(fd);

	/* shared writable mmap reaches the lower file; private mmap reads */
	fd = open(up("a.txt"), O_RDWR); CHECK(fd >= 0);
	map = mmap(NULL, 12000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	CHECK(map != MAP_FAILED);
	memcpy(map + 5000, "MMAPPED", 7);
	CHECK(!msync(map, 12000, MS_SYNC));
	CHECK(!munmap(map, 12000));
	CHECK(pread(fd, buf, 7, 5000) == 7 && !memcmp(buf, "MMAPPED", 7));
	close(fd);
	fd = open(lo("a.txt"), O_RDONLY); CHECK(fd >= 0);
	CHECK(pread(fd, buf, 7, 5000) == 7 && !memcmp(buf, "MMAPPED", 7));
	close(fd);
	fd = open(up("a.txt"), O_RDONLY); CHECK(fd >= 0);
	map = mmap(NULL, 12000, PROT_READ, MAP_PRIVATE, fd, 0);
	CHECK(map != MAP_FAILED && !memcmp(map + 5000, "MMAPPED", 7));
	CHECK(!munmap(map, 12000)); close(fd);

	/* directories: mkdir, nested create, readdir, rmdir ordering */
	CHECK(!mkdir(up("d"), 0750));
	CHECK(!stat(lo("d"), &lst) && S_ISDIR(lst.st_mode));
	fd = open(up("d/inner"), O_WRONLY | O_CREAT, 0600); CHECK(fd >= 0);
	CHECK(write(fd, "x", 1) == 1); close(fd);
	CHECK(count_entries(up("d")) == 1 && count_entries(lo("d")) == 1);
	CHECK(count_entries(up("")) == count_entries(lo("")));
	errno = 0; CHECK(rmdir(up("d")) == -1 && errno == ENOTEMPTY);
	CHECK(!unlink(up("d/inner")));
	errno = 0; CHECK(stat(lo("d/inner"), &lst) == -1 && errno == ENOENT);
	CHECK(!rmdir(up("d")));
	errno = 0; CHECK(stat(lo("d"), &lst) == -1 && errno == ENOENT);
	errno = 0; CHECK(stat(up("d"), &st) == -1 && errno == ENOENT);

	/* chmod and utimes pass through */
	CHECK(!chmod(up("a.txt"), 0600));
	CHECK(!stat(lo("a.txt"), &lst) && (lst.st_mode & 07777) == 0600);

	/* unlink + recreate under the same name: the new file must be the one opened afterwards
	 * (this is the SQLite -wal/-shm pattern that lost WAL frames in phase 2e) */
	fd = open(up("again"), O_RDWR | O_CREAT | O_TRUNC, 0600); CHECK(fd >= 0);
	CHECK(write(fd, "old", 3) == 3); close(fd);
	CHECK(!unlink(up("again")));
	fd = open(up("again"), O_RDWR | O_CREAT | O_TRUNC, 0600); CHECK(fd >= 0);
	CHECK(write(fd, "new", 3) == 3); close(fd);
	fd = open(up("again"), O_RDONLY); CHECK(fd >= 0);
	CHECK(read(fd, buf, 3) == 3 && !memcmp(buf, "new", 3)); close(fd);
	CHECK(same_content(up("again"), lo("again")));
	CHECK(!stat(up("again"), &st) && !stat(lo("again"), &lst) && st.st_ino == lst.st_ino);
	{	/* and with the old file still open elsewhere while the name is reused */
		int old = open(up("again"), O_RDONLY); CHECK(old >= 0);
		CHECK(!unlink(up("again")));
		fd = open(up("again"), O_RDWR | O_CREAT | O_TRUNC, 0600); CHECK(fd >= 0);
		CHECK(write(fd, "third", 5) == 3 + 2); close(fd);
		fd = open(up("again"), O_RDONLY); CHECK(fd >= 0);
		CHECK(read(fd, buf, 5) == 5 && !memcmp(buf, "third", 5)); close(fd);
		CHECK(pread(old, buf, 3, 0) == 3 && !memcmp(buf, "new", 3));	/* deleted file keeps its content */
		close(old);
		CHECK(same_content(up("again"), lo("again")));
	}
	CHECK(!unlink(up("again")));
	CHECK(!mkdir(up("dagain"), 0700) && !rmdir(up("dagain")) && !mkdir(up("dagain"), 0700));
	CHECK(!stat(lo("dagain"), &lst) && S_ISDIR(lst.st_mode));
	fd = open(up("dagain/inner"), O_WRONLY | O_CREAT, 0600); CHECK(fd >= 0); close(fd);
	CHECK(!stat(lo("dagain/inner"), &lst));
	CHECK(!unlink(up("dagain/inner")) && !rmdir(up("dagain")));

	/* rename: plain, over an existing target, directory, cross-directory, noreplace */
	CHECK(!rename(up("a.txt"), up("c.txt")));
	errno = 0; CHECK(stat(lo("a.txt"), &lst) == -1 && errno == ENOENT);
	CHECK(!stat(lo("c.txt"), &lst) && lst.st_size == 12000);
	CHECK(!stat(up("c.txt"), &st) && st.st_ino == lst.st_ino);
	fd = open(up("victim"), O_WRONLY | O_CREAT, 0600); CHECK(fd >= 0); close(fd);
	CHECK(!rename(up("c.txt"), up("victim")));
	CHECK(!stat(lo("victim"), &lst) && lst.st_size == 12000);
	errno = 0; CHECK(stat(lo("c.txt"), &lst) == -1 && errno == ENOENT);
	CHECK(!rename(up("victim"), up("a.txt")));
	CHECK(!mkdir(up("d1"), 0750) && !mkdir(up("d2"), 0750));
	CHECK(!rename(up("d1"), up("d2/moved")));
	CHECK(!stat(lo("d2/moved"), &lst) && S_ISDIR(lst.st_mode));
	errno = 0; CHECK(rename(up("d2"), up("d2/moved/loop")) == -1 && errno == EINVAL);
	CHECK(!rmdir(up("d2/moved")) && !rmdir(up("d2")));
	errno = 0; CHECK(renameat2(AT_FDCWD, up("a.txt"), AT_FDCWD, up("a.txt"), RENAME_NOREPLACE) == -1 && errno == EEXIST);
	CHECK(same_content(up("a.txt"), lo("a.txt")));
	/* atomic replace after an unlink: delete a name, then rename a new file over it (a cached
	 * negative dentry must not keep the deleted lower dentry; found with ai-memory, journal 0057) */
	fd = open(up("gone.md"), O_WRONLY | O_CREAT, 0600); CHECK(fd >= 0 && write(fd, "old", 3) == 3); close(fd);
	CHECK(!stat(up("gone.md"), &st) && !unlink(up("gone.md")));
	errno = 0; CHECK(stat(up("gone.md"), &st) == -1 && errno == ENOENT);
	fd = open(up("gone.md.tmp"), O_WRONLY | O_CREAT, 0600); CHECK(fd >= 0 && write(fd, "new!", 4) == 4); close(fd);
	CHECK(!rename(up("gone.md.tmp"), up("gone.md")));
	CHECK(!stat(lo("gone.md"), &lst) && lst.st_size == 4);
	CHECK(!stat(up("gone.md"), &st) && st.st_ino == lst.st_ino && st.st_size == 4);
	errno = 0; CHECK(stat(lo("gone.md.tmp"), &lst) == -1 && errno == ENOENT);
	/* the same for a directory name removed with rmdir */
	CHECK(!mkdir(up("gonedir"), 0700) && !stat(up("gonedir"), &st) && !rmdir(up("gonedir")));
	CHECK(!mkdir(up("gonedir.tmp"), 0700) && !rename(up("gonedir.tmp"), up("gonedir")));
	CHECK(!stat(lo("gonedir"), &lst) && S_ISDIR(lst.st_mode) && !rmdir(up("gonedir")));
	CHECK(!unlink(up("gone.md")));

	/* xattr: opaque pass-through to the lower file */
	CHECK(!setxattr(up("a.txt"), "user.other.kind", "note", 4, 0));
	CHECK(getxattr(lo("a.txt"), "user.other.kind", buf, sizeof buf) == 4 && !memcmp(buf, "note", 4));
	CHECK(getxattr(up("a.txt"), "user.other.kind", buf, sizeof buf) == 4 && !memcmp(buf, "note", 4));
	errno = 0; CHECK(setxattr(up("a.txt"), "user.other.kind", "x", 1, XATTR_CREATE) == -1 && errno == EEXIST);
	CHECK(!setxattr(up("a.txt"), "user.other.kind", "decision", 8, XATTR_REPLACE));
	CHECK(getxattr(lo("a.txt"), "user.other.kind", buf, sizeof buf) == 8);
	n = listxattr(up("a.txt"), buf, sizeof buf); CHECK(n > 0 && memmem(buf, (size_t)n, "user.other.kind", 15));
	CHECK(!removexattr(up("a.txt"), "user.other.kind"));
	errno = 0; CHECK(getxattr(lo("a.txt"), "user.other.kind", buf, sizeof buf) == -1 && errno == ENODATA);
	errno = 0; CHECK(getxattr(up("a.txt"), "user.other.missing", buf, sizeof buf) == -1 && errno == ENODATA);

	/* typed attributes: the layer validates user.moob.* before the lower write */
	CHECK(!setxattr(up("a.txt"), "user.moob.kind", "\x01note", 5, 0));
	CHECK(getxattr(lo("a.txt"), "user.moob.kind", buf, sizeof buf) == 5 && !memcmp(buf, "\x01note", 5));
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.kind", "\x07x", 2, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.kind", "\x01\xff", 2, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.n", "\x02\x01", 2, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.b", "\x04\x02", 2, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.Kind", "\x01x", 2, 0) == -1 && errno == EINVAL);
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.format", "1", 1, 0) == -1 && errno == EPERM);
	errno = 0; CHECK(removexattr(up("a.txt"), "user.moob.format") == -1 && errno == EPERM);
	CHECK(getxattr(lo("a.txt"), "user.moob.kind", buf, sizeof buf) == 5);	/* rejected writes changed nothing */
	errno = 0; CHECK(getxattr(lo("a.txt"), "user.moob.n", buf, sizeof buf) == -1 && errno == ENODATA);
	CHECK(!setxattr(up("a.txt"), "user.other", "anything", 8, 0));	/* opaque namespaces untouched */
	CHECK(!removexattr(up("a.txt"), "user.other"));
	for (i = 1; i < 64; i++) {
		char name[32];
		snprintf(name, sizeof name, "user.moob.k%d", i);
		CHECK(!setxattr(up("a.txt"), name, "\x04\x01", 2, 0));	/* needs a 4 KiB xattr block below */
	}
	errno = 0; CHECK(setxattr(up("a.txt"), "user.moob.k64", "\x04\x01", 2, 0) == -1 && errno == ENOSPC);
	CHECK(!setxattr(up("a.txt"), "user.moob.k1", "\x04\x00", 2, 0));	/* replacing stays allowed */
	for (i = 1; i < 64; i++) {
		char name[32];
		snprintf(name, sizeof name, "user.moob.k%d", i);
		CHECK(!removexattr(up("a.txt"), name));
	}
	CHECK(!removexattr(up("a.txt"), "user.moob.kind"));

	/* hard links and symlinks: pass through, or refused with nolinks */
	if (links) {
		CHECK(!link(up("a.txt"), up("b.txt")));
		CHECK(!stat(lo("b.txt"), &lst) && lst.st_nlink == 2);
		CHECK(!stat(up("a.txt"), &st) && st.st_nlink == 2 && st.st_ino == lst.st_ino);
		CHECK(!unlink(up("b.txt")));
		CHECK(!stat(up("a.txt"), &st) && st.st_nlink == 1);
		CHECK(!symlink("a.txt", up("s")));
		n = readlink(up("s"), buf, sizeof buf); CHECK(n == 5 && !memcmp(buf, "a.txt", 5));
		CHECK(!lstat(lo("s"), &lst) && S_ISLNK(lst.st_mode));
		CHECK(!stat(up("s"), &st) && st.st_size == 12000);
		CHECK(same_content(up("s"), lo("a.txt")));
		CHECK(!unlink(up("s")));
	} else {
		errno = 0; CHECK(link(up("a.txt"), up("b.txt")) == -1 && errno == EPERM);
		errno = 0; CHECK(symlink("a.txt", up("s")) == -1 && errno == EPERM);
		errno = 0; CHECK(stat(lo("b.txt"), &lst) == -1 && errno == ENOENT);
	}

	/* reflink and dedupe are delegated to the lower filesystem */
	{
		int src = open(up("a.txt"), O_RDONLY), dst = open(up("clone"), O_RDWR | O_CREAT | O_TRUNC, 0600);
		CHECK(src >= 0 && dst >= 0);
		errno = 0;
		if (reflink) {
			struct file_dedupe_range *dr = calloc(1, sizeof(*dr) + sizeof(struct file_dedupe_range_info));
			int twin = open(up("twin"), O_RDWR | O_CREAT | O_TRUNC, 0600);
			CHECK(dr && twin >= 0);
			CHECK(!ioctl(dst, FICLONE, src));
			CHECK(!fstat(dst, &st) && st.st_size == 12000);
			CHECK(same_content(up("clone"), lo("a.txt")));
			CHECK(shared_extent(up("clone")) == 1 && shared_extent(lo("clone")) == 1);
			CHECK(pwrite(dst, "COW", 3, 5000) == 3);
			CHECK(pread(src, buf, 7, 5000) == 7 && !memcmp(buf, "MMAPPED", 7));
			/* twin is a byte copy written the slow way, then deduplicated against a.txt */
			for (i = 0; i < 12000; i += (int)sizeof buf) {
				n = pread(src, buf, sizeof buf, i); CHECK(n > 0);
				CHECK(pwrite(twin, buf, (size_t)n, i) == n);
			}
			CHECK(!fsync(twin) && !fsync(src));
			CHECK(shared_extent(up("twin")) == 0);
			{
				/* the lower call runs as the mounter, who may dedupe into anything; the
				 * caller's right to change the target is checked before it */
				int ro = open(up("twin"), O_RDONLY);

				CHECK(ro >= 0);
				src_fd = src;
				CHECK(as_user(ro, 6) == 0);
				close(ro);
				CHECK(shared_extent(up("twin")) == 0);
			}
			dr->src_offset = 0; dr->src_length = 8192; dr->dest_count = 1;
			dr->info[0].dest_fd = twin; dr->info[0].dest_offset = 0;
			CHECK(!ioctl(src, FIDEDUPERANGE, dr));
			CHECK(dr->info[0].status == FILE_DEDUPE_RANGE_SAME && dr->info[0].bytes_deduped == 8192);
			CHECK(shared_extent(up("twin")) == 1);
			close(twin); free(dr);
			CHECK(!unlink(up("twin")));
		} else {
			CHECK(ioctl(dst, FICLONE, src) == -1 && (errno == EOPNOTSUPP || errno == EXDEV || errno == EINVAL));
		}
		close(src); close(dst);
		CHECK(!unlink(up("clone")));
	}

	/* an unprivileged write and an unprivileged truncate clear set-user-ID and set-group-ID */
	fd = open(up("suid"), O_RDWR | O_CREAT | O_EXCL, 0755); CHECK(fd >= 0);
	CHECK(write(fd, "xxxx", 4) == 4);
	for (i = 0; i < 2; i++) {
		CHECK(!fchown(fd, 1001, 1001) && !fchmod(fd, 06755));
		CHECK(!stat(lo("suid"), &lst) && (lst.st_mode & 07777) == 06755);
		CHECK(as_user(fd, i) == 0);
		CHECK(!stat(up("suid"), &st) && (st.st_mode & 07777) == 0755);
		CHECK(!stat(lo("suid"), &lst) && (lst.st_mode & 07777) == 0755);
	}
	CHECK(st.st_size == 2);
	/* chown clears them too, for every caller, through the same ->setattr path */
	CHECK(!fchmod(fd, 06755) && !fchown(fd, 1001, 1001));
	CHECK(!stat(lo("suid"), &lst) && (lst.st_mode & 07777) == 0755);
	/* so do the calls that change the content without a write: copy_file_range, a clone and
	 * fallocate. The lower runs them with the mounter's credentials, which would keep the bits,
	 * so the layer must clear them first, as it does for a write. */
	src_fd = open(up("suidsrc"), O_RDWR | O_CREAT | O_EXCL, 0644); CHECK(src_fd >= 0);
	memset(buf, 'S', 4096);
	CHECK(write(src_fd, buf, 4096) == 4096 && !fsync(src_fd));
	for (i = 3; i <= 5; i++) {
		if (i == 4 && !reflink)
			continue;
		CHECK(!fchmod(fd, 06755));
		CHECK(!stat(lo("suid"), &lst) && (lst.st_mode & 07777) == 06755);
		CHECK(as_user(fd, i) == 0);
		CHECK(!stat(lo("suid"), &lst) && (lst.st_mode & 07777) == 0755);
	}
	CHECK(pread(fd, buf, 1, 0) == 1 && buf[0] == 'S');
	{
		/* file capabilities go the same way: an ordinary write removes them and succeeds */
		static const unsigned char caps[20] = { 0x01, 0x00, 0x00, 0x02, 0xc0 };	/* v2, setuid+setgid, effective */
		int how[] = { 0, 3, 5 };
		unsigned k;

		for (k = 0; k < 3; k++) {
			CHECK(!fsetxattr(fd, "security.capability", caps, sizeof caps, 0));
			CHECK(getxattr(lo("suid"), "security.capability", buf, sizeof buf) == (ssize_t)sizeof caps);
			CHECK(as_user(fd, how[k]) == 0);
			errno = 0;
			CHECK(getxattr(lo("suid"), "security.capability", buf, sizeof buf) == -1 && errno == ENODATA);
		}
	}
	close(src_fd);
	close(fd);
	CHECK(!unlink(up("suid")) && !unlink(up("suidsrc")));

	/* an ACL read through the layer is the one just set, not one remembered from an earlier read */
	{
		/* version 2; then tag, permissions and id of each entry, little-endian */
		unsigned char acl[2][44] = {
			{ 2, 0, 0, 0,  1, 0, 6, 0, 0xff, 0xff, 0xff, 0xff,  2, 0, 4, 0, 0xe9, 3, 0, 0,
			  4, 0, 4, 0, 0xff, 0xff, 0xff, 0xff,  0x10, 0, 4, 0, 0xff, 0xff, 0xff, 0xff,
			  0x20, 0, 4, 0, 0xff, 0xff, 0xff, 0xff },	/* user 1001: r-- */
			{ 2, 0, 0, 0,  1, 0, 6, 0, 0xff, 0xff, 0xff, 0xff,  2, 0, 6, 0, 0xea, 3, 0, 0,
			  4, 0, 4, 0, 0xff, 0xff, 0xff, 0xff,  0x10, 0, 6, 0, 0xff, 0xff, 0xff, 0xff,
			  0x20, 0, 4, 0, 0xff, 0xff, 0xff, 0xff },	/* user 1002: rw- */
		};
		const char *name = "system.posix_acl_access";
		unsigned char got[64];

		fd = open(up("acl"), O_WRONLY | O_CREAT | O_EXCL, 0644); CHECK(fd >= 0); close(fd);
		if (getxattr(up("acl"), name, got, sizeof got) < 0 && errno == EOPNOTSUPP) {
			puts("note: lower mounted without POSIX ACLs, ACL read test skipped");
		} else {
			for (i = 0; i < 4; i++) {
				CHECK(!setxattr(up("acl"), name, acl[i % 2], sizeof acl[0], 0));
				CHECK(getxattr(up("acl"), name, got, sizeof got) == (ssize_t)sizeof acl[0]);
				CHECK(!memcmp(got, acl[i % 2], sizeof acl[0]));
				CHECK(getxattr(lo("acl"), name, got, sizeof got) == (ssize_t)sizeof acl[0]);
				CHECK(!memcmp(got, acl[i % 2], sizeof acl[0]));
			}
		}
		CHECK(!unlink(up("acl")));
	}

	/* file flags (lsattr, chattr) are the lower's, read and changed through the layer */
	{
		struct fsxattr fsx;
		int flags = -1, lflags = -2, lfd, w;

		fd = open(up("flags"), O_RDWR | O_CREAT | O_EXCL, 0666); CHECK(fd >= 0);
		CHECK(!fchmod(fd, 0666) && write(fd, "abc", 3) == 3);
		lfd = open(lo("flags"), O_RDONLY); CHECK(lfd >= 0);
		CHECK(!ioctl(fd, FS_IOC_GETFLAGS, &flags) && !ioctl(lfd, FS_IOC_GETFLAGS, &lflags));
		CHECK(flags == lflags);
		CHECK(!ioctl(fd, FS_IOC_FSGETXATTR, &fsx));
		CHECK(as_user(fd, 7) == 0);		/* not the owner */
		CHECK(!fchown(fd, 1001, 1001));
		CHECK(as_user(fd, 7) == 0);		/* the owner, without the capability */
		CHECK(!ioctl(lfd, FS_IOC_GETFLAGS, &lflags) && !(lflags & FS_APPEND_FL));
		flags |= FS_APPEND_FL;
		CHECK(!ioctl(fd, FS_IOC_SETFLAGS, &flags));
		CHECK(!ioctl(lfd, FS_IOC_GETFLAGS, &lflags) && (lflags & FS_APPEND_FL));
		{
			struct statx sx;

			CHECK(!statx(AT_FDCWD, up("flags"), 0, STATX_BASIC_STATS, &sx));
			CHECK((sx.stx_attributes_mask & STATX_ATTR_APPEND) && (sx.stx_attributes & STATX_ATTR_APPEND));
			CHECK(!(sx.stx_attributes & STATX_ATTR_IMMUTABLE));
		}
		/* append-only is enforced on the layer's inode from then on */
		errno = 0; CHECK(open(up("flags"), O_WRONLY) == -1 && errno == EPERM);
		errno = 0; CHECK(truncate(up("flags"), 0) == -1 && errno == EPERM);
		errno = 0; CHECK(unlink(up("flags")) == -1 && errno == EPERM);
		w = open(up("flags"), O_WRONLY | O_APPEND); CHECK(w >= 0);
		CHECK(write(w, "d", 1) == 1);
		close(w);
		CHECK(as_user(fd, 7) == 0);		/* the owner cannot clear it either */
		flags = (flags & ~FS_APPEND_FL) | FS_IMMUTABLE_FL;
		CHECK(!ioctl(fd, FS_IOC_SETFLAGS, &flags));
		errno = 0; CHECK(open(up("flags"), O_WRONLY | O_APPEND) == -1 && errno == EPERM);
		flags &= ~FS_IMMUTABLE_FL;
		CHECK(!ioctl(fd, FS_IOC_SETFLAGS, &flags));
		CHECK(!ioctl(lfd, FS_IOC_GETFLAGS, &lflags) && !(lflags & (FS_APPEND_FL | FS_IMMUTABLE_FL)));
		CHECK(!fstat(fd, &st) && st.st_size == 4);
		close(lfd); close(fd);
		CHECK(!unlink(up("flags")));
	}

	/* unlink removes from lower; open descriptor keeps reading */
	fd = open(up("a.txt"), O_RDONLY); CHECK(fd >= 0);
	CHECK(!unlink(up("a.txt")));
	errno = 0; CHECK(stat(lo("a.txt"), &lst) == -1 && errno == ENOENT);
	n = pread(fd, buf, 7, 5000); CHECK(n == 7 && !memcmp(buf, "MMAPPED", 7));
	close(fd);
	CHECK(count_entries(up("")) == count_entries(lo("")));

	puts("LAYER_PASSTHROUGH_TESTS_PASS");
	return 0;
}
