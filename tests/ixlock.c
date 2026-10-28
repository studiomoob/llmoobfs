/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Issue #117: how long instantiating and evicting layer inodes that have no index record wait
 * while another task holds the index lock (vm/suite.sh step ixlock). The caller keeps a domain
 * build running with scan_hold_us set, so the lock is held for writing almost all the time.
 *
 *   ixlock stat <dir> <files>   stats <dir>/f00000 ... (layer inodes not in memory yet);
 *                               prints "ixlock: stat_ms=<n>"
 *   ixlock drop                 writes 2 to /proc/sys/vm/drop_caches, which evicts unused
 *                               inodes; prints "ixlock: drop_ms=<n>"
 *
 * Exit 0 with the line, 2 on an error. The caller judges the times. Disposable VM only.
 */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
	char path[4096];
	long long t0 = now_ms();
	struct stat st;
	int i, n, fd;

	if (argc == 4 && !strcmp(argv[1], "stat")) {
		n = atoi(argv[3]);
		for (i = 0; i < n; i++) {
			snprintf(path, sizeof path, "%s/f%05d", argv[2], i);
			if (stat(path, &st)) {
				perror(path);
				return 2;
			}
		}
		printf("ixlock: stat_ms=%lld\n", now_ms() - t0);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "drop")) {
		fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
		if (fd < 0 || write(fd, "2\n", 2) != 2 || close(fd)) {
			perror("drop_caches");
			return 2;
		}
		printf("ixlock: drop_ms=%lld\n", now_ms() - t0);
		return 0;
	}
	fprintf(stderr, "usage: ixlock stat <dir> <files> | ixlock drop\n");
	return 2;
}
