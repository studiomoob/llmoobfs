/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * listdir: finding 0114 item 8 under SELinux (vm/selinux-guest.sh, journal 0284). Directory
 * reads through the layer run with the mounter's credentials (moobl_iterate(), ADR-0011); when
 * they ran with the caller's, SELinux judged the caller against the lower directory file, which
 * the mounter opened. The guest runs this program in a domain that may list the directory but
 * may not use a descriptor of the mounter's domain (no "fd use" on kernel_t), so the old
 * behaviour fails at iterate_dir() with EACCES and the current one does not.
 *
 *   listdir <dir>    open the directory and read it whole with getdents64;
 *                    exit 0, 10 if open fails, 11 if a read fails
 *   listdir fdprobe  control: descriptor 3, opened by the parent in the mounter's domain, must
 *                    be unusable here (SELinux replaces it with the null device at exec);
 *                    exit 0 if it reads nothing, 20 if it read data
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
	if (!strcmp(argv[1], "fdprobe"))
		return read(3, buf, sizeof(buf)) > 0 ? 20 : 0;
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
