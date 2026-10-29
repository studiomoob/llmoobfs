// SPDX-License-Identifier: GPL-2.0-only
/*
 * Measure the memcg charge of a retained QUERY2 buffer or a built index.
 * The caller creates a private cgroup and plants the domain outside it.
 *
 *   memcg-query buffer|index <cgroup> <domain>   the charge of one query (journal 0180)
 *   memcg-query hold <cgroup> <domain>           S9 (journal 0235): as uid 1001, open <domain>
 *       again and again and query it with the maximum limit, keeping every descriptor and so
 *       its retained buffer, until a query fails (at most 64 descriptors). The cgroup must
 *       have a memory.max. Passes
 *       only if that failure is ENOMEM with the request untouched; then closes two descriptors,
 *       prints MEMCG_HOLD_CAPPED and waits for SIGTERM, still holding the rest.
 *   memcg-query build <cgroup> <domain>          S9: as uid 1001, one QUERY2 that has to build
 *       <domain> inside a cgroup too small for its index. Passes only on ENOMEM with the
 *       request untouched.
 * The last two set oom_score_adj to -1000 first: the memory cgroup's OOM killer then finds no
 * victim and the charge fails with ENOMEM (Linux 6.12.112 mm/memcontrol.c try_charge_memcg(),
 * mm/oom_kill.c out_of_memory()), instead of killing the caller, which would also free what
 * it holds.
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

static int join_cgroup(const char *cg)
{
	char path[PATH_MAX], pid[32];
	int fd, n;

	n = snprintf(path, sizeof(path), "%s/cgroup.procs", cg);
	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	n = snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	if (n < 0 || (size_t)n >= sizeof(pid) || write(fd, pid, (size_t)n) != n) {
		close(fd);
		return -1;
	}
	return close(fd);
}

static int open_counter(const char *cg, const char *name)
{
	char path[PATH_MAX];
	int n = snprintf(path, sizeof(path), "%s/%s", cg, name);

	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	return open(path, O_RDONLY | O_CLOEXEC);
}

/* Unkillable by the OOM killer, then uid 1001; the cgroup must already be limited. */
static int confine(const char *cg)
{
	char max[32];
	int fd = open_counter(cg, "memory.max");
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, max, sizeof(max) - 1);
	close(fd);
	if (n <= 0 || !strncmp(max, "max", 3)) {
		fprintf(stderr, "memcg-query: %s has no memory.max\n", cg);
		return -1;
	}
	fd = open("/proc/self/oom_score_adj", O_WRONLY | O_CLOEXEC);
	if (fd < 0 || write(fd, "-1000", 5) != 5 || close(fd))
		return -1;
	if (join_cgroup(cg) || setgroups(0, NULL) || setgid(1001) || setuid(1001))
		return -1;
	return 0;
}

static void query_init(struct moob_query2 *q, void *results, __u32 limit)
{
	memset(q, 0, sizeof(*q));
	q->version = MOOB_QUERY2_VERSION;
	q->nterms = 1;
	q->limit = limit;
	q->results = (uintptr_t)results;
	strcpy(q->terms[0].name, "kind");
	q->terms[0].op = MOOB_OP_EXISTS;
	q->count = 777;
	q->truncated = 777;
}

/* A refused query leaves the caller's request as it was sent: no count, no cursor. */
static int untouched(const struct moob_query2 *q)
{
	static const unsigned char zero[MOOB_QUERY2_CURSOR];

	return q->count == 777 && q->truncated == 777 && !memcmp(q->cursor, zero, sizeof(zero));
}

/* 16 MiB holds about seven 2 MiB buffers; past this many the cgroup did not cap them */
#define HOLD_MAX 64

static int hold(const char *cg, const char *domain)
{
	static int fds[HOLD_MAX];
	struct moob_query2 q;
	uint64_t before, current;
	size_t bytes = MOOB_QUERY_MAX_LIMIT * sizeof(struct moob_query_result);
	void *results;
	int counter, held = 0, err = 0, i;

	counter = open_counter(cg, "memory.current");
	if (counter < 0 || confine(cg))
		return 1;
	/* touched now: a page fault of this process at the cgroup's limit would not fail */
	results = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (results == MAP_FAILED || read_counter(counter, NULL, &before))
		return 1;
	memset(results, 0xa5, bytes);
	while (held < HOLD_MAX) {
		int fd = open(domain, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

		if (fd < 0) {
			err = errno;
			break;
		}
		query_init(&q, results, MOOB_QUERY_MAX_LIMIT);
		if (ioctl(fd, MOOB_IOC_QUERY2, &q)) {
			err = errno;
			close(fd);
			if (!untouched(&q)) {
				fprintf(stderr, "memcg-query: refused query changed the request\n");
				return 1;
			}
			break;
		}
		if (q.count != 0 || q.truncated)
			return 1;
		fds[held++] = fd;
	}
	/* room for this process to print and wait, without leaving the cap */
	for (i = 0; i < 2 && held > 0; i++)
		close(fds[--held]);
	if (read_counter(counter, NULL, &current))
		return 1;
	printf("MEMCG_HOLD_%s: descriptors=%d errno=%d bytes_per_buffer=%zu before=%" PRIu64
	       " current=%" PRIu64 "\n", err == ENOMEM ? "CAPPED" : "FAILED", held, err, bytes,
	       before, current);
	fflush(stdout);
	if (err != ENOMEM || held < 1)
		return 1;
	for (;;)
		pause();
}

static int build(const char *cg, const char *domain)
{
	struct moob_query_result result;
	struct moob_query2 q;
	int fd;

	if (confine(cg))
		return 1;
	fd = open(domain, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return 1;
	query_init(&q, &result, 1);
	if (!ioctl(fd, MOOB_IOC_QUERY2, &q)) {
		fprintf(stderr, "memcg-query: the build fitted the cgroup (count=%u)\n", q.count);
		return 1;
	}
	if (errno != ENOMEM || !untouched(&q)) {
		fprintf(stderr, "memcg-query: build refused with errno=%d count=%u\n", errno, q.count);
		return 1;
	}
	printf("MEMCG_BUILD_CAPPED: errno=ENOMEM, request untouched\n");
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

	if (argc == 4 && !strcmp(argv[1], "hold"))
		return hold(argv[2], argv[3]);
	if (argc == 4 && !strcmp(argv[1], "build"))
		return build(argv[2], argv[3]);
	if (argc != 4 || (strcmp(argv[1], "buffer") && strcmp(argv[1], "index"))) {
		fprintf(stderr, "usage: memcg-query buffer|index|hold|build <cgroup> <domain>\n");
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
