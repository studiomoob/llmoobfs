// SPDX-License-Identifier: GPL-2.0-only
/*
 * In-memory attribute index of the MoobFS memory layer (phase 2d, domains in Q2a).
 *
 * Built per permission domain by the first query on it (ADR-0012), by walking
 * that domain's lower subtree and reading every user.moob.* attribute; a built
 * domain is kept current by the xattr, unlink, rmdir, rename, setattr, set_acl
 * and link paths of main.c; never persisted. The index is
 * partitioned by permission domain (docs/spec-query-v2.md): a partition is the
 * set of entries of one attribute name in one domain, split again by whether
 * the inode passes the homogeneity rule. Answers MOOB_IOC_QUERY (all
 * partitions; privileged when domains are on) and MOOB_IOC_GET_INFO on the
 * mount root, and MOOB_IOC_QUERY2 and MOOB_IOC_QUERY2_BATCH (one domain) on a
 * domain directory.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/uaccess.h>
#include <linux/dcache.h>
#include <linux/seqlock.h>
#include <linux/lockref.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <linux/limits.h>
#include <linux/posix_acl.h>
#include <linux/capability.h>
#include <linux/random.h>
#include <linux/siphash.h>
#include <linux/sched/mm.h>
#include <linux/shrinker.h>
#include <linux/jiffies.h>
#include <linux/security.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include "moobl.h"
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

#define CREATE_TRACE_POINTS
#include "moobl_trace.h"

/*
 * The index lock is taken when a layer inode is evicted (moobl_index_inode_evicted), and
 * memory reclaim evicts inodes. An allocation made with the lock held must therefore not
 * recurse into filesystem reclaim, or the task would wait for the lock it holds. Every
 * section runs in a NOFS scope, so the accounted allocations inside it can
 * keep the reclaim behavior of GFP_KERNEL_ACCOUNT.
 */
static inline unsigned int ix_write_lock(struct moobl_index *ix)
{
	down_write(&ix->lock);
	return memalloc_nofs_save();
}

static inline void ix_write_unlock(struct moobl_index *ix, unsigned int nofs)
{
	memalloc_nofs_restore(nofs);
	up_write(&ix->lock);
}

static inline unsigned int ix_read_lock(struct moobl_index *ix)
{
	down_read(&ix->lock);
	return memalloc_nofs_save();
}

static inline void ix_read_unlock(struct moobl_index *ix, unsigned int nofs)
{
	memalloc_nofs_restore(nofs);
	up_read(&ix->lock);
}

/* One partition: ordered in ix->names by (name, domain, hetero). */
struct moobl_ix_name {
	struct rb_node node;
	struct rb_root entries;		/* struct moobl_ix_entry, ordered by (type, value, ino) */
	u32 count;
	u64 domain;			/* lower ino of the domain directory; 0: outside any domain */
	bool hetero;			/* inodes failing the homogeneity rule */
	char name[MOOB_ATTR_NAME_MAX + 1];
};

struct moobl_ix_inode {
	u64 ino;
	struct list_head entries;	/* this inode's typed attributes */
	struct inode *upper;		/* cached layer inode, or NULL */
	char *path;			/* mount-relative path recorded at scan or write time */
	u64 domain;			/* partition key of all its entries */
	bool hetero;
};

/* Where an inode's entries belong (docs/spec-query-v2.md §2, §3). */
struct moobl_place {
	u64 domain;
	bool hetero;
};

struct moobl_ix_entry {
	struct rb_node node;
	struct list_head by_ino;
	struct moobl_ix_name *name;
	struct moobl_ix_inode *owner;
	u8 type;
	u16 len;			/* payload length, type byte excluded */
	u8 value[];
};

static inline struct moobl_index *IX(struct super_block *sb)
{
	return &MOOBL_SB(sb)->index;
}

/* ---- ordering ---------------------------------------------------------- */

static u64 double_key(const u8 *payload)
{
	u64 bits = moob_attr_get64(payload);

	/* Maps IEEE 754 binary64 to an unsigned key with the same ordering. */
	return (bits & (1ULL << 63)) ? ~bits : (bits | (1ULL << 63));
}

/* Compares a stored entry with (type, payload, len); ino breaks ties on request. */
static int value_cmp(u8 atype, const u8 *a, u16 alen, u8 btype, const u8 *b, u16 blen)
{
	if (atype != btype)
		return atype < btype ? -1 : 1;
	switch (atype) {
	case MOOB_T_INT64:
	case MOOB_T_TIME: {
		s64 x = (s64)moob_attr_get64(a), y = (s64)moob_attr_get64(b);

		return x < y ? -1 : x > y ? 1 : 0;
	}
	case MOOB_T_DOUBLE: {
		u64 x = double_key(a), y = double_key(b);

		return x < y ? -1 : x > y ? 1 : 0;
	}
	case MOOB_T_BOOL:
		return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
	default: {
		int c = memcmp(a, b, min(alen, blen));

		if (c)
			return c;
		return alen < blen ? -1 : alen > blen ? 1 : 0;
	}
	}
}

static int entry_cmp(const struct moobl_ix_entry *e, u8 type, const u8 *v, u16 len, u64 ino)
{
	int c = value_cmp(e->type, e->value, e->len, type, v, len);

	if (c)
		return c;
	return e->owner->ino < ino ? -1 : e->owner->ino > ino ? 1 : 0;
}

/* ---- accounting and lookup --------------------------------------------- */

static int charge(struct moobl_index *ix, size_t bytes)
{
	if (ix->used + bytes > ix->budget) {
		ix->refusals++;	/* never reset: concurrent builds compare samples (issue #111) */
		return -ENOMEM;
	}
	ix->used += bytes;
	return 0;
}

/*
 * An update failed after the lower change (F2, ADR-0016): the domain's entries no longer match
 * the lower filesystem. A built domain is built again by the next query that needs it
 * (domain_fresh), as after a rename in it; a domain being built starts its build again. Only
 * that domain is affected, and no answer comes from it until it is built again.
 */
static void domain_lost(struct moobl_index *ix, u64 domain, int err)
{
	struct moobl_domain *dom = domain ? xa_load(&ix->domains, (unsigned long)domain)
					  : &ix->outside;

	lockdep_assert_held_write(&ix->lock);
	trace_moobfs_index_incomplete(ix->dev, err);
	pr_warn_ratelimited("moobfs_layer: index update failed (%d); its domain is built again by the next query\n",
			    err);
	if (!dom)
		return;
	if (dom->state == MOOBL_DOM_BUILDING)
		dom->restart = true;
	else if (dom->state == MOOBL_DOM_BUILT)
		WRITE_ONCE(dom->lost, true);
}

static void uncharge(struct moobl_index *ix, size_t bytes)
{
	ix->used -= min_t(u64, ix->used, bytes);
}

static int part_cmp(const char *name, u64 domain, bool hetero, const struct moobl_ix_name *nm)
{
	int c = strcmp(name, nm->name);

	if (c)
		return c;
	if (domain != nm->domain)
		return domain < nm->domain ? -1 : 1;
	return (int)hetero - (int)nm->hetero;
}

static struct moobl_ix_name *name_find(struct moobl_index *ix, const char *name, u64 domain,
				       bool hetero)
{
	struct rb_node *n = ix->names.rb_node;

	lockdep_assert_held(&ix->lock);
	while (n) {
		struct moobl_ix_name *nm = rb_entry(n, struct moobl_ix_name, node);
		int c = part_cmp(name, domain, hetero, nm);

		if (!c)
			return nm;
		n = c < 0 ? n->rb_left : n->rb_right;
	}
	return NULL;
}

/* First partition of this name at or after (domain, hetero), or NULL. */
static struct moobl_ix_name *name_lower_bound(struct moobl_index *ix, const char *name, u64 domain,
					      bool hetero)
{
	struct rb_node *n = ix->names.rb_node;
	struct moobl_ix_name *best = NULL;

	while (n) {
		struct moobl_ix_name *nm = rb_entry(n, struct moobl_ix_name, node);

		if (part_cmp(name, domain, hetero, nm) <= 0) {
			best = nm;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	return best && !strcmp(best->name, name) ? best : NULL;
}

static struct moobl_ix_name *name_get(struct moobl_index *ix, const char *name, u64 domain,
				      bool hetero)
{
	struct rb_node **p = &ix->names.rb_node, *parent = NULL;
	struct moobl_ix_name *nm;

	lockdep_assert_held_write(&ix->lock);
	while (*p) {
		int c;

		parent = *p;
		nm = rb_entry(parent, struct moobl_ix_name, node);
		c = part_cmp(name, domain, hetero, nm);
		if (!c)
			return nm;
		p = c < 0 ? &parent->rb_left : &parent->rb_right;
	}
	if (charge(ix, sizeof(*nm)))
		return ERR_PTR(-ENOMEM);
	nm = kzalloc(sizeof(*nm), GFP_KERNEL_ACCOUNT);
	if (!nm) {
		uncharge(ix, sizeof(*nm));
		return ERR_PTR(-ENOMEM);
	}
	strscpy(nm->name, name, sizeof(nm->name));
	nm->domain = domain;
	nm->hetero = hetero;
	nm->entries = RB_ROOT;
	rb_link_node(&nm->node, parent, p);
	rb_insert_color(&nm->node, &ix->names);
	return nm;
}

static void name_put_if_empty(struct moobl_index *ix, struct moobl_ix_name *nm)
{
	if (nm->count)
		return;
	rb_erase(&nm->node, &ix->names);
	kfree(nm);
	uncharge(ix, sizeof(*nm));
}

static struct moobl_ix_inode *inode_rec_get(struct moobl_index *ix, u64 ino)
{
	struct moobl_ix_inode *rec = xa_load(&ix->inodes, ino);

	lockdep_assert_held_write(&ix->lock);
	if (rec)
		return rec;
	if (charge(ix, sizeof(*rec)))
		return ERR_PTR(-ENOMEM);
	rec = kzalloc(sizeof(*rec), GFP_KERNEL_ACCOUNT);
	if (!rec) {
		uncharge(ix, sizeof(*rec));
		return ERR_PTR(-ENOMEM);
	}
	rec->ino = ino;
	INIT_LIST_HEAD(&rec->entries);
	if (xa_err(xa_store(&ix->inodes, ino, rec, GFP_KERNEL_ACCOUNT))) {
		kfree(rec);
		uncharge(ix, sizeof(*rec));
		return ERR_PTR(-ENOMEM);
	}
	ix->inode_count++;
	return rec;
}

static void inode_rec_put_if_empty(struct moobl_index *ix, struct moobl_ix_inode *rec)
{
	if (!list_empty(&rec->entries))
		return;
	if (rec->hetero)
		ix->hetero_count--;
	xa_erase(&ix->inodes, rec->ino);
	if (rec->path)
		uncharge(ix, strlen(rec->path) + 1);
	kfree(rec->path);
	kfree(rec);
	uncharge(ix, sizeof(*rec));
	ix->inode_count--;
}

static void entry_free(struct moobl_index *ix, struct moobl_ix_entry *e)
{
	lockdep_assert_held_write(&ix->lock);
	rb_erase(&e->node, &e->name->entries);
	list_del(&e->by_ino);
	e->name->count--;
	ix->entries--;
	uncharge(ix, sizeof(*e) + e->len);
	name_put_if_empty(ix, e->name);
	kfree(e);
}

/*
 * A record that lost part of its file's attributes is dropped whole (journal 0156): with no
 * record, rec_whole() cannot vouch for it, and a rename or a link reads the file again from
 * the lower filesystem instead of carrying the partial record into another built domain.
 */
static void rec_drop(struct moobl_index *ix, struct moobl_ix_inode *rec)
{
	struct moobl_ix_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
		entry_free(ix, e);
	inode_rec_put_if_empty(ix, rec);
}

static struct moobl_ix_entry *entry_find(struct moobl_ix_inode *rec, const char *name)
{
	struct moobl_ix_entry *e;

	list_for_each_entry(e, &rec->entries, by_ino)
		if (!strcmp(e->name->name, name))
			return e;
	return NULL;
}

/* The entry of rec under one name node of rec's partition (NULL node: none carries it). */
static struct moobl_ix_entry *entry_find_node(struct moobl_ix_inode *rec,
					      const struct moobl_ix_name *nm)
{
	struct moobl_ix_entry *e;

	if (!nm)
		return NULL;
	list_for_each_entry(e, &rec->entries, by_ino)
		if (e->name == nm)
			return e;
	return NULL;
}

static void entry_link(struct moobl_ix_name *nm, struct moobl_ix_entry *e, u64 ino)
{
	struct rb_node **p = &nm->entries.rb_node, *parent = NULL;

	while (*p) {
		struct moobl_ix_entry *cur;

		parent = *p;
		cur = rb_entry(parent, struct moobl_ix_entry, node);
		p = entry_cmp(cur, e->type, e->value, e->len, ino) < 0 ? &parent->rb_right
								       : &parent->rb_left;
	}
	rb_link_node(&e->node, parent, p);
	rb_insert_color(&e->node, &nm->entries);
}

/*
 * Moves every entry of an inode to the partition given by pl. An entry that cannot be
 * moved for lack of memory is dropped and the domain of pl is built again (domain_lost), so it
 * can never be answered from a partition it no longer belongs to. Caller holds the write lock.
 */
static int rec_set_partition(struct moobl_index *ix, struct moobl_ix_inode *rec,
			     const struct moobl_place *pl)
{
	struct moobl_ix_entry *e, *tmp;
	int err = 0;

	if (rec->domain == pl->domain && rec->hetero == pl->hetero)
		return 0;
	list_for_each_entry_safe(e, tmp, &rec->entries, by_ino) {
		struct moobl_ix_name *old = e->name, *nm;

		rb_erase(&e->node, &old->entries);
		old->count--;
		nm = name_get(ix, old->name, pl->domain, pl->hetero);
		if (IS_ERR(nm)) {
			list_del(&e->by_ino);
			ix->entries--;
			uncharge(ix, sizeof(*e) + e->len);
			kfree(e);
			name_put_if_empty(ix, old);
			err = -ENOMEM;
			continue;
		}
		e->name = nm;
		entry_link(nm, e, rec->ino);
		nm->count++;
		name_put_if_empty(ix, old);
	}
	if (rec->hetero != pl->hetero) {
		if (pl->hetero)
			ix->hetero_count++;
		else
			ix->hetero_count--;
	}
	rec->domain = pl->domain;
	rec->hetero = pl->hetero;
	if (err) {
		/* the entries that moved go too: the caller frees the record (rec_drop) */
		while (!list_empty(&rec->entries))
			entry_free(ix, list_first_entry(&rec->entries, struct moobl_ix_entry, by_ino));
		domain_lost(ix, pl->domain, err);
	}
	return err;
}

/* Replaces or inserts one typed attribute. Caller holds the write lock. */
static int index_put(struct moobl_index *ix, u64 ino, const struct moobl_place *pl,
		     const char *name, const u8 *value, size_t size)
{
	struct moobl_ix_inode *rec;
	struct moobl_ix_name *nm;
	struct moobl_ix_entry *e, *old;
	u16 len = (u16)(size - 1);
	int err;

	lockdep_assert_held_write(&ix->lock);
	if (value[0] == MOOB_T_RAW)
		return 0;	/* raw payloads are stored but never indexed */
	rec = inode_rec_get(ix, ino);
	if (IS_ERR(rec))
		return PTR_ERR(rec);
	err = rec_set_partition(ix, rec, pl);
	if (err) {
		inode_rec_put_if_empty(ix, rec);
		return err;
	}
	old = entry_find(rec, name);
	if (old)
		entry_free(ix, old);	/* before name_get: freeing may drop the name node */
	nm = name_get(ix, name, rec->domain, rec->hetero);
	if (IS_ERR(nm)) {
		rec_drop(ix, rec);	/* the old value is gone, the new one not stored */
		return PTR_ERR(nm);
	}
	if (charge(ix, sizeof(*e) + len)) {
		name_put_if_empty(ix, nm);
		rec_drop(ix, rec);
		return -ENOMEM;
	}
	e = kmalloc(sizeof(*e) + len, GFP_KERNEL_ACCOUNT);
	if (!e) {
		uncharge(ix, sizeof(*e) + len);
		name_put_if_empty(ix, nm);
		rec_drop(ix, rec);
		return -ENOMEM;
	}
	e->name = nm;
	e->owner = rec;
	e->type = value[0];
	e->len = len;
	memcpy(e->value, value + 1, len);
	entry_link(nm, e, ino);
	list_add(&e->by_ino, &rec->entries);
	nm->count++;
	ix->entries++;
	return 0;
}

static int index_set_path(struct moobl_index *ix, struct moobl_ix_inode *rec, const char *path)
{
	size_t len = strlen(path) + 1;
	char *copy;

	if (rec->path && !strcmp(rec->path, path))
		return 0;
	if (charge(ix, len))
		return -ENOMEM;	/* the old name is kept; QUERY will fall back to the dentry cache */
	copy = kmemdup(path, len, GFP_KERNEL_ACCOUNT);
	if (!copy) {
		uncharge(ix, len);
		return -ENOMEM;
	}
	if (rec->path)
		uncharge(ix, strlen(rec->path) + 1);
	kfree(rec->path);
	rec->path = copy;
	return 0;
}

/* ---- domains and the homogeneity rule (docs/spec-query-v2.md §2, §3) ---- */

#define MOOBL_MAX_DEPTH 64

/* True when the inode carries an access ACL beyond the minimal one (mode bits only). */
static bool ext_acl(struct inode *inode, int *err)
{
	struct posix_acl *acl;
	bool ext;

	if (!IS_POSIXACL(inode))
		return false;
	acl = get_inode_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl)) {
		if (err)
			*err = PTR_ERR(acl);
		return true;	/* unknown: treat as restrictive */
	}
	if (!acl)
		return false;
	ext = acl->a_count > 3;
	posix_acl_release(acl);
	return ext;
}

/*
 * Sufficient condition for "everyone who can traverse the domain directory dom can also
 * reach in": read it (want_read, a file or a directory carrying attributes) or traverse it
 * (an intermediate directory). Either in grants the class bit to owner, group and others
 * and has no extended ACL, or in and dom share owner and group, neither has an extended
 * ACL, and in grants the bit to every class that dom lets traverse. The bit of others alone
 * is not enough: the kernel judges the owner by the owner bits and a member of the group by
 * the group bits, never by those of others (acl_permission_check), so a file 0604 is not
 * readable by its group (journal 0116). Lower inodes: permissions are the
 * lower filesystem's. An ACL that could not be read counts as restrictive; a caller that
 * must not take that for an answer (a build) passes err and gets the error there.
 */
static bool reach_ok(struct inode *in, struct inode *dom, bool want_read, int *err)
{
	/* octal class bits: read 0444 or search 0111, split per owner, group, other */
	umode_t u = want_read ? 0400 : 0100, g = want_read ? 0040 : 0010;
	umode_t o = want_read ? 0004 : 0001;
	bool acl = ext_acl(in, err);

	if (!acl && (in->i_mode & (u | g | o)) == (u | g | o))
		return true;
	if (acl || ext_acl(dom, err))
		return false;
	if (!uid_eq(in->i_uid, dom->i_uid) || !gid_eq(in->i_gid, dom->i_gid))
		return false;
	if ((dom->i_mode & 0100) && !(in->i_mode & u))
		return false;
	if ((dom->i_mode & 0010) && !(in->i_mode & g))
		return false;
	if ((dom->i_mode & 0001) && !(in->i_mode & o))
		return false;
	return true;
}

static bool target_ok(struct inode *in, struct inode *dom, int *err)
{
	if (S_ISREG(in->i_mode) && in->i_nlink > 1)
		return false;	/* another name may live in another domain */
	return reach_ok(in, dom, true, err);
}

/*
 * Where a layer dentry is, as one consistent reading: its distance from the mount root in
 * *depth and, when n is not 0 and the dentry is at least that deep, its ancestor at depth n
 * (the domain directory) referenced in *domd. False when the path is deeper than
 * MOOBL_MAX_DEPTH.
 *
 * The walk is done under the dcache's rename_lock sequence counter, as path lookup does, and
 * repeated if a rename happened meanwhile. Reading the depth and the ancestor in two separate
 * walks (journal 0096) let a rename of the file between them hand back a directory that is
 * not at depth n: the write was then taken for one outside every built domain and the index
 * was not updated (journal 0098). Nothing here allocates: an allocation failure must not be
 * mistaken for a path that is too deep (journal 0096).
 */
static bool where_is(struct dentry *dentry, unsigned int n, unsigned int *depth,
		     struct dentry **domd)
{
	struct dentry *d, *dom;
	unsigned int k, i, seq;
	bool need;

	for (;;) {
		dom = NULL;
		rcu_read_lock();
		seq = read_seqbegin(&rename_lock);
		for (k = 0, d = dentry; !IS_ROOT(d) && k <= MOOBL_MAX_DEPTH; k++)
			d = READ_ONCE(d->d_parent);
		need = n && k >= n && k <= MOOBL_MAX_DEPTH;
		if (need) {
			for (i = 0, d = dentry; i < k - n; i++)
				d = READ_ONCE(d->d_parent);
			if (lockref_get_not_dead(&d->d_lockref))
				dom = d;
		}
		rcu_read_unlock();
		if (!read_seqretry(&rename_lock, seq) && (!need || dom))
			break;
		dput(dom);
		cond_resched();
	}
	*depth = k;
	*domd = dom;
	return k <= MOOBL_MAX_DEPTH;
}

/*
 * Partition of a layer dentry that carries attributes. With self, the inode is placed at the
 * name of dentry (a rename target, before the VFS moves the dentry). May sleep (ACL reads).
 */
static void place_dentry_above(struct moobl_index *ix, struct dentry *dentry, struct inode *self,
			       struct moobl_place *pl, bool *above)
{
	unsigned int n = ix->domain_depth, k, i, tries;
	struct dentry *domd, *d, *p;
	struct inode *dom;
	bool ok;

	pl->domain = 0;
	pl->hetero = false;
	if (above)
		*above = false;
	if (!n)
		return;
	for (tries = 0; tries < 8; tries++) {
		if (!where_is(dentry, n, &k, &domd)) {
			pl->hetero = true;	/* cannot evaluate: keep it out of every domain answer */
			return;
		}
		if (k <= n) {
			dput(domd);
			return;
		}
		dom = moobl_lower_inode(d_inode(domd));
		ok = true;
		/* the directories between the two; a file moved meanwhile no longer leads to domd */
		d = dget_parent(dentry);
		for (i = 0; d != domd && !IS_ROOT(d) && i < MOOBL_MAX_DEPTH; i++) {
			if (ok)
				ok = reach_ok(moobl_lower_inode(d_inode(d)), dom, false, NULL);
			p = dget_parent(d);
			dput(d);
			d = p;
		}
		if (d == domd) {
			dput(d);
			if (above)
				*above = ok;	/* every directory between the domain and the name passes */
			if (ok)
				ok = target_ok(moobl_lower_inode(self ? self : d_inode(dentry)), dom,
					       NULL);
			pl->domain = dom->i_ino;
			pl->hetero = !ok;
			dput(domd);
			return;
		}
		dput(d);
		dput(domd);
	}
	pl->hetero = true;	/* it kept moving: out of every domain answer until it settles */
}

static void place_dentry(struct moobl_index *ix, struct dentry *dentry, struct inode *self,
			 struct moobl_place *pl)
{
	place_dentry_above(ix, dentry, self, pl, NULL);
}

/*
 * Places dentry and takes the index write lock with the placement still current: a rename or
 * a link that completed while it was being computed (it sleeps, with no lock held) makes it
 * be computed again. Without this a write could put a file back into the domain it had just
 * been renamed out of.
 */
static unsigned int place_and_lock(struct moobl_index *ix, struct dentry *dentry,
				   struct moobl_place *pl)
{
	unsigned int nofs, tries;
	u64 gen;

	for (tries = 0;; tries++) {
		gen = READ_ONCE(ix->move_gen);
		place_dentry(ix, dentry, NULL, pl);
		nofs = ix_write_lock(ix);
		if (ix->move_gen == gen || tries == 8)
			return nofs;
		ix_write_unlock(ix, nofs);
	}
}

/* Depth of a layer dentry and the lower ino of its ancestor at depth n (0 when none or negative). */
static bool depth_and_domain(struct moobl_index *ix, struct dentry *dentry, unsigned int *depth,
			     u64 *domain)
{
	struct dentry *dom;

	*domain = 0;
	if (!where_is(dentry, ix->domain_depth, depth, &dom))
		return false;
	/* the dentry itself may be negative (a rename target): it then has no domain inode yet */
	if (dom && d_really_is_positive(dom))
		*domain = moobl_lower_inode(d_inode(dom))->i_ino;
	dput(dom);
	return true;
}

/* ---- building a domain on demand (ADR-0012) ----------------------------- */

/*
 * Nothing is indexed at mount. The part of the index that belongs to a domain is built by
 * the first query on it, from the lower filesystem, while the domain stays writable. The
 * method is the online index build of Mohan and Narang (SIGMOD 1992) in its variant without
 * a side file: during the build the write paths update the index directly, and the scan
 * must not overwrite what they wrote with an older reading. The scan announces the file it
 * is reading (struct moobl_loader on ix->loaders), reads its attributes from the lower
 * filesystem with no lock held, and replaces that file's entries under the index lock only
 * if no update from main.c touched the same file meanwhile (index_touched), reading again
 * otherwise. A first version compared one counter bumped by every update of the mount: under
 * a steady write load no build ever finished (journal 0098). A file is therefore always absent
 * from the index or whole in it. A directory rename can carry a subtree from a part not yet
 * scanned into one already scanned: the rename records the directory in the domain's side
 * list (struct moved_dir) and the build scans it again before it declares the domain built.
 */
#define MOOBL_LOAD_TRIES 16
#define MOOBL_BUILD_TRIES 4
#define MOOBL_MOVED_ROUNDS 64

/* A directory renamed inside a domain while it was being built, at its new name. */
struct moved_dir {
	struct list_head list;
	struct dentry *lower;		/* referenced */
	char *path;			/* mount-relative */
	unsigned int depth;
	struct moobl_place self;	/* partition of its own attributes */
	bool inner_ok;			/* every directory from the domain down to it passes */
};

static void moved_dir_free(struct moved_dir *md)
{
	if (!md)
		return;
	dput(md->lower);
	kfree(md->path);
	kfree(md);
}

static void moved_list_free(struct list_head *head)
{
	while (!list_empty(head)) {
		struct moved_dir *md = list_first_entry(head, struct moved_dir, list);

		list_del(&md->list);
		moved_dir_free(md);
	}
}

/*
 * For tests only (tests/buildrace.c, vm/suite.sh). The window between reading a file and
 * storing what was read lasts microseconds, so concurrent writes almost never land in it and
 * a test cannot tell a correct build from one without the conflict check. scan_delay_us makes
 * the scan sleep in that window. A reading repeated because an update arrived is reported by
 * the tracepoint moobfs_scan_reread (journal 0102), which replaced a counter parameter.
 */
static unsigned int scan_delay_us;
module_param(scan_delay_us, uint, 0644);
MODULE_PARM_DESC(scan_delay_us, "tests only: microseconds the index scan sleeps between reading a file and storing it");

/* Tests only: fail one listed attribute's value read, after listxattr succeeds. */
static int scan_getxattr_errno_once;
module_param(scan_getxattr_errno_once, int, 0644);
MODULE_PARM_DESC(scan_getxattr_errno_once, "tests only: errno for one index-scan getxattr");

/* One file being read from the lower filesystem into the index, by a build or an adoption. */
struct moobl_loader {
	struct list_head list;
	u64 ino;
	bool dirty;	/* an update from main.c touched ino since the reading began */
};

/* Every update from main.c says which inode it is about. Caller holds the write lock. */
static void index_touched(struct moobl_index *ix, u64 ino)
{
	struct moobl_loader *ld;

	lockdep_assert_held_write(&ix->lock);
	list_for_each_entry(ld, &ix->loaders, list)
		if (ld->ino == ino)
			ld->dirty = true;
}

static bool domain_live(struct moobl_index *ix, u64 domain)
{
	struct moobl_domain *dom = domain ? xa_load(&ix->domains, (unsigned long)domain)
					  : &ix->outside;

	lockdep_assert_held(&ix->lock);
	return dom && dom->state != MOOBL_DOM_NONE;
}

/*
 * Built, with names that can be trusted. A domain that is built but had a directory renamed
 * in it (or above it) is built again by the next query: the names recorded below that
 * directory are stale, and so may be the homogeneity of its files, which now have other
 * directories between them and the domain.
 */
static bool domain_fresh(struct moobl_index *ix, const struct moobl_domain *dom)
{
	return READ_ONCE(dom->state) == MOOBL_DOM_BUILT && !READ_ONCE(dom->paths_stale) &&
	       !READ_ONCE(dom->lost) && READ_ONCE(dom->top_gen) == READ_ONCE(ix->top_gen);
}

static struct moobl_domain *domain_get(struct moobl_index *ix, u64 domain)
{
	struct moobl_domain *dom, *cur;
	unsigned int nofs;

	if (!domain)
		return &ix->outside;
	dom = xa_load(&ix->domains, (unsigned long)domain);
	if (dom)
		return dom;
	dom = kzalloc(sizeof(*dom), GFP_KERNEL_ACCOUNT);
	if (!dom)
		return NULL;
	mutex_init(&dom->build);
	INIT_LIST_HEAD(&dom->moved);
	nofs = ix_write_lock(ix);
	cur = xa_load(&ix->domains, (unsigned long)domain);
	if (!cur && xa_err(xa_store(&ix->domains, (unsigned long)domain, dom,
				     GFP_KERNEL_ACCOUNT)))
		cur = ERR_PTR(-ENOMEM);
	ix_write_unlock(ix, nofs);
	if (cur) {
		kfree(dom);
		return IS_ERR(cur) ? NULL : cur;
	}
	return dom;
}

/* Removes everything the index holds for one domain. Caller holds the write lock. */
static void domain_drop(struct moobl_index *ix, u64 domain)
{
	struct rb_node *n, *next;

	lockdep_assert_held_write(&ix->lock);
	for (n = rb_first(&ix->names); n; n = next) {
		struct moobl_ix_name *nm = rb_entry(n, struct moobl_ix_name, node);
		bool last = false;

		next = rb_next(n);
		/*
		 * The walk covers every name of the mount and can free millions of entries with the
		 * index lock held for writing; the lock is a semaphore, so give the CPU away between
		 * names (review item 6, journal 0120). next stays valid: nobody else changes the
		 * tree while the write lock is held.
		 */
		cond_resched();
		if (nm->domain != domain)
			continue;
		while (!last) {		/* freeing its last entry frees nm */
			struct moobl_ix_entry *e = rb_entry(rb_first(&nm->entries),
							    struct moobl_ix_entry, node);
			struct moobl_ix_inode *rec = e->owner;

			last = nm->count == 1;
			entry_free(ix, e);
			inode_rec_put_if_empty(ix, rec);
		}
	}
}

/*
 * Takes a built domain down: back to "not built", nothing of it left in the index, and the next
 * query builds it again. Caller holds dom->build, ix->query_sem for writing (no query is reading
 * the domain) and the index lock for writing.
 */
static void domain_takedown(struct moobl_index *ix, struct moobl_domain *dom, u64 domain)
{
	lockdep_assert_held(&dom->build);
	lockdep_assert_held_write(&ix->query_sem);
	lockdep_assert_held_write(&ix->lock);
	domain_drop(ix, domain);
	WRITE_ONCE(dom->state, MOOBL_DOM_NONE);
	ix->domains_built--;
	ix->drop_gen++;
}

/* How long a domain used by a query is kept from the shrinker (mount option index_idle). */
static unsigned long reclaim_idle(const struct moobl_index *ix)
{
	return (unsigned long)ix->idle * HZ;
}

/*
 * The built domain queried least recently, other than except, and not used in the last idle
 * jiffies; NULL when there is none. The cursor finds the next oldest candidate after a busy
 * build mutex, ordered by (used_at, key). Domains are never removed from ix->domains before
 * unmount, so the walk needs no lock; the state is checked again under domain_release()'s locks.
 * A plain walk rather than a list_lru: the domains of one mount are few, and the index memory
 * is not charged to a memory cgroup, so a per-cgroup, per-node list would buy nothing.
 */
static struct moobl_domain *domain_lru(struct moobl_index *ix, struct moobl_domain *except,
				       unsigned long idle, bool after, unsigned long after_used,
				       u64 after_key, u64 *key, unsigned long *selected_used)
{
	struct moobl_domain *dom, *best = NULL;
	unsigned long idx, used, best_used = 0;

	used = READ_ONCE(ix->outside.used_at);
	if (READ_ONCE(ix->outside.state) == MOOBL_DOM_BUILT && &ix->outside != except &&
	    time_after_eq(jiffies, used + idle) &&
	    (!after || time_after(used, after_used))) {
		best = &ix->outside;
		best_used = used;
		*key = 0;
	}
	xa_for_each(&ix->domains, idx, dom) {
		used = READ_ONCE(dom->used_at);
		if (dom == except || READ_ONCE(dom->state) != MOOBL_DOM_BUILT ||
		    time_before(jiffies, used + idle) ||
		    (after && !time_after(used, after_used) &&
		     !(used == after_used && (u64)idx > after_key)))
			continue;
		if (!best || time_before(used, best_used) ||
		    (used == best_used && (u64)idx < *key)) {
			best = dom;
			best_used = used;
			*key = idx;
		}
	}
	if (best)
		*selected_used = best_used;
	return best;
}

/*
 * Releases the least recently queried available built domain other than except. From the shrinker
 * (reclaim), every lock is only tried: reclaim can run inside a query, a build or an index
 * update of this very mount. From a build over the budget the caller holds except->build, so
 * the other domain's build mutex is only tried (two builds could otherwise wait for each
 * other) and the rest is taken in the order of domain_ensure(). A busy candidate does not hide
 * later candidates that can be released. Returns whether one was released.
 */
static bool domain_release(struct moobl_index *ix, struct moobl_domain *except, bool reclaim)
{
	unsigned long idle = reclaim ? reclaim_idle(ix) : 0;
	struct moobl_domain *dom;
	unsigned int nofs, attempts, limit;
	bool done = false;
	u64 key = 0;
	unsigned long after_used = 0, selected_used;
	u64 after_key = 0;
	bool after = false;

	/* A busy oldest domain must not hide another domain that can make room. */
	limit = READ_ONCE(ix->domains_built) + 1;
	for (attempts = 0; attempts < limit; attempts++) {
		dom = domain_lru(ix, except, idle, after, after_used, after_key,
				 &key, &selected_used);
		if (!dom)
			return false;
		if (mutex_trylock(&dom->build))
			break;
		after = true;
		after_used = selected_used;
		after_key = key;
	}
	if (attempts == limit)
		return false;
	if (reclaim) {
		if (!down_write_trylock(&ix->query_sem))
			goto out_build;
		nofs = memalloc_nofs_save();
		if (!down_write_trylock(&ix->lock)) {
			memalloc_nofs_restore(nofs);
			goto out_query;
		}
	} else {
		down_write(&ix->query_sem);
		nofs = ix_write_lock(ix);
	}
	if (dom->state == MOOBL_DOM_BUILT) {
		u64 before = ix->used;

		domain_takedown(ix, dom, key);
		ix->releases++;
		trace_moobfs_domain_release(ix->dev, key, reclaim, (s64)(before - ix->used));
		done = true;
	}
	ix_write_unlock(ix, nofs);
out_query:
	up_write(&ix->query_sem);
out_build:
	mutex_unlock(&dom->build);
	return done;
}

#define MOOBL_BUDGET_RELEASES 1024

/*
 * The typed attributes of one file as read from the lower filesystem: records of the value
 * size (u16), the attribute name with its NUL, and the value.
 */
struct attr_buf {
	u8 *data;
	size_t len, cap;
	char *list;	/* XATTR_LIST_MAX bytes */
	u8 *value;	/* MOOB_ATTR_VALUE_MAX + 1 bytes */
};

static void attr_buf_free(struct attr_buf *b)
{
	kvfree(b->data);
	kvfree(b->list);
	kfree(b->value);
}

static int attr_buf_init(struct attr_buf *b)
{
	memset(b, 0, sizeof(*b));
	b->list = kvmalloc(XATTR_LIST_MAX, GFP_KERNEL_ACCOUNT);
	b->value = kmalloc(MOOB_ATTR_VALUE_MAX + 1, GFP_KERNEL_ACCOUNT);
	if (!b->list || !b->value) {
		attr_buf_free(b);
		return -ENOMEM;
	}
	return 0;
}

static int attr_buf_add(struct attr_buf *b, const char *name, size_t n)
{
	size_t nl = strlen(name) + 1, need = b->len + sizeof(u16) + nl + n;
	u16 size = (u16)n;

	if (need > b->cap) {
		size_t cap = max_t(size_t, need, max_t(size_t, 2 * b->cap, PAGE_SIZE));
		u8 *data = kvmalloc(cap, GFP_KERNEL_ACCOUNT);

		if (!data)
			return -ENOMEM;
		if (b->len)
			memcpy(data, b->data, b->len);
		kvfree(b->data);
		b->data = data;
		b->cap = cap;
	}
	memcpy(b->data + b->len, &size, sizeof(size));
	memcpy(b->data + b->len + sizeof(size), name, nl);
	memcpy(b->data + b->len + sizeof(size) + nl, b->value, n);
	b->len = need;
	return 0;
}

/* Reads the valid typed attributes of one lower dentry into b. No lock of ours is held. */
static int attrs_read(struct dentry *lower, struct attr_buf *b)
{
	ssize_t len;
	char *p;
	int err = 0;

	b->len = 0;
	len = vfs_listxattr(lower, b->list, XATTR_LIST_MAX);
	if (len == -EOPNOTSUPP || len == -ENODATA || len == 0)
		return 0;
	if (len == -ERANGE)
		return 0;	/* more than the list holds: left out of the index, reported by moobfs-check */
	if (len < 0)
		return (int)len;
	for (p = b->list; p < b->list + len && !err; p += strlen(p) + 1) {
		const char *name = p + MOOB_ATTR_PREFIX_LEN;
		ssize_t n;

		if (strncmp(p, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN))
			continue;
		if (moob_attr_name_reserved(name, strlen(name)) || !moob_attr_name_ok(name, strlen(name)))
			continue;
		n = xchg(&scan_getxattr_errno_once, 0);
		n = n > 0 ? -n : vfs_getxattr(&nop_mnt_idmap, lower, p, b->value,
						  MOOB_ATTR_VALUE_MAX + 1);
		if (n == -ENODATA || n == -ERANGE)
			continue;	/* removed meanwhile, or exceeds the typed-value limit */
		if (n < 0)
			return (int)n;
		if (!n || moob_attr_validate(b->value, n) != MOOB_ATTR_OK)
			continue;	/* invalid encodings are not indexed; moobfs-check reports them */
		if (b->value[0] == MOOB_T_RAW)
			continue;	/* raw payloads are stored but never indexed */
		err = attr_buf_add(b, name, n);
	}
	return err;
}

/* Puts what b holds in place of whatever the index has for ino. Caller holds the write lock. */
static int index_replace(struct moobl_index *ix, u64 ino, const struct moobl_place *pl,
			 const struct attr_buf *b)
{
	struct moobl_ix_inode *rec = xa_load(&ix->inodes, ino);
	struct moobl_ix_entry *e, *tmp;
	size_t off = 0;
	int err = 0;

	if (rec) {
		list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
	}
	while (off < b->len && !err) {
		const char *name = (const char *)b->data + off + sizeof(u16);
		size_t nl = strlen(name) + 1;
		u16 size;

		memcpy(&size, b->data + off, sizeof(size));
		err = index_put(ix, ino, pl, name, b->data + off + sizeof(size) + nl, size);
		off += sizeof(size) + nl + size;
	}
	return err;
}

static void record_dentry_path(struct moobl_index *ix, struct moobl_ix_inode *rec,
			       struct dentry *dentry);

/* Whether dentry still has the last component of path as its name (the rename takes d_lock). */
static bool name_is(struct dentry *dentry, const char *path)
{
	const char *base = strrchr(path, '/');
	size_t len;
	bool same;

	base = base ? base + 1 : path;
	len = strlen(base);
	spin_lock(&dentry->d_lock);
	same = dentry->d_name.len == len && !memcmp(dentry->d_name.name, base, len);
	spin_unlock(&dentry->d_lock);
	return same;
}

/*
 * Brings the index to what the lower filesystem holds for one file or directory, placed at
 * pl. A dentry that was removed meanwhile is skipped, and so is one that left parent, the
 * lower directory its name was read from (when given), and one whose name is no longer the last
 * component of path: the write path that removed, moved or renamed it has updated the index.
 * The name comes from path (the scan) or from where (a layer dentry).
 * With dom (the lower domain directory), the file's own homogeneity is not taken from pl but
 * decided here at every reading, after the loader is registered: a change of permissions that
 * completed before is seen by the reading, one that runs after makes the reading be done
 * again. pl->hetero then says only whether the directories down to the file pass. A
 * classification computed before the reading could otherwise outlive a chmod that found no
 * record to update (journal 0152).
 * -EAGAIN when updates to this very file kept arriving for MOOBL_LOAD_TRIES readings.
 */
static int index_load(struct super_block *sb, struct dentry *lower, struct dentry *parent,
		      const struct moobl_place *pl, struct inode *dom, const char *path,
		      struct dentry *where, struct attr_buf *b)
{
	struct moobl_index *ix = IX(sb);
	struct inode *li = d_inode(lower), *upper;
	struct moobl_loader ld = { .ino = li->i_ino };
	struct moobl_place cur = *pl;
	struct moobl_ix_inode *rec;
	unsigned int nofs;
	int tries, err = 0;

	nofs = ix_write_lock(ix);
	list_add(&ld.list, &ix->loaders);
	ix_write_unlock(ix, nofs);
	for (tries = 0; tries < MOOBL_LOAD_TRIES; tries++) {
		err = 0;
		if (!IS_ROOT(lower) && d_unhashed(lower))
			break;	/* removed */
		if (parent && READ_ONCE(lower->d_parent) != parent)
			break;	/* renamed away */
		if (path && !IS_ROOT(lower) && !name_is(lower, path))
			break;	/* renamed within its directory: the rename hook adopted it (0156) */
		if (dom && !pl->hetero) {
			cur.hetero = !target_ok(li, dom, &err);
			if (err)	/* an ACL that could not be read is not an answer */
				break;
		}
		err = attrs_read(lower, b);
		if (err)
			break;
		if (unlikely(READ_ONCE(scan_delay_us)) && path)
			usleep_range(scan_delay_us, scan_delay_us + 50);
		nofs = ix_write_lock(ix);
		if (ld.dirty) {		/* this file was updated while it was being read */
			ld.dirty = false;
			trace_moobfs_scan_reread(ix->dev, li->i_ino);
			ix_write_unlock(ix, nofs);
			cond_resched();
			err = -EAGAIN;
			continue;
		}
		list_del(&ld.list);
		err = index_replace(ix, li->i_ino, &cur, b);
		rec = err ? NULL : xa_load(&ix->inodes, li->i_ino);
		if (rec && path)
			err = index_set_path(ix, rec, path);	/* a scan has no other source of the name */
		else if (rec)
			record_dentry_path(ix, rec, where);
		ix_write_unlock(ix, nofs);
		if (!rec || err)
			return err;
		/*
		 * The layer inode may have been in memory before the record existed, in which
		 * case moobl_index_inode_cached() found nothing to link. Looked up after the
		 * record is in, so that one of the two always makes the link.
		 */
		upper = moobl_find_inode(sb, li);
		if (upper) {
			nofs = ix_write_lock(ix);
			rec = xa_load(&ix->inodes, li->i_ino);
			if (rec)
				rec->upper = upper;
			ix_write_unlock(ix, nofs);
			iput(upper);	/* outside the lock: the last reference evicts, which takes it */
		}
		return 0;
	}
	nofs = ix_write_lock(ix);
	list_del(&ld.list);
	ix_write_unlock(ix, nofs);
	return err;	/* -EAGAIN when every reading was overtaken by an update */
}

struct scan_name {
	struct list_head list;
	int len;
	char name[];
};

/*
 * Names read from one directory before they are handled. A directory is read in batches of this
 * many names, as getdents(2) reads it, instead of whole: a user could otherwise make a build hold
 * a listing as large as the directory, outside the index budget (review item 5, journal 0120).
 */
#define MOOBL_SCAN_BATCH 1024U

struct scan_ctx {
	struct dir_context ctx;
	struct list_head names;
	unsigned int count;	/* names in this batch */
	int err;
};

static bool scan_actor(struct dir_context *ctx, const char *name, int len, loff_t pos,
		       u64 ino, unsigned int d_type)
{
	struct scan_ctx *sc = container_of(ctx, struct scan_ctx, ctx);
	struct scan_name *sn;

	if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.'))
		return true;
	if (sc->count == MOOBL_SCAN_BATCH)
		return false;	/* not consumed: the next iterate_dir() starts with it */
	sn = kmalloc(sizeof(*sn) + len, GFP_KERNEL_ACCOUNT);
	if (!sn) {
		sc->err = -ENOMEM;
		return false;
	}
	sn->len = len;
	memcpy(sn->name, name, len);
	list_add_tail(&sn->list, &sc->names);
	sc->count++;
	return true;
}

struct scan_dir {
	struct list_head list;
	struct dentry *dentry;		/* lower directory, referenced */
	struct dentry *parent;		/* lower directory it was found in, referenced; NULL: the start */
	char *path;			/* mount-relative, "" for the root */
	unsigned int depth;		/* 0 for the root */
	struct moobl_place self;	/* partition of the directory's own attributes */
	bool skip_self;			/* its own attributes belong to another scan */
	u64 inner;			/* domain of entries inside it; 0: none */
	bool inner_ok;			/* every directory from the domain down to it passes */
	struct dentry *dom;		/* lower domain directory, referenced, or NULL */
};

/* The domain directory against which index_load() decides a scanned directory's own place. */
static struct inode *self_dom(const struct scan_dir *sd)
{
	return sd->self.domain && sd->dom && sd->self.domain == sd->inner ? d_inode(sd->dom) : NULL;
}

/* One walk of a part of the lower tree. */
struct scan_job {
	struct super_block *sb;
	struct attr_buf buf;
	struct list_head stack;		/* directories still to read */
	unsigned int stop;		/* directories at this depth are not entered; 0: none such */
	bool index;			/* false: only collect the directories at depth stop */
	struct list_head found;		/* those directories, when !index */
};

static void scan_dir_free(struct scan_dir *sd)
{
	dput(sd->dentry);
	dput(sd->parent);
	dput(sd->dom);
	kfree(sd->path);
	kfree(sd);
}

static void scan_list_free(struct list_head *head)
{
	while (!list_empty(head)) {
		struct scan_dir *sd = list_first_entry(head, struct scan_dir, list);

		list_del(&sd->list);
		scan_dir_free(sd);
	}
}

/* A child directory of the scan: its record takes cpath and goes on the stack. */
static int scan_push_dir(struct scan_job *job, struct scan_dir *cur, struct dentry *child,
			 char *cpath)
{
	struct moobl_index *ix = IX(job->sb);
	struct scan_dir *nd = kzalloc(sizeof(*nd), GFP_KERNEL_ACCOUNT);
	unsigned int dn = ix->domain_depth;

	if (!nd) {
		kfree(cpath);
		return -ENOMEM;
	}
	int err = 0;

	nd->dentry = dget(child);
	nd->parent = dget(cur->dentry);
	nd->path = cpath;
	nd->depth = cur->depth + 1;
	if (dn && nd->depth == dn) {
		nd->inner = d_inode(child)->i_ino;
		nd->inner_ok = true;
		nd->dom = dget(child);
	} else if (dn && nd->depth > dn) {
		struct inode *dom = d_inode(cur->dom);

		nd->dom = dget(cur->dom);
		nd->inner = cur->inner;
		nd->self.domain = cur->inner;
		nd->self.hetero = !cur->inner_ok;	/* the directory itself: index_load() */
		nd->inner_ok = cur->inner_ok && reach_ok(d_inode(child), dom, false, &err);
		if (err) {	/* an ACL that could not be read is not an answer */
			scan_dir_free(nd);
			return err;
		}
	}
	if (job->stop && nd->depth == job->stop) {
		if (!job->index) {
			list_add_tail(&nd->list, &job->found);
			return 0;
		}
		/* a domain directory: its own attributes belong outside, its content to its domain */
		err = index_load(job->sb, child, cur->dentry, &nd->self, self_dom(nd), cpath, NULL,
				 &job->buf);
		scan_dir_free(nd);
		return err;
	}
	list_add(&nd->list, &job->stack);
	return 0;
}

/* A child regular file of the scan: its attributes go into the index; frees cpath. */
static int scan_file(struct scan_job *job, struct scan_dir *cur, struct dentry *child, char *cpath)
{
	struct moobl_place fpl = { .domain = cur->inner, .hetero = cur->inner && !cur->inner_ok };
	int err;

	err = index_load(job->sb, child, cur->dentry, &fpl, cur->inner ? d_inode(cur->dom) : NULL,
			 cpath, NULL, &job->buf);
	kfree(cpath);
	return err;
}

/* One name read from a scanned directory: a directory is queued, a regular file indexed. */
static int scan_child(struct scan_job *job, struct scan_dir *cur, struct scan_name *sn)
{
	struct dentry *child = lookup_one_len_unlocked(sn->name, cur->dentry, sn->len);
	char *cpath;
	int err = 0;

	if (IS_ERR(child))	/* ENOENT: the directory died after its names were read (R2-10) */
		return PTR_ERR(child) == -ENOENT ? 0 : PTR_ERR(child);
	if (d_really_is_negative(child) || !(d_is_dir(child) || d_is_reg(child)))
		goto out;
	if (!job->index && !d_is_dir(child))
		goto out;
	err = moobl_other_device(job->sb, child);
	if (err) {
		if (err > 0)
			pr_info_once("moobfs_layer: a directory on another device (a btrfs subvolume) inside the lower tree is left out\n");
		err = err < 0 ? err : 0;
		goto out;
	}
	if (moobl_casefolded(child))	/* refused by lookups, so out of the answers too */
		goto out;
	if (IX(job->sb)->domain_depth && cur->depth >= MOOBL_MAX_DEPTH) {
		/*
		 * Beyond the depth where_is() can walk, the write paths place nothing: a chmod there
		 * sets no fallback and a write is not applied. What they cannot keep current is not
		 * indexed either (journal 0156).
		 */
		pr_info_once("moobfs_layer: names more than %d components below the mount root are left out of the index\n",
			     MOOBL_MAX_DEPTH);
		goto out;
	}
	cpath = kasprintf(GFP_KERNEL_ACCOUNT, "%s%s%.*s", cur->path,
			  cur->path[0] ? "/" : "", sn->len,
			  sn->name);
	if (!cpath) {
		err = -ENOMEM;
		goto out;
	}
	if (d_is_dir(child))
		err = scan_push_dir(job, cur, child, cpath);
	else
		err = scan_file(job, cur, child, cpath);
out:
	dput(child);
	return err;
}

/* Walks what is on job->stack, with the mounter's credentials already in effect. */
static int scan_run(struct scan_job *job)
{
	struct moobl_sb *sbi = MOOBL_SB(job->sb);
	int err = 0;

	while (!list_empty(&job->stack) && !err) {
		struct scan_dir *cur = list_first_entry(&job->stack, struct scan_dir, list);
		struct path dpath = { .mnt = sbi->lower.mnt, .dentry = cur->dentry };
		struct scan_ctx sc = { .ctx.actor = scan_actor, .names = LIST_HEAD_INIT(sc.names) };
		struct scan_name *sn, *tmp;
		struct file *dir;

		list_del(&cur->list);
		if (fatal_signal_pending(current)) {
			err = -EINTR;
			goto drop;
		}
		cond_resched();
		if (job->index && !cur->skip_self) {
			err = index_load(job->sb, cur->dentry, cur->parent, &cur->self,
					 self_dom(cur), cur->path, NULL, &job->buf);
			if (err)
				goto drop;
		}
		dir = dentry_open(&dpath, O_RDONLY | O_DIRECTORY, sbi->creds);
		if (IS_ERR(dir)) {
			/* removed since its name was read: nothing below it is left */
			err = PTR_ERR(dir) == -ENOENT && !IS_ROOT(cur->dentry) ? 0 : PTR_ERR(dir);
			goto drop;
		}
		for (;;) {	/* one batch of names at a time; the file keeps the position */
			bool full;

			sc.count = 0;
			err = iterate_dir(dir, &sc.ctx);
			if (err == -ENOENT && !IS_ROOT(cur->dentry))
				err = 0;
			if (!err)
				err = sc.err;
			full = sc.count == MOOBL_SCAN_BATCH;
			list_for_each_entry_safe(sn, tmp, &sc.names, list) {
				if (!err)
					err = scan_child(job, cur, sn);
				list_del(&sn->list);
				kfree(sn);
			}
			if (err || !full)
				break;
			if (fatal_signal_pending(current)) {
				err = -EINTR;
				break;
			}
			cond_resched();
		}
		fput(dir);
drop:
		scan_dir_free(cur);
	}
	scan_list_free(&job->stack);
	return err;
}

static int scan_job_init(struct scan_job *job, struct super_block *sb)
{
	job->sb = sb;
	INIT_LIST_HEAD(&job->stack);
	INIT_LIST_HEAD(&job->found);
	job->stop = 0;
	job->index = true;
	return attr_buf_init(&job->buf);
}

/* Queues the directory a scan starts from; path is copied. */
static int scan_start(struct scan_job *job, struct dentry *lower, const char *path,
		      unsigned int depth, bool domain_root)
{
	struct scan_dir *sd = kzalloc(sizeof(*sd), GFP_KERNEL_ACCOUNT);

	if (!sd)
		return -ENOMEM;
	sd->path = kstrdup(path, GFP_KERNEL_ACCOUNT);
	if (!sd->path) {
		kfree(sd);
		return -ENOMEM;
	}
	sd->dentry = dget(lower);
	sd->depth = depth;
	if (domain_root) {
		sd->skip_self = true;
		sd->inner = d_inode(lower)->i_ino;
		sd->inner_ok = true;
		sd->dom = dget(lower);
	}
	list_add(&sd->list, &job->stack);
	return 0;
}

/* Scans again the directories of the side list; takes what each entry holds. */
static int scan_moved(struct scan_job *job, struct list_head *todo, struct dentry *domlower,
		      u64 domain)
{
	struct moved_dir *md;
	int err = 0;

	list_for_each_entry(md, todo, list) {
		struct scan_dir *sd;

		if (!domain && job->stop && md->depth == job->stop) {
			/* a domain directory: only its own attributes belong outside */
			err = index_load(job->sb, md->lower, NULL, &md->self, NULL, md->path,
					 NULL, &job->buf);
			if (err)
				break;
			continue;
		}
		sd = kzalloc(sizeof(*sd), GFP_KERNEL_ACCOUNT);
		if (!sd) {
			err = -ENOMEM;
			break;
		}
		sd->dentry = md->lower;
		sd->path = md->path;
		md->lower = NULL;
		md->path = NULL;
		sd->depth = md->depth;
		sd->self = md->self;
		if (domain) {
			sd->inner = domain;
			sd->inner_ok = md->inner_ok;
			sd->dom = dget(domlower);
		}
		/* in the order of the renames: the last name a directory was given is scanned last */
		list_add_tail(&sd->list, &job->stack);
	}
	if (err) {
		scan_list_free(&job->stack);
		return err;
	}
	return scan_run(job);
}

/*
 * The generation of renames of domain directories and those above them, to sample before a
 * name is taken. The rename hook raises it under the lock after the lower rename, so a name
 * read from the lower tree after this sample is the new one or the build ends stale.
 */
static u64 rename_gen(struct moobl_index *ix)
{
	unsigned int nofs = ix_read_lock(ix);
	u64 gen = ix->top_gen;

	ix_read_unlock(ix, nofs);
	return gen;
}

/*
 * Makes sure the domain whose directory is lower (mount-relative path) is built, building it
 * if it is not; domain 0 is what belongs to no domain, read from the lower root down to the
 * domain directories. Callers of a domain being built wait for it. When the build does not fit
 * the index budget and may_release is set, the domains queried least recently are released
 * until it fits. Returns -EAGAIN when the domain kept changing under the build, -ENOMEM also
 * when it does not fit the budget; after a failure nothing of the domain is left in the index.
 * gen is ix->top_gen as sampled before path was taken (rename_gen()): a rename of the domain
 * directory or one above it after that sample leaves the build stale, never fresh under the
 * old name (issue #110).
 */
static int domain_ensure(struct super_block *sb, struct dentry *lower, u64 domain, const char *path,
			 u64 gen, bool may_release)
{
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct moobl_index *ix = &sbi->index;
	struct moobl_domain *dom = domain_get(ix, domain);
	const struct cred *old;
	struct scan_job job;
	unsigned int nofs, released = 0;
	u64 t0, used0, refused = 0;
	int tries, err;

	if (!dom)
		return -ENOMEM;
	if (domain_fresh(ix, dom)) {
		WRITE_ONCE(dom->used_at, jiffies);
		return 0;
	}
	if (mutex_lock_killable(&dom->build))
		return -EINTR;
	if (domain_fresh(ix, dom)) {
		WRITE_ONCE(dom->used_at, jiffies);
		mutex_unlock(&dom->build);
		return 0;
	}
	err = scan_job_init(&job, sb);
	if (err) {
		mutex_unlock(&dom->build);
		return err;
	}
	if (READ_ONCE(dom->state) == MOOBL_DOM_BUILT) {
		/* stale: taken down with no query running on it, then built as if never built */
		down_write(&ix->query_sem);
		nofs = ix_write_lock(ix);
		domain_takedown(ix, dom, domain);
		ix_write_unlock(ix, nofs);
		up_write(&ix->query_sem);
	}
	job.stop = domain ? 0 : ix->domain_depth;
	t0 = ktime_get_ns();
	used0 = READ_ONCE(ix->used);
	old = override_creds(sbi->creds);
	for (tries = 0; tries < MOOBL_BUILD_TRIES; tries++) {
		LIST_HEAD(todo);
		unsigned int rounds;

		nofs = ix_write_lock(ix);
		WRITE_ONCE(dom->state, MOOBL_DOM_BUILDING);
		refused = ix->refusals;	/* a build of another domain cannot clear what follows */
		dom->restart = false;
		dom->top_gen = gen;
		ix_write_unlock(ix, nofs);
		err = scan_start(&job, lower, path, domain ? ix->domain_depth : 0, domain != 0);
		if (!err)
			err = scan_run(&job);
		/* the side list: directories renamed meanwhile, until none is left under the lock */
		for (rounds = 0;; rounds++) {
			nofs = ix_write_lock(ix);
			if (!err && (dom->restart ||
				     (rounds == MOOBL_MOVED_ROUNDS && !list_empty(&dom->moved))))
				err = -EAGAIN;
			if (err || list_empty(&dom->moved))
				break;
			list_splice_init(&dom->moved, &todo);
			ix_write_unlock(ix, nofs);
			err = scan_moved(&job, &todo, lower, domain);
			moved_list_free(&todo);
		}
		/* still under the lock: no rename can be recorded between the check and the state */
		list_splice_init(&dom->moved, &todo);
		if (err) {
			domain_drop(ix, domain);
			WRITE_ONCE(dom->state, MOOBL_DOM_NONE);
		} else {
			WRITE_ONCE(dom->used_at, jiffies);
			WRITE_ONCE(dom->state, MOOBL_DOM_BUILT);
			dom->paths_stale = false;	/* every renamed directory was scanned again */
			WRITE_ONCE(dom->lost, false);	/* every update meanwhile is in the build */
			ix->domains_built++;
			ix->builds++;
		}
		/* over the budget: make room by releasing what was queried least recently */
		if (err == -ENOMEM && ix->refusals != refused && may_release &&
		    released < MOOBL_BUDGET_RELEASES) {
			ix_write_unlock(ix, nofs);
			moved_list_free(&todo);
			if (domain_release(ix, dom, false)) {
				released++;
				tries--;
				continue;
			}
			break;
		}
		ix_write_unlock(ix, nofs);
		moved_list_free(&todo);
		if (err != -EAGAIN)
			break;
	}
	revert_creds(old);
	attr_buf_free(&job.buf);
	trace_moobfs_domain_build(ix->dev, domain, err, min_t(int, tries + 1, MOOBL_BUILD_TRIES),
				  released, (s64)(READ_ONCE(ix->used) - used0), ktime_get_ns() - t0);
	mutex_unlock(&dom->build);
	if (err && err != -EINTR)
		pr_warn_ratelimited("moobfs_layer: building the index of a domain failed (%d)\n", err);
	return err;
}

/* Everything, for the mount-wide QUERY: what is outside the domains, then every domain. */
static int domain_ensure_all(struct super_block *sb)
{
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct moobl_index *ix = &sbi->index;
	const struct cred *old;
	struct scan_job job;
	struct scan_dir *sd;
	u64 gen = rename_gen(ix);	/* before the scan reads the names of the domains */
	int err;

	/* no release over the budget: this query needs every domain at once */
	err = domain_ensure(sb, sbi->lower.dentry, 0, "", gen, false);
	if (err || !ix->domain_depth)
		return err;
	err = scan_job_init(&job, sb);
	if (err)
		return err;
	job.stop = ix->domain_depth;
	job.index = false;
	old = override_creds(sbi->creds);
	err = scan_start(&job, sbi->lower.dentry, "", 0, false);
	if (!err)
		err = scan_run(&job);
	revert_creds(old);
	list_for_each_entry(sd, &job.found, list) {
		if (err)
			break;
		err = domain_ensure(sb, sd->dentry, d_inode(sd->dentry)->i_ino, sd->path, gen, false);
	}
	scan_list_free(&job.found);
	attr_buf_free(&job.buf);
	return err;
}

/* Mount: nothing is read but whether the lower root answers listxattr. */
void moobl_index_mount(struct super_block *sb)
{
	struct moobl_sb *sbi = MOOBL_SB(sb);
	const struct cred *old;
	ssize_t probe;

	if (!sbi->index.enabled)
		return;
	old = override_creds(sbi->creds);
	probe = vfs_listxattr(sbi->lower.dentry, NULL, 0);
	revert_creds(old);
	sbi->index.xattr_ok = probe >= 0 || probe == -ENODATA;
}

void moobl_index_init(struct super_block *sb, u64 budget, bool enabled, unsigned int domain_depth,
		      unsigned int idle)
{
	struct moobl_index *ix = IX(sb);

	/* a cursor carries the value of the entry it stopped at */
	BUILD_BUG_ON(MOOB_ATTR_STRING_MAX > MOOB_QUERY_VALUE_MAX);
	init_rwsem(&ix->lock);
	/* teach lockdep that reclaim takes this lock, so it reports an allocation outside a NOFS scope */
	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&ix->lock);
	fs_reclaim_release(GFP_KERNEL);
	ix->names = RB_ROOT;
	xa_init(&ix->inodes);
	xa_init(&ix->fallback);
	xa_init(&ix->domains);
	init_rwsem(&ix->query_sem);
	INIT_LIST_HEAD(&ix->loaders);
	mutex_init(&ix->outside.build);
	INIT_LIST_HEAD(&ix->outside.moved);
	ix->domain_depth = domain_depth;
	get_random_bytes(&ix->cursor_key, sizeof(ix->cursor_key));
	ix->budget = budget;
	ix->idle = idle;
	ix->dev = sb->s_dev;
	ix->enabled = enabled;
	ix->initialized = true;
}

/*
 * What the shrinker can free: an estimate in pages, the index memory shared out evenly among
 * the built domains, times the number of those not used in the last ix->idle seconds. The
 * index does not count its memory per domain.
 */
static unsigned long moobl_shrink_count(struct shrinker *s, struct shrink_control *sc)
{
	struct moobl_index *ix = s->private_data;
	unsigned long idle = reclaim_idle(ix), n = 0, idx;
	u32 built = READ_ONCE(ix->domains_built);
	struct moobl_domain *dom;

	if (!built)
		return SHRINK_EMPTY;
	if (READ_ONCE(ix->outside.state) == MOOBL_DOM_BUILT &&
	    time_after_eq(jiffies, READ_ONCE(ix->outside.used_at) + idle))
		n++;
	xa_for_each(&ix->domains, idx, dom)
		if (READ_ONCE(dom->state) == MOOBL_DOM_BUILT &&
		    time_after_eq(jiffies, READ_ONCE(dom->used_at) + idle))
			n++;
	if (!n)
		return SHRINK_EMPTY;
	return max_t(unsigned long, 1, div_u64(READ_ONCE(ix->used), PAGE_SIZE) * n / built);
}

/*
 * Releases built domains, least recently queried first, until about nr_to_scan pages are
 * freed. Without __GFP_FS the caller may hold filesystem locks of this mount; nothing is tried.
 */
static unsigned long moobl_shrink_scan(struct shrinker *s, struct shrink_control *sc)
{
	struct moobl_index *ix = s->private_data;
	unsigned long freed = 0;

	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;
	while (freed < sc->nr_to_scan) {
		u64 before = READ_ONCE(ix->used), after;

		if (!domain_release(ix, NULL, true))
			break;
		after = READ_ONCE(ix->used);
		freed += max_t(unsigned long, 1, div_u64(before > after ? before - after : 0, PAGE_SIZE));
	}
	return freed ? freed : SHRINK_STOP;
}

int moobl_index_shrinker_start(struct super_block *sb)
{
	struct moobl_index *ix = IX(sb);

	if (!ix->enabled)
		return 0;
	ix->shrinker = shrinker_alloc(0, "moobfs-index:%u:%u", MAJOR(sb->s_dev), MINOR(sb->s_dev));
	if (!ix->shrinker)
		return -ENOMEM;
	ix->shrinker->count_objects = moobl_shrink_count;
	ix->shrinker->scan_objects = moobl_shrink_scan;
	ix->shrinker->seeks = DEFAULT_SEEKS;
	ix->shrinker->private_data = ix;
	shrinker_register(ix->shrinker);
	return 0;
}

void moobl_index_destroy(struct super_block *sb)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_domain *dom;
	unsigned long ino;

	if (!ix->initialized)
		return;	/* fill_super failed before the index existed */
	shrinker_free(ix->shrinker);	/* waits for a scan that is running */
	nofs = ix_write_lock(ix);
	xa_for_each(&ix->inodes, ino, rec) {
		struct moobl_ix_entry *e, *tmp;

		list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
		cond_resched();	/* every inode of the mount, at unmount (journal 0120) */
	}
	xa_destroy(&ix->inodes);
	xa_destroy(&ix->fallback);
	xa_for_each(&ix->domains, ino, dom)
		kfree(dom);
	xa_destroy(&ix->domains);
	ix_write_unlock(ix, nofs);
}

/* ---- updates from main.c ------------------------------------------------ */

static void record_dentry_path(struct moobl_index *ix, struct moobl_ix_inode *rec,
			       struct dentry *dentry)
{
	char *buf = (char *)__get_free_page(GFP_KERNEL_ACCOUNT);
	char *p = buf ? dentry_path_raw(dentry, buf, PAGE_SIZE) : ERR_PTR(-ENOMEM);

	lockdep_assert_held_write(&ix->lock);
	if (IS_ERR(p) || index_set_path(ix, rec, *p == '/' ? p + 1 : p)) {
		/* no name rather than the old one: QUERY then takes the dentry cache or none (0156) */
		if (rec->path)
			uncharge(ix, strlen(rec->path) + 1);
		kfree(rec->path);
		rec->path = NULL;
	}
	if (buf)
		free_page((unsigned long)buf);
}

int moobl_index_set(struct super_block *sb, struct dentry *dentry, const char *name,
		    const void *value, size_t size)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct inode *inode = d_inode(dentry);
	struct moobl_ix_inode *rec;
	struct moobl_place pl;
	int err;

	if (!ix->enabled)
		return 0;
	if (((const u8 *)value)[0] == MOOB_T_RAW) {
		/* raw is stored and never indexed: an indexed value it replaces must go */
		moobl_index_remove(sb, inode->i_ino, name);
		return 0;
	}
	/*
	 * A write that resolved its path before an unlink and ran after it (both under the
	 * inode lock): the file has no name left, and a record made now would stay for ever
	 * under the name it had. A file made with O_TMPFILE is read when it is linked in.
	 */
	if (!inode->i_nlink)
		return 0;
	nofs = place_and_lock(ix, dentry, &pl);
	index_touched(ix, inode->i_ino);
	if (!domain_live(ix, pl.domain)) {
		/* nothing to keep current: the build of the domain will read this value */
		ix_write_unlock(ix, nofs);
		return 0;
	}
	err = index_put(ix, inode->i_ino, &pl, name, value, size);
	if (err) {
		/* the lower write already succeeded: the domain is built again by the next query */
		domain_lost(ix, pl.domain, err);
	} else {
		rec = xa_load(&ix->inodes, inode->i_ino);
		if (rec) {
			rec->upper = inode;
			record_dentry_path(ix, rec, dentry);
		}
	}
	ix_write_unlock(ix, nofs);
	return err;
}

void moobl_index_remove(struct super_block *sb, u64 ino, const char *name)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_ix_entry *e;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	index_touched(ix, ino);
	rec = xa_load(&ix->inodes, ino);
	if (rec) {
		e = entry_find(rec, name);
		if (e)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
	}
	ix_write_unlock(ix, nofs);
}

void moobl_index_drop_inode(struct super_block *sb, u64 ino)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_ix_entry *e, *tmp;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	index_touched(ix, ino);
	rec = xa_load(&ix->inodes, ino);
	if (rec) {
		list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
	}
	ix_write_unlock(ix, nofs);
}

/* Whether rec holds every attribute of its file: true once the domain it is in is built. */
static bool rec_whole(struct moobl_index *ix, const struct moobl_ix_inode *rec)
{
	struct moobl_domain *dom = rec->domain ? xa_load(&ix->domains, (unsigned long)rec->domain)
					       : &ix->outside;

	return dom && dom->state == MOOBL_DOM_BUILT;
}

/*
 * A file that arrives in a built domain from one that is not (a rename, a new hard link) has
 * no record to move: its attributes are read from the lower filesystem. On failure the domain
 * is built again by the next query, as after any update that fails after the lower change.
 */
static void index_adopt(struct super_block *sb, struct dentry *lower, struct dentry *where,
			const struct moobl_place *pl)
{
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct moobl_index *ix = &sbi->index;
	const struct cred *old;
	struct attr_buf buf;
	unsigned int nofs;
	int err;

	err = attr_buf_init(&buf);
	if (!err) {
		old = override_creds(sbi->creds);
		err = index_load(sb, lower, NULL, pl, NULL, NULL, where, &buf);
		revert_creds(old);
		attr_buf_free(&buf);
	}
	if (err) {
		nofs = ix_write_lock(ix);
		domain_lost(ix, pl->domain, err);
		ix_write_unlock(ix, nofs);
	}
}

/*
 * What a build needs to scan a renamed directory again: where it now is (the rename target
 * where, before the VFS moves the dentries) and how it is placed. *target is the domain whose
 * build must do it, 0 for the part outside every domain. NULL when it cannot be described;
 * the caller then makes the build start again.
 */
static struct moved_dir *dir_moved_prepare(struct moobl_index *ix, struct dentry *moved,
					   struct dentry *where, u64 *target)
{
	unsigned int n = ix->domain_depth, k;
	struct moved_dir *md = NULL;
	struct dentry *domd;
	char *buf, *p;
	bool above;

	*target = 0;
	if (!where_is(where, n, &k, &domd))
		return NULL;
	if (n && k > n)
		*target = moobl_lower_inode(d_inode(domd))->i_ino;
	buf = (char *)__get_free_page(GFP_KERNEL_ACCOUNT);
	md = kzalloc(sizeof(*md), GFP_KERNEL_ACCOUNT);
	if (!buf || !md)
		goto fail;
	p = dentry_path_raw(where, buf, PAGE_SIZE);
	if (IS_ERR(p))
		goto fail;
	md->path = kstrdup(*p == '/' ? p + 1 : p, GFP_KERNEL_ACCOUNT);
	if (!md->path)
		goto fail;
	md->depth = k;
	if (n && k > n) {
		struct inode *dom = moobl_lower_inode(d_inode(domd));

		place_dentry_above(ix, where, d_inode(moved), &md->self, &above);
		if (md->self.domain)	/* its own homogeneity is decided when it is read */
			md->self.hetero = !above;
		md->inner_ok = above && reach_ok(moobl_lower_inode(d_inode(moved)), dom, false, NULL);
	}
	md->lower = dget(moobl_lower_dentry(moved));
	free_page((unsigned long)buf);
	dput(domd);
	return md;
fail:
	if (buf)
		free_page((unsigned long)buf);
	kfree(md);
	dput(domd);
	return NULL;
}

/*
 * inode now lives at the name of where (the rename target dentry, called before the VFS
 * moves the dentries, so where already carries the new parent and name).
 */
void moobl_index_renamed(struct super_block *sb, struct dentry *moved, struct dentry *where)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct inode *inode = d_inode(moved);
	struct moobl_ix_inode *rec;
	struct moobl_ix_entry *e, *tmp;
	struct moobl_place pl;
	bool adopt = false;

	if (!ix->enabled)
		return;
	if (S_ISDIR(inode->i_mode)) {
		/*
		 * Same depth and domain (EXDEV otherwise): the recorded names went stale, and a
		 * build in progress may miss the subtree, so it is told to scan it again.
		 */
		struct moobl_domain *dom;
		struct moved_dir *md;
		u64 target;

		md = dir_moved_prepare(ix, moved, where, &target);
		nofs = ix_write_lock(ix);
		index_touched(ix, inode->i_ino);
		ix->move_gen++;
		ix->paths_stale = true;	/* descendants keep old names; QUERY uses the dentry cache */
		dom = target ? xa_load(&ix->domains, (unsigned long)target) : &ix->outside;
		if (!target && ix->domain_depth)
			ix->top_gen++;	/* a domain directory or one above: every recorded name below */
		if (dom)
			dom->paths_stale = true;	/* the next query builds it again (domain_fresh) */
		if (dom && dom->state == MOOBL_DOM_BUILDING) {
			if (md) {
				list_add_tail(&md->list, &dom->moved);
				md = NULL;
			} else {
				dom->restart = true;
			}
		}
		ix_write_unlock(ix, nofs);
		moved_dir_free(md);
		return;
	}
	place_dentry(ix, where, inode, &pl);	/* a file may change domain: move its entries */
	nofs = ix_write_lock(ix);
	index_touched(ix, inode->i_ino);
	ix->move_gen++;
	rec = xa_load(&ix->inodes, inode->i_ino);
	if (!domain_live(ix, pl.domain)) {
		if (rec) {	/* it left the built part of the index */
			list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
				entry_free(ix, e);
			inode_rec_put_if_empty(ix, rec);
		}
	} else if (rec && rec_whole(ix, rec)) {
		record_dentry_path(ix, rec, where);
		if (rec_set_partition(ix, rec, &pl))
			inode_rec_put_if_empty(ix, rec);	/* emptied: nothing else frees it (R2-8) */
	} else {
		/*
		 * No record, or one that may hold only part of the file: in a domain being built
		 * a write creates the record of a file the scan has not reached, with the one
		 * attribute written. If such a file is then renamed, the scan no longer finds it
		 * where it read its name and leaves it to this path, which must read it whole.
		 */
		adopt = true;
	}
	ix_write_unlock(ix, nofs);
	if (adopt)
		index_adopt(sb, moobl_lower_dentry(moved), where, &pl);
}

/* new_dentry is a new hard link: in a built domain the file may have no record yet. */
void moobl_index_linked(struct super_block *sb, struct dentry *new_dentry)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_place pl;
	bool adopt;

	if (!ix->enabled || d_really_is_negative(new_dentry))
		return;
	place_dentry(ix, new_dentry, NULL, &pl);
	nofs = ix_write_lock(ix);
	index_touched(ix, d_inode(new_dentry)->i_ino);
	ix->move_gen++;
	rec = xa_load(&ix->inodes, d_inode(new_dentry)->i_ino);
	adopt = domain_live(ix, pl.domain) && !(rec && rec_whole(ix, rec));
	ix_write_unlock(ix, nofs);
	if (adopt)
		index_adopt(sb, moobl_lower_dentry(new_dentry), new_dentry, &pl);
}

/*
 * A change of mode, owner, group, access ACL or link count. For a file or a directory
 * carrying attributes, its own partition is recomputed. For a directory inside a domain
 * (or the domain directory itself), every file below it may change homogeneity: rather
 * than walking the subtree, the domain falls back to per-result checks until the next
 * mount (docs/spec-query-v2.md §3; recorded in journal 0035).
 */
void moobl_index_perm_changed(struct super_block *sb, struct dentry *dentry)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_place pl;
	unsigned int depth;
	u64 domain;

	if (!ix->enabled || !ix->domain_depth || d_really_is_negative(dentry))
		return;
	if (d_is_dir(dentry) && depth_and_domain(ix, dentry, &depth, &domain) &&
	    depth >= ix->domain_depth && domain)
		if (xa_err(xa_store(&ix->fallback, (unsigned long)domain, xa_mk_value(1),
				    GFP_KERNEL_ACCOUNT)))
			WRITE_ONCE(ix->fallback_all, true);	/* no memory to note it: check everywhere */
	nofs = place_and_lock(ix, dentry, &pl);
	index_touched(ix, d_inode(dentry)->i_ino);
	rec = xa_load(&ix->inodes, d_inode(dentry)->i_ino);
	if (rec && rec_set_partition(ix, rec, &pl))
		inode_rec_put_if_empty(ix, rec);	/* emptied: nothing else frees it (R2-8) */
	ix_write_unlock(ix, nofs);
}

/*
 * A directory keeps its depth and domain across a rename, otherwise the partition of its
 * whole subtree would change: the caller returns EXDEV (docs/spec-query-v2.md §5).
 */
bool moobl_rename_crosses_domain(struct super_block *sb, struct dentry *old_dentry,
				 struct dentry *new_dentry)
{
	struct moobl_index *ix = IX(sb);
	unsigned int od, nd;
	u64 odom, ndom;

	if (!ix->enabled || !ix->domain_depth)
		return false;
	if (!depth_and_domain(ix, old_dentry, &od, &odom) ||
	    !depth_and_domain(ix, new_dentry, &nd, &ndom))
		return true;
	if (od == ix->domain_depth)
		odom = ndom = 0;	/* a domain directory itself: its domain is its own inode */
	return od != nd || odom != ndom;
}

void moobl_index_inode_cached(struct super_block *sb, struct inode *inode)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, inode->i_ino);
	if (rec)
		rec->upper = inode;
	ix_write_unlock(ix, nofs);
}

void moobl_index_inode_evicted(struct super_block *sb, struct inode *inode)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, inode->i_ino);
	if (rec && rec->upper == inode)
		rec->upper = NULL;
	ix_write_unlock(ix, nofs);
}

/* ---- QUERY -------------------------------------------------------------- */

/* allowed: the term flags the caller's request version accepts (0 for the v1 QUERY). */
static int term_check(const struct moob_query_term *t, u8 allowed)
{
	size_t nlen = strnlen(t->name, sizeof(t->name));

	if (nlen == sizeof(t->name) || !moob_attr_name_ok(t->name, nlen) || (t->flags & ~allowed) ||
	    t->pad[0] || t->pad[1] || t->pad[2])
		return -EINVAL;
	if (t->op == MOOB_OP_EXISTS)	/* "exists or missing" would match everything */
		return (t->type || t->len || t->flags) ? -EINVAL : 0;
	if (t->op < MOOB_OP_EQ || t->op > MOOB_OP_PREFIX)
		return -EINVAL;
	switch (t->type) {
	case MOOB_T_STRING:
		if (t->len > MOOB_QUERY_VALUE_MAX || !moob_utf8_valid(t->value, t->len))
			return -EINVAL;
		return 0;
	case MOOB_T_INT64:
	case MOOB_T_TIME:
	case MOOB_T_DOUBLE:
		if (t->len != 8 || t->op == MOOB_OP_PREFIX)
			return -EINVAL;
		if (t->type == MOOB_T_DOUBLE) {
			u8 v[9] = { MOOB_T_DOUBLE };

			memcpy(v + 1, t->value, 8);
			if (moob_attr_validate(v, 9) != MOOB_ATTR_OK)
				return -EINVAL;
		}
		return 0;
	case MOOB_T_BOOL:
		if (t->len != 1 || t->value[0] > 1 || t->op == MOOB_OP_PREFIX)
			return -EINVAL;
		return 0;
	default:
		return -EINVAL;	/* raw values are not indexed and cannot be queried */
	}
}

static bool entry_matches(const struct moobl_ix_entry *e, const struct moob_query_term *t)
{
	int c;

	if (t->op == MOOB_OP_EXISTS)
		return true;
	if (e->type != t->type)
		return false;
	if (t->op == MOOB_OP_PREFIX)
		return e->len >= t->len && !memcmp(e->value, t->value, t->len);
	c = value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
	switch (t->op) {
	case MOOB_OP_EQ: return c == 0;
	case MOOB_OP_LT: return c < 0;
	case MOOB_OP_LE: return c <= 0;
	case MOOB_OP_GT: return c > 0;
	case MOOB_OP_GE: return c >= 0;
	default: return false;
	}
}

static bool inode_matches_rest(struct moobl_ix_inode *rec, const struct moob_query_term *terms,
			       u32 nterms, struct moobl_ix_name *const *tn)
{
	u32 i;

	for (i = 1; i < nterms; i++) {
		struct moobl_ix_entry *e = tn ? entry_find_node(rec, tn[i]) :
					   entry_find(rec, terms[i].name);

		if (!e) {
			if (!(terms[i].flags & MOOB_TERM_MISSING_OK))
				return false;
			continue;
		}
		if (!entry_matches(e, &terms[i]))
			return false;
	}
	return true;
}

/* Ordered scans walk the order attribute: every term is tested on the inode. */
static bool inode_matches_all(struct moobl_ix_inode *rec, const struct moob_query_term *terms,
			      u32 nterms, struct moobl_ix_name *const *tn)
{
	u32 i;

	for (i = 0; i < nterms; i++) {
		struct moobl_ix_entry *e = tn ? entry_find_node(rec, tn[i]) :
					   entry_find(rec, terms[i].name);

		if (!e) {
			if (!(terms[i].flags & MOOB_TERM_MISSING_OK))
				return false;
			continue;
		}
		if (!entry_matches(e, &terms[i]))
			return false;
	}
	return true;
}

/*
 * The name of rec, relative to the mount root: from the dentry cache when the inode is in
 * memory, else the name recorded at the scan. NULL when neither is known. *held gets a dentry
 * reference to drop once the index lock is released: dropping the last reference to an
 * unlinked file evicts its inode, and eviction takes the index lock.
 */
/*
 * Whether the names recorded for rec's domain can be trusted. A directory rename leaves the
 * recorded names of everything below it stale; since ADR-0012 that is known per domain, and a
 * domain built after the rename has fresh names again.
 */
static bool paths_fresh(struct moobl_index *ix, const struct moobl_ix_inode *rec)
{
	struct moobl_domain *dom = rec->domain ? xa_load(&ix->domains, (unsigned long)rec->domain)
					       : &ix->outside;

	return dom && !dom->paths_stale && dom->top_gen == ix->top_gen;
}

static const char *result_path(struct moobl_index *ix, struct moobl_ix_inode *rec,
			       char *pathbuf, struct dentry **held)
{
	lockdep_assert_held(&ix->lock);
	*held = NULL;
	if (rec->upper) {
		struct dentry *alias = d_find_alias(rec->upper);

		if (alias) {
			char *p = dentry_path_raw(alias, pathbuf, PAGE_SIZE);

			*held = alias;
			if (!IS_ERR(p))
				return *p == '/' ? p + 1 : p;
		}
	}
	if (rec->path && paths_fresh(ix, rec))
		return rec->path;
	return NULL;
}

/*
 * Candidates are gathered under the index lock in bounded chunks, then checked
 * against the caller's own credentials with the lock released: the recorded
 * path is resolved through the VFS (search permission on every directory,
 * read permission on the file), the resolved object must belong to this mount
 * and carry the indexed inode number. Entries the caller may not reach are
 * omitted silently and do not count toward limit or truncated, so neither
 * existence nor count leaks. Operational failures (ENOMEM, EFAULT) are
 * returned, never turned into an empty answer. The scan resumes after each
 * chunk from the last (type, value, ino) key, which stays meaningful even if
 * the tree changed meanwhile, so the number of examined candidates is not
 * bounded by the result limit.
 */
#define MOOBL_QUERY_CHUNK 256U

struct moobl_candidate {
	u64 ino;
	char *path;	/* mount-relative, or NULL when unknown */
	struct dentry *held;	/* dropped after the index lock, see result_path() */
	u8 type;	/* scan key of the entry that produced it (QUERY2 cursors) */
	u16 len;
	u8 *value;
	bool ok;	/* a result: the caller may read it, or no check is asked */
};

struct moobl_resume {
	u64 pdomain;		/* partition being scanned */
	bool phetero;
	bool valid;		/* the entry key below is set */
	u8 type;
	u16 len;
	u64 ino;
	int lead;		/* equality plan: index into leap_idx of the scanned run; -1: unset */
	u8 value[MOOB_QUERY_VALUE_MAX];
};

/*
 * The walk runs in the caller's mount namespace and can wait for as long as something the
 * caller controls decides: a FUSE filesystem mounted over a directory of the domain whose
 * daemon never answers (request_wait_answer, fs/fuse/dev.c, cannot be interrupted once the
 * request is read). So it runs with query_sem released (query_run, journal 0116), and it does
 * not leave this mount nor follow symbolic links: a result is on this mount under its
 * recorded name, and anything else would be refused below anyway.
 */
/* 1 = readable, 0 = inaccessible/stale, -ENOMEM = unable to decide under memory pressure. */
static int caller_may_read(struct file *file, struct super_block *sb, const char *path, u64 ino)
{
	struct path p;
	int err, ok = 0;

	lockdep_assert_not_held(&IX(sb)->query_sem);
	if (!path || !*path)
		return false;
	err = vfs_path_lookup(file->f_path.dentry, file->f_path.mnt, path,
			      LOOKUP_NO_XDEV | LOOKUP_NO_SYMLINKS, &p);
	if (err)
		return err == -ENOMEM ? err : 0;
	if (p.mnt == file->f_path.mnt && p.dentry->d_sb == sb && d_inode(p.dentry) &&
	    d_inode(p.dentry)->i_ino == ino) {
		err = inode_permission(&nop_mnt_idmap, d_inode(p.dentry), MAY_READ);
		ok = err == -ENOMEM ? err : !err;
	}
	path_put(&p);
	return ok;
}

/* First entry strictly after the resume key, or the first entry when no key. */
static struct rb_node *first_after(struct moobl_ix_name *nm, const struct moobl_resume *r)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	if (!r->valid)
		return rb_first(&nm->entries);
	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);

		if (entry_cmp(e, r->type, r->value, r->len, r->ino) > 0) {
			best = n;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	return best;
}

/* Last entry strictly before the resume key (descending scans). */
static struct rb_node *last_before(struct moobl_ix_name *nm, const struct moobl_resume *r)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);

		if (entry_cmp(e, r->type, r->value, r->len, r->ino) < 0) {
			best = n;
			n = n->rb_right;
		} else {
			n = n->rb_left;
		}
	}
	return best;
}

/* Ordered scans: the first (ascending) or last (descending) entry of the order type. */
static struct rb_node *seek_type(struct moobl_ix_name *nm, u8 type, bool desc)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);
		bool go_left = desc ? e->type > type : e->type >= type;

		if (go_left) {
			if (!desc)
				best = n;
			n = n->rb_left;
		} else {
			if (desc)
				best = n;
			n = n->rb_right;
		}
	}
	return best;
}

/*
 * Where the scan of the first term starts and where it can stop. Entries are ordered by
 * (type, value, ino), so every op except EXISTS matches a contiguous run: EQ, GE, GT and
 * PREFIX start at the first entry whose (type, value) is not below the term; LT and LE start
 * at the first entry of the term's type. EXISTS keeps the full scan.
 */
static struct rb_node *seek_first(struct moobl_ix_name *nm, const struct moob_query_term *t)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	if (t->op == MOOB_OP_EXISTS)
		return rb_first(&nm->entries);
	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);
		int c;

		if (t->op == MOOB_OP_LT || t->op == MOOB_OP_LE)
			c = e->type < t->type ? -1 : 0;
		else
			c = value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
		if (c >= 0) {
			best = n;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	return best;
}

/* True when no entry at or after e (in tree order) can match the term. */
static bool past_end(const struct moobl_ix_entry *e, const struct moob_query_term *t)
{
	int c;

	if (t->op == MOOB_OP_EXISTS)
		return false;
	if (e->type != t->type)
		return e->type > t->type;
	c = value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
	switch (t->op) {
	case MOOB_OP_EQ:
	case MOOB_OP_LE:
		return c > 0;
	case MOOB_OP_LT:
		return c >= 0;
	case MOOB_OP_PREFIX:
		/* strings with a given prefix form one run starting at the prefix itself */
		return c > 0 && !(e->len >= t->len && !memcmp(e->value, t->value, t->len));
	default:
		return false;
	}
}

/* What a query scans and how its results are authorized and named. */
struct moobl_qscope {
	bool all;		/* v1: every partition of terms[0].name */
	u64 domain;		/* QUERY2: the domain's homogeneous partition */
	bool check;		/* authorize each result with the caller's credentials */
	unsigned int strip;	/* leading path components dropped from mount-relative paths */
	const char *scan;	/* attribute tree walked: order_by, or terms[0].name */
	bool ordered;		/* walk the order_type run of scan; test every term */
	bool desc;
	u8 otype;
	bool leap;		/* intersect the equality terms below by leapfrog (spec §4.4) */
	u8 nleap;
	u8 leap_idx[MOOB_QUERY_MAX_TERMS];	/* equality terms; the first one leads */
};

/* Path relative to the scope: the mount-relative path without its first `strip` names. */
static const char *scope_path(const char *path, unsigned int strip)
{
	while (strip--) {
		path = strchr(path, '/');
		if (!path)
			return NULL;
		path++;
	}
	return *path ? path : NULL;
}

/* Records the inode of e as a candidate; with want_key, also e's scan key for the cursor. */
static int add_candidate(struct moobl_index *ix, struct moobl_ix_entry *e,
			 const struct moobl_qscope *sc, struct moobl_candidate *c, char *pathbuf,
			 bool want_key)
{
	const char *path;
	int err = 0;

	lockdep_assert_held(&ix->lock);
	path = result_path(ix, e->owner, pathbuf, &c->held);
	c->ino = e->owner->ino;
	c->path = NULL;
	c->value = NULL;
	if (path) {
		const char *rel = scope_path(path, sc->strip);

		/* the whole name: the permission check resolves it; only the reply is cut */
		if (rel) {
			c->path = kstrdup(rel, GFP_KERNEL_ACCOUNT);
			if (!c->path)
				err = -ENOMEM;
		}
	}
	if (want_key) {
		c->type = e->type;
		c->len = e->len;
		c->value = kmalloc(max_t(u16, e->len, 1), GFP_KERNEL_ACCOUNT);
		if (c->value)
			memcpy(c->value, e->value, e->len);
		else
			err = -ENOMEM;
	}
	return err;
}

/* First entry of the equality run of t whose inode is >= ino, or NULL past the run. */
static struct moobl_ix_entry *seek_eq(struct moobl_ix_name *nm, const struct moob_query_term *t,
				      u64 ino)
{
	struct rb_node *n = nm->entries.rb_node;
	struct moobl_ix_entry *best = NULL;

	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);

		if (entry_cmp(e, t->type, t->value, t->len, ino) >= 0) {
			best = e;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	if (!best || best->type != t->type ||
	    value_cmp(best->type, best->value, best->len, t->type, t->value, t->len))
		return NULL;
	return best;
}

/* True while e is still inside the equality run of t. */
static bool in_eq_run(const struct moobl_ix_entry *e, const struct moob_query_term *t)
{
	return e->type == t->type &&
	       !value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
}

/*
 * Equality plan (docs/spec-query-v2.md §4.4, revised by journal 0044): inside one value, the
 * entries of an equality term are ordered by inode. Once per call, the runs of all equality
 * terms are walked in lockstep from the resume inode until the first one ends: that one is
 * the shortest, at a cost proportional to it. Then that run alone is scanned and every term is
 * tested on each inode. Results come in inode order whichever run leads, so a call resumes
 * after the last inode only. The first implementation (journal 0037) was a leapfrog with one
 * tree seek per step; on the benchmark host it was 2.7 to 3.6 times slower than scanning a
 * short run, and only faster when the first term's run was long. Called with the index lock
 * held for reading.
 */
static int leap_collect(struct moobl_index *ix, const struct moobl_qscope *sc,
			const struct moob_query_term *terms, u32 nterms,
			struct moobl_resume *resume, struct moobl_candidate *cand, u32 *ncand,
			u32 cap, char *pathbuf, bool want_key, bool *more)
{
	struct moobl_ix_name *nm[MOOB_QUERY_MAX_TERMS];
	struct moobl_ix_name *tn[MOOB_QUERY_MAX_TERMS];
	struct rb_node *it[MOOB_QUERY_MAX_TERMS];
	const struct moob_query_term *lt;
	struct moobl_ix_entry *e;
	u64 x = 0;
	u32 k;

	lockdep_assert_held(&ix->lock);
	for (k = 0; k < nterms; k++)
		tn[k] = name_find(ix, terms[k].name, sc->domain, false);

	if (resume->valid) {
		if (resume->ino == U64_MAX)
			return 0;
		x = resume->ino + 1;
	}
	for (k = 0; k < sc->nleap; k++) {
		nm[k] = name_find(ix, terms[sc->leap_idx[k]].name, sc->domain, false);
		if (!nm[k])
			return 0;
	}
	if (resume->lead < 0) {
		for (k = 0; k < sc->nleap; k++) {
			e = seek_eq(nm[k], &terms[sc->leap_idx[k]], x);
			if (!e)
				return 0;	/* an empty run: the intersection is empty */
			it[k] = &e->node;
		}
		while (resume->lead < 0) {
			for (k = 0; k < sc->nleap; k++) {
				struct rb_node *n = rb_next(it[k]);

				if (!n || !in_eq_run(rb_entry(n, struct moobl_ix_entry, node),
						     &terms[sc->leap_idx[k]])) {
					resume->lead = (int)k;	/* the first run to end */
					break;
				}
				it[k] = n;
			}
		}
	}
	lt = &terms[sc->leap_idx[resume->lead]];
	for (e = seek_eq(nm[resume->lead], lt, x); e;) {
		struct rb_node *n;

		resume->valid = true;
		resume->type = e->type;
		resume->len = e->len;
		resume->ino = e->owner->ino;
		memcpy(resume->value, e->value, e->len);
		if (inode_matches_all(e->owner, terms, nterms, tn)) {
			int err = add_candidate(ix, e, sc, &cand[*ncand], pathbuf, want_key);

			(*ncand)++;
			if (err)
				return err;
			if (*ncand == cap) {
				*more = true;
				return 0;
			}
		}
		n = rb_next(&e->node);
		e = n ? rb_entry(n, struct moobl_ix_entry, node) : NULL;
		if (e && !in_eq_run(e, lt))
			e = NULL;
	}
	return 0;
}

/* Fills one result record from a candidate; last gets its scan key (cursor). */
static void put_result(struct moob_query_result *one, const struct moobl_candidate *c,
		       struct moobl_resume *last)
{
	memset(one, 0, sizeof(*one));
	one->ino = c->ino;
	if (c->path) {
		size_t len = strlen(c->path);

		if (len >= MOOB_QUERY_PATH_MAX) {
			one->flags = MOOB_RES_PATH_TRUNCATED;
			len = MOOB_QUERY_PATH_MAX - 1;
		}
		one->path_len = (u32)len;
		memcpy(one->path, c->path, len);	/* the record is zeroed: NUL follows */
	} else {
		one->flags = MOOB_RES_PATH_UNKNOWN;
	}
	if (last) {
		last->valid = true;
		last->type = c->type;
		last->len = c->len;
		last->ino = c->ino;
		memcpy(last->value, c->value, c->len);
	}
}

/*
 * Results reach the caller only here, once ix->query_sem is released. A copy to user space can
 * wait on a page fault for as long as whoever backs that memory decides, and a query must not
 * hold up the queries of every other user of the mount meanwhile: a writer of query_sem (a
 * rebuild, a release over the budget) would wait for it, and new readers behind the writer
 * (journal 0105). btrfs's search ioctl avoids faulting under its locks for a related reason
 * (commit a48b73eca4ce).
 */
static int results_to_user(struct moobl_index *ix, u64 uptr, const struct moob_query_result *k,
			   u32 n)
{
	lockdep_assert_not_held(&ix->query_sem);
	if (n && copy_to_user(u64_to_user_ptr(uptr), k, (size_t)n * sizeof(*k)))
		return -EFAULT;
	return 0;
}

static long query2_pin(struct super_block *sb, struct file *file, u64 domain);

/*
 * Takes query_sem for reading again after the per-result checks of a chunk ran without it,
 * and makes sure what the query reads is still there. QUERY2: its domain may have been taken
 * down meanwhile (a rebuild after a directory rename, a release over the budget); it is built
 * and pinned again, and the scan resumes from its key as after any concurrent change. v1
 * reads every partition: if any was taken down, the answer could silently lack it, so the
 * query fails with EAGAIN as when its first pin did not hold (do_query). Returns with
 * query_sem held for reading in every case, as its callers expect.
 */
static long query_repin(struct super_block *sb, struct file *file, const struct moobl_qscope *sc,
			u64 gen)
{
	struct moobl_index *ix = IX(sb);
	struct moobl_domain *dom;
	long err;

	down_read(&ix->query_sem);
	if (sc->all)
		return READ_ONCE(ix->drop_gen) == gen ? 0 : -EAGAIN;
	dom = xa_load(&ix->domains, (unsigned long)sc->domain);
	if (dom && domain_fresh(ix, dom))
		return 0;
	up_read(&ix->query_sem);
	err = query2_pin(sb, file, sc->domain);
	if (err)
		down_read(&ix->query_sem);
	return err;
}

/*
 * Runs one query, with query_sem held for reading by the caller. start (may be NULL) resumes
 * strictly after (or, descending, before) a scan key; last (may be NULL) receives the key of
 * the last result returned. With per-result checks, query_sem is released while a chunk is
 * checked and taken again before the next one (query_repin): a check walks paths with the
 * caller's credentials and can wait on anything the caller mounted, and a wait with
 * query_sem held would stop every query of the mount once a writer queues behind it
 * (rwsem_down_read_slowpath), the class of finding F7 (journal 0116).
 */
static long query_run(struct super_block *sb, struct file *file,
		      const struct moob_query_term *terms, u32 nterms, u32 limit,
		      struct moob_query_result *kres, const struct moobl_qscope *sc,
		      const struct moobl_resume *start,
		      struct moobl_resume *last, u32 *count_out, u32 *truncated_out)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_candidate *cand;
	struct moobl_resume *resume;
	char *pathbuf;
	struct moobl_ix_name *tn_buf[MOOB_QUERY_MAX_TERMS];
	struct moobl_ix_name **tn = sc->all ? NULL : tn_buf;
	u32 i, count = 0, truncated = 0;
	bool exhausted = false;
	long err = 0;

	cand = kcalloc(MOOBL_QUERY_CHUNK, sizeof(*cand), GFP_KERNEL_ACCOUNT);
	resume = kzalloc(sizeof(*resume), GFP_KERNEL_ACCOUNT);
	pathbuf = (char *)__get_free_page(GFP_KERNEL_ACCOUNT);
	if (!cand || !resume || !pathbuf) {
		err = -ENOMEM;
		goto out;
	}
	if (start)
		*resume = *start;
	resume->lead = -1;
	resume->pdomain = sc->all ? resume->pdomain : sc->domain;
	if (last)
		last->valid = false;
	while (!exhausted && !err) {
		struct moobl_ix_name *nm;
		struct moobl_ix_entry *seen = NULL;
		struct rb_node *n;
		u32 ncand = 0;
		bool more = false;
		/*
		 * Without per-result checks every candidate is a result, so a chunk needs at most
		 * the results still missing plus one (that one only sets truncated). Gathering a
		 * full chunk for a small limit walked far past the answer (journal 0048).
		 */
		u32 cap = sc->check ? MOOBL_QUERY_CHUNK :
			  min_t(u32, MOOBL_QUERY_CHUNK, limit - count + 1);

		nofs = ix_read_lock(ix);
		if (sc->leap) {
			err = leap_collect(ix, sc, terms, nterms, resume, cand, &ncand, cap, pathbuf,
					   last != NULL, &more);
			goto chunk_done;
		}
		/*
		 * QUERY2: candidates all sit in the domain's homogeneous partition, so each term's
		 * attribute is one name node and entries are matched by pointer, not by strcmp
		 */
		if (!sc->all)
			for (i = 0; i < nterms; i++)
				tn[i] = name_find(ix, terms[i].name, sc->domain, false);
		nm = sc->all ? name_lower_bound(ix, sc->scan, resume->pdomain, resume->phetero)
			     : name_find(ix, sc->scan, sc->domain, false);
		if (nm && (nm->domain != resume->pdomain || nm->hetero != resume->phetero)) {
			resume->pdomain = nm->domain;	/* the partition we stopped in is gone */
			resume->phetero = nm->hetero;
			resume->valid = false;
		}
		if (!nm)
			n = NULL;
		else if (resume->valid)
			n = sc->desc ? last_before(nm, resume) : first_after(nm, resume);
		else
			n = sc->ordered ? seek_type(nm, sc->otype, sc->desc) : seek_first(nm, &terms[0]);
		while (nm && !err) {
			struct moobl_ix_entry *e = n ? rb_entry(n, struct moobl_ix_entry, node) : NULL;
			bool end = !e || (sc->ordered ? e->type != sc->otype : past_end(e, &terms[0]));
			bool match;

			if (end) {
				struct rb_node *next = sc->all ? rb_next(&nm->node) : NULL;
				struct moobl_ix_name *nx = next ? rb_entry(next, struct moobl_ix_name, node)
								: NULL;

				if (!nx || strcmp(nx->name, sc->scan))
					break;
				nm = nx;	/* v1: next partition of the same name */
				resume->pdomain = nm->domain;
				resume->phetero = nm->hetero;
				resume->valid = false;
				seen = NULL;
				n = seek_first(nm, &terms[0]);
				continue;
			}
			seen = e;	/* the resume key is copied from it when the chunk ends */
			n = sc->desc ? rb_prev(n) : rb_next(n);
			if (sc->ordered)
				match = inode_matches_all(e->owner, terms, nterms, tn);
			else
				match = entry_matches(e, &terms[0]) &&
					inode_matches_rest(e->owner, terms, nterms, tn);
			if (!match)
				continue;
			err = add_candidate(ix, e, sc, &cand[ncand], pathbuf, last != NULL);
			ncand++;
			if (ncand == cap) {
				more = true;
				break;
			}
		}
		if (seen) {
			resume->valid = true;
			resume->type = seen->type;
			resume->len = seen->len;
			resume->ino = seen->owner->ino;
			memcpy(resume->value, seen->value, seen->len);
		}
chunk_done:
		if (!more)
			exhausted = true;
		ix_read_unlock(ix, nofs);
		for (i = 0; i < ncand; i++) {
			dput(cand[i].held);
			cand[i].held = NULL;
			cand[i].ok = !err && !sc->check;
		}
		if (!err && sc->check && ncand) {
			/* stable here: a takedown holds query_sem for writing */
			u64 gen = READ_ONCE(ix->drop_gen);
			long rerr;

			up_read(&ix->query_sem);
			for (i = 0; i < ncand && !err; i++) {
				int readable = caller_may_read(file, sb, cand[i].path, cand[i].ino);

				if (readable < 0)
					err = readable;
				else
					cand[i].ok = readable;
			}
			rerr = query_repin(sb, file, sc, gen);
			if (rerr && !err)
				err = rerr;
		}
		for (i = 0; i < ncand; i++) {
			bool ok = !err && cand[i].ok;

			if (ok && count == limit) {
				truncated = 1;
				exhausted = true;
			} else if (ok) {
				put_result(&kres[count], &cand[i], last);
				count++;
			}
			kfree(cand[i].path);
			cand[i].path = NULL;
			kfree(cand[i].value);
			cand[i].value = NULL;
		}
	}
	*count_out = count;
	*truncated_out = truncated;
out:
	if (cand)
		for (i = 0; i < MOOBL_QUERY_CHUNK; i++) {
			dput(cand[i].held);
			kfree(cand[i].path);
			kfree(cand[i].value);
		}
	kfree(cand);
	kfree(resume);
	if (pathbuf)
		free_page((unsigned long)pathbuf);
	return err;
}

/* v1: every partition, each result checked; privileged when domains are on (spec §4.5). */
static long do_query(struct super_block *sb, struct file *file, void __user *arg)
{
	struct moobl_index *ix = IX(sb);
	struct moobl_qscope sc = { .all = true, .check = true };
	struct moob_query_result *kres = NULL;
	struct moob_query *q;
	u32 i;
	long err = 0;

	if (!ix->enabled)
		return -EOPNOTSUPP;	/* noindex (ENOSYS until journal 0050) */
	if (ix->domain_depth && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	/* the request is read and checked before anything is built for it (journal 0105) */
	q = memdup_user(arg, sizeof(*q));
	if (IS_ERR(q))
		return PTR_ERR(q);
	if (q->version != MOOB_UAPI_VERSION || !q->nterms || q->nterms > MOOB_QUERY_MAX_TERMS ||
	    !q->limit || q->limit > MOOB_QUERY_MAX_LIMIT || q->flags || q->reserved[0] ||
	    q->reserved[1]) {
		err = -EINVAL;
		goto out;
	}
	for (i = 0; i < q->nterms && !err; i++)
		err = term_check(&q->terms[i], 0);
	if (err)
		goto out;
	kres = moobl_qbuf_get(file, q->limit);
	if (!kres) {
		err = -ENOMEM;
		goto out;
	}
	/* this query reads every partition: all built, and none taken down while it runs */
	for (i = 0;; i++) {
		u64 gen = READ_ONCE(ix->drop_gen);

		err = domain_ensure_all(sb);
		if (err)
			goto out;
		down_read(&ix->query_sem);
		if (READ_ONCE(ix->drop_gen) == gen)
			break;
		up_read(&ix->query_sem);
		if (i == 8) {
			err = -EAGAIN;
			goto out;
		}
	}
	sc.scan = q->terms[0].name;
	err = query_run(sb, file, q->terms, q->nterms, q->limit, kres, &sc, NULL, NULL,
			&q->count, &q->truncated);
	up_read(&ix->query_sem);
	if (!err)
		err = results_to_user(ix, q->results, kres, q->count);
	if (!err && copy_to_user(arg, q, sizeof(*q)))
		err = -EFAULT;
out:
	if (kres)
		moobl_qbuf_put(file, kres);
	kfree(q);
	return err;
}

static bool all_zero(const void *p, size_t n)
{
	const u8 *b = p;

	while (n--)
		if (*b++)
			return false;
	return true;
}

/*
 * QUERY2 cursor (docs/spec-query-v2.md §4.1): the scan key of the last result returned,
 * a hash of the query (domain, fallback state, order and terms; not the limit) and a MAC,
 * both keyed with a per-mount random key, so a cursor from another mount or query, a
 * corrupted one, or one issued before the domain fell back is refused with ESTALE. Values
 * longer than the inline space are recovered from the inode's current entry and must
 * still agree with the stored prefix and length.
 */
#define MOOBL_CURSOR_MAGIC 0xc2
#define MOOBL_CURSOR_INLINE 96

struct moobl_cursor {
	u8 magic;
	u8 type;
	u16 len;
	u8 pad[4];
	u64 ino;
	u64 qhash;
	u8 value[MOOBL_CURSOR_INLINE];
	u64 mac;
} __packed;

struct moobl_qhash_head {
	u64 domain;
	u8 fallback;
	u8 order;
	u8 order_type;
	u8 pad;
	u32 nterms;
	char order_by[MOOB_QUERY_NAME_MAX + 1];
};

static int query_hash(struct moobl_index *ix, const struct moob_query2 *q, u64 domain,
		      bool fallback, u64 *out)
{
	size_t tsz = (size_t)q->nterms * sizeof(q->terms[0]);
	struct moobl_qhash_head *h = kzalloc(sizeof(*h) + tsz, GFP_KERNEL_ACCOUNT);

	if (!h)
		return -ENOMEM;
	h->domain = domain;
	h->fallback = fallback;
	h->order = q->order;
	h->order_type = q->order_type;
	h->nterms = q->nterms;
	memcpy(h->order_by, q->order_by, sizeof(h->order_by));
	memcpy(h + 1, q->terms, tsz);
	*out = siphash(h, sizeof(*h) + tsz, &ix->cursor_key);
	kfree(h);
	return 0;
}

static void cursor_encode(struct moobl_index *ix, u8 *out, const struct moobl_resume *r,
			  u64 qhash)
{
	struct moobl_cursor c;

	BUILD_BUG_ON(sizeof(c) != MOOB_QUERY2_CURSOR);
	memset(&c, 0, sizeof(c));
	c.magic = MOOBL_CURSOR_MAGIC;
	c.type = r->type;
	c.len = r->len;
	c.ino = r->ino;
	c.qhash = qhash;
	memcpy(c.value, r->value, min_t(u16, r->len, MOOBL_CURSOR_INLINE));
	c.mac = siphash(&c, offsetof(struct moobl_cursor, mac), &ix->cursor_key);
	memcpy(out, &c, sizeof(c));
}

static int cursor_decode(struct moobl_index *ix, const u8 *in, u64 qhash, const char *scan,
			 const struct moobl_qscope *sc, struct moobl_resume *r)
{
	unsigned int nofs;
	struct moobl_cursor c;

	memcpy(&c, in, sizeof(c));
	if (c.magic != MOOBL_CURSOR_MAGIC ||
	    c.mac != siphash(&c, offsetof(struct moobl_cursor, mac), &ix->cursor_key) ||
	    c.qhash != qhash || c.len > MOOB_QUERY_VALUE_MAX ||
	    (sc->ordered && c.type != sc->otype))
		return -ESTALE;
	r->valid = true;
	r->type = c.type;
	r->len = c.len;
	r->ino = c.ino;
	if (sc->leap) {
		r->len = 0;	/* the equality plan resumes after the inode, whichever run leads */
	} else if (c.len <= MOOBL_CURSOR_INLINE) {
		memcpy(r->value, c.value, c.len);
	} else {
		struct moobl_ix_inode *rec;
		struct moobl_ix_entry *e = NULL;
		int err = -ESTALE;

		nofs = ix_read_lock(ix);
		rec = xa_load(&ix->inodes, c.ino);
		if (rec)
			e = entry_find(rec, scan);
		if (e && e->type == c.type && e->len == c.len &&
		    !memcmp(e->value, c.value, MOOBL_CURSOR_INLINE)) {
			memcpy(r->value, e->value, e->len);
			err = 0;
		}
		ix_read_unlock(ix, nofs);
		if (err)
			return err;
	}
	return 0;
}

static int order_check(const struct moob_query2 *q)
{
	size_t n = strnlen(q->order_by, sizeof(q->order_by));

	if (!n)
		return (q->order || q->order_type || !q->nterms) ? -EINVAL : 0;
	if (n == sizeof(q->order_by) || !moob_attr_name_ok(q->order_by, n))
		return -EINVAL;
	if (q->order != MOOB_ORDER_ASC && q->order != MOOB_ORDER_DESC)
		return -EINVAL;
	switch (q->order_type) {
	case MOOB_T_STRING:
	case MOOB_T_INT64:
	case MOOB_T_DOUBLE:
	case MOOB_T_TIME:
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * The name of lower relative to the lower root, in buf (two pages). The layer's own dentry is
 * moved by the VFS only after the rename hook has raised the generation, so its name may still
 * be the old one after the sample; the lower dentry was moved before.
 */
static char *lower_rel_path(struct super_block *sb, struct dentry *lower, char *buf)
{
	char *root = dentry_path_raw(MOOBL_SB(sb)->lower.dentry, buf + PAGE_SIZE, PAGE_SIZE);
	char *p = dentry_path_raw(lower, buf, PAGE_SIZE);
	size_t n;

	if (IS_ERR(root))
		return root;
	if (IS_ERR(p))
		return p;
	n = strcmp(root, "/") ? strlen(root) : 0;
	if (strncmp(p, root, n) || (p[n] != '/' && p[n]))
		return ERR_PTR(-ESTALE);	/* no longer below the lower root */
	p += n;
	return *p == '/' ? p + 1 : p;
}

/*
 * QUERY2 on a domain directory (docs/spec-query-v2.md §4): the caller must be able to read
 * and traverse the domain directory now; results come from the domain's homogeneous
 * partition without per-result checks, unless a permission change inside the domain made
 * it fall back to them. With order_by the attribute's entries of order_type are walked in
 * the requested direction; a cursor resumes after the last result of a previous call.
 */
/*
 * The first query on a domain builds its part of the index (ADR-0012), and so does the first
 * one after a directory rename made it stale. Returns with ix->query_sem held for reading and
 * the domain built and fresh: it cannot be taken down until the caller releases the semaphore.
 */
static long query2_pin(struct super_block *sb, struct file *file, u64 domain)
{
	struct moobl_index *ix = IX(sb);
	struct dentry *lower = moobl_lower_dentry(file->f_path.dentry);
	struct moobl_domain *dom;
	char *buf, *p;
	long err = 0;
	int tries;
	u64 gen;

	for (tries = 0; tries < 8; tries++) {
		dom = xa_load(&ix->domains, (unsigned long)domain);
		if (!dom || !domain_fresh(ix, dom)) {
			buf = (char *)__get_free_pages(GFP_KERNEL_ACCOUNT, 1);
			if (!buf)
				return -ENOMEM;
			gen = rename_gen(ix);
			p = lower_rel_path(sb, lower, buf);
			if (IS_ERR(p))
				err = PTR_ERR(p);
			else
				err = domain_ensure(sb, lower, domain, p, gen, true);
			free_pages((unsigned long)buf, 1);
			if (err)
				return err;
			dom = xa_load(&ix->domains, (unsigned long)domain);
		}
		down_read(&ix->query_sem);
		if (dom && domain_fresh(ix, dom)) {
			WRITE_ONCE(dom->used_at, jiffies);
			return 0;
		}
		up_read(&ix->query_sem);
	}
	return -EAGAIN;	/* directories kept being renamed in it */
}

static long query2_target(struct super_block *sb, struct file *file, u64 *domain,
			  unsigned int *depth)
{
	struct moobl_index *ix = IX(sb);

	if (!ix->enabled || !ix->domain_depth)
		return -EOPNOTSUPP;	/* noindex (ENOSYS until journal 0050) or domains off */
	if (!d_is_dir(file->f_path.dentry) ||
	    !depth_and_domain(ix, file->f_path.dentry, depth, domain) ||
	    *depth != ix->domain_depth)
		return -ENOTTY;
	if (inode_permission(&nop_mnt_idmap, file_inode(file), MAY_EXEC | MAY_READ))
		return -EACCES;
	return 0;
}

static long query2_validate(const struct moob_query2 *q)
{
	u32 i, plain = 0;
	long err;

	if (q->version != MOOB_QUERY2_VERSION || q->nterms > MOOB_QUERY_MAX_TERMS ||
	    !q->limit || q->limit > MOOB_QUERY_MAX_LIMIT || q->flags ||
	    !all_zero(q->pad, sizeof(q->pad)) || !all_zero(q->reserved, sizeof(q->reserved)))
		return -EINVAL;
	err = order_check(q);
	for (i = 0; i < q->nterms && !err; i++) {
		err = term_check(&q->terms[i], MOOB_TERM_MISSING_OK);
		if (!(q->terms[i].flags & MOOB_TERM_MISSING_OK))
			plain++;
	}
	/* a scan needs one term that only matches carriers of its attribute (spec §4.1) */
	if (!err && !q->order_by[0] && !plain)
		err = -EINVAL;
	return err;
}

/*
 * An LSM that labels inodes (SELinux, Smack) decides access by labels the homogeneity rule
 * cannot see, so a partition built from mode bits and ACLs may hold files the policy hides from
 * the caller. With one active, every QUERY2 result is checked for the caller, as after a
 * permission change (finding F6 of ADR-0011, journal 0104). An LSM that keeps labels for an
 * inode names them in security_inode_listsecurity(); AppArmor and Landlock keep none.
 */
static bool lsm_labels(struct super_block *sb)
{
	return security_inode_listsecurity(d_inode(sb->s_root), NULL, 0) > 0;
}

/* Runs a validated request (kernel copy) and fills its count, truncated and cursor. */
static long query2_exec(struct super_block *sb, struct file *file, struct moob_query2 *q,
			u64 domain, unsigned int depth, struct moob_query_result *kres)
{
	struct moobl_index *ix = IX(sb);
	struct moobl_qscope sc = { 0 };
	struct moobl_resume *start = NULL, *last = NULL;
	struct moob_query_term *t = NULL;
	u64 qhash;
	u32 i;
	long err;

	sc.domain = domain;
	sc.strip = depth;
	sc.check = READ_ONCE(ix->fallback_all) ||
		   xa_load(&ix->fallback, (unsigned long)domain) != NULL || lsm_labels(sb);
	sc.ordered = q->order_by[0] != '\0';
	sc.desc = q->order == MOOB_ORDER_DESC;
	sc.otype = q->order_type;
	err = query_hash(ix, q, domain, sc.check, &qhash);
	if (err)
		return err;
	/* execution copy of the terms: a plain scan must lead with a term without MISSING_OK */
	t = kmemdup(q->terms, sizeof(q->terms), GFP_KERNEL_ACCOUNT);
	start = kzalloc(sizeof(*start), GFP_KERNEL_ACCOUNT);
	last = kzalloc(sizeof(*last), GFP_KERNEL_ACCOUNT);
	if (!t || !start || !last) {
		err = -ENOMEM;
		goto out;
	}
	if (!sc.ordered && (t[0].flags & MOOB_TERM_MISSING_OK)) {
		for (i = 1; i < q->nterms; i++)
			if (!(t[i].flags & MOOB_TERM_MISSING_OK)) {
				swap(t[0], t[i]);
				break;
			}
	}
	sc.scan = sc.ordered ? q->order_by : t[0].name;
	if (!sc.ordered) {
		for (i = 0; i < q->nterms; i++)
			if (t[i].op == MOOB_OP_EQ && !(t[i].flags & MOOB_TERM_MISSING_OK))
				sc.leap_idx[sc.nleap++] = (u8)i;
		sc.leap = sc.nleap >= 2;
		if (sc.leap)
			sc.scan = t[sc.leap_idx[0]].name;	/* cursor key: the lead term */
	}
	if (!all_zero(q->cursor, sizeof(q->cursor))) {
		err = cursor_decode(ix, q->cursor, qhash, sc.scan, &sc, start);
		if (err)
			goto out;
	}
	err = query_run(sb, file, t, q->nterms, q->limit, kres, &sc, start, last,
			&q->count, &q->truncated);
	if (err)
		goto out;
	memset(q->cursor, 0, sizeof(q->cursor));
	if (q->truncated && last->valid)
		cursor_encode(ix, q->cursor, last, qhash);
out:
	kfree(t);
	kfree(start);
	kfree(last);
	return err;
}

/*
 * One validated QUERY2: built and pinned, run into a kernel buffer, unpinned, and only then
 * copied to the caller (results_to_user).
 */
static long query2_one(struct super_block *sb, struct file *file, struct moob_query2 *q,
		       u64 domain, unsigned int depth)
{
	struct moob_query_result *kres = moobl_qbuf_get(file, q->limit);
	long err;

	if (!kres)
		return -ENOMEM;
	err = query2_pin(sb, file, domain);
	if (!err) {
		err = query2_exec(sb, file, q, domain, depth, kres);
		up_read(&IX(sb)->query_sem);
	}
	if (!err)
		err = results_to_user(IX(sb), q->results, kres, q->count);
	moobl_qbuf_put(file, kres);
	return err;
}

static long do_query2(struct super_block *sb, struct file *file, void __user *arg)
{
	struct moob_query2 *q;
	unsigned int depth;
	u64 domain;
	long err;

	err = query2_target(sb, file, &domain, &depth);
	if (err)
		return err;
	q = memdup_user(arg, sizeof(*q));
	if (IS_ERR(q))
		return PTR_ERR(q);
	err = query2_validate(q);
	if (!err)
		err = query2_one(sb, file, q, domain, depth);
	if (!err && copy_to_user(arg, q, sizeof(*q)))
		err = -EFAULT;
	kfree(q);
	return err;
}

/* Several QUERY2 requests on one domain: one permission check, all validated first (§4.3). */
static long do_query2_batch(struct super_block *sb, struct file *file, void __user *arg)
{
	struct moob_query2_batch b;
	struct moob_query2 *qs;
	void __user *uq;
	unsigned int depth;
	u64 domain;
	u32 i;
	long err;

	err = query2_target(sb, file, &domain, &depth);
	if (err)
		return err;
	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;
	if (b.version != MOOB_QUERY2_VERSION || !b.nqueries || b.nqueries > MOOB_QUERY2_BATCH_MAX ||
	    !all_zero(b.reserved, sizeof(b.reserved)))
		return -EINVAL;
	uq = u64_to_user_ptr(b.queries);
	qs = kvmalloc_array(b.nqueries, sizeof(*qs), GFP_KERNEL_ACCOUNT);
	if (!qs)
		return -ENOMEM;
	if (copy_from_user(qs, uq, b.nqueries * sizeof(*qs))) {
		err = -EFAULT;
		goto out;
	}
	for (i = 0; i < b.nqueries && !err; i++)
		err = query2_validate(&qs[i]);
	for (i = 0; i < b.nqueries && !err; i++)
		err = query2_one(sb, file, &qs[i], domain, depth);
	if (!err && copy_to_user(uq, qs, b.nqueries * sizeof(*qs)))
		err = -EFAULT;
out:
	kvfree(qs);
	return err;
}

static long do_get_info(struct super_block *sb, void __user *arg)
{
	unsigned int nofs;
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct moobl_index *ix = &sbi->index;
	struct moob_info info;
	const char *fs = sbi->lower.dentry->d_sb->s_type->name;
	bool admin = ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN);

	memset(&info, 0, sizeof(info));
	info.uapi_version = MOOB_UAPI_VERSION;
	info.format_version = MOOB_ATTR_FORMAT_VERSION;
	info.attr_name_max = MOOB_ATTR_NAME_MAX;
	info.attr_string_max = MOOB_ATTR_STRING_MAX;
	info.attr_raw_max = MOOB_ATTR_RAW_MAX;
	info.attr_max_per_file = MOOB_ATTR_MAX_PER_FILE;
	strscpy(info.lower_fs, fs, sizeof(info.lower_fs));
	/* Capabilities are advertised from the lower filesystem type, not probed. */
	if (!strcmp(fs, "btrfs"))
		info.caps |= MOOB_CAP_REFLINK | MOOB_CAP_DATA_CHECKSUMS | MOOB_CAP_SNAPSHOTS;
	else if (!strcmp(fs, "xfs") || !strcmp(fs, "ocfs2") || !strcmp(fs, "bcachefs"))
		info.caps |= MOOB_CAP_REFLINK;
	if (sbi->nolinks)
		info.flags |= MOOB_FLAG_NOLINKS;
	nofs = ix_read_lock(ix);
	if (ix->xattr_ok)
		info.caps |= MOOB_CAP_XATTR;
	if (!ix->enabled)
		info.flags |= MOOB_FLAG_INDEX_OFF;
	if (ix->paths_stale)
		info.flags |= MOOB_FLAG_PATHS_STALE;
	if (ix->incomplete)
		info.flags |= MOOB_FLAG_INDEX_INCOMPLETE;
	info.index_budget = ix->budget;
	info.domain_depth = ix->domain_depth;
	if (ix->enabled)
		info.flags |= MOOB_FLAG_ON_DEMAND;
	if (ix->domain_depth)
		info.flags |= MOOB_FLAG_DOMAINS;
	/* The counters sum every user's memories: administrators only (ADR-0011, F1). */
	if (admin) {
		info.flags |= MOOB_FLAG_COUNTERS;
		info.index_used = ix->used;
		info.index_entries = ix->entries;
		info.index_inodes = ix->inode_count;
		info.heterogeneous = ix->hetero_count;
		info.domains_built = ix->domains_built;
		info.domain_builds = ix->builds;
		info.domain_releases = ix->releases;
	}
	ix_read_unlock(ix, nofs);
	if (admin) {
		unsigned long idx;
		void *v;

		xa_for_each(&ix->fallback, idx, v)
			info.fallback_domains++;
	}
	return copy_to_user(arg, &info, sizeof(info)) ? -EFAULT : 0;
}

long moobl_root_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct super_block *sb = file_inode(file)->i_sb;

	if (cmd == MOOB_IOC_QUERY2)
		return do_query2(sb, file, (void __user *)arg);
	if (cmd == MOOB_IOC_QUERY2_BATCH)
		return do_query2_batch(sb, file, (void __user *)arg);
	if (file->f_path.dentry != sb->s_root)
		return -ENOTTY;
	switch (cmd) {
	case MOOB_IOC_QUERY:
		return do_query(sb, file, (void __user *)arg);
	case MOOB_IOC_GET_INFO:
		return do_get_info(sb, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}
