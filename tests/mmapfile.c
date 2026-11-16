/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mmapfile: a shared mapping of a file, for the LSM runs (vm/selinux-guest.sh and
 * vm/smack-guest.sh, journal 0298).
 *   mmapfile r <file>   opens read-only, maps PROT_READ, MAP_SHARED, reads every byte;
 *   mmapfile w <file>   opens read-write, maps PROT_READ | PROT_WRITE, MAP_SHARED, writes 'M'
 *                       to the first byte and syncs it with msync(MS_SYNC).
 * Exit 0, 10 if open fails, 11 if fstat fails or the file is empty, 12 if mmap fails, 13 if
 * msync fails, 64 on bad usage. On success prints the first byte after the access.
 * Static, so that it runs from a labelled tmpfs without the shared-library rules.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	unsigned long sum = 0;
	struct stat st;
	char *p;
	off_t i;
	int fd, w;

	if (argc != 3 || (strcmp(argv[1], "r") && strcmp(argv[1], "w")))
		return 64;
	w = argv[1][0] == 'w';
	fd = open(argv[2], w ? O_RDWR : O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "mmapfile: open %s: %s\n", argv[2], strerror(errno));
		return 10;
	}
	if (fstat(fd, &st) || st.st_size == 0) {
		fprintf(stderr, "mmapfile: fstat %s: empty or %s\n", argv[2], strerror(errno));
		return 11;
	}
	p = mmap(NULL, st.st_size, PROT_READ | (w ? PROT_WRITE : 0), MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "mmapfile: mmap %s: %s\n", argv[2], strerror(errno));
		return 12;
	}
	if (w) {
		p[0] = 'M';
		if (msync(p, st.st_size, MS_SYNC)) {
			fprintf(stderr, "mmapfile: msync %s: %s\n", argv[2], strerror(errno));
			return 13;
		}
	}
	for (i = 0; i < st.st_size; i++)
		sum += (unsigned char)p[i];
	printf("MMAPFILE %s %s first=%c sum=%lu\n", argv[1], argv[2], p[0], sum);
	munmap(p, st.st_size);
	close(fd);
	return 0;
}
