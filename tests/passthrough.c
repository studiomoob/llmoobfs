/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Passthrough gaps of issue #115 (V1, V3, V5, V7 of journal 0155; journal 0224): what the
 * layer forwards must act as it does on the lower file.
 *   fadvise    posix_fadvise(DONTNEED) on a layer descriptor empties the lower page cache
 *   readahead  readahead(2) on a layer descriptor fills it
 *   statx      O_DIRECT alignment, atomic writes, subvolume and the encrypted, verity and DAX
 *              attributes are the lower's
 *   dropinode  a file removed behind the layer frees its space once the layer drops its name
 *   noatime    O_NOATIME set with F_SETFL after open stops the lower's access time
 *   limits     the two accepted limits stay as documented (V2 and the cachestat part of V1,
 *              journal 0284): sync_file_range(2) on a layer descriptor succeeds and writes
 *              nothing back, and cachestat(2) on it reports no cached page, while the same
 *              calls on the lower descriptor do (README "Limits", docs/matrix-and-limits.md)
 *   noexec     SB_I_NOEXEC and SB_I_NODEV as docs/when-not-to-use.md §3 states them (M8.5,
 *              journal 0284): execve and access(X_OK) fail with EACCES, mmap with PROT_EXEC
 *              with EPERM, opening a device node with EACCES
 *   swapflag   the lower's S_SWAPFILE does not outlive the lower's swap area on the layer inode
 *              (V8 of journal 0155, fixed in journal 0156): a layer inode made while its lower
 *              file is an active swap area, then swapoff on the lower, and fallocate, rename and
 *              unlink through the layer succeed, as on the lower. With the whole i_flags word
 *              copied when the layer inode is made, the VFS refuses them on the layer inode
 *              (ETXTBSY, ETXTBSY, EPERM: vfs_fallocate(), vfs_rename(), may_delete())
 * Usage: passthrough <layer dir> <lower dir>. Every case runs; exit 1 if any failed.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/swap.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <linux/fs.h>

/*
 * The statx fields compared below are newer than some C libraries' struct statx (glibc 2.39 of
 * Ubuntu 24.04, used by CI, has no stx_subvol), so the test calls the system call with the
 * structure of the Linux 6.12 UAPI (include/uapi/linux/stat.h), 256 bytes.
 */
#ifndef STATX_DIOALIGN
#define STATX_DIOALIGN 0x00002000U
#endif
#ifndef STATX_SUBVOL
#define STATX_SUBVOL 0x00008000U
#endif
#ifndef STATX_WRITE_ATOMIC
#define STATX_WRITE_ATOMIC 0x00010000U
#endif
struct pt_statx_ts {
	int64_t tv_sec;
	uint32_t tv_nsec;
	int32_t reserved;
};
struct pt_statx {
	uint32_t stx_mask, stx_blksize;
	uint64_t stx_attributes;
	uint32_t stx_nlink, stx_uid, stx_gid;
	uint16_t stx_mode, spare0;
	uint64_t stx_ino, stx_size, stx_blocks, stx_attributes_mask;
	struct pt_statx_ts stx_atime, stx_btime, stx_ctime, stx_mtime;
	uint32_t stx_rdev_major, stx_rdev_minor, stx_dev_major, stx_dev_minor;
	uint64_t stx_mnt_id;
	uint32_t stx_dio_mem_align, stx_dio_offset_align;
	uint64_t stx_subvol;
	uint32_t stx_atomic_write_unit_min, stx_atomic_write_unit_max, stx_atomic_write_segments_max;
	uint32_t spare1;
	uint64_t spare3[9];
};
_Static_assert(sizeof(struct pt_statx) == 256, "struct statx of the Linux 6.12 UAPI");

static int pt_statx(const char *path, unsigned int mask, struct pt_statx *st)
{
	return (int)syscall(SYS_statx, AT_FDCWD, path, 0, mask, st);
}

static const char *layer, *lower;
static char lp[4096], up[4096];

static void paths(const char *name)
{
	snprintf(up, sizeof(up), "%s/%s", layer, name);
	snprintf(lp, sizeof(lp), "%s/%s", lower, name);
}

/* Write size bytes through the layer, flush them and close: clean pages that advice can drop. */
static int fill(const char *path, size_t size)
{
	static char buf[1 << 16];
	size_t done;
	int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);

	if (fd < 0)
		return -1;
	memset(buf, 'm', sizeof(buf));
	for (done = 0; done < size; done += sizeof(buf))
		if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
			close(fd);
			return -1;
		}
	if (fsync(fd)) {
		close(fd);
		return -1;
	}
	return close(fd);
}

/* Pages of the lower file in its page cache. */
static int resident(size_t size)
{
	long pg = sysconf(_SC_PAGESIZE);
	size_t n = (size + pg - 1) / pg, i, in = 0;
	unsigned char *vec = calloc(n, 1);
	int fd = open(lp, O_RDONLY);
	void *map;

	if (!vec || fd < 0)
		goto fail;
	map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		goto fail;
	if (mincore(map, size, vec)) {
		munmap(map, size);
		goto fail;
	}
	for (i = 0; i < n; i++)
		in += vec[i] & 1;
	munmap(map, size);
	close(fd);
	free(vec);
	return (int)in;
fail:
	perror("resident");
	if (fd >= 0)
		close(fd);
	free(vec);
	return -1;
}

static int t_fadvise(void)
{
	const size_t size = 16 << 20;
	static char buf[1 << 16];
	const int pages = (int)(size / (size_t)sysconf(_SC_PAGESIZE));
	int fd, lfd, before, after, ra = -1, i, ok = 1;

	paths("pt-fadvise");
	if (fill(up, size))
		return perror("fadvise fill"), 0;
	fd = open(up, O_RDONLY);
	if (fd < 0)
		return perror("fadvise open"), 0;
	while (read(fd, buf, sizeof(buf)) > 0)
		;
	before = resident(size);
	if (posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED))
		perror("posix_fadvise");
	after = resident(size);
	printf("PT_FADVISE pages=%d resident_before=%d after_dontneed=%d\n", pages, before, after);
	if (before < pages / 2 || after < 0 || after > pages / 10) {
		puts("PT_FADVISE_FAIL");
		ok = 0;
	} else {
		puts("PT_FADVISE_PASS");
	}
	/* empty the lower cache through the lower itself, so that readahead starts from nothing */
	lfd = open(lp, O_RDONLY);
	if (lfd >= 0) {
		posix_fadvise(lfd, 0, 0, POSIX_FADV_DONTNEED);
		close(lfd);
	}
	before = resident(size);
	if (readahead(fd, 0, 4 << 20))
		perror("readahead");
	for (i = 0; i < 40; i++) {	/* the reads were submitted; wait up to 2 s for them */
		ra = resident(size);
		if (ra >= 16)
			break;
		usleep(50000);
	}
	/* readahead(2) reads at most the device's readahead window: ask for a few pages only */
	printf("PT_READAHEAD pages=%d resident_before=%d after_readahead=%d\n", pages, before, ra);
	if (before < 0 || before > pages / 10 || ra < 16) {
		puts("PT_READAHEAD_FAIL");
		ok = 0;
	} else {
		puts("PT_READAHEAD_PASS");
	}
	close(fd);
	unlink(up);
	return ok;
}

#define PT_ATTRS (STATX_ATTR_ENCRYPTED | STATX_ATTR_VERITY | STATX_ATTR_DAX)

static int t_statx(void)
{
	const unsigned int mask = STATX_BASIC_STATS | STATX_BTIME | STATX_DIOALIGN | STATX_SUBVOL |
				  STATX_WRITE_ATOMIC;
	const unsigned int extra = STATX_DIOALIGN | STATX_SUBVOL | STATX_WRITE_ATOMIC;
	struct pt_statx a, b;
	int ok;

	paths("pt-statx");
	if (fill(up, 1 << 16))
		return perror("statx fill"), 0;
	if (pt_statx(lp, mask, &a) || pt_statx(up, mask, &b)) {
		perror("statx");
		unlink(up);
		return 0;
	}
	printf("PT_STATX lower mask=%#x dio=%u/%u subvol=%llu awu=%u/%u/%u attrs=%#llx/%#llx\n",
	       a.stx_mask & extra, a.stx_dio_mem_align, a.stx_dio_offset_align,
	       (unsigned long long)a.stx_subvol, a.stx_atomic_write_unit_min,
	       a.stx_atomic_write_unit_max, a.stx_atomic_write_segments_max,
	       (unsigned long long)(a.stx_attributes & PT_ATTRS),
	       (unsigned long long)(a.stx_attributes_mask & PT_ATTRS));
	printf("PT_STATX layer mask=%#x dio=%u/%u subvol=%llu awu=%u/%u/%u attrs=%#llx/%#llx\n",
	       b.stx_mask & extra, b.stx_dio_mem_align, b.stx_dio_offset_align,
	       (unsigned long long)b.stx_subvol, b.stx_atomic_write_unit_min,
	       b.stx_atomic_write_unit_max, b.stx_atomic_write_segments_max,
	       (unsigned long long)(b.stx_attributes & PT_ATTRS),
	       (unsigned long long)(b.stx_attributes_mask & PT_ATTRS));
	ok = (a.stx_mask & extra) == (b.stx_mask & extra) &&
	     (a.stx_attributes & PT_ATTRS) == (b.stx_attributes & PT_ATTRS) &&
	     (a.stx_attributes_mask & PT_ATTRS) == (b.stx_attributes_mask & PT_ATTRS);
	if (ok && (a.stx_mask & STATX_DIOALIGN))
		ok = a.stx_dio_mem_align == b.stx_dio_mem_align &&
		     a.stx_dio_offset_align == b.stx_dio_offset_align;
	if (ok && (a.stx_mask & STATX_SUBVOL))
		ok = a.stx_subvol == b.stx_subvol;
	if (ok && (a.stx_mask & STATX_WRITE_ATOMIC))
		ok = a.stx_atomic_write_unit_min == b.stx_atomic_write_unit_min &&
		     a.stx_atomic_write_unit_max == b.stx_atomic_write_unit_max &&
		     a.stx_atomic_write_segments_max == b.stx_atomic_write_segments_max;
	/* the comparison means nothing if the lower reported none of these fields */
	if (ok && !(a.stx_mask & extra) && !(a.stx_attributes_mask & PT_ATTRS)) {
		puts("PT_STATX_EMPTY lower reports none of the fields");
		ok = 0;
	}
	puts(ok ? "PT_STATX_PASS" : "PT_STATX_FAIL");
	unlink(up);
	return ok;
}

static long long freed_bytes(const struct statvfs *s0)
{
	struct statvfs s;
	int fd = open(lower, O_RDONLY | O_DIRECTORY);

	if (fd < 0)
		return -1;
	syncfs(fd);
	close(fd);
	if (statvfs(lower, &s))
		return -1;
	return ((long long)s.f_bfree - (long long)s0->f_bfree) * (long long)s.f_frsize;
}

static int t_dropinode(void)
{
	const size_t size = 32 << 20;
	struct statvfs s0;
	struct stat st;
	long long freed = -1;
	int i, fd;

	paths("pt-drop");
	if (fill(up, size))
		return perror("drop fill"), 0;
	if (stat(up, &st))	/* the layer has the name and its inode in memory */
		return perror("drop stat"), 0;
	fd = open(lower, O_RDONLY | O_DIRECTORY);
	if (fd >= 0) {
		syncfs(fd);
		close(fd);
	}
	if (statvfs(lower, &s0))
		return perror("drop statvfs"), 0;
	if (unlink(lp))	/* removed behind the layer */
		return perror("drop unlink"), 0;
	if (stat(up, &st) == 0 || errno != ENOENT) {
		puts("PT_DROP_INODE_FAIL layer still shows the removed file");
		return 0;
	}
	for (i = 0; i < 50; i++) {	/* up to 10 s for the lower to account the free blocks */
		freed = freed_bytes(&s0);
		if (freed >= (long long)size * 3 / 4)
			break;
		usleep(200000);
	}
	printf("PT_DROP_INODE freed=%lld of %zu bytes\n", freed, size);
	if (freed < (long long)size * 3 / 4) {
		puts("PT_DROP_INODE_FAIL");
		return 0;
	}
	puts("PT_DROP_INODE_PASS");
	return 1;
}

static long long lower_atime(void)
{
	struct stat st;

	return stat(lp, &st) ? -1 : (long long)st.st_atim.tv_sec;
}

static int t_noatime(void)
{
	const struct timespec ts[2] = { { 1000, 0 }, { 0, UTIME_NOW } };
	long long set, reset;
	char c;
	int fd, flags, ok;

	paths("pt-noatime");
	if (fill(up, 1 << 16))
		return perror("noatime fill"), 0;
	fd = open(up, O_RDONLY);
	if (fd < 0 || utimensat(AT_FDCWD, lp, ts, 0)) {
		perror("noatime setup");
		return 0;
	}
	flags = fcntl(fd, F_GETFL);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NOATIME)) {
		perror("fcntl set O_NOATIME");
		return 0;
	}
	if (pread(fd, &c, 1, 0) != 1)
		perror("pread");
	set = lower_atime();
	/* control: once the flag is cleared again, the same read moves the lower's access time */
	if (fcntl(fd, F_SETFL, flags & ~O_NOATIME))
		perror("fcntl clear O_NOATIME");
	if (pread(fd, &c, 1, 0) != 1)
		perror("pread");
	reset = lower_atime();
	close(fd);
	printf("PT_NOATIME lower_atime set=%lld cleared=%lld (planted 1000)\n", set, reset);
	ok = set == 1000 && reset != 1000;
	puts(ok ? "PT_NOATIME_PASS" : "PT_NOATIME_FAIL");
	unlink(up);
	return ok;
}

#ifndef SYS_cachestat
#define SYS_cachestat 451	/* the same number on every architecture (Linux 6.5) */
#endif
struct pt_cachestat_range {
	uint64_t off, len;
};
struct pt_cachestat {
	uint64_t nr_cache, nr_dirty, nr_writeback, nr_evicted, nr_recently_evicted;
};

static int cstat(int fd, struct pt_cachestat *cs)
{
	struct pt_cachestat_range r = { 0, 0 };	/* len 0: to the end of the file */

	memset(cs, 0, sizeof(*cs));
	return (int)syscall(SYS_cachestat, fd, &r, cs, 0);
}

/* Accepted limits V2 and V1 (cachestat): if one is ever lifted, this fails and the documents change. */
static int t_limits(void)
{
	static char buf[1 << 16];
	struct pt_cachestat lay, low, after;
	int ufd, lfd, i, r, ok;

	paths("pt-limits");
	ufd = open(up, O_CREAT | O_EXCL | O_RDWR, 0644);
	lfd = ufd < 0 ? -1 : open(lp, O_RDONLY);
	if (ufd < 0 || lfd < 0) {
		perror("limits open");
		return 0;
	}
	memset(buf, 'l', sizeof(buf));
	for (i = 0; i < 16; i++)	/* 1 MiB, left dirty: no fsync */
		if (write(ufd, buf, sizeof(buf)) != (ssize_t)sizeof(buf))
			return perror("limits write"), 0;
	r = (int)sync_file_range(ufd, 0, 0, SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE |
				 SYNC_FILE_RANGE_WAIT_AFTER);
	if (cstat(ufd, &lay) || cstat(lfd, &low)) {
		perror("cachestat");
		return 0;
	}
	/* control: the same call on the lower descriptor writes the pages back */
	if (sync_file_range(lfd, 0, 0, SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE |
			    SYNC_FILE_RANGE_WAIT_AFTER) || cstat(lfd, &after))
		perror("lower sync_file_range");
	printf("PT_LIMITS sync_file_range=%d layer_cache=%llu lower_cache=%llu lower_dirty=%llu "
	       "lower_dirty_after_lower_sync=%llu\n", r, (unsigned long long)lay.nr_cache,
	       (unsigned long long)low.nr_cache, (unsigned long long)low.nr_dirty,
	       (unsigned long long)after.nr_dirty);
	ok = r == 0 && lay.nr_cache == 0 && low.nr_cache > 0 && low.nr_dirty > 0 && after.nr_dirty == 0;
	puts(ok ? "PT_LIMITS_PASS" : "PT_LIMITS_FAIL");
	close(ufd);
	close(lfd);
	unlink(up);
	return ok;
}

/* SB_I_NOEXEC and SB_I_NODEV (M8.5): the errnos docs/when-not-to-use.md §3 gives. */
static int t_noexec(void)
{
	char dev[4096];
	char *const argv[] = { up, NULL };
	int fd, ok = 1, st, e;
	pid_t pid;
	void *m;

	paths("pt-noexec");
	fd = open(up, O_CREAT | O_EXCL | O_WRONLY, 0755);
	if (fd < 0 || write(fd, "#!/bin/sh\nexit 0\n", 17) != 17 || close(fd))
		return perror("noexec create"), 0;
	errno = 0;
	if (!access(up, X_OK) || errno != EACCES) {
		printf("PT_NOEXEC access(X_OK): %s\n", strerror(errno));
		ok = 0;
	}
	pid = fork();
	if (pid == 0) {
		execve(up, argv, NULL);
		_exit(errno == EACCES ? 0 : 1);
	}
	if (pid < 0 || waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st)) {
		puts("PT_NOEXEC execve: not refused with EACCES");
		ok = 0;
	}
	fd = open(up, O_RDONLY);
	m = fd < 0 ? MAP_FAILED : mmap(NULL, 4096, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0);
	e = errno;
	if (m != MAP_FAILED || e != EPERM) {
		printf("PT_NOEXEC mmap(PROT_EXEC): %s\n", m == MAP_FAILED ? strerror(e) : "mapped");
		ok = 0;
	}
	if (m != MAP_FAILED)
		munmap(m, 4096);
	if (fd >= 0)
		close(fd);
	unlink(up);
	/* a character device made on the lower (the layer has no mknod of devices to test) */
	snprintf(dev, sizeof(dev), "%s/pt-noexec-null", lower);
	if (mknod(dev, S_IFCHR | 0666, makedev(1, 3))) {
		perror("mknod on the lower");
		ok = 0;
	} else {
		snprintf(dev, sizeof(dev), "%s/pt-noexec-null", layer);
		errno = 0;
		fd = open(dev, O_RDWR);
		if (fd >= 0 || errno != EACCES) {
			printf("PT_NOEXEC device open: %s\n", fd >= 0 ? "opened" : strerror(errno));
			ok = 0;
		}
		if (fd >= 0)
			close(fd);
		snprintf(dev, sizeof(dev), "%s/pt-noexec-null", lower);
		unlink(dev);
	}
	puts(ok ? "PT_NOEXEC_PASS" : "PT_NOEXEC_FAIL");
	return ok;
}

/*
 * A swap area of npages pages in an open file: the header of union swap_header
 * (include/linux/swap.h of 6.12): version 1 and last_page at byte 1024, the SWAPSPACE2 magic in
 * the last ten bytes of the first page, as read_swap_header() of mm/swapfile.c checks them.
 */
static int swap_area(int fd, long pg, unsigned int npages)
{
	char *page = calloc(1, (size_t)pg);
	uint32_t info[3] = { 1, npages - 1, 0 };	/* version, last_page, nr_badpages */
	unsigned int i;
	int err = -1;

	if (!page)
		return -1;
	memcpy(page + 1024, info, sizeof(info));
	memcpy(page + pg - 10, "SWAPSPACE2", 10);
	if (write(fd, page, (size_t)pg) != (ssize_t)pg)
		goto out;
	memset(page, 0, (size_t)pg);
	for (i = 1; i < npages; i++)	/* written, not allocated: swapon refuses holes */
		if (write(fd, page, (size_t)pg) != (ssize_t)pg)
			goto out;
	err = fsync(fd);
out:
	free(page);
	return err;
}

/* errno of opening path for writing and growing it by one page, 0 if both succeed */
static int grow_errno(const char *path, long pg)
{
	int fd = open(path, O_WRONLY), e = 0;

	if (fd < 0)
		return errno;
	if (fallocate(fd, 0, 0, 65 * pg))
		e = errno;
	close(fd);
	return e;
}

static int t_swapflag(void)
{
	char up2[4096];
	long pg = sysconf(_SC_PAGESIZE);
	struct stat st;
	struct statfs sf;
	int fd, pin, flags, ok = 1, e_active, e_fall, e_ren, e_unl;

	paths("pt-swapflag");
	snprintf(up2, sizeof(up2), "%s/pt-swapflag-moved", layer);
	/* made on the lower, so that the layer has no inode for it before swapon */
	fd = open(lp, O_CREAT | O_EXCL | O_WRONLY, 0600);
	if (fd < 0)
		return perror("swapflag create"), 0;
	/* btrfs makes a swap area only of a nocow file, set while the file is empty */
	if (!fstatfs(fd, &sf) && sf.f_type == 0x9123683e) {	/* BTRFS_SUPER_MAGIC */
		if (ioctl(fd, FS_IOC_GETFLAGS, &flags))
			return perror("swapflag getflags"), 0;
		flags |= FS_NOCOW_FL;
		if (ioctl(fd, FS_IOC_SETFLAGS, &flags))
			return perror("swapflag nocow"), 0;
	}
	if (swap_area(fd, pg, 64) || close(fd))
		return perror("swapflag swap area"), 0;
	if (swapon(lp, 0)) {
		perror("swapon on the lower");
		unlink(lp);
		puts("PT_SWAPFLAG_FAIL swapon");
		return 0;
	}
	/* the layer's inode is made now, while the lower inode carries S_SWAPFILE */
	if (stat(up, &st) || (pin = open(up, O_RDONLY)) < 0) {
		perror("swapflag layer lookup");
		swapoff(lp);
		unlink(lp);
		return 0;
	}
	/* control: while the area is active the refusal is the lower's, through the layer */
	e_active = grow_errno(up, pg);
	if (swapoff(lp)) {
		perror("swapoff on the lower");
		close(pin);
		return 0;
	}
	/* the same layer inode (pinned by the descriptor); the lower is no longer a swap area */
	e_fall = grow_errno(up, pg);
	e_ren = rename(up, up2) ? errno : 0;
	e_unl = unlink(e_ren ? up : up2) ? errno : 0;
	close(pin);
	printf("PT_SWAPFLAG active_fallocate=%s after_swapoff: fallocate=%s rename=%s unlink=%s\n",
	       e_active ? strerror(e_active) : "0", e_fall ? strerror(e_fall) : "0",
	       e_ren ? strerror(e_ren) : "0", e_unl ? strerror(e_unl) : "0");
	if (e_active != ETXTBSY || e_fall || e_ren || e_unl)
		ok = 0;
	if (e_unl) {	/* leave nothing behind for the next cases */
		unlink(lp);
		snprintf(lp, sizeof(lp), "%s/pt-swapflag-moved", lower);
		unlink(lp);
	}
	puts(ok ? "PT_SWAPFLAG_PASS" : "PT_SWAPFLAG_FAIL");
	return ok;
}

int main(int argc, char **argv)
{
	int ok = 1;

	if (argc != 3)
		return 2;
	layer = argv[1];
	lower = argv[2];
	setvbuf(stdout, NULL, _IOLBF, 0);
	ok &= t_fadvise();
	ok &= t_statx();
	ok &= t_dropinode();
	ok &= t_noatime();
	ok &= t_limits();
	ok &= t_noexec();
	ok &= t_swapflag();
	if (!ok)
		return 1;
	puts("PASSTHROUGH_PASS");
	return 0;
}
