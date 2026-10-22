// SPDX-License-Identifier: GPL-2.0-only
/*
 * Measure the memcg charge of a retained QUERY2 buffer or a built index.
 * The caller creates a private cgroup and plants the domain outside it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "../include/moobfs_ioctl.h"

static int read_counter(int fd, const char *key, uint64_t *value)
{
	char buf[8192];
	char *end;
	char *line;
	ssize_t n;
	unsigned long long parsed;

	if (lseek(fd, 0, SEEK_SET) < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	line = buf;
	if (key) {
		size_t len = strlen(key);

		while (*line && (strncmp(line, key, len) || line[len] != ' ')) {
			line = strchr(line, '\n');
			if (!line)
				return -1;
			line++;
		}
		if (!*line)
			return -1;
		line += len + 1;
	}
	errno = 0;
	parsed = strtoull(line, &end, 10);
	if (errno || end == line || (*end != '\n' && *end != 0))
		return -1;
	*value = parsed;
	return 0;
}

int main(int argc, char **argv)
{
	char path[PATH_MAX], pid[32];
	struct moob_query2 q = { 0 };
	uint64_t before, held, closed;
	void *results;
	size_t bytes;
	int procs, counter, domain, index_mode;
	int n;

	if (argc != 4 || (strcmp(argv[1], "buffer") && strcmp(argv[1], "index"))) {
		fprintf(stderr, "usage: memcg-query buffer|index <cgroup> <domain>\n");
		return 64;
	}
	index_mode = !strcmp(argv[1], "index");
	n = snprintf(path, sizeof(path), "%s/cgroup.procs", argv[2]);
	if (n < 0 || (size_t)n >= sizeof(path))
		return 1;
	procs = open(path, O_WRONLY | O_CLOEXEC);
	if (procs < 0)
		return 1;
	n = snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	if (n < 0 || (size_t)n >= sizeof(pid) || write(procs, pid, (size_t)n) != n)
		return 1;
	close(procs);
	n = snprintf(path, sizeof(path), "%s/%s", argv[2],
		     index_mode ? "memory.stat" : "memory.current");
	if (n < 0 || (size_t)n >= sizeof(path))
		return 1;
	counter = open(path, O_RDONLY | O_CLOEXEC);
	if (counter < 0)
		return 1;
	if (setgroups(0, NULL) || setgid(1001) || setuid(1001))
		return 1;
	domain = open(argv[3], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (domain < 0) {
		perror("open domain");
		return 1;
	}
	q.limit = index_mode ? 1 : MOOB_QUERY_MAX_LIMIT;
	bytes = q.limit * sizeof(struct moob_query_result);
	results = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (results == MAP_FAILED)
		return 1;
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.results = (uintptr_t)results;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	if (read_counter(counter, index_mode ? "slab_unreclaimable" : NULL, &before))
		return 1;
	if (ioctl(domain, MOOB_IOC_QUERY2, &q)) {
		perror("MOOB_IOC_QUERY2");
		return 1;
	}
	if (q.count != (index_mode ? 1U : 0U) ||
	    read_counter(counter, index_mode ? "slab_unreclaimable" : NULL, &held))
		return 1;
	close(domain);
	if (read_counter(counter, index_mode ? "slab_unreclaimable" : NULL, &closed))
		return 1;
	printf("MEMCG_%s_RESULT: before=%" PRIu64 " held=%" PRIu64
	       " closed=%" PRIu64 " delta=%" PRId64 " bytes=%zu\n",
	       index_mode ? "INDEX" : "BUFFER", before, held, closed,
	       (int64_t)(held - before), bytes);
	munmap(results, bytes);
	close(counter);
	/* A built index should exceed ordinary per-open metadata by this much. */
	return held >= before + (index_mode ? 131072 : bytes / 2) ? 0 : 1;
}
