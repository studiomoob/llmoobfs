// SPDX-License-Identifier: GPL-2.0-only
/*
 * Scheduling latency across the index's long walks (0114 item 6, journal 0120; journal 0289).
 *
 *   walklatency plant <dir> <files> <attrs>
 *       makes <files> empty files in <dir>, each with <attrs> typed string attributes whose
 *       names no other file uses (user.moob.w<file>a<k>): every index name then has one entry,
 *       so a walk that gives the CPU away between names gives it away between entries too
 *   walklatency probe <max ms> <min ms> umount <target>
 *   walklatency probe <max ms> <min ms> write <file> <text>
 *       runs the action on CPU 0 while a SCHED_FIFO thread pinned to the same CPU sleeps for
 *       1 ms at a time and records how late it wakes up, as cyclictest of rt-tests measures
 *       wakeup latency. On a kernel without forced preemption (PREEMPT_NONE or
 *       PREEMPT_VOLUNTARY) a woken real-time thread gets the CPU from a task in the kernel
 *       only at a scheduling point, such as cond_resched(); a walk without one delays it by
 *       the whole walk. Prints one WALKLATENCY line. Exit 0 if the largest delay during the
 *       action is under <max ms>; 1 if it is not; 3 if the action took less than <min ms>,
 *       too short for the probe to tell a walk with scheduling points from one without; 2 on
 *       an error.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

static long long now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static int plant(const char *dir, const char *files_s, const char *attrs_s)
{
	static const char value[] = "\001v";	/* type byte 1 (string), then the value */
	long files = atol(files_s), attrs = atol(attrs_s), i, k;
	char path[4096], name[64];
	int fd;

	if (files < 1 || attrs < 1 || attrs > 64)
		return 2;
	for (i = 0; i < files; i++) {
		snprintf(path, sizeof(path), "%s/f%ld", dir, i);
		fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
		if (fd < 0)
			return perror(path), 2;
		for (k = 0; k < attrs; k++) {
			snprintf(name, sizeof(name), "user.moob.w%lda%ld", i, k);
			if (fsetxattr(fd, name, value, sizeof(value) - 1, XATTR_CREATE))
				return perror(name), 2;
		}
		close(fd);
	}
	printf("WALKLATENCY_PLANT files=%ld attrs=%ld names=%ld\n", files, attrs, files * attrs);
	return 0;
}

static volatile int stop;
static long long max_late, wakeups;

static void *probe(void *arg)
{
	long long next = now_ns(), late;
	struct timespec t;

	(void)arg;
	while (!stop) {
		next += 1000000;
		t.tv_sec = next / 1000000000LL;
		t.tv_nsec = next % 1000000000LL;
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL) == EINTR)
			;
		late = now_ns() - next;
		if (late > max_late)
			max_late = late;
		wakeups++;
		if (late > 1000000)	/* late by more than a period: start again from now */
			next = now_ns();
	}
	return NULL;
}

static int action(char **argv, int *err)
{
	int fd;
	ssize_t len;

	if (!strcmp(argv[0], "umount") && argv[1] && !argv[2])
		return *err = umount(argv[1]) ? errno : 0, 0;
	if (!strcmp(argv[0], "write") && argv[1] && argv[2] && !argv[3]) {
		fd = open(argv[1], O_WRONLY);
		if (fd < 0)
			return *err = errno, 0;
		len = (ssize_t)strlen(argv[2]);
		*err = write(fd, argv[2], (size_t)len) == len ? 0 : errno;
		close(fd);
		return 0;
	}
	return -1;
}

static int run_probe(int argc, char **argv)
{
	long long max_ms, min_ms, t0, t1, baseline;
	struct sched_param sp = { .sched_priority = 1 };
	pthread_attr_t attr;
	pthread_t th;
	cpu_set_t cpus;
	int err = 0, rc;

	if (argc < 6)
		return 2;
	max_ms = atoll(argv[2]);
	min_ms = atoll(argv[3]);
	CPU_ZERO(&cpus);
	CPU_SET(0, &cpus);
	if (sched_setaffinity(0, sizeof(cpus), &cpus))
		return perror("sched_setaffinity"), 2;
	/* the thread inherits CPU 0; its policy is set explicitly, not inherited */
	if (pthread_attr_init(&attr) ||
	    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED) ||
	    pthread_attr_setschedpolicy(&attr, SCHED_FIFO) ||
	    pthread_attr_setschedparam(&attr, &sp))
		return fputs("pthread attributes\n", stderr), 2;
	rc = pthread_create(&th, &attr, probe, NULL);
	if (rc)
		return fprintf(stderr, "pthread_create: %s\n", strerror(rc)), 2;
	usleep(200000);		/* the probe alone with an idle action: the guest's own noise */
	baseline = max_late;
	max_late = 0;
	wakeups = 0;
	t0 = now_ns();
	if (action(argv + 4, &err))
		return fputs("usage: probe <max ms> <min ms> umount <target> | write <file> <text>\n",
			     stderr), 2;
	t1 = now_ns();
	stop = 1;
	pthread_join(th, NULL);
	printf("WALKLATENCY action=%s errno=%d action_ms=%lld max_latency_ms=%lld.%03lld "
	       "baseline_ms=%lld.%03lld wakeups=%lld limit_ms=%lld\n", argv[4], err,
	       (t1 - t0) / 1000000, max_late / 1000000, max_late / 1000 % 1000,
	       baseline / 1000000, baseline / 1000 % 1000, wakeups, max_ms);
	if (err)
		return fprintf(stderr, "%s: %s\n", argv[4], strerror(err)), 2;
	if ((t1 - t0) / 1000000 < min_ms)
		return 3;
	return max_late >= max_ms * 1000000 ? 1 : 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc == 5 && !strcmp(argv[1], "plant"))
		return plant(argv[2], argv[3], argv[4]);
	if (argc >= 6 && !strcmp(argv[1], "probe"))
		return run_probe(argc, argv);
	fputs("usage: walklatency plant <dir> <files> <attrs> | "
	      "probe <max ms> <min ms> umount <target> | probe <max ms> <min ms> write <file> <text>\n",
	      stderr);
	return 2;
}
