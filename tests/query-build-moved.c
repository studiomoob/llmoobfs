/* SPDX-License-Identifier: GPL-2.0-only */
/* Force a directory move during QUERY2's first scan and require the moved
 * rescan to share the original work allowance. Dedicated VM and ftrace only.
 * With "any" after the domain, the query's own answer is not judged: the suite
 * then checks what the build stored for the moved directory (journal 0215). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

static void die(const char *what)
{
	perror(what);
	exit(2);
}

static void trace_write(const char *name, const char *value)
{
	char path[256];
	size_t len = strlen(value);
	int fd;

	if (snprintf(path, sizeof(path), "/sys/kernel/tracing/%s", name) >= (int)sizeof(path))
		exit(2);
	fd = open(path, O_WRONLY | O_TRUNC);
	if (fd < 0 || write(fd, value, len) != (ssize_t)len)
		die(path);
	close(fd);
}

static int load_calls(void)
{
	char line[1024];
	FILE *fp = fopen("/sys/kernel/tracing/trace", "r");
	int n = 0;

	if (!fp)
		die("trace read");
	while (fgets(line, sizeof(line), fp))
		if (strstr(line, " index_load ") || strstr(line, " index_load <-"))
			n++;
	fclose(fp);
	return n;
}

static int query_child(const char *domain, int any)
{
	struct moob_query_result result, before;
	struct moob_query2 q = {0};
	int fd, rc, saved;

	if (setgid(1001) || setuid(1001))
		die("drop query credentials");
	fd = open(domain, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		die("open domain");
	memset(&result, 0xa5, sizeof(result));
	before = result;
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = 1;
	q.results = (uint64_t)(uintptr_t)&result;
	q.count = 777;
	q.truncated = 777;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	errno = 0;
	rc = ioctl(fd, MOOB_IOC_QUERY2, &q);
	saved = errno;
	close(fd);
	if (any) {
		printf("QUERY_BUILD_MOVED_CHILD_DONE: rc=%d errno=%d\n", rc, rc ? saved : 0);
		return 0;
	}
	if (rc != -1 || saved != E2BIG || q.count != 777 || q.truncated != 777 ||
	    memcmp(&result, &before, sizeof(result)) ||
	    memcmp(q.cursor, (unsigned char[MOOB_QUERY2_CURSOR]){0}, sizeof(q.cursor))) {
		fprintf(stderr, "query-build-moved: rc=%d errno=%d count=%u\n", rc, saved, q.count);
		return 1;
	}
	printf("QUERY_BUILD_MOVED_CHILD_PASS: moved rescan returned E2BIG without output\n");
	return 0;
}

int main(int argc, char **argv)
{
	char oldpath[4096], newpath[4096], pidstr[32], go;
	int pipefd[2], status, seen = 0, i;
	pid_t pid;
	int any = argc == 3 && !strcmp(argv[2], "any");

	if (argc != 2 && !any) {
		fprintf(stderr, "usage: query-build-moved DOMAIN [any]\n");
		return 64;
	}
	setbuf(stdout, NULL);
	if (snprintf(oldpath, sizeof(oldpath), "%s/s", argv[1]) >= (int)sizeof(oldpath) ||
	    snprintf(newpath, sizeof(newpath), "%s/t", argv[1]) >= (int)sizeof(newpath))
		return 64;
	if (pipe(pipefd))
		die("pipe");
	pid = fork();
	if (pid < 0)
		die("fork");
	if (!pid) {
		close(pipefd[1]);
		if (read(pipefd[0], &go, 1) != 1)
			die("start query");
		_exit(query_child(argv[1], any));
	}
	close(pipefd[0]);
	trace_write("tracing_on", "0");
	trace_write("current_tracer", "nop");
	trace_write("set_ftrace_filter", "index_load");
	snprintf(pidstr, sizeof(pidstr), "%ld", (long)pid);
	trace_write("set_ftrace_pid", pidstr);
	trace_write("trace", "");
	trace_write("current_tracer", "function");
	trace_write("tracing_on", "1");
	if (write(pipefd[1], "x", 1) != 1)
		die("release query");
	close(pipefd[1]);
	for (i = 0; i < 10000; i++) {
		seen = load_calls();
		if (seen >= 4)
			break;
		usleep(1000);
	}
	if (seen < 4)
		die("scan did not reach a file in the moved directory");
	if (rename(oldpath, newpath))
		die("move scanned directory");
	if (waitpid(pid, &status, 0) != pid)
		die("wait query");
	trace_write("tracing_on", "0");
	trace_write("current_tracer", "nop");
	trace_write("set_ftrace_filter", "");
	trace_write("set_ftrace_pid", "");
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return 1;
	printf("QUERY_BUILD_MOVED_PASS: synchronized rename after %d index_load calls\n", seen);
	return 0;
}
