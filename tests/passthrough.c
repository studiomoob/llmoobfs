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
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

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
	if (!ok)
		return 1;
	puts("PASSTHROUGH_PASS");
	return 0;
}
