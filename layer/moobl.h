/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared declarations of the MoobFS memory layer (main.c and index.c). */
#ifndef MOOBL_H
#define MOOBL_H
#include <linux/fs.h>
#include <linux/rbtree.h>
#include <linux/xarray.h>
#include <linux/rwsem.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/path.h>
#include <linux/cred.h>
#include <linux/siphash.h>

#define MOOBL_SUPER_MAGIC 0x4d4f4f42	/* "MOOB" */
#define MOOBL_NAME "moobfs_layer"
#define MOOBL_INDEX_BUDGET_DEFAULT (128ULL << 20)
#define MOOBL_INDEX_IDLE_DEFAULT 60U

/*
 * One permission domain's part of the index (ADR-0012): not built until somebody queries the
 * domain, then kept current by the write paths. state changes under the index lock; build
 * serializes the builders of this domain and is taken before any lock of the lower filesystem.
 */
enum { MOOBL_DOM_NONE, MOOBL_DOM_BUILDING, MOOBL_DOM_BUILT };

struct moobl_domain {
	struct mutex build;
	u8 state;
	struct list_head moved;		/* directories renamed in it during its build (index lock) */
	bool restart;			/* one of them could not be recorded: build again */
	bool paths_stale;		/* a directory was renamed in it since it was built */
	bool lost;			/* an update of it failed since it was built (F2) */
	u64 top_gen;			/* ix->top_gen when its build began */
	unsigned long used_at;		/* jiffies of the last query that used it */
};

struct moobl_index {
	struct rw_semaphore lock;
	struct rb_root names;		/* struct moobl_ix_name, by attribute name */
	struct xarray inodes;		/* ino -> struct moobl_ix_inode */
	u64 used, budget;		/* bytes accounted to the index */
	u32 entries, inode_count;
	bool enabled;			/* false with noindex= */
	bool paths_stale;		/* a directory was renamed since mount (GET_INFO) */
	u64 top_gen;			/* bumped when a domain directory, or one above, is renamed */
	bool xattr_ok;			/* lower root answered listxattr */
	bool incomplete;		/* not set since ADR-0016: a failed update rebuilds its domain */
	bool initialized;		/* moobl_index_init() ran for this superblock */
	unsigned int domain_depth;	/* depth of domain directories below the root; 0: off */
	struct xarray fallback;		/* domain ino -> value: answer with per-result checks */
	bool fallback_all;		/* a domain could not be recorded above: check every domain */
	u32 hetero_count;		/* indexed inodes failing the homogeneity rule */
	siphash_key_t cursor_key;	/* per-mount key authenticating QUERY2 cursors */
	struct xarray domains;		/* domain ino -> struct moobl_domain, never removed */
	struct moobl_domain outside;	/* what belongs to no domain (everything with domain_depth=0) */
	struct rw_semaphore query_sem;	/* read: a query runs; write: a built domain is taken down */
	u64 drop_gen;			/* bumped when a built domain is taken down */
	struct list_head loaders;	/* struct moobl_loader: files being read into the index now */
	u64 move_gen;			/* bumped by every rename and link: a placement is recomputed on change */
	u32 domains_built;		/* domains in state built, the outside part included */
	u32 builds;			/* builds completed since mount */
	u32 releases;			/* built domains released since mount */
	bool over_budget;		/* charge() refused an allocation (index lock) */
	unsigned int idle;		/* seconds a used domain is kept from the shrinker */
	struct shrinker *shrinker;	/* releases built domains under memory pressure */
	dev_t dev;			/* the layer's anonymous device, for tracepoints */
};

struct moobl_sb {
	struct path lower;		/* lower root */
	dev_t lower_dev;		/* device getattr reports for the lower root */
	const struct cred *creds;	/* mounter credentials for delegated work */
	bool nolinks;			/* refuse link and symlink creation */
	struct moobl_index index;
};

struct moobl_inode {
	struct inode vfs_inode;
	struct inode *lower;
};

struct moobl_dentry {
	struct path lower;
	struct rcu_head rcu;
};

static inline struct moobl_sb *MOOBL_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct moobl_inode *MOOBL_I(struct inode *inode)
{
	return container_of(inode, struct moobl_inode, vfs_inode);
}

static inline struct inode *moobl_lower_inode(struct inode *inode)
{
	return MOOBL_I(inode)->lower;
}

static inline struct path *moobl_lower_path(struct dentry *dentry)
{
	return &((struct moobl_dentry *)dentry->d_fsdata)->lower;
}

static inline struct dentry *moobl_lower_dentry(struct dentry *dentry)
{
	return moobl_lower_path(dentry)->dentry;
}

/* main.c */
struct inode *moobl_find_inode(struct super_block *sb, struct inode *lower);
int moobl_other_device(struct super_block *sb, struct dentry *lower);

/* index.c */
void moobl_index_init(struct super_block *sb, u64 budget, bool enabled, unsigned int domain_depth,
		      unsigned int idle);
int moobl_index_shrinker_start(struct super_block *sb);
void moobl_index_mount(struct super_block *sb);
void moobl_index_destroy(struct super_block *sb);
int moobl_index_set(struct super_block *sb, struct dentry *dentry, const char *name,
		    const void *value, size_t size);
void moobl_index_remove(struct super_block *sb, u64 ino, const char *name);
void moobl_index_drop_inode(struct super_block *sb, u64 ino);
void moobl_index_renamed(struct super_block *sb, struct dentry *moved, struct dentry *where);
void moobl_index_linked(struct super_block *sb, struct dentry *new_dentry);
void moobl_index_inode_cached(struct super_block *sb, struct inode *inode);
void moobl_index_inode_evicted(struct super_block *sb, struct inode *inode);
long moobl_root_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void moobl_index_perm_changed(struct super_block *sb, struct dentry *dentry);
bool moobl_rename_crosses_domain(struct super_block *sb, struct dentry *old_dentry,
				 struct dentry *new_dentry);

#endif
