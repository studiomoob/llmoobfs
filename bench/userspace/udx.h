/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * udx: the user-space baseline of the MoobFS index (docs/history/0092). Typed attributes are
 * ordinary "user.moob.*" extended attributes on a plain filesystem; every domain directory
 * carries a SQLite database "<domain>/.udx.db" that mirrors them. No kernel module.
 *
 * This header holds what udx-index (one full pass) and udx-watch (inotify follower) share:
 * the schema, the walk, the homogeneity rule and the state-based "make the rows of this path
 * equal to what is on disk now" operation. udx-query only uses the names and udx_open_ro().
 *
 * Schema (version 1), one database per domain:
 *
 *   entry(id, path, ino, kind, nlink, ok, trav)
 *       one row per file or directory below the domain (the domain directory itself and
 *       symbolic links are left out, as moobfs-index --domain does). path is relative to the
 *       domain and stored as a BLOB, because a file name is bytes, not text. ok is 1 when the
 *       entry passes the homogeneity rule of docs/spec-query-v2.md §3 as the oracle evaluates
 *       it; a query lists only ok = 1. trav (directories) says whether everyone who can
 *       traverse the domain can list and traverse this directory (ADR-0025); when it changes
 *       the subtree is evaluated again. nlink is kept to notice hard links, which fail the rule.
 *   attr(eid, name, type, v)
 *       one row per valid, indexable typed attribute (raw values and invalid values are not
 *       stored: for a query they are missing, as in the oracle). v uses SQLite's own typing:
 *       an INTEGER for int64, time and bool, an INTEGER order key for double (the IEEE bits
 *       mapped so that integer order is the oracle's total order, -0.0 below +0.0), and a
 *       BLOB for string (memcmp order, which is what the oracle and the layer use).
 *   meta(key, value)
 *       schema, state ('synced' after a complete pass, 'resync' while one is known to be
 *       needed), synced_ns, overflows.
 *
 * Indexes: attr's primary key (eid, name) answers "the attributes of this file" and the
 * per-file probes of a conjunction; attr_value (name, type, v, eid) is a covering index for
 * every operator (eq, ranges, prefix as a range, exists as a name scan) and for --order, since
 * (name, type) are fixed by the query and v, eid follow. entry's UNIQUE(path) serves updates by
 * path and subtree ranges ([p/, p0) is every path below p); entry_ino finds the other names
 * of a hard-linked inode, and the partial index entry_linked every name with more than one
 * link (normally none), for when a whole domain disappears with names nobody listed.
 */
#ifndef UDX_H
#define UDX_H
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sqlite3.h>
#include "moobfs_attr.h"
#include "ext-acl.h"

#define UDX_DB_NAME ".udx.db"
#define UDX_SCHEMA_VERSION 1
#define UDX_NAME_MAX 64U	/* MOOB_QUERY_NAME_MAX: a longer name cannot be asked for */
#define UDX_FN static __attribute__((unused))

#ifndef SQLITE_OPEN_NOFOLLOW
#define SQLITE_OPEN_NOFOLLOW 0
#endif

static const char udx_schema[] =
	"CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value) WITHOUT ROWID;"
	"CREATE TABLE IF NOT EXISTS entry("
	" id INTEGER PRIMARY KEY, path BLOB NOT NULL UNIQUE, ino INTEGER NOT NULL,"
	" kind INTEGER NOT NULL, nlink INTEGER NOT NULL, ok INTEGER NOT NULL, trav INTEGER NOT NULL);"
	"CREATE INDEX IF NOT EXISTS entry_ino ON entry(ino);"
	"CREATE INDEX IF NOT EXISTS entry_linked ON entry(nlink) WHERE nlink > 1;"
	"CREATE TABLE IF NOT EXISTS attr("
	" eid INTEGER NOT NULL REFERENCES entry(id) ON DELETE CASCADE,"
	" name TEXT NOT NULL, type INTEGER NOT NULL, v NOT NULL,"
	" PRIMARY KEY(eid, name)) WITHOUT ROWID;"
	"CREATE INDEX IF NOT EXISTS attr_value ON attr(name, type, v, eid);";

enum {
	UDX_S_SEL_ENTRY, UDX_S_INS_ENTRY, UDX_S_UPD_ENTRY, UDX_S_DEL_ENTRY, UDX_S_SEL_ATTRS,
	UDX_S_DEL_ATTRS, UDX_S_INS_ATTR, UDX_S_SEL_RANGE, UDX_S_SEL_ALL, UDX_S_SEL_INO,
	UDX_S_SEL_LINKED, UDX_S_SET_META, UDX_S_N
};

static const char *const udx_sql[UDX_S_N] = {
	"SELECT id, ino, kind, nlink, ok, trav FROM entry WHERE path = ?1",
	"INSERT INTO entry(path, ino, kind, nlink, ok, trav) VALUES(?1, ?2, ?3, ?4, ?5, ?6)",
	"UPDATE entry SET ino = ?2, kind = ?3, nlink = ?4, ok = ?5, trav = ?6 WHERE id = ?1",
	"DELETE FROM entry WHERE id = ?1",
	"SELECT name, type, v FROM attr WHERE eid = ?1 ORDER BY name",
	"DELETE FROM attr WHERE eid = ?1",
	"INSERT INTO attr(eid, name, type, v) VALUES(?1, ?2, ?3, ?4)",
	"SELECT id, ino, nlink, kind FROM entry WHERE path = ?1 OR (path >= ?2 AND path < ?3)",
	"SELECT id, ino, nlink, kind FROM entry",
	"SELECT path FROM entry WHERE ino = ?1",
	"SELECT path FROM entry WHERE nlink > 1 AND kind = 0",
	"INSERT INTO meta(key, value) VALUES(?1, ?2) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
};

struct udx_db {
	sqlite3 *db;
	char dir[PATH_MAX];	/* the domain directory */
	size_t dirlen;
	struct stat dst;	/* the domain directory, for the homogeneity rule */
	int dacl;
	sqlite3_stmt *st[UDX_S_N];
};

struct udx_attr {
	char name[UDX_NAME_MAX + 1];
	int type;
	int64_t k;			/* every type but string */
	size_t slen;
	ma_u8 s[MOOB_ATTR_STRING_MAX];
};

/* Called for every directory the walk enters, before its entries are read (udx-watch adds
 * the inotify watch here, so nothing created afterwards is missed). */
static void (*udx_dir_hook)(const char *full);
/* Inodes whose link count was or is above one: their other names must be looked at again. */
static int udx_track_links;
static uint64_t *udx_links;
static size_t udx_nlinks, udx_links_cap;
/* Ids of the rows the current walk touched; rows of the same subtree not in it are gone. */
static sqlite3_int64 *udx_seen;
static size_t udx_nseen, udx_seen_cap;
static int udx_walked;
static unsigned long udx_rows_written;

UDX_FN void udx_die(const char *what, sqlite3 *db)
{
	fprintf(stderr, "udx: %s: %s\n", what, db ? sqlite3_errmsg(db) : strerror(errno));
	exit(1);
}

UDX_FN void *udx_grow(void *p, size_t *cap, size_t need, size_t item)
{
	if (need <= *cap)
		return p;
	*cap = need * 2 > 64 ? need * 2 : 64;
	p = realloc(p, *cap * item);
	if (!p)
		udx_die("realloc", NULL);
	return p;
}

/* The order-preserving key of a double: integer order equals the oracle's value_cmp(). */
UDX_FN int64_t udx_double_key(ma_u64 bits)
{
	ma_u64 u = (bits >> 63) ? ~bits : (bits | (1ULL << 63));

	return (int64_t)(u ^ (1ULL << 63));
}

UDX_FN int udx_is_db_name(const char *name)
{
	return !strncmp(name, UDX_DB_NAME, sizeof UDX_DB_NAME - 1);
}

UDX_FN void udx_exec(sqlite3 *db, const char *sql)
{
	if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK)
		udx_die(sql, db);
}

UDX_FN void udx_step_done(struct udx_db *d, int s)
{
	if (sqlite3_step(d->st[s]) != SQLITE_DONE)
		udx_die(udx_sql[s], d->db);
	sqlite3_reset(d->st[s]);
	udx_rows_written++;
}

/* A zero-length blob bound from a NULL pointer would be SQL NULL; always pass a pointer. */
UDX_FN void udx_bind_blob(sqlite3_stmt *st, int i, const void *p, size_t n)
{
	sqlite3_bind_blob(st, i, n ? p : "", (int)n, SQLITE_TRANSIENT);
}

UDX_FN void udx_meta_text(struct udx_db *d, const char *key, const char *value)
{
	sqlite3_bind_text(d->st[UDX_S_SET_META], 1, key, -1, SQLITE_STATIC);
	sqlite3_bind_text(d->st[UDX_S_SET_META], 2, value, -1, SQLITE_STATIC);
	udx_step_done(d, UDX_S_SET_META);
}

UDX_FN void udx_meta_int(struct udx_db *d, const char *key, int64_t value)
{
	sqlite3_bind_text(d->st[UDX_S_SET_META], 1, key, -1, SQLITE_STATIC);
	sqlite3_bind_int64(d->st[UDX_S_SET_META], 2, value);
	udx_step_done(d, UDX_S_SET_META);
}

UDX_FN int64_t udx_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int udx_no_fchown(int fd, uid_t uid, gid_t gid)
{
	(void)fd; (void)uid; (void)gid;
	return 0;
}

/*
 * As root, SQLite calls fchown() on the -wal and -shm files every time it opens them, to keep
 * them with the owner of the database. Each call is an IN_ATTRIB event in the directory
 * udx-watch watches: with enough domains the events of one full pass overflow the queue, which
 * asks for another full pass, without end (seen in the test with a queue of 16 events). The
 * call is switched off through SQLite's system-call table and udx_open() sets the owner once,
 * when it differs.
 */
UDX_FN void udx_sqlite_init(void)
{
	static int done;
	sqlite3_vfs *vfs;

	if (done)
		return;
	done = 1;
	sqlite3_initialize();
	vfs = sqlite3_vfs_find(NULL);
	if (vfs && vfs->xSetSystemCall)
		vfs->xSetSystemCall(vfs, "fchown", (sqlite3_syscall_ptr)udx_no_fchown);
}

UDX_FN void udx_own(const char *db, const char *suffix, const struct stat *want)
{
	char path[PATH_MAX + 8];
	struct stat st;

	snprintf(path, sizeof path, "%s%s", db, suffix);
	if (!lstat(path, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 &&
	    (st.st_uid != want->st_uid || st.st_gid != want->st_gid) && lchown(path, want->st_uid, want->st_gid))
		perror(path);
}

UDX_FN void udx_close(struct udx_db *d)
{
	int i;

	for (i = 0; i < UDX_S_N; i++)
		sqlite3_finalize(d->st[i]);
	sqlite3_close(d->db);
	d->db = NULL;
}

/*
 * Opens (and with create, makes) the database of a domain directory for writing. The file is
 * created 0600 and, when the caller is root, given to the owner of the domain directory, so
 * that owner can query it. A database that is not a plain single-link file is refused: the
 * directory may be writable by people the indexer should not trust (see the journal).
 * Returns 0, or -1 when there is no usable database.
 */
UDX_FN int udx_open(struct udx_db *d, const char *dir, int create)
{
	char path[PATH_MAX];
	struct stat st;
	int i, fd;

	memset(d, 0, sizeof *d);
	if ((size_t)snprintf(d->dir, sizeof d->dir, "%s", dir) >= sizeof d->dir - sizeof UDX_DB_NAME - 8)
		return -1;
	d->dirlen = strlen(d->dir);
	if (stat(dir, &d->dst) || !S_ISDIR(d->dst.st_mode))
		return -1;
	{	/* the domain directory as the oracle judges it: followed, as stat() above */
		static unsigned char acl[65536];
		ssize_t n = getxattr(dir, "system.posix_acl_access", acl, sizeof acl);

		d->dacl = moob_ext_acl_bytes(acl, n, n < 0 ? errno : 0, d->dst.st_mode);
	}
	snprintf(path, sizeof path, "%s/%s", dir, UDX_DB_NAME);
	if (create) {
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
		if (fd >= 0) {
			if (geteuid() == 0 && fchown(fd, d->dst.st_uid, d->dst.st_gid))
				perror("udx: fchown");
			close(fd);
		} else if (errno != EEXIST) {
			fprintf(stderr, "udx: %s: %s\n", path, strerror(errno));
			return -1;
		}
	}
	if (lstat(path, &st))
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_nlink != 1 ||
	    (st.st_uid != d->dst.st_uid && st.st_uid != geteuid())) {
		fprintf(stderr, "udx: %s: not a plain file of the domain owner, refused\n", path);
		return -1;
	}
	/* the domain changed hands: root gives the index to the new owner, so they can ask */
	if (geteuid() == 0 && (st.st_uid != d->dst.st_uid || st.st_gid != d->dst.st_gid)) {
		if (lchown(path, d->dst.st_uid, d->dst.st_gid))
			perror(path);
		st.st_uid = d->dst.st_uid;
		st.st_gid = d->dst.st_gid;
	}
	udx_sqlite_init();
	if (sqlite3_open_v2(path, &d->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, NULL) != SQLITE_OK) {
		fprintf(stderr, "udx: %s: %s\n", path, sqlite3_errmsg(d->db));
		sqlite3_close(d->db);
		return -1;
	}
	sqlite3_busy_timeout(d->db, 10000);
	/* the -wal and -shm files stay when the last connection closes: making and removing them
	 * are events in the very directory udx-watch watches, and it would wake itself */
	i = 1;
	sqlite3_file_control(d->db, NULL, SQLITE_FCNTL_PERSIST_WAL, &i);
	/* WAL: a reader never blocks the writer; NORMAL: a crash can lose the last transactions,
	 * which the next full pass puts back, since the files are the truth and not this database */
	udx_exec(d->db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;");
	udx_exec(d->db, "BEGIN IMMEDIATE");
	udx_exec(d->db, udx_schema);
	udx_exec(d->db, "INSERT OR IGNORE INTO meta(key, value) VALUES('schema', 1), ('overflows', 0), ('state', 'resync')");
	udx_exec(d->db, "COMMIT");
	if (geteuid() == 0) {
		udx_own(path, "-wal", &st);
		udx_own(path, "-shm", &st);
	}
	for (i = 0; i < UDX_S_N; i++)
		if (sqlite3_prepare_v2(d->db, udx_sql[i], -1, &d->st[i], NULL) != SQLITE_OK)
			udx_die(udx_sql[i], d->db);
	return 0;
}

/*
 * The oracle's reach_ok() (tools/moobfs-index.c): can everyone who can traverse the domain read
 * this file (want_read), or list and traverse this directory? Since ADR-0025 a search-only
 * directory keeps its names from those who cannot list it, so an intermediate directory needs
 * read and search for every class the domain lets through; every class is judged, never owner
 * or group by the bits of others, and an ACL counts only when it is extended (tools/ext-acl.h).
 */
UDX_FN int udx_reach(const struct udx_db *d, const char *full, const struct stat *st, int want_read)
{
	mode_t u = want_read ? S_IRUSR : S_IRUSR | S_IXUSR;
	mode_t g = want_read ? S_IRGRP : S_IRGRP | S_IXGRP;
	mode_t o = want_read ? S_IROTH : S_IROTH | S_IXOTH;
	static unsigned char buf[65536];	/* XATTR_SIZE_MAX */
	ssize_t n = lgetxattr(full, "system.posix_acl_access", buf, sizeof buf);
	int err = n < 0 ? errno : 0;
	/* a path renamed or removed since its lstat is judged on that lstat, as before: the
	 * events of the change refresh it; only an ACL that exists and cannot be read is
	 * restrictive, as in the oracle, which never sees a vanished path */
	int acl = err == ENOENT || err == ENOTDIR ? 0 : moob_ext_acl_bytes(buf, n, err, st->st_mode);

	if (!acl && (st->st_mode & (u | g | o)) == (u | g | o))
		return 1;
	if (acl || d->dacl || st->st_uid != d->dst.st_uid || st->st_gid != d->dst.st_gid)
		return 0;
	if ((d->dst.st_mode & S_IXUSR) && (st->st_mode & u) != u)
		return 0;
	if ((d->dst.st_mode & S_IXGRP) && (st->st_mode & g) != g)
		return 0;
	if ((d->dst.st_mode & S_IXOTH) && (st->st_mode & o) != o)
		return 0;
	return 1;
}

static int udx_attr_cmp(const void *a, const void *b)
{
	return strcmp(((const struct udx_attr *)a)->name, ((const struct udx_attr *)b)->name);
}

/*
 * Reads what a query can see of a path: every "user.moob.<name>" whose value passes
 * moob_attr_validate() and is not raw, exactly the test of moobfs-index's term_matches().
 * Returns the count; *out points to a static array sorted by name.
 */
UDX_FN size_t udx_read_attrs(const char *full, struct udx_attr **out)
{
	static char *names;
	static size_t names_cap;
	static struct udx_attr *attrs;
	static size_t attrs_cap;
	ma_u8 v[MOOB_ATTR_VALUE_MAX + 1];
	char xname[MOOB_ATTR_PREFIX_LEN + UDX_NAME_MAX + 1];
	ssize_t len, n;
	size_t count = 0, off, nl;

	*out = attrs;
	for (;;) {
		/* with a size of zero the call returns the size needed instead of the names */
		len = names_cap ? llistxattr(full, names, names_cap) : -1;
		if (len >= 0)
			break;
		if (names_cap && errno != ERANGE)
			return 0;	/* no xattrs on this kind of file, or it is gone */
		len = llistxattr(full, NULL, 0);
		if (len < 0)
			return 0;
		names = udx_grow(names, &names_cap, (size_t)len + 256, 1);
	}
	for (off = 0; off < (size_t)len; off += nl + 1) {
		const char *nm = names + off;
		struct udx_attr *a;

		nl = strlen(nm);
		if (nl < MOOB_ATTR_PREFIX_LEN || nl > MOOB_ATTR_PREFIX_LEN + UDX_NAME_MAX ||
		    memcmp(nm, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN))
			continue;
		memcpy(xname, nm, nl + 1);
		n = lgetxattr(full, xname, v, sizeof v);
		if (n <= 0 || moob_attr_validate(v, (size_t)n) != MOOB_ATTR_OK || v[0] == MOOB_T_RAW)
			continue;
		attrs = udx_grow(attrs, &attrs_cap, count + 1, sizeof *attrs);
		a = &attrs[count++];
		memcpy(a->name, nm + MOOB_ATTR_PREFIX_LEN, nl - MOOB_ATTR_PREFIX_LEN + 1);
		a->type = v[0];
		a->slen = 0;
		a->k = 0;
		switch (v[0]) {
		case MOOB_T_STRING:
			a->slen = (size_t)n - 1;
			memcpy(a->s, v + 1, a->slen);
			break;
		case MOOB_T_DOUBLE:
			a->k = udx_double_key(moob_attr_get64(v + 1));
			break;
		case MOOB_T_BOOL:
			a->k = v[1];
			break;
		default:
			a->k = (int64_t)moob_attr_get64(v + 1);
		}
	}
	if (count > 1)
		qsort(attrs, count, sizeof *attrs, udx_attr_cmp);
	*out = attrs;
	return count;
}

UDX_FN void udx_link_note(uint64_t ino)
{
	if (!udx_track_links)
		return;
	udx_links = udx_grow(udx_links, &udx_links_cap, udx_nlinks + 1, sizeof *udx_links);
	udx_links[udx_nlinks++] = ino;
}

/* Are the stored attribute rows of eid equal to the n attributes just read? */
UDX_FN int udx_attrs_equal(struct udx_db *d, sqlite3_int64 eid, const struct udx_attr *a, size_t n)
{
	sqlite3_stmt *st = d->st[UDX_S_SEL_ATTRS];
	size_t i = 0;
	int same = 1, rc;

	sqlite3_bind_int64(st, 1, eid);
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		const char *name = (const char *)sqlite3_column_text(st, 0);

		if (i == n || strcmp(name, a[i].name) || sqlite3_column_int(st, 1) != a[i].type) {
			same = 0;
			break;
		}
		if (a[i].type == MOOB_T_STRING) {
			if ((size_t)sqlite3_column_bytes(st, 2) != a[i].slen ||
			    (a[i].slen && memcmp(sqlite3_column_blob(st, 2), a[i].s, a[i].slen))) {
				same = 0;
				break;
			}
		} else if (sqlite3_column_int64(st, 2) != a[i].k) {
			same = 0;
			break;
		}
		i++;
	}
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		udx_die("read attr rows", d->db);
	sqlite3_reset(st);
	return same && i == n;
}

/*
 * Makes the row of one existing path equal to the disk. Writes nothing when nothing changed,
 * so a full pass over an unchanged tree only reads. Sets *fresh when the row is new and
 * *trav_changed when a directory's traversal bit changed.
 */
UDX_FN void udx_put(struct udx_db *d, const char *full, const struct stat *st, int ok, int trav,
		    int *fresh, int *trav_changed)
{
	const char *rel = full + d->dirlen + 1;
	sqlite3_stmt *sel = d->st[UDX_S_SEL_ENTRY], *w;
	struct udx_attr *attrs;
	size_t n = udx_read_attrs(full, &attrs), i;
	int kind = S_ISDIR(st->st_mode) ? 1 : 0, links = S_ISREG(st->st_mode) && st->st_nlink > 1;
	sqlite3_int64 id = 0;
	int have, rc;

	*fresh = *trav_changed = 0;
	udx_bind_blob(sel, 1, rel, strlen(rel));
	rc = sqlite3_step(sel);
	if (rc != SQLITE_ROW && rc != SQLITE_DONE)
		udx_die("read entry", d->db);
	have = rc == SQLITE_ROW;
	if (have) {
		uint64_t oino = (uint64_t)sqlite3_column_int64(sel, 1);
		int okind = sqlite3_column_int(sel, 2), olinks = !okind && sqlite3_column_int64(sel, 3) > 1;
		int same = oino == (uint64_t)st->st_ino && okind == kind &&
			   sqlite3_column_int64(sel, 3) == (sqlite3_int64)st->st_nlink &&
			   sqlite3_column_int(sel, 4) == ok && sqlite3_column_int(sel, 5) == trav;

		id = sqlite3_column_int64(sel, 0);
		*trav_changed = sqlite3_column_int(sel, 5) != trav;
		if (olinks)
			udx_link_note(oino);
		sqlite3_reset(sel);
		if (!same) {
			w = d->st[UDX_S_UPD_ENTRY];
			sqlite3_bind_int64(w, 1, id);
			sqlite3_bind_int64(w, 2, (sqlite3_int64)st->st_ino);
			sqlite3_bind_int(w, 3, kind);
			sqlite3_bind_int64(w, 4, (sqlite3_int64)st->st_nlink);
			sqlite3_bind_int(w, 5, ok);
			sqlite3_bind_int(w, 6, trav);
			udx_step_done(d, UDX_S_UPD_ENTRY);
		}
	} else {
		sqlite3_reset(sel);
		w = d->st[UDX_S_INS_ENTRY];
		udx_bind_blob(w, 1, rel, strlen(rel));
		sqlite3_bind_int64(w, 2, (sqlite3_int64)st->st_ino);
		sqlite3_bind_int(w, 3, kind);
		sqlite3_bind_int64(w, 4, (sqlite3_int64)st->st_nlink);
		sqlite3_bind_int(w, 5, ok);
		sqlite3_bind_int(w, 6, trav);
		udx_step_done(d, UDX_S_INS_ENTRY);
		id = sqlite3_last_insert_rowid(d->db);
		*fresh = 1;
	}
	if (links)
		udx_link_note((uint64_t)st->st_ino);
	udx_seen = udx_grow(udx_seen, &udx_seen_cap, udx_nseen + 1, sizeof *udx_seen);
	udx_seen[udx_nseen++] = id;
	if (have && udx_attrs_equal(d, id, attrs, n))
		return;
	if (have) {
		sqlite3_bind_int64(d->st[UDX_S_DEL_ATTRS], 1, id);
		udx_step_done(d, UDX_S_DEL_ATTRS);
	}
	w = d->st[UDX_S_INS_ATTR];
	for (i = 0; i < n; i++) {
		sqlite3_bind_int64(w, 1, id);
		sqlite3_bind_text(w, 2, attrs[i].name, -1, SQLITE_STATIC);
		sqlite3_bind_int(w, 3, attrs[i].type);
		if (attrs[i].type == MOOB_T_STRING)
			udx_bind_blob(w, 4, attrs[i].s, attrs[i].slen);
		else
			sqlite3_bind_int64(w, 4, attrs[i].k);
		udx_step_done(d, UDX_S_INS_ATTR);
	}
}

static int udx_id_cmp(const void *a, const void *b)
{
	sqlite3_int64 x = *(const sqlite3_int64 *)a, y = *(const sqlite3_int64 *)b;

	return x < y ? -1 : x > y;
}

/*
 * Deletes rows: of rel itself (self), and of everything below rel; rel NULL means the whole
 * domain. With keep_seen, rows the walk just touched stay.
 */
UDX_FN void udx_prune(struct udx_db *d, const char *rel, int self, int keep_seen)
{
	static sqlite3_int64 *victims;
	static size_t victims_cap;
	sqlite3_stmt *st = d->st[rel ? UDX_S_SEL_RANGE : UDX_S_SEL_ALL];
	size_t n = 0, i, rl = rel ? strlen(rel) : 0;
	char lo[PATH_MAX + 2], hi[PATH_MAX + 2];
	int rc;

	if (rel) {
		memcpy(lo, rel, rl); lo[rl] = '/';
		memcpy(hi, rel, rl); hi[rl] = '/' + 1;
		udx_bind_blob(st, 1, rel, self ? rl : 0);	/* no path is empty */
		udx_bind_blob(st, 2, lo, rl + 1);
		udx_bind_blob(st, 3, hi, rl + 1);
	}
	if (keep_seen && udx_nseen > 1)
		qsort(udx_seen, udx_nseen, sizeof *udx_seen, udx_id_cmp);
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		sqlite3_int64 id = sqlite3_column_int64(st, 0);

		if (keep_seen && udx_nseen && bsearch(&id, udx_seen, udx_nseen, sizeof id, udx_id_cmp))
			continue;
		if (sqlite3_column_int64(st, 2) > 1 && !sqlite3_column_int(st, 3))
			udx_link_note((uint64_t)sqlite3_column_int64(st, 1));
		victims = udx_grow(victims, &victims_cap, n + 1, sizeof *victims);
		victims[n++] = id;
	}
	if (rc != SQLITE_DONE)
		udx_die("read entry range", d->db);
	sqlite3_reset(st);
	for (i = 0; i < n; i++) {
		sqlite3_bind_int64(d->st[UDX_S_DEL_ENTRY], 1, victims[i]);
		udx_step_done(d, UDX_S_DEL_ENTRY);
	}
}

UDX_FN int udx_visit(struct udx_db *d, char *full, size_t len, const struct stat *st, int chain_ok, int recurse);

/* Visits what is inside the directory full (its length is len); full is used as a buffer. */
UDX_FN void udx_children(struct udx_db *d, DIR *dp, char *full, size_t len, int chain_ok)
{
	struct dirent *e;

	udx_walked = 1;
	while ((e = readdir(dp))) {
		size_t nl = strlen(e->d_name);
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (len == d->dirlen && udx_is_db_name(e->d_name))
			continue;	/* the index itself is not an entry */
		if (len + 1 + nl >= PATH_MAX)
			continue;
		full[len] = '/';
		memcpy(full + len + 1, e->d_name, nl + 1);
		if (!lstat(full, &st))
			udx_visit(d, full, len + 1 + nl, &st, chain_ok, 1);
	}
	full[len] = 0;
}

/*
 * One entry, and with recurse (or when a directory is new or its traversal bit changed)
 * everything below it. Returns 0 when the oracle would not list the path at all: a symbolic
 * link, another filesystem, a directory that cannot be opened.
 */
UDX_FN int udx_visit(struct udx_db *d, char *full, size_t len, const struct stat *st, int chain_ok, int recurse)
{
	int fresh, trav_changed;

	if (S_ISLNK(st->st_mode) || st->st_dev != d->dst.st_dev)
		return 0;
	if (S_ISDIR(st->st_mode)) {
		DIR *dp = opendir(full);
		int trav;

		if (!dp)
			return 0;
		if (udx_dir_hook)
			udx_dir_hook(full);
		trav = udx_reach(d, full, st, 0);
		udx_put(d, full, st, chain_ok && udx_reach(d, full, st, 1), trav, &fresh, &trav_changed);
		if (recurse || fresh || trav_changed)
			udx_children(d, dp, full, len, chain_ok && trav);
		closedir(dp);
		return 1;
	}
	udx_put(d, full, st, chain_ok && !(S_ISREG(st->st_mode) && st->st_nlink > 1) && udx_reach(d, full, st, 1),
		0, &fresh, &trav_changed);
	return 1;
}

/* The whole domain: every row ends equal to the disk. The caller holds the transaction. */
UDX_FN void udx_sync_all(struct udx_db *d)
{
	char full[PATH_MAX];
	DIR *dp;

	udx_nseen = 0;
	memcpy(full, d->dir, d->dirlen + 1);
	dp = opendir(full);
	if (!dp)
		return;
	udx_children(d, dp, full, d->dirlen, 1);
	closedir(dp);
	udx_prune(d, NULL, 0, 1);
}

/*
 * One path relative to the domain: whatever is there now replaces what the database had,
 * including "nothing". This is what every inotify event turns into, so the order of events
 * and how many arrive per change do not matter.
 */
UDX_FN void udx_refresh(struct udx_db *d, const char *rel, int recurse)
{
	char full[PATH_MAX];
	struct stat st;
	size_t i, rl = strlen(rel);
	int chain_ok = 1, gone = 0;

	if (!rl || d->dirlen + 1 + rl >= PATH_MAX || (udx_is_db_name(rel) && !strchr(rel, '/')))
		return;
	memcpy(full, d->dir, d->dirlen);
	full[d->dirlen] = '/';
	memcpy(full + d->dirlen + 1, rel, rl + 1);
	/* every directory strictly between the domain and the entry, as the oracle does; a
	 * component that stopped being a directory (a symbolic link put in its place) ends it */
	for (i = d->dirlen + 1; full[i] && !gone; i++) {
		if (full[i] != '/')
			continue;
		full[i] = 0;
		if (lstat(full, &st) || !S_ISDIR(st.st_mode) || st.st_dev != d->dst.st_dev)
			gone = 1;
		else if (!udx_reach(d, full, &st, 0))
			chain_ok = 0;
		full[i] = '/';
	}
	udx_nseen = 0;
	udx_walked = 0;
	if (gone || lstat(full, &st) || !udx_visit(d, full, d->dirlen + 1 + rl, &st, chain_ok, recurse)) {
		udx_prune(d, rel, 1, 0);
		return;
	}
	if (!S_ISDIR(st.st_mode))
		udx_prune(d, rel, 0, 0);	/* a file where a directory was */
	else if (udx_walked)
		udx_prune(d, rel, 0, 1);
}

#endif
