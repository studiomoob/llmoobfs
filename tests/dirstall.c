/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * dirstall: a reader of a directory stalled inside getdents64(2) while it holds the directory's
 * lock (review finding V-c of journal 0114, journal 0278).
 *
 * iterate_dir() (fs/readdir.c) takes the directory's i_rwsem for reading and keeps it while
 * the filesystem copies each entry to the caller's buffer (filldir64). The buffer here is
 * anonymous memory registered with userfaultfd for missing pages, so the first copy faults and
 * the fault waits in handle_userfault() (fs/userfaultfd.c) until this program fills the page:
 * the directory stays locked, by a reader, for as long as it likes. A writer of that directory
 * then queues, and every later lookup that needs the lock queues behind the writer.
 *
 *   dirstall probe
 *   dirstall <directory> <ready file> <release file> <max hold, seconds>
 *
 * probe exits 0 when a userfaultfd that handles kernel-mode faults can be made (the kernel has
 * CONFIG_USERFAULTFD and the caller CAP_SYS_PTRACE, or vm.unprivileged_userfaultfd is 1), 77
 * when it cannot. The second form opens <directory>, starts getdents64 into the registered
 * buffer in a thread, creates <ready file> once the fault has arrived, waits until <release
 * file> exists (or the maximum hold has passed, a failure), fills the page and checks that
 * getdents64 then returned entries. Exit 0 only if the reader stalled and finished cleanly.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static char *buf;
static long page;
static int dirfd_;
static volatile long nread = -1;
static volatile int rerr, done;

static int uffd_open(void)
{
	struct uffdio_api api = { .api = UFFD_API };
	int fd = (int)syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK);

	if (fd < 0)
		return -errno;
	if (ioctl(fd, UFFDIO_API, &api)) {
		int e = errno;

		close(fd);
		return -e;
	}
	return fd;
}

static void *reader(void *arg)
{
	(void)arg;
	nread = syscall(SYS_getdents64, dirfd_, buf, (unsigned int)page);
	rerr = nread < 0 ? errno : 0;
	__atomic_store_n(&done, 1, __ATOMIC_RELEASE);
	return NULL;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Fills the missing page at addr with zeros; the faulting copy then proceeds. */
static int fill(int uffd, unsigned long addr)
{
	static char zero[65536];
	struct uffdio_copy cp = {
		.dst = addr & ~(unsigned long)(page - 1),
		.src = (unsigned long)zero,
		.len = (unsigned long)page,
	};

	if (ioctl(uffd, UFFDIO_COPY, &cp) && errno != EEXIST) {
		perror("dirstall: UFFDIO_COPY");
		return -1;
	}
	return 0;
}

/* Waits up to ms for a page fault on the buffer; returns its address, 0 on timeout. */
static unsigned long wait_fault(int uffd, int ms)
{
	struct pollfd p = { .fd = uffd, .events = POLLIN };
	struct uffd_msg msg;

	if (poll(&p, 1, ms) <= 0)
		return 0;
	if (read(uffd, &msg, sizeof msg) != (ssize_t)sizeof msg)
		return 0;
	if (msg.event != UFFD_EVENT_PAGEFAULT) {
		fprintf(stderr, "dirstall: unexpected userfaultfd event %u\n", msg.event);
		return 0;
	}
	return (unsigned long)msg.arg.pagefault.address;
}

int main(int argc, char **argv)
{
	struct uffdio_register reg;
	unsigned long addr;
	pthread_t t;
	double start, hold;
	int uffd, r, rc = 0;
	FILE *f;

	page = sysconf(_SC_PAGESIZE);
	if (argc == 2 && !strcmp(argv[1], "probe")) {
		char v[16] = "?";

		f = fopen("/proc/sys/vm/unprivileged_userfaultfd", "r");
		if (f) {
			if (!fgets(v, sizeof v, f))
				strcpy(v, "?");
			v[strcspn(v, "\n")] = 0;
			fclose(f);
		}
		uffd = uffd_open();
		if (uffd < 0) {
			printf("dirstall: no userfaultfd for kernel faults (%s; "
			       "vm.unprivileged_userfaultfd=%s)\n", strerror(-uffd), v);
			return 77;
		}
		printf("dirstall: userfaultfd available (vm.unprivileged_userfaultfd=%s)\n", v);
		close(uffd);
		return 0;
	}
	if (argc != 5 || page <= 0 || page > 65536)
		return 2;
	hold = atof(argv[4]);
	uffd = uffd_open();
	if (uffd < 0) {
		fprintf(stderr, "dirstall: userfaultfd: %s\n", strerror(-uffd));
		return 2;
	}
	buf = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) {
		perror("dirstall: mmap");
		return 2;
	}
	memset(&reg, 0, sizeof reg);
	reg.range.start = (unsigned long)buf;
	reg.range.len = (unsigned long)page;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &reg)) {
		perror("dirstall: UFFDIO_REGISTER");
		return 2;
	}
	dirfd_ = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd_ < 0) {
		perror(argv[1]);
		return 2;
	}
	if (pthread_create(&t, NULL, reader, NULL)) {
		fprintf(stderr, "dirstall: pthread_create failed\n");
		return 2;
	}
	addr = wait_fault(uffd, 10000);
	if (!addr || addr < (unsigned long)buf || addr >= (unsigned long)buf + (unsigned long)page) {
		/* no fault: getdents64 never reached the buffer, so nothing was held */
		fprintf(stderr, "dirstall: no fault on the buffer (done=%d nread=%ld)\n", done, nread);
		return 1;
	}
	r = open(argv[2], O_CREAT | O_WRONLY | O_CLOEXEC, 0644);
	if (r < 0 || close(r))
		return 2;
	printf("dirstall: reader stalled in getdents64 of %s\n", argv[1]);
	fflush(stdout);
	start = now();
	while (access(argv[3], F_OK)) {
		if (now() - start > hold) {
			fprintf(stderr, "dirstall: not released within %.0f s; releasing\n", hold);
			rc = 1;
			break;
		}
		if (__atomic_load_n(&done, __ATOMIC_ACQUIRE)) {
			fprintf(stderr, "dirstall: getdents64 ended while stalled\n");
			rc = 1;
			break;
		}
		usleep(50000);
	}
	if (fill(uffd, addr))
		return 1;
	/* any later fault on the same page is filled at once (none is expected) */
	while (!__atomic_load_n(&done, __ATOMIC_ACQUIRE)) {
		unsigned long a = wait_fault(uffd, 100);

		if (a && fill(uffd, a))
			return 1;
	}
	pthread_join(t, NULL);
	if (nread <= 0) {
		fprintf(stderr, "dirstall: getdents64 returned %ld (%s)\n", nread, strerror(rerr));
		return 1;
	}
	printf("dirstall: reader released after %.1f s, getdents64 returned %ld bytes\n",
	       now() - start, nread);
	return rc;
}
