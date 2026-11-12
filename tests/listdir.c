/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * listdir: a directory read through the layer under SELinux (vm/selinux-guest.sh, journal 0286).
 * Opens a directory and reads it whole with getdents64; exit 0, 10 if open fails, 11 if a read
 * fails.
 * Static, so that it runs from a labelled tmpfs without the shared-library rules.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	static char buf[1 << 15];
	long n, total = 0;
	int fd;

	if (argc != 2)
		return 64;
	fd = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		fprintf(stderr, "listdir: open %s: %s\n", argv[1], strerror(errno));
		return 10;
	}
	while ((n = syscall(SYS_getdents64, fd, buf, sizeof(buf))) > 0)
		total += n;
	if (n < 0) {
		fprintf(stderr, "listdir: getdents64 %s: %s\n", argv[1], strerror(errno));
		return 11;
	}
	printf("LISTDIR %s bytes=%ld\n", argv[1], total);
	return 0;
}
