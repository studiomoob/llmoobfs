// SPDX-License-Identifier: GPL-2.0-only
/*
 * S9 (journal 0235): the cost of mounting the layer over a large lower directory.
 *
 *   mount-scale mount <lower dir> <target> <extra options>   mount(2) of moobfs_layer with
 *       lower=<lower dir>[,<extra options>] ("-" for none), timed; then GET_INFO
 *   mount-scale umount <target>                              umount(2), timed
 *
 * Prints one MOUNT_SCALE line with the wall time of the call, the change of the Slab and
 * SUnreclaim lines of /proc/meminfo across it (kB; other activity of the guest is included,
 * so small values are noise), and for a mount the index bytes and built domains GET_INFO
 * reports right after it (index_used is 0 for a mount with noindex, which has no counters).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <time.h>
#include <unistd.h>

#include "../include/moobfs_ioctl.h"

static int meminfo(long long *slab, long long *sunreclaim)
{
	char line[256];
	FILE *f = fopen("/proc/meminfo", "r");
	int found = 0;

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "Slab: %lld kB", slab) == 1)
			found |= 1;
		else if (sscanf(line, "SUnreclaim: %lld kB", sunreclaim) == 1)
			found |= 2;
	}
	fclose(f);
	return found == 3 ? 0 : -1;
}

static unsigned long long now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned long long)t.tv_sec * 1000000000ULL + (unsigned long long)t.tv_nsec;
}

int main(int argc, char **argv)
{
	long long slab0, sun0, slab1, sun1;
	unsigned long long t0, t1;
	char opts[4096];
	int is_mount, n, err;

	is_mount = argc == 5 && !strcmp(argv[1], "mount");
	if (!is_mount && !(argc == 3 && !strcmp(argv[1], "umount"))) {
		fprintf(stderr, "usage: mount-scale mount <lower> <target> <options|->\n"
				"       mount-scale umount <target>\n");
		return 64;
	}
	if (is_mount) {
		n = strcmp(argv[4], "-") ?
		    snprintf(opts, sizeof(opts), "lower=%s,%s", argv[2], argv[4]) :
		    snprintf(opts, sizeof(opts), "lower=%s", argv[2]);
		if (n < 0 || (size_t)n >= sizeof(opts))
			return 64;
	}
	if (meminfo(&slab0, &sun0))
		return 1;
	t0 = now_ns();
	err = is_mount ? mount("none", argv[3], "moobfs_layer", 0, opts) : umount(argv[2]);
	t1 = now_ns();
	if (err) {
		perror(argv[1]);
		return 1;
	}
	if (meminfo(&slab1, &sun1))
		return 1;
	printf("MOUNT_SCALE op=%s wall_ns=%llu slab_kb=%lld sunreclaim_kb=%lld", argv[1], t1 - t0,
	       slab1 - slab0, sun1 - sun0);
	if (is_mount) {
		struct moob_info info;
		int fd = open(argv[3], O_RDONLY | O_DIRECTORY | O_CLOEXEC);

		if (fd < 0 || ioctl(fd, MOOB_IOC_GET_INFO, &info)) {
			perror("GET_INFO");
			return 1;
		}
		close(fd);
		printf(" index_used=%" PRIu64 " domains_built=%u", (uint64_t)info.index_used,
		       info.domains_built);
	}
	printf("\n");
	return 0;
}
