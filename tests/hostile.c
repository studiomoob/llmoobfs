/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Adversarial lower trees for the MoobFS layer (security roadmap S4, ADR-0011 promise 5): what a
 * process with direct access to the lower directory could leave there. Deterministic per seed.
 *
 *   hostile plant <lower-dir> <seed> <files>    files with valid and invalid user.moob.* attributes
 *   hostile mutate <lower-dir> <seed> <files>   change, add and remove attributes of those files,
 *                                               and unlink some: to run behind a mounted layer
 *   hostile probe <dir>                         read everything back: stat, listxattr and getxattr
 *                                               of every entry; fails only if a call hangs or the
 *                                               tree cannot be walked
 *   hostile marker <lower-dir> <text>|--size N  set user.moob.format on the lower root to the
 *                                               text, or to N bytes of '1'
 *
 * Names: a pool the queries of vm/suite.sh ask for (k0..k5) and names the layer must skip
 * (upper case, other punctuation, leading, trailing and double dots, 65 bytes or more).
 * Values: every valid type, and for each rule of moob_attr_validate one value that breaks it
 * (no bytes at all, unknown type byte, short and long numbers, a bool of 2, a NaN, bad UTF-8, a
 * string of 1 025 bytes, raw of 4 097, and values near the 64 KiB limit of an extended
 * attribute). Some files get more than the 64 attributes the layer allows per file.
 * A call the lower filesystem refuses (name or value too large for it) is not an error here.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

static uint64_t state;

static uint64_t next(void)
{
	uint64_t z = (state += 0x9e3779b97f4a7c15ULL);

	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

static unsigned pick(unsigned n) { return (unsigned)(next() % n); }

static unsigned long planted, refused;

static void set(const char *path, const char *name, const unsigned char *value, size_t size)
{
	char full[512];

	snprintf(full, sizeof full, "user.moob.%s", name);
	if (lsetxattr(path, full, value, size, 0)) refused++; else planted++;
}

/* A name for attribute slot `slot` of a file: mostly from the pool, sometimes one to be skipped. */
static void make_name(char *out, size_t cap, unsigned slot)
{
	static const char *const bad[] = {
		"Kind", "k-0", "k 0", ".k0", "k0.", "k..0", "k/0", "k\t0", "k\xc3\xa9", "K0", "",
		"k\"0", "k\\0", "k\"}, {\"path\": \"x",
	};
	unsigned r = pick(10);

	if (r < 6) {
		snprintf(out, cap, "k%u", pick(6));
	} else if (r < 8) {
		snprintf(out, cap, "x%u.y%u", slot, pick(4));	/* valid, outside the pool */
	} else if (r < 9) {
		snprintf(out, cap, "%s", bad[pick(sizeof bad / sizeof bad[0])]);
	} else {
		size_t n = 65 + pick(120);	/* longer than MOOB_ATTR_NAME_MAX */

		if (n >= cap) n = cap - 1;
		memset(out, 'a' + (int)pick(26), n);
		out[n] = 0;
	}
}

/* Fills value with one typed attribute, valid or broken; returns its size. */
static size_t make_value(unsigned char *v, size_t cap)
{
	static const size_t big[] = { 1026, 4098, 4099, 8192, 32768, 65535, 65536 };
	unsigned r = pick(24), i;
	size_t n;

	memset(v, 0, 16);
	switch (r) {
	case 0: v[0] = 1; n = 1 + pick(40); for (i = 1; i < n; i++) v[i] = (unsigned char)('a' + pick(26)); return n;
	case 1: v[0] = 1; v[1] = (unsigned char)('a' + pick(3)); return 2;	/* few distinct strings */
	case 2: v[0] = 2; v[1] = (unsigned char)pick(8); return 9;
	case 3: v[0] = 3; v[7] = (unsigned char)pick(4); v[8] = 0x40; return 9;
	case 4: v[0] = 4; v[1] = (unsigned char)pick(2); return 2;
	case 5: v[0] = 5; v[1] = (unsigned char)pick(200); v[5] = 1; return 9;
	case 6: v[0] = 6; n = 1 + pick(300); for (i = 1; i < n; i++) v[i] = (unsigned char)next(); return n;
	case 7: v[0] = 1; return 1;						/* empty string: valid */
	case 8: return 0;							/* no type byte */
	case 9: v[0] = (unsigned char)(pick(2) ? 0 : 7 + pick(249)); return 1 + pick(12);	/* unknown type */
	case 10: v[0] = (unsigned char)(pick(2) ? 2 : 5); return 1 + pick(8);	/* number too short */
	case 11: v[0] = (unsigned char)(2 + pick(2)); return 10 + pick(8);	/* number too long */
	case 12: v[0] = 4; v[1] = (unsigned char)(2 + pick(250)); return 2;	/* bool out of range */
	case 13: v[0] = 4; return 1 + 2 * pick(2);				/* bool of 0 or 2 payload bytes */
	case 14: v[0] = 3; memset(v + 1, 0xff, 8); return 9;			/* NaN */
	case 15: v[0] = 1; v[1] = 0xc3; v[2] = 0x28; return 3;			/* bad UTF-8 */
	case 16: v[0] = 1; v[1] = 'a'; v[2] = 0; v[3] = 'b'; return 4;		/* NUL inside a string */
	case 17: v[0] = 1; v[1] = 0xed; v[2] = 0xa0; v[3] = 0x80; return 4;	/* UTF-16 surrogate */
	case 18: v[0] = 1; v[1] = 0xf4; v[2] = 0x90; v[3] = 0x80; v[4] = 0x80; return 5;	/* above U+10FFFF */
	case 19: v[0] = 1; v[1] = 0xe2; v[2] = 0x82; return 3;			/* truncated sequence */
	default:
		n = big[pick(sizeof big / sizeof big[0])];
		if (n > cap) n = cap;
		memset(v, 'z', n);
		v[0] = (unsigned char)(pick(2) ? 1 : 6);	/* string or raw beyond its limit */
		return n;
	}
}

static void file_path(char *out, size_t cap, const char *root, unsigned k)
{
	snprintf(out, cap, "%s/d%u/e%u/f%u", root, k % 5, k % 3, k);
}

static int plant(const char *root, unsigned files, int mutate)
{
	static unsigned char value[65536];
	char path[4096], name[256];
	unsigned k, a;

	for (k = 0; k < files; k++) {
		unsigned nattrs = pick(12) ? pick(9) : 60 + pick(30);	/* some files beyond 64 */
		int fd;

		snprintf(path, sizeof path, "%s/d%u", root, k % 5); mkdir(path, 0755);
		snprintf(path, sizeof path, "%s/d%u/e%u", root, k % 5, k % 3); mkdir(path, 0755);
		file_path(path, sizeof path, root, k);
		if (mutate) {
			unsigned what = pick(8);

			if (what == 0) { unlink(path); continue; }
			if (what == 1) {	/* drop every attribute */
				static char list[65536];
				ssize_t len = llistxattr(path, list, sizeof list), off;

				for (off = 0; off < len; off += (ssize_t)strlen(list + off) + 1)
					lremovexattr(path, list + off);
				continue;
			}
		}
		fd = open(path, O_WRONLY | O_CREAT, 0644);
		if (fd < 0) { perror(path); return 1; }
		if (write(fd, "hostile\n", 8) != 8) { perror("write"); return 1; }
		close(fd);
		for (a = 0; a < nattrs; a++) {
			make_name(name, sizeof name, a);
			set(path, name, value, make_value(value, sizeof value));
		}
		if (!pick(16)) {	/* a second name for the same inode */
			char other[4096 + 8];

			snprintf(other, sizeof other, "%s.link", path);
			if (link(path, other) && errno != EEXIST) { perror("link"); return 1; }
		}
	}
	/* attributes on directories are indexed too */
	for (k = 0; k < 5; k++) {
		snprintf(path, sizeof path, "%s/d%u", root, k);
		for (a = 0; a < 6; a++) {
			make_name(name, sizeof name, a);
			set(path, name, value, make_value(value, sizeof value));
		}
	}
	printf("HOSTILE_%s: files=%u attributes_set=%lu refused_by_lower=%lu\n", mutate ? "MUTATE" : "PLANT",
	       files, planted, refused);
	return 0;
}

static unsigned long entries, attrs_read;

static int probe(const char *dir)
{
	static char list[65536], value[65536];
	DIR *d = opendir(dir);
	struct dirent *e;
	char path[4096];
	struct stat st;

	if (!d) { perror(dir); return 1; }
	while ((e = readdir(d))) {
		ssize_t len, off;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (lstat(path, &st))
			continue;	/* removed behind the mount: a stale entry may fail, it must not hang */
		entries++;
		len = llistxattr(path, list, sizeof list);
		for (off = 0; off < len; off += (ssize_t)strlen(list + off) + 1)
			if (lgetxattr(path, list + off, value, sizeof value) >= 0)
				attrs_read++;
		if (S_ISREG(st.st_mode)) {
			int fd = open(path, O_RDONLY);

			if (fd >= 0) {
				if (read(fd, value, 16) < 0) perror("read");
				close(fd);
			}
		}
		if (S_ISDIR(st.st_mode) && probe(path)) { closedir(d); return 1; }
	}
	closedir(d);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 5 && (!strcmp(argv[1], "plant") || !strcmp(argv[1], "mutate"))) {
		state = strtoull(argv[3], NULL, 10) * 2654435761ULL + (argv[1][0] == 'm');
		return plant(argv[2], (unsigned)strtoul(argv[4], NULL, 10), argv[1][0] == 'm');
	}
	if (argc == 3 && !strcmp(argv[1], "probe")) {
		if (probe(argv[2])) return 1;
		printf("HOSTILE_PROBE: entries=%lu attributes_read=%lu\n", entries, attrs_read);
		return 0;
	}
	if (argc >= 4 && !strcmp(argv[1], "marker")) {
		static char buf[4096];
		size_t n;

		if (argc == 5 && !strcmp(argv[3], "--size")) {
			n = strtoul(argv[4], NULL, 10);
			if (n > sizeof buf) return 64;
			memset(buf, '1', n);
		} else if (argc == 4) {
			n = strlen(argv[3]);
			if (n > sizeof buf) return 64;
			memcpy(buf, argv[3], n);
		} else {
			return 64;
		}
		if (lsetxattr(argv[2], "user.moob.format", buf, n, 0)) { perror("format marker"); return 1; }
		return 0;
	}
	fprintf(stderr, "usage: hostile plant|mutate <lower-dir> <seed> <files> | probe <dir> | marker <lower-dir> <text>|--size N\n");
	return 64;
}
