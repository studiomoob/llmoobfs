// SPDX-License-Identifier: GPL-2.0-only
/*
 * MoobFS memory layer: a stacked filesystem over one directory of a lower filesystem (ext4,
 * btrfs or XFS). This file holds the pass-through (inodes, dentries, name, file and directory
 * operations, typed attribute validation, mount); index.c holds the attribute index and the
 * query ioctls. Stacking approach: ADR-0007; credential model: ADR-0011.
 *
 * Patterns follow fs/ecryptfs (one upper inode per lower inode, lower path
 * kept in dentry fsdata) and fs/overlayfs (I/O delegated through
 * fs/backing-file.c, mounter credentials for backing I/O).
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fs_stack.h>
#include <linux/backing-file.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/cred.h>
#include <linux/uio.h>
#include <linux/splice.h>
#include <linux/seq_file.h>
#include <linux/xattr.h>
#include <linux/fiemap.h>
#include <linux/fileattr.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include "moobfs_attr.h"

#include "moobl.h"
#include "moobfs_ioctl.h"

static struct kmem_cache *moobl_inode_cache;

/*
 * Attribute copy for unlocked paths (getattr, revalidate, after I/O). It
 * deliberately leaves i_nlink alone: set_nlink() is not safe against
 * concurrent callers and corrupts the superblock's remove count when two
 * CPUs race on the same inode. Link counts change only in the name
 * operations below, which the VFS serializes with the parent's lock.
 */
#define MOOBL_COPY_I_FLAGS (S_SYNC | S_NOATIME | S_APPEND | S_IMMUTABLE)

static void moobl_copy_attr(struct inode *dest, const struct inode *src)
{
	dest->i_mode = src->i_mode;
	dest->i_uid = src->i_uid;
	dest->i_gid = src->i_gid;
	dest->i_rdev = src->i_rdev;
	dest->i_blkbits = src->i_blkbits;
	/* only the flags the VFS enforces on this inode; the rest describe the lower's own state */
	inode_set_flags(dest, src->i_flags & MOOBL_COPY_I_FLAGS, MOOBL_COPY_I_FLAGS);
	fsstack_copy_attr_times(dest, src);
	fsstack_copy_inode_size(dest, (struct inode *)src);
}

static int moobl_set_lower_path(struct dentry *dentry, struct vfsmount *mnt,
				struct dentry *lower_dentry)
{
	struct moobl_dentry *info = kzalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return -ENOMEM;
	info->lower.mnt = mntget(mnt);
	info->lower.dentry = lower_dentry;	/* reference transferred */
	dentry->d_fsdata = info;
	return 0;
}

/* ---- inodes ----------------------------------------------------------- */

static const struct inode_operations moobl_dir_iops;
static const struct inode_operations moobl_file_iops;
static const struct inode_operations moobl_special_iops;
static const struct inode_operations moobl_symlink_iops;
static const struct file_operations moobl_dir_fops;
static const struct file_operations moobl_file_fops;

/* An open file of the layer: the lower file, and the credentials of its writes (moobl_wcred_get) */
struct moobl_file {
	struct file *real;
	spinlock_t lock;
	const struct cred *wcred;	/* last write credentials built, or NULL */
};

static struct file *moobl_real(const struct file *file)
{
	return ((struct moobl_file *)file->private_data)->real;
}

static const struct address_space_operations moobl_aops = {
	/* Data never lives in this mapping; mmap is delegated to the lower file. */
	.direct_IO = noop_direct_IO,
};

static int moobl_inode_test(struct inode *inode, void *lower)
{
	return moobl_lower_inode(inode) == lower;
}

static int moobl_inode_set(struct inode *inode, void *data)
{
	struct inode *lower = data;

	MOOBL_I(inode)->lower = lower;
	fsstack_copy_attr_all(inode, lower);
	fsstack_copy_inode_size(inode, lower);
	inode->i_ino = lower->i_ino;
#ifdef CONFIG_FS_POSIX_ACL
	/*
	 * ACLs are the lower's. The VFS would otherwise remember here the first one it
	 * read and return it after the lower's changed.
	 */
	inode->i_acl = ACL_DONT_CACHE;
	inode->i_default_acl = ACL_DONT_CACHE;
#endif
	inode->i_mapping->a_ops = &moobl_aops;
	if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &moobl_dir_iops;
		inode->i_fop = &moobl_dir_fops;
	} else if (S_ISREG(inode->i_mode)) {
		inode->i_op = &moobl_file_iops;
		inode->i_fop = &moobl_file_fops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &moobl_symlink_iops;
	} else {
		/* Devices and fifos: attributes only; opening them is refused. */
		inode->i_op = &moobl_special_iops;
		init_special_inode(inode, inode->i_mode, lower->i_rdev);
	}
	return 0;
}

/* Returns a referenced upper inode for the lower inode, taking a reference on lower. */
static struct inode *moobl_get_inode(struct super_block *sb, struct inode *lower)
{
	struct inode *inode;

	if (lower->i_sb != MOOBL_SB(sb)->lower.dentry->d_sb)
		return ERR_PTR(-EXDEV);
	ihold(lower);
	inode = iget5_locked(sb, (unsigned long)lower, moobl_inode_test,
			     moobl_inode_set, lower);
	if (!inode) {
		iput(lower);
		return ERR_PTR(-ENOMEM);
	}
	if (!(inode->i_state & I_NEW)) {
		iput(lower);	/* existing upper inode already holds its reference */
	} else {
		unlock_new_inode(inode);
		moobl_index_inode_cached(sb, inode);
	}
	return inode;
}

/* The layer inode of a lower inode if it is in memory, referenced; NULL otherwise. May sleep. */
struct inode *moobl_find_inode(struct super_block *sb, struct inode *lower)
{
	return ilookup5(sb, (unsigned long)lower, moobl_inode_test, lower);
}

static struct inode *moobl_alloc_inode(struct super_block *sb)
{
	struct moobl_inode *mi = alloc_inode_sb(sb, moobl_inode_cache, GFP_KERNEL);

	if (!mi)
		return NULL;
	mi->lower = NULL;
	return &mi->vfs_inode;
}

static void moobl_free_inode(struct inode *inode)
{
	kmem_cache_free(moobl_inode_cache, MOOBL_I(inode));
}

static void moobl_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	if (moobl_lower_inode(inode)) {
		moobl_index_inode_evicted(inode->i_sb, inode);
		iput(moobl_lower_inode(inode));
	}
}

static int moobl_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int err = vfs_statfs(moobl_lower_path(dentry), buf);

	if (!err)
		buf->f_type = MOOBL_SUPER_MAGIC;
	return err;
}

static int moobl_show_options(struct seq_file *m, struct dentry *root)
{
	seq_puts(m, ",lower=");
	seq_path(m, &MOOBL_SB(root->d_sb)->lower, " \t\n\\,");
	if (MOOBL_SB(root->d_sb)->nolinks)
		seq_puts(m, ",nolinks");
	if (!MOOBL_SB(root->d_sb)->index.enabled)
		seq_puts(m, ",noindex");
	else if (MOOBL_SB(root->d_sb)->index.budget != MOOBL_INDEX_BUDGET_DEFAULT)
		seq_printf(m, ",index_budget=%llu", MOOBL_SB(root->d_sb)->index.budget);
	if (MOOBL_SB(root->d_sb)->index.domain_depth != 1)
		seq_printf(m, ",domain_depth=%u", MOOBL_SB(root->d_sb)->index.domain_depth);
	if (MOOBL_SB(root->d_sb)->index.enabled &&
	    MOOBL_SB(root->d_sb)->index.idle != MOOBL_INDEX_IDLE_DEFAULT)
		seq_printf(m, ",index_idle=%u", MOOBL_SB(root->d_sb)->index.idle);
	return 0;
}

/*
 * syncfs(2) on the layer has to reach the lower filesystem, which holds every page and all
 * metadata. sync(2) visits the lower superblock by itself, so the layer is skipped there
 * (SB_I_SKIP_SYNC) and this runs for syncfs, and for the final sync of an unmount.
 */
static int moobl_sync_fs(struct super_block *sb, int wait)
{
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct super_block *lower_sb;
	int err;

	if (!wait || !sbi || !sbi->lower.dentry)
		return 0;
	lower_sb = sbi->lower.dentry->d_sb;
	down_read(&lower_sb->s_umount);
	err = sync_filesystem(lower_sb);
	up_read(&lower_sb->s_umount);
	return err;
}

static const struct super_operations moobl_sops = {
	.alloc_inode = moobl_alloc_inode,
	.free_inode = moobl_free_inode,
	.evict_inode = moobl_evict_inode,
	.statfs = moobl_statfs,
	.sync_fs = moobl_sync_fs,
	.show_options = moobl_show_options,
};

/* ---- dentries --------------------------------------------------------- */

/*
 * RCU-walk revalidation: no locks, no references, no sleeping. The answer is "valid" only
 * in the plain case: the lower filesystem has no revalidation of its own, the lower dentry
 * is still hashed, and the upper dentry agrees with it (positive and mapped to the same
 * lower inode with a link, or both negative). Anything else returns -ECHILD and the VFS
 * retries in ref-walk, which runs the full check below. Safe because moobl_dentry is
 * freed with call_rcu and lower dentries of local filesystems are freed after a grace
 * period, and the lower pointer is published with WRITE_ONCE in moobl_instantiate.
 * Attributes are not copied here; getattr and the name operations refresh them.
 */
static int moobl_d_revalidate_rcu(struct dentry *dentry)
{
	struct moobl_dentry *info = READ_ONCE(dentry->d_fsdata);
	struct dentry *lower;
	struct inode *inode, *lower_inode;

	if (!info)
		return -ECHILD;
	lower = READ_ONCE(info->lower.dentry);
	if (!lower || (READ_ONCE(lower->d_flags) & DCACHE_OP_REVALIDATE) || d_unhashed(lower))
		return -ECHILD;
	inode = d_inode_rcu(dentry);
	lower_inode = d_inode_rcu(lower);
	if (inode) {
		if (!lower_inode || moobl_lower_inode(inode) != lower_inode || !READ_ONCE(inode->i_nlink))
			return -ECHILD;
	} else if (lower_inode) {
		return -ECHILD;
	}
	return 1;
}

static int moobl_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct dentry *lower;
	int rc = 1;

	if (flags & LOOKUP_RCU)
		return moobl_d_revalidate_rcu(dentry);
	/*
	 * Ref-walk runs without the parent's lock, so moobl_instantiate() can swap a negative
	 * dentry's lower dentry and drop the old one meanwhile. The old one is freed only after a
	 * grace period: read it under RCU, and hold it only to call the lower's own revalidation
	 * (review item 3, journal 0120). A lower dentry already dead was replaced: look up again.
	 */
	rcu_read_lock();
	lower = READ_ONCE(((struct moobl_dentry *)dentry->d_fsdata)->lower.dentry);
	if (READ_ONCE(lower->d_flags) & DCACHE_OP_REVALIDATE) {
		if (!lockref_get_not_dead(&lower->d_lockref)) {
			rcu_read_unlock();
			return 0;
		}
		rcu_read_unlock();
		rc = lower->d_op->d_revalidate(lower, flags);
		dput(lower);
	} else {
		rcu_read_unlock();
	}
	if (d_really_is_positive(dentry)) {
		struct inode *inode = d_inode(dentry);

		moobl_copy_attr(inode, moobl_lower_inode(inode));
		if (!inode->i_nlink)
			return 0;
	}
	return rc;
}

static void moobl_dentry_free_rcu(struct rcu_head *head)
{
	kfree(container_of(head, struct moobl_dentry, rcu));
}

static void moobl_d_release(struct dentry *dentry)
{
	struct moobl_dentry *info = dentry->d_fsdata;

	if (info) {
		path_put(&info->lower);
		call_rcu(&info->rcu, moobl_dentry_free_rcu);
	}
}

static const struct dentry_operations moobl_dops = {
	.d_revalidate = moobl_d_revalidate,
	.d_release = moobl_d_release,
};

/* ---- directory inode operations --------------------------------------- */

/*
 * 1 when lower is a directory on another device than the lower root, 0 when not, or an error.
 * A btrfs subvolume below the root is the usual case: it is a separate filesystem with its own
 * anonymous device, and it numbers its inodes from the start again, while the index and the
 * layer's inode cache identify a file by its inode number alone, so it cannot be entered. Every
 * inode of a subvolume reports the subvolume's device, so checking directories is enough. The
 * device is the one getattr reports (st_dev), not i_sb->s_dev, which btrfs subvolumes share.
 * The lower root may itself be a subvolume; it is never looked up.
 */
int moobl_other_device(struct super_block *sb, struct dentry *lower)
{
	struct path p = { .mnt = MOOBL_SB(sb)->lower.mnt, .dentry = lower };
	struct kstat st;
	int err;

	if (!d_is_dir(lower))
		return 0;
	err = vfs_getattr_nosec(&p, &st, STATX_BASIC_STATS, AT_STATX_DONT_SYNC);
	if (err)
		return err;
	return st.dev != MOOBL_SB(sb)->lower_dev;
}

static struct dentry *moobl_lookup(struct inode *dir, struct dentry *dentry,
				   unsigned int flags)
{
	struct path *parent = moobl_lower_path(dentry->d_parent);
	struct dentry *lower_dentry;
	struct inode *lower_inode, *inode;
	int err;

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	lower_dentry = lookup_one_len_unlocked(dentry->d_name.name, parent->dentry,
					       dentry->d_name.len);
	if (IS_ERR(lower_dentry))
		return ERR_CAST(lower_dentry);
	err = moobl_other_device(dir->i_sb, lower_dentry);
	if (err) {
		dput(lower_dentry);
		return ERR_PTR(err < 0 ? err : -EXDEV);
	}
	err = moobl_set_lower_path(dentry, parent->mnt, lower_dentry);
	if (err) {
		dput(lower_dentry);
		return ERR_PTR(err);
	}
	fsstack_copy_attr_atime(dir, d_inode(parent->dentry));
	lower_inode = READ_ONCE(lower_dentry->d_inode);
	if (!lower_inode) {
		d_add(dentry, NULL);
		return NULL;
	}
	inode = moobl_get_inode(dir->i_sb, lower_inode);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	return d_splice_alias(inode, dentry);
}

/*
 * Lock the lower parent and re-lookup the lower child under that lock, as
 * ecryptfs does, so the dentry used for the mutation is current.
 */
/*
 * Write access on the lower mount, around every change the layer makes to the lower (journal
 * 0117). The system call took write access on the layer's mount only; without this a change
 * through the layer would ignore a read-only lower mount and the freeze of the lower
 * filesystem (ext4 warns when a transaction starts on a frozen filesystem). Order, as
 * overlayfs does with ovl_want_write(): the layer's own inode locks (taken by the VFS), then
 * the lower's freeze protection, then the lower's inode locks.
 */
static int moobl_want_write(struct super_block *sb)
{
	return mnt_want_write(MOOBL_SB(sb)->lower.mnt);
}

static void moobl_drop_write(struct super_block *sb)
{
	mnt_drop_write(MOOBL_SB(sb)->lower.mnt);
}

static int moobl_lock_parent(struct dentry *dentry, struct dentry **lower_dentry,
			     struct inode **lower_dir)
{
	struct dentry *lower_parent = moobl_lower_dentry(dentry->d_parent);
	int err = moobl_want_write(dentry->d_sb);

	if (err)
		return err;
	*lower_dir = d_inode(lower_parent);
	inode_lock_nested(*lower_dir, I_MUTEX_PARENT);
	*lower_dentry = lookup_one_len(dentry->d_name.name, lower_parent,
				       dentry->d_name.len);
	if (IS_ERR(*lower_dentry)) {
		inode_unlock(*lower_dir);
		moobl_drop_write(dentry->d_sb);
		return PTR_ERR(*lower_dentry);
	}
	return 0;
}

static void moobl_unlock_parent(struct dentry *dentry, struct dentry *lower_dentry,
				struct inode *lower_dir)
{
	dput(lower_dentry);
	inode_unlock(lower_dir);
	moobl_drop_write(dentry->d_sb);
}

static int moobl_instantiate(struct inode *dir, struct dentry *dentry,
			     struct dentry *lower_dentry, struct inode *lower_dir)
{
	struct moobl_dentry *info = dentry->d_fsdata;
	struct inode *inode;

	if (d_really_is_negative(lower_dentry))
		return -EIO;
	inode = moobl_get_inode(dir->i_sb, d_inode(lower_dentry));
	if (IS_ERR(inode))
		return PTR_ERR(inode);
	/*
	 * A negative upper dentry may still reference the lower dentry of a
	 * name that was unlinked and is now being recreated (SQLite does this
	 * with -wal and -shm). Point it at the dentry the new file lives in,
	 * otherwise later opens would reach the deleted lower inode.
	 */
	if (info->lower.dentry != lower_dentry) {
		struct dentry *stale = info->lower.dentry;

		/*
		 * published for lockless readers (both paths of moobl_d_revalidate); the
		 * stale dentry stays valid for them until the end of the grace period
		 */
		WRITE_ONCE(info->lower.dentry, dget(lower_dentry));
		dput(stale);
	}
	fsstack_copy_attr_times(dir, lower_dir);
	fsstack_copy_inode_size(dir, lower_dir);
	d_instantiate(dentry, inode);
	return 0;
}

static int moobl_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct dentry *lower_dentry;
	struct inode *lower_dir;
	int err;

	err = moobl_lock_parent(dentry, &lower_dentry, &lower_dir);
	if (err)
		return err;
	err = vfs_create(&nop_mnt_idmap, lower_dir, lower_dentry, mode, excl);
	if (!err)
		err = moobl_instantiate(dir, dentry, lower_dentry, lower_dir);
	moobl_unlock_parent(dentry, lower_dentry, lower_dir);
	return err;
}

static int moobl_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct dentry *lower_dentry;
	struct inode *lower_dir;
	int err;

	err = moobl_lock_parent(dentry, &lower_dentry, &lower_dir);
	if (err)
		return err;
	err = vfs_mkdir(&nop_mnt_idmap, lower_dir, lower_dentry, mode);
	if (!err)
		err = moobl_instantiate(dir, dentry, lower_dentry, lower_dir);
	if (!err)
		set_nlink(dir, lower_dir->i_nlink);
	moobl_unlock_parent(dentry, lower_dentry, lower_dir);
	return err;
}

static int moobl_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct dentry *lower_dentry;
	struct inode *lower_dir;
	int err;

	err = moobl_lock_parent(dentry, &lower_dentry, &lower_dir);
	if (err)
		return err;
	dget(lower_dentry);	/* keep the lower dentry from going negative under us */
	if (d_unhashed(lower_dentry) || d_really_is_negative(lower_dentry))
		err = -ENOENT;
	else
		err = vfs_unlink(&nop_mnt_idmap, lower_dir, lower_dentry, NULL);
	if (!err) {
		fsstack_copy_attr_times(dir, lower_dir);
		set_nlink(inode, moobl_lower_inode(inode)->i_nlink);
		inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
	}
	dput(lower_dentry);
	moobl_unlock_parent(dentry, lower_dentry, lower_dir);
	if (!err && !inode->i_nlink)
		moobl_index_drop_inode(dir->i_sb, inode->i_ino);
	return err;
}

static int moobl_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct inode *lower_dir;
	int err;

	err = moobl_lock_parent(dentry, &lower_dentry, &lower_dir);
	if (err)
		return err;
	dget(lower_dentry);
	if (d_unhashed(lower_dentry) || d_really_is_negative(lower_dentry))
		err = -ENOENT;
	else
		err = vfs_rmdir(&nop_mnt_idmap, lower_dir, lower_dentry);
	if (!err) {
		fsstack_copy_attr_times(dir, lower_dir);
		set_nlink(dir, lower_dir->i_nlink);
		clear_nlink(d_inode(dentry));
	}
	dput(lower_dentry);
	moobl_unlock_parent(dentry, lower_dentry, lower_dir);
	if (!err) {
		moobl_index_drop_inode(dir->i_sb, d_inode(dentry)->i_ino);
		d_drop(dentry);
	}
	return err;
}

static int moobl_rename(struct mnt_idmap *idmap, struct inode *old_dir,
			struct dentry *old_dentry, struct inode *new_dir,
			struct dentry *new_dentry, unsigned int flags)
{
	struct dentry *lower_old_dir = moobl_lower_dentry(old_dentry->d_parent);
	struct dentry *lower_new_dir = moobl_lower_dentry(new_dentry->d_parent);
	struct dentry *lower_old = dget(moobl_lower_dentry(old_dentry));
	struct dentry *lower_new = NULL;
	struct inode *target = d_inode(new_dentry);
	struct dentry *trap;
	struct renamedata rd = {
		.old_mnt_idmap = &nop_mnt_idmap,
		.old_dir = d_inode(lower_old_dir),
		.old_dentry = lower_old,
		.new_mnt_idmap = &nop_mnt_idmap,
		.new_dir = d_inode(lower_new_dir),
		.flags = flags,
	};
	int err;

	/*
	 * A directory may not move to another domain or depth: its subtree's partition would
	 * change (docs/spec-query-v2.md §5); like a cross-device rename, mv copies instead.
	 */
	if ((d_is_dir(old_dentry) && moobl_rename_crosses_domain(old_dir->i_sb, old_dentry, new_dentry)) ||
	    ((flags & RENAME_EXCHANGE) && target && d_is_dir(new_dentry) &&
	     moobl_rename_crosses_domain(old_dir->i_sb, new_dentry, old_dentry))) {
		err = -EXDEV;
		goto out_put;
	}
	err = moobl_want_write(old_dir->i_sb);
	if (err)
		goto out_put;
	trap = lock_rename(lower_old_dir, lower_new_dir);
	if (IS_ERR(trap)) {
		err = PTR_ERR(trap);
		goto out_drop;
	}
	/*
	 * Re-look up the target under the parents' locks, as create does (moobl_lock_parent): a
	 * negative upper dentry left by an unlink still references the unhashed lower dentry of
	 * the deleted file, and renaming onto that name (write a temporary file, then rename it
	 * over the old name) would otherwise fail with EINVAL.
	 */
	lower_new = lookup_one_len(new_dentry->d_name.name, lower_new_dir, new_dentry->d_name.len);
	if (IS_ERR(lower_new)) {
		err = PTR_ERR(lower_new);
		lower_new = NULL;
		goto out_unlock;
	}
	rd.new_dentry = lower_new;
	/* the lower tree changed behind the layer: refuse instead of acting on another file */
	err = -ESTALE;
	if (d_really_is_positive(new_dentry) != d_really_is_positive(lower_new))
		goto out_unlock;
	if (target && moobl_lower_inode(target) != d_inode(lower_new))
		goto out_unlock;
	err = -EINVAL;
	if (lower_old->d_parent != lower_old_dir || lower_new->d_parent != lower_new_dir)
		goto out_unlock;
	if (d_unhashed(lower_old) || d_unhashed(lower_new))
		goto out_unlock;
	if (trap == lower_old)
		goto out_unlock;
	err = -ENOTEMPTY;
	if (trap == lower_new)
		goto out_unlock;
	err = vfs_rename(&rd);
	if (err)
		goto out_unlock;
	/* Name operations hold the parents' locks: link counts may be copied here. */
	if (target)
		fsstack_copy_attr_all(target, moobl_lower_inode(target));
	fsstack_copy_attr_all(new_dir, d_inode(lower_new_dir));
	if (new_dir != old_dir)
		fsstack_copy_attr_all(old_dir, d_inode(lower_old_dir));
	if ((flags & RENAME_EXCHANGE) && target)
		fsstack_copy_attr_all(d_inode(old_dentry),
				      moobl_lower_inode(d_inode(old_dentry)));
	/* A replaced file or directory is gone, unless other names still link to the file. */
	if (target && !(flags & RENAME_EXCHANGE) &&
	    (S_ISDIR(target->i_mode) || !moobl_lower_inode(target)->i_nlink))
		moobl_index_drop_inode(old_dir->i_sb, target->i_ino);
	/* The VFS moves the dentries after this returns: place each inode at its new name. */
	moobl_index_renamed(old_dir->i_sb, old_dentry, new_dentry);
	if ((flags & RENAME_EXCHANGE) && target)
		moobl_index_renamed(old_dir->i_sb, new_dentry, old_dentry);
out_unlock:
	unlock_rename(lower_old_dir, lower_new_dir);
out_drop:
	moobl_drop_write(old_dir->i_sb);
out_put:
	if (lower_new)
		dput(lower_new);
	dput(lower_old);
	return err;
}

static int moobl_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry)
{
	struct inode *inode = d_inode(old_dentry);
	struct dentry *lower_old = moobl_lower_dentry(old_dentry);
	struct dentry *lower_dentry;
	struct inode *lower_dir;
	int err;

	if (MOOBL_SB(dir->i_sb)->nolinks)
		return -EPERM;
	err = moobl_lock_parent(new_dentry, &lower_dentry, &lower_dir);
	if (err)
		return err;
	err = vfs_link(lower_old, &nop_mnt_idmap, lower_dir, lower_dentry, NULL);
	if (!err)
		err = moobl_instantiate(dir, new_dentry, lower_dentry, lower_dir);
	if (!err)
		set_nlink(inode, moobl_lower_inode(inode)->i_nlink);
	moobl_unlock_parent(new_dentry, lower_dentry, lower_dir);
	if (!err) {
		moobl_index_linked(dir->i_sb, new_dentry);
		moobl_index_perm_changed(dir->i_sb, old_dentry);	/* nlink > 1: heterogeneous */
	}
	return err;
}

static int moobl_symlink(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	struct dentry *lower_dentry;
	struct inode *lower_dir;
	int err;

	if (MOOBL_SB(dir->i_sb)->nolinks)
		return -EPERM;
	err = moobl_lock_parent(dentry, &lower_dentry, &lower_dir);
	if (err)
		return err;
	err = vfs_symlink(&nop_mnt_idmap, lower_dir, lower_dentry, symname);
	if (!err)
		err = moobl_instantiate(dir, dentry, lower_dentry, lower_dir);
	moobl_unlock_parent(dentry, lower_dentry, lower_dir);
	return err;
}

static const char *moobl_get_link(struct dentry *dentry, struct inode *inode,
				  struct delayed_call *done)
{
	if (!dentry)
		return ERR_PTR(-ECHILD);
	fsstack_copy_attr_atime(inode, moobl_lower_inode(inode));
	return vfs_get_link(moobl_lower_dentry(dentry), done);
}

static int moobl_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
			u64 start, u64 len)
{
	struct inode *lower = moobl_lower_inode(inode);
	const struct cred *old;
	int err;

	if (!lower->i_op->fiemap)
		return -EOPNOTSUPP;
	old = override_creds(MOOBL_SB(inode->i_sb)->creds);
	err = lower->i_op->fiemap(lower, fieinfo, start, len);
	revert_creds(old);
	return err;
}

/*
 * File flags (lsattr, chattr, FS_IOC_FS[GS]ETXATTR) are the lower's. Both calls run with the
 * caller's credentials, so the lower decides who may change what (owner, CAP_LINUX_IMMUTABLE).
 */
static int moobl_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	return vfs_fileattr_get(moobl_lower_dentry(dentry), fa);
}

static int moobl_fileattr_set(struct mnt_idmap *idmap, struct dentry *dentry,
			      struct fileattr *fa)
{
	struct dentry *lower = moobl_lower_dentry(dentry);
	struct fileattr old = {};
	int err;

	err = vfs_fileattr_get(lower, &old);
	if (err)
		return err;
	/* names in this dentry cache are case-sensitive; a lower directory must stay so */
	if (fa->flags_valid && ((fa->flags ^ old.flags) & FS_CASEFOLD_FL))
		return -EOPNOTSUPP;
	err = moobl_want_write(dentry->d_sb);
	if (err)
		return err;
	err = vfs_fileattr_set(&nop_mnt_idmap, lower, fa);
	moobl_drop_write(dentry->d_sb);
	if (!err)
		moobl_copy_attr(d_inode(dentry), d_inode(lower));
	return err;
}

/* ---- POSIX ACLs: delegated to the lower dentry, as ecryptfs does ------------- */

static struct posix_acl *moobl_get_acl(struct mnt_idmap *idmap, struct dentry *dentry, int type)
{
	return vfs_get_acl(&nop_mnt_idmap, moobl_lower_dentry(dentry), posix_acl_xattr_name(type));
}

static int moobl_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
			 struct posix_acl *acl, int type)
{
	struct dentry *lower = moobl_lower_dentry(dentry);
	int err;

	err = moobl_want_write(dentry->d_sb);
	if (err)
		return err;
	if (acl)
		err = vfs_set_acl(&nop_mnt_idmap, lower, posix_acl_xattr_name(type), acl);
	else
		err = vfs_remove_acl(&nop_mnt_idmap, lower, posix_acl_xattr_name(type));
	moobl_drop_write(dentry->d_sb);
	if (!err) {
		moobl_copy_attr(d_inode(dentry), d_inode(lower));
		if (type == ACL_TYPE_ACCESS)
			moobl_index_perm_changed(dentry->d_sb, dentry);
	}
	return err;
}

/* ---- extended attributes: pass-through, with typed user.moob.* validated on write ---- */

static ssize_t moobl_listxattr(struct dentry *dentry, char *list, size_t size)
{
	ssize_t ret = vfs_listxattr(moobl_lower_dentry(dentry), list, size);

	if (ret >= 0)
		fsstack_copy_attr_atime(d_inode(dentry), moobl_lower_inode(d_inode(dentry)));
	return ret;
}

/*
 * security.* is read with the mounter's credentials, as overlayfs does (ovl_xattr_get): an LSM
 * reads an inode's label through this handler when the inode is set up, with the credentials
 * of whoever reached the file first, and a confined first caller would leave the inode
 * unlabelled (journal 0104). A caller's own read was checked against this inode by the VFS
 * before the handler ran. Every other name is read with the caller's credentials.
 */
static int moobl_xattr_get(const struct xattr_handler *handler, struct dentry *dentry,
			   struct inode *inode, const char *name, void *value, size_t size)
{
	const char *full = xattr_full_name(handler, name);
	const struct cred *old;
	int ret;

	if (strncmp(full, XATTR_SECURITY_PREFIX, XATTR_SECURITY_PREFIX_LEN))
		return vfs_getxattr(&nop_mnt_idmap, moobl_lower_dentry(dentry), full, value, size);
	old = override_creds(MOOBL_SB(inode->i_sb)->creds);
	ret = vfs_getxattr(&nop_mnt_idmap, moobl_lower_dentry(dentry), full, value, size);
	revert_creds(old);
	return ret;
}

/*
 * Count the typed attributes already present on the lower inode. Bounded by
 * the lower filesystem's own listxattr limits; runs only when a new typed
 * attribute is being created.
 */
static int moobl_count_typed(struct dentry *lower)
{
	ssize_t len;
	char *list, *p;
	int count = 0;

	len = vfs_listxattr(lower, NULL, 0);
	if (len <= 0)
		return len < 0 ? (int)len : 0;
	list = kvmalloc(len, GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	len = vfs_listxattr(lower, list, len);
	if (len < 0) {
		kvfree(list);
		return (int)len;
	}
	for (p = list; p < list + len; p += strlen(p) + 1)
		if (!strncmp(p, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN) &&
		    !moob_attr_name_reserved(p + MOOB_ATTR_PREFIX_LEN,
					     strlen(p) - MOOB_ATTR_PREFIX_LEN))
			count++;
	kvfree(list);
	return count;
}

/* Typed attribute rules of docs/spec-m2.md §3.5, applied before the lower write. */
static int moobl_typed_check(struct dentry *lower, const char *full, const void *value,
			     size_t size)
{
	const char *name = full + MOOB_ATTR_PREFIX_LEN;
	size_t nlen = strlen(name);
	int err;

	if (moob_attr_name_reserved(name, nlen))
		return -EPERM;	/* user.moob.format belongs to the tools, on the lower root */
	if (!moob_attr_name_ok(name, nlen))
		return -EINVAL;
	if (!value)
		return 0;	/* removal of a well-named attribute */
	if (size > MOOB_ATTR_VALUE_MAX || moob_attr_validate(value, size) != MOOB_ATTR_OK)
		return -EINVAL;
	/*
	 * Whether the attribute exists already (a replacement does not count toward the limit).
	 * Read without a permission check: the VFS authorized the write on the layer's inode, and
	 * vfs_getxattr() would ask read permission, refusing a typed attribute on a write-only
	 * file the lower itself accepts (review item 7, journal 0120).
	 */
	err = __vfs_getxattr(lower, d_inode(lower), full, NULL, 0);
	if (err == -ENODATA) {
		err = moobl_count_typed(lower);
		if (err < 0)
			return err;
		if (err >= MOOB_ATTR_MAX_PER_FILE)
			return -ENOSPC;
	} else if (err < 0 && err != -ERANGE) {
		return err;
	}
	return 0;
}

static int moobl_xattr_set(const struct xattr_handler *handler, struct mnt_idmap *idmap,
			   struct dentry *dentry, struct inode *inode, const char *name,
			   const void *value, size_t size, int flags)
{
	struct dentry *lower = moobl_lower_dentry(dentry);
	const char *full = xattr_full_name(handler, name);
	int err;

	bool typed = !strncmp(full, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN);

	if (typed) {
		err = moobl_typed_check(lower, full, value, size);
		if (err)
			return err;
	}
	err = moobl_want_write(dentry->d_sb);
	if (err)
		return err;
	if (value) {
		err = vfs_setxattr(&nop_mnt_idmap, lower, full, value, size, flags);
	} else if (!strcmp(full, XATTR_NAME_CAPS)) {
		/*
		 * File capabilities are removed by the kernel when an ordinary user changes
		 * the file (cap_inode_killpriv), a caller the lower would refuse. A removal
		 * asked by a user was authorized on this inode before the handler ran
		 * (cap_inode_removexattr), so the mounter's credentials are safe here.
		 */
		const struct cred *old = override_creds(MOOBL_SB(inode->i_sb)->creds);

		err = vfs_removexattr(&nop_mnt_idmap, lower, full);
		revert_creds(old);
	} else {
		err = vfs_removexattr(&nop_mnt_idmap, lower, full);
	}
	moobl_drop_write(dentry->d_sb);
	if (err)
		return err;
	moobl_copy_attr(inode, d_inode(lower));
	if (typed) {
		if (value)
			moobl_index_set(inode->i_sb, dentry, full + MOOB_ATTR_PREFIX_LEN, value, size);
		else
			moobl_index_remove(inode->i_sb, inode->i_ino, full + MOOB_ATTR_PREFIX_LEN);
	}
	return 0;
}

static const struct xattr_handler moobl_xattr_handler = {
	.prefix = "",	/* every namespace; permission checks happen in the VFS and below */
	.get = moobl_xattr_get,
	.set = moobl_xattr_set,
};

static const struct xattr_handler * const moobl_xattr_handlers[] = {
	&moobl_xattr_handler,
	NULL
};

/* ---- attribute operations shared by files and directories -------------- */

#define MOOBL_STATX_ATTRS (STATX_ATTR_COMPRESSED | STATX_ATTR_IMMUTABLE | \
			   STATX_ATTR_APPEND | STATX_ATTR_NODUMP)

static int moobl_getattr(struct mnt_idmap *idmap, const struct path *path,
			 struct kstat *stat, u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_inode(dentry);
	struct kstat lower_stat;
	int err;

	err = vfs_getattr_nosec(moobl_lower_path(dentry), &lower_stat, request_mask, flags);
	if (err)
		return err;
	moobl_copy_attr(inode, moobl_lower_inode(inode));
	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	stat->blocks = lower_stat.blocks;
	stat->blksize = lower_stat.blksize;
	/* what statx reports beyond stat: the lower's file flags and its creation time */
	stat->attributes |= lower_stat.attributes & MOOBL_STATX_ATTRS;
	stat->attributes_mask |= lower_stat.attributes_mask & MOOBL_STATX_ATTRS;
	if (lower_stat.result_mask & STATX_BTIME) {
		stat->btime = lower_stat.btime;
		stat->result_mask |= STATX_BTIME;
	}
	return 0;
}

static int moobl_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			 struct iattr *ia)
{
	struct inode *inode = d_inode(dentry);
	struct dentry *lower_dentry = moobl_lower_dentry(dentry);
	struct inode *lower_inode = d_inode(lower_dentry);
	struct iattr lower_ia = *ia;
	int err;

	err = setattr_prepare(&nop_mnt_idmap, dentry, ia);
	if (err)
		return err;
	if (ia->ia_valid & ATTR_FILE)
		lower_ia.ia_file = moobl_real(ia->ia_file);
	/*
	 * A write or truncate of a set-user-ID or set-group-ID file arrives with
	 * ATTR_KILL_S*ID and the ATTR_MODE the VFS derived from it. notify_change()
	 * refuses that pair with BUG(); the lower call derives the mode again.
	 */
	if (lower_ia.ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		lower_ia.ia_valid &= ~ATTR_MODE;
	err = moobl_want_write(dentry->d_sb);
	if (err)
		return err;
	inode_lock(lower_inode);
	err = notify_change(&nop_mnt_idmap, lower_dentry, &lower_ia, NULL);
	inode_unlock(lower_inode);
	moobl_drop_write(dentry->d_sb);
	if (!err) {
		moobl_copy_attr(inode, lower_inode);
		if (ia->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
			moobl_index_perm_changed(dentry->d_sb, dentry);
	}
	return err;
}

static int moobl_permission(struct mnt_idmap *idmap, struct inode *inode, int mask)
{
	return inode_permission(&nop_mnt_idmap, moobl_lower_inode(inode), mask);
}

static const struct inode_operations moobl_dir_iops = {
	.lookup = moobl_lookup,
	.create = moobl_create,
	.mkdir = moobl_mkdir,
	.unlink = moobl_unlink,
	.rmdir = moobl_rmdir,
	.rename = moobl_rename,
	.link = moobl_link,
	.symlink = moobl_symlink,
	.getattr = moobl_getattr,
	.setattr = moobl_setattr,
	.permission = moobl_permission,
	.listxattr = moobl_listxattr,
	.get_acl = moobl_get_acl,
	.set_acl = moobl_set_acl,
	.fileattr_get = moobl_fileattr_get,
	.fileattr_set = moobl_fileattr_set,
};

static const struct inode_operations moobl_file_iops = {
	.getattr = moobl_getattr,
	.setattr = moobl_setattr,
	.permission = moobl_permission,
	.listxattr = moobl_listxattr,
	.fiemap = moobl_fiemap,
	.get_acl = moobl_get_acl,
	.set_acl = moobl_set_acl,
	.fileattr_get = moobl_fileattr_get,
	.fileattr_set = moobl_fileattr_set,
};

static const struct inode_operations moobl_symlink_iops = {
	.get_link = moobl_get_link,
	.getattr = moobl_getattr,
	.setattr = moobl_setattr,
	.permission = moobl_permission,
	.listxattr = moobl_listxattr,
};

static const struct inode_operations moobl_special_iops = {
	.getattr = moobl_getattr,
	.setattr = moobl_setattr,
	.permission = moobl_permission,
	.listxattr = moobl_listxattr,
};

/* ---- regular file operations ------------------------------------------ */

static void moobl_file_accessed(struct file *file)
{
	if (!(file->f_flags & O_NOATIME))
		touch_atime(&file->f_path);
}

static void moobl_file_modified(struct file *file)
{
	struct inode *inode = file_inode(file);

	moobl_copy_attr(inode, moobl_lower_inode(inode));
}

static void moobl_file_end_write(struct file *file, loff_t pos, ssize_t ret)
{
	moobl_file_modified(file);
}

/*
 * Write credentials (finding F4, journal 0132). Writes run with the mounter's credentials but
 * with the writer's fsuid, fsgid and groups: ext4 grants its reserved blocks by the fsuid and
 * groups of the task that allocates (ext4_has_free_clusters(), the reserve owner being uid 0 by
 * default), so with the mounter's own a write by an ordinary user could spend the reserve. They
 * are built as overlayfs builds the credentials of a creation (ovl_create_or_link()) and kept
 * while the same identity writes.
 */
static bool moobl_same_id(const struct cred *a, const struct cred *b)
{
	return uid_eq(a->fsuid, b->fsuid) && gid_eq(a->fsgid, b->fsgid) &&
	       a->group_info == b->group_info;
}

/* The credentials a write of the current task runs with; put_cred() them after the call. */
static const struct cred *moobl_wcred_get(struct file *file)
{
	const struct cred *mnt = MOOBL_SB(file_inode(file)->i_sb)->creds;
	const struct cred *cur = current_cred(), *old, *c;
	struct moobl_file *mf = file->private_data;
	struct cred *wc;

	if (moobl_same_id(cur, mnt))
		return get_cred(mnt);
	spin_lock(&mf->lock);
	c = mf->wcred;
	if (c && moobl_same_id(c, cur)) {
		get_cred(c);
		spin_unlock(&mf->lock);
		return c;
	}
	spin_unlock(&mf->lock);
	old = override_creds(mnt);
	wc = prepare_creds();
	revert_creds(old);
	if (!wc)
		return ERR_PTR(-ENOMEM);
	wc->fsuid = cur->fsuid;
	wc->fsgid = cur->fsgid;
	set_groups(wc, cur->group_info);
	spin_lock(&mf->lock);
	old = mf->wcred;
	mf->wcred = get_cred(wc);
	spin_unlock(&mf->lock);
	if (old)
		put_cred(old);
	return wc;
}

static int moobl_file_init(struct file *file, struct file *real)
{
	struct moobl_file *mf = kmalloc(sizeof(*mf), GFP_KERNEL);

	if (!mf)
		return -ENOMEM;
	mf->real = real;
	spin_lock_init(&mf->lock);
	mf->wcred = NULL;
	file->private_data = mf;
	return 0;
}

#define MOOBL_CTX(file) {						\
	.cred = MOOBL_SB(file_inode(file)->i_sb)->creds,		\
	.user_file = (file),						\
	.accessed = moobl_file_accessed,				\
	.end_write = moobl_file_end_write,				\
}

static int moobl_open(struct inode *inode, struct file *file)
{
	struct path *lower = moobl_lower_path(file->f_path.dentry);
	struct file *realfile;
	int err;

	/* Creation and truncation were handled by the VFS before open. */
	file->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);
	realfile = backing_file_open(file, file->f_flags, lower,
				     MOOBL_SB(inode->i_sb)->creds);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);
	err = moobl_file_init(file, realfile);
	if (err)
		fput(realfile);
	return err;
}

static int moobl_release(struct inode *inode, struct file *file)
{
	struct moobl_file *mf = file->private_data;

	fput(mf->real);
	if (mf->wcred)
		put_cred(mf->wcred);
	kfree(mf);
	return 0;
}

static ssize_t moobl_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct backing_file_ctx ctx = MOOBL_CTX(file);

	if (!iov_iter_count(iter))
		return 0;
	return backing_file_read_iter(moobl_real(file), iter, iocb,
				      iocb->ki_flags, &ctx);
}

static ssize_t moobl_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct backing_file_ctx ctx = MOOBL_CTX(file);
	struct inode *inode = file_inode(file);
	ssize_t ret;

	if (!iov_iter_count(iter))
		return 0;
	ctx.cred = moobl_wcred_get(file);
	if (IS_ERR(ctx.cred))
		return PTR_ERR(ctx.cred);
	inode_lock(inode);
	ret = file_remove_privs(file);
	if (!ret)
		ret = backing_file_write_iter(moobl_real(file), iter, iocb,
					      iocb->ki_flags, &ctx);
	inode_unlock(inode);
	put_cred(ctx.cred);
	return ret;
}

static ssize_t moobl_splice_read(struct file *in, loff_t *ppos,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags)
{
	struct backing_file_ctx ctx = MOOBL_CTX(in);

	return backing_file_splice_read(moobl_real(in), ppos, pipe, len, flags, &ctx);
}

static ssize_t moobl_splice_write(struct pipe_inode_info *pipe, struct file *out,
				  loff_t *ppos, size_t len, unsigned int flags)
{
	struct backing_file_ctx ctx = MOOBL_CTX(out);
	struct inode *inode = file_inode(out);
	ssize_t ret;

	ctx.cred = moobl_wcred_get(out);
	if (IS_ERR(ctx.cred))
		return PTR_ERR(ctx.cred);
	inode_lock(inode);
	ret = file_remove_privs(out);
	if (!ret)
		ret = backing_file_splice_write(pipe, moobl_real(out), ppos, len,
						flags, &ctx);
	inode_unlock(inode);
	put_cred(ctx.cred);
	return ret;
}

static int moobl_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct backing_file_ctx ctx = MOOBL_CTX(file);

	return backing_file_mmap(moobl_real(file), vma, &ctx);
}

static int moobl_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	const struct cred *old = override_creds(MOOBL_SB(file_inode(file)->i_sb)->creds);
	int err = vfs_fsync_range(moobl_real(file), start, end, datasync);

	revert_creds(old);
	return err;
}

static loff_t moobl_llseek(struct file *file, loff_t offset, int whence)
{
	struct file *real = moobl_real(file);
	struct inode *inode = file_inode(file);
	loff_t ret;

	if (offset == 0) {
		if (whence == SEEK_CUR)
			return file->f_pos;
		if (whence == SEEK_SET)
			return vfs_setpos(file, 0, 0);
	}
	inode_lock(inode);
	real->f_pos = file->f_pos;
	ret = vfs_llseek(real, offset, whence);
	file->f_pos = real->f_pos;
	inode_unlock(inode);
	return ret;
}

static long moobl_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	const struct cred *old, *wc;
	long ret;

	wc = moobl_wcred_get(file);
	if (IS_ERR(wc))
		return PTR_ERR(wc);
	inode_lock(inode);
	ret = file_remove_privs(file);
	if (!ret) {
		old = override_creds(wc);
		ret = vfs_fallocate(moobl_real(file), mode, offset, len);
		revert_creds(old);
		moobl_file_modified(file);
	}
	inode_unlock(inode);
	put_cred(wc);
	return ret;
}

static loff_t moobl_remap_file_range(struct file *in, loff_t pos_in, struct file *out,
				     loff_t pos_out, loff_t len, unsigned int flags)
{
	struct inode *inode_out = file_inode(out);
	const struct cred *old, *wc;
	loff_t ret;

	if (file_inode(in)->i_sb != inode_out->i_sb)
		return -EXDEV;
	wc = moobl_wcred_get(out);
	if (IS_ERR(wc))
		return PTR_ERR(wc);
	if (!(flags & REMAP_FILE_DEDUP)) {
		/*
		 * A clone changes the content. The lower call runs with the mounter's
		 * credentials, which keep set-user-ID, set-group-ID and file capabilities,
		 * so they are dropped here with the caller's, as for a write. A dedupe
		 * leaves the content as it was. The lock is held across the lower call, as
		 * for a write.
		 */
		inode_lock(inode_out);
		ret = file_remove_privs(out);
		if (ret)
			goto unlock;
	}
	old = override_creds(wc);
	if (flags & REMAP_FILE_DEDUP)
		ret = vfs_dedupe_file_range_one(moobl_real(in), pos_in, moobl_real(out),
						pos_out, len, flags);
	else
		ret = vfs_clone_file_range(moobl_real(in), pos_in, moobl_real(out),
					   pos_out, len, flags);
	revert_creds(old);
	moobl_file_modified(out);
unlock:
	if (!(flags & REMAP_FILE_DEDUP))
		inode_unlock(inode_out);
	put_cred(wc);
	return ret;
}

static ssize_t moobl_copy_file_range(struct file *in, loff_t pos_in, struct file *out,
				     loff_t pos_out, size_t len, unsigned int flags)
{
	const struct cred *old, *wc;
	ssize_t ret;

	if (file_inode(in)->i_sb != file_inode(out)->i_sb)
		return -EXDEV;
	wc = moobl_wcred_get(out);
	if (IS_ERR(wc))
		return PTR_ERR(wc);
	/* as for a clone: drop the privileges of the target with the caller's credentials */
	inode_lock(file_inode(out));
	ret = file_remove_privs(out);
	if (ret)
		goto unlock;
	old = override_creds(wc);
	ret = vfs_copy_file_range(moobl_real(in), pos_in, moobl_real(out), pos_out,
				  len, flags);
	revert_creds(old);
	moobl_file_modified(out);
unlock:
	inode_unlock(file_inode(out));
	put_cred(wc);
	return ret;
}

static int moobl_flush(struct file *file, fl_owner_t id)
{
	struct file *real = moobl_real(file);

	if (real->f_op->flush)
		return real->f_op->flush(real, id);
	return 0;
}

static const struct file_operations moobl_file_fops = {
	.open = moobl_open,
	.release = moobl_release,
	.llseek = moobl_llseek,
	.read_iter = moobl_read_iter,
	.write_iter = moobl_write_iter,
	.splice_read = moobl_splice_read,
	.splice_write = moobl_splice_write,
	.mmap = moobl_mmap,
	.fsync = moobl_fsync,
	.fallocate = moobl_fallocate,
	.flush = moobl_flush,
	.remap_file_range = moobl_remap_file_range,
	.copy_file_range = moobl_copy_file_range,
};

/* ---- directory file operations ---------------------------------------- */

static int moobl_dir_open(struct inode *inode, struct file *file)
{
	struct file *real = dentry_open(moobl_lower_path(file->f_path.dentry),
					O_RDONLY | O_DIRECTORY,
					MOOBL_SB(inode->i_sb)->creds);
	int err;

	if (IS_ERR(real))
		return PTR_ERR(real);
	err = moobl_file_init(file, real);
	if (err)
		fput(real);
	return err;
}

/*
 * Directory reads are data operations on an open file: like read(2) on a file they run with the
 * mounter's credentials (ADR-0011), the caller having been checked when it opened the layer's
 * directory. Without the switch, an LSM saw the caller in iterate_dir()'s
 * security_file_permission() against the lower directory's label, which ADR-0011 does not say
 * (review item 8, journal 0120); overlayfs reads directories the same way.
 */
static int moobl_iterate(struct file *file, struct dir_context *ctx)
{
	struct file *real = moobl_real(file);
	const struct cred *old;
	int err;

	real->f_pos = ctx->pos;
	old = override_creds(MOOBL_SB(file_inode(file)->i_sb)->creds);
	err = iterate_dir(real, ctx);
	revert_creds(old);
	ctx->pos = real->f_pos;
	fsstack_copy_attr_atime(file_inode(file), file_inode(real));
	return err;
}

static loff_t moobl_dir_llseek(struct file *file, loff_t offset, int whence)
{
	struct file *real = moobl_real(file);
	loff_t ret;

	inode_lock(file_inode(file));
	ret = vfs_llseek(real, offset, whence);
	if (ret >= 0)
		file->f_pos = ret;
	inode_unlock(file_inode(file));
	return ret;
}

static const struct file_operations moobl_dir_fops = {
	.open = moobl_dir_open,
	.release = moobl_release,
	.iterate_shared = moobl_iterate,
	.llseek = moobl_dir_llseek,
	.read = generic_read_dir,
	.fsync = moobl_fsync,
	.unlocked_ioctl = moobl_root_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

/* ---- mount ------------------------------------------------------------- */

struct moobl_fc {
	char *lower;
	bool nolinks;
	bool noindex;
	u64 index_budget;
	int domain_depth;		/* -1: default */
	int index_idle;			/* -1: default */
};

enum { Opt_lower, Opt_nolinks, Opt_noindex, Opt_index_budget, Opt_domain_depth, Opt_index_idle };

static const struct fs_parameter_spec moobl_params[] = {
	fsparam_string("lower", Opt_lower),
	fsparam_flag("nolinks", Opt_nolinks),
	fsparam_flag("noindex", Opt_noindex),
	fsparam_string("index_budget", Opt_index_budget),
	fsparam_u32("domain_depth", Opt_domain_depth),
	fsparam_u32("index_idle", Opt_index_idle),
	{}
};

/*
 * An option given on remount through the new mount API, already parsed into ctx: accepted when
 * it equals the mounted value, refused otherwise. libmount sends every option of the mount's
 * mountinfo line again on "mount -o remount,ro" (fspick, then fsconfig for each), so refusing
 * them all made every remount fail with util-linux 2.41 (journal 0118); a change is still
 * never accepted and dropped (journal 0106). NFS compares in the same way
 * (nfs_compare_remount_data). lower= is compared as the directory it resolves to, since
 * mountinfo prints it with seq_path.
 */
static int moobl_reconf_same(struct fs_context *fc, struct moobl_fc *ctx, int opt,
			     const char *key)
{
	struct moobl_sb *sbi = MOOBL_SB(fc->root->d_sb);
	struct path p;
	bool same;
	int err;

	switch (opt) {
	case Opt_lower:
		err = kern_path(ctx->lower, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &p);
		if (err == -ENOMEM)
			return err;
		same = !err && path_equal(&p, &sbi->lower);
		if (!err)
			path_put(&p);
		break;
	case Opt_nolinks:
		same = sbi->nolinks;
		break;
	case Opt_noindex:
		same = !sbi->index.enabled;
		break;
	case Opt_index_budget:
		same = ctx->index_budget == sbi->index.budget;
		break;
	case Opt_domain_depth:
		same = (unsigned int)ctx->domain_depth == sbi->index.domain_depth;
		break;
	case Opt_index_idle:
		same = (unsigned int)ctx->index_idle == sbi->index.idle;
		break;
	default:
		same = false;
	}
	return same ? 0 : invalfc(fc, "%s cannot be changed on remount", key);
}

static int moobl_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct moobl_fc *ctx = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	/*
	 * No option can change on remount (read-only and read-write are the VFS's). As overlayfs
	 * does, the old mount(2) API passes the current options again and they are ignored. The
	 * new API is told about a change instead of having it accepted and dropped (journal
	 * 0106), and an option repeated with its current value is accepted (journal 0118).
	 */
	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE && fc->oldapi)
		return 0;
	opt = fs_parse(fc, moobl_params, param, &result);
	if (opt < 0)
		return opt;
	switch (opt) {
	case Opt_lower:
		kfree(ctx->lower);
		ctx->lower = param->string;
		param->string = NULL;
		break;
	case Opt_nolinks:
		ctx->nolinks = true;
		break;
	case Opt_noindex:
		ctx->noindex = true;
		break;
	case Opt_index_budget: {
		char *end;
		u64 v = memparse(param->string, &end);

		if (*end || v < (1ULL << 20) || v > (4ULL << 30))
			return invalfc(fc, "index_budget must be between 1M and 4G");
		ctx->index_budget = v;
		break;
	}
	case Opt_domain_depth:
		if (result.uint_32 > 8)
			return invalfc(fc, "domain_depth must be between 0 and 8");
		ctx->domain_depth = (int)result.uint_32;
		break;
	case Opt_index_idle:
		if (result.uint_32 > 86400)
			return invalfc(fc, "index_idle must be between 0 and 86400 seconds");
		ctx->index_idle = (int)result.uint_32;
		break;
	}
	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE)
		return moobl_reconf_same(fc, ctx, opt, param->key);
	return 0;
}

static int moobl_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct moobl_fc *ctx = fc->fs_private;
	struct moobl_sb *sbi;
	struct inode *root_inode;
	struct dentry *root, *lower_root;
	struct cred *cred;
	int err;

	if (!ctx->lower)
		return invalfc(fc, "lower= is required");
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;
	/* Initialize the index state first: kill_sb tears it down on every failure path. */
	moobl_index_init(sb, ctx->index_budget ? ctx->index_budget : MOOBL_INDEX_BUDGET_DEFAULT,
			 !ctx->noindex, ctx->domain_depth < 0 ? 1 : (unsigned int)ctx->domain_depth,
			 ctx->index_idle < 0 ? MOOBL_INDEX_IDLE_DEFAULT : (unsigned int)ctx->index_idle);
	err = kern_path(ctx->lower, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &sbi->lower);
	if (err) {
		/* the walk's own error: ENOENT, ENOTDIR, EACCES, or ENOMEM, not always EINVAL */
		errorfc(fc, "cannot resolve lower directory");
		return err;
	}
	if (sbi->lower.dentry->d_sb->s_magic == MOOBL_SUPER_MAGIC)
		return invalfc(fc, "lower cannot be another moobfs_layer mount");
	/*
	 * The layer passes the lower's ids through unmapped (nop_mnt_idmap): on an idmapped
	 * lower mount its checks would use the on-disk ids, not the ones that mount shows, and a
	 * caller could act on files the lower mount gives to someone else (journal 0117).
	 */
	if (is_idmapped_mnt(sbi->lower.mnt))
		return invalfc(fc, "lower cannot be an idmapped mount");
	sb->s_stack_depth = sbi->lower.dentry->d_sb->s_stack_depth + 1;
	{
		struct kstat st;

		err = vfs_getattr_nosec(&sbi->lower, &st, STATX_BASIC_STATS, AT_STATX_DONT_SYNC);
		if (err)
			return err;
		sbi->lower_dev = st.dev;
	}
	if (sb->s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH)
		return invalfc(fc, "maximum filesystem stacking depth exceeded");
	cred = prepare_creds();
	if (!cred)
		return -ENOMEM;
	/* Delegated work never overrides disk quota limits or uses reserved space. */
	cap_lower(cred->cap_effective, CAP_SYS_RESOURCE);
	sbi->creds = cred;
	sbi->nolinks = ctx->nolinks;
	sb->s_xattr = moobl_xattr_handlers;
	{
		/* Refuse a lower tree written by a newer encoding; absence is fine. */
		u8 marker[16];
		int n = vfs_getxattr(&nop_mnt_idmap, sbi->lower.dentry,
				     MOOB_ATTR_PREFIX MOOB_ATTR_FORMAT_NAME, marker, sizeof(marker));

		if (n > 0) {
			unsigned int version = moob_attr_parse_format(marker, n);

			if (!version || version > MOOB_ATTR_FORMAT_VERSION)
				return invalfc(fc, "lower tree uses an unsupported moobfs attribute format");
		} else if (n == 0 || n == -ERANGE) {
			/* An empty marker, or one longer than any version number, is not a version. */
			return invalfc(fc, "lower tree uses an unsupported moobfs attribute format");
		} else if (n == -EOPNOTSUPP) {
			pr_info("moobfs_layer: lower filesystem has no user xattr support; typed attributes unavailable\n");
		} else if (n != -ENODATA) {
			errorfc(fc, "cannot read the moobfs format marker on the lower root");
			return n;
		}
	}
	sb->s_magic = MOOBL_SUPER_MAGIC;
	sb->s_op = &moobl_sops;
	sb->s_d_op = &moobl_dops;
	sb->s_maxbytes = sbi->lower.dentry->d_sb->s_maxbytes;
	sb->s_time_gran = sbi->lower.dentry->d_sb->s_time_gran;
	sb->s_blocksize = sbi->lower.dentry->d_sb->s_blocksize;
	sb->s_blocksize_bits = sbi->lower.dentry->d_sb->s_blocksize_bits;
	sb->s_flags |= SB_NOSEC;	/* lower enforces suid/sgid clearing */
	if (sbi->lower.dentry->d_sb->s_flags & SB_POSIXACL)
		sb->s_flags |= SB_POSIXACL;	/* ACL xattrs route to get_acl/set_acl, delegated below */
	sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV | SB_I_SKIP_SYNC;

	root_inode = moobl_get_inode(sb, d_inode(sbi->lower.dentry));
	if (IS_ERR(root_inode))
		return PTR_ERR(root_inode);
	root = d_make_root(root_inode);
	if (!root)
		return -ENOMEM;
	sb->s_root = root;
	/*
	 * moobl_set_lower_path() takes the reference only when it succeeds: on failure it is
	 * dropped here, or the lower root stays pinned after the mount fails (found by syzkaller
	 * with an allocation fault, journal 0112).
	 */
	lower_root = dget(sbi->lower.dentry);
	err = moobl_set_lower_path(root, sbi->lower.mnt, lower_root);
	if (err) {
		dput(lower_root);
		return err;
	}
	err = moobl_index_shrinker_start(sb);
	if (err)
		return err;
	moobl_index_mount(sb);
	return 0;
}

static int moobl_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, moobl_fill_super);
}

static void moobl_free_fc(struct fs_context *fc)
{
	struct moobl_fc *ctx = fc->fs_private;

	if (ctx) {
		kfree(ctx->lower);
		kfree(ctx);
	}
}

static const struct fs_context_operations moobl_context_ops = {
	.parse_param = moobl_parse_param,
	.get_tree = moobl_get_tree,
	.free = moobl_free_fc,
};

static int moobl_init_fs_context(struct fs_context *fc)
{
	struct moobl_fc *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);

	if (!ctx)
		return -ENOMEM;
	ctx->domain_depth = -1;
	ctx->index_idle = -1;
	fc->fs_private = ctx;
	fc->ops = &moobl_context_ops;
	return 0;
}

static void moobl_kill_sb(struct super_block *sb)
{
	struct moobl_sb *sbi = sb->s_fs_info;

	kill_anon_super(sb);
	if (sbi) {
		moobl_index_destroy(sb);
		if (sbi->creds)
			put_cred(sbi->creds);
		path_put(&sbi->lower);
		kfree(sbi);
	}
}

static struct file_system_type moobl_fs_type = {
	.owner = THIS_MODULE,
	.name = MOOBL_NAME,
	.init_fs_context = moobl_init_fs_context,
	.parameters = moobl_params,
	.kill_sb = moobl_kill_sb,
};
MODULE_ALIAS_FS(MOOBL_NAME);

static void moobl_inode_init_once(void *obj)
{
	inode_init_once(&((struct moobl_inode *)obj)->vfs_inode);
}

static int __init moobl_init(void)
{
	int err;

	moobl_inode_cache = kmem_cache_create("moobl_inode", sizeof(struct moobl_inode),
					      0, SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT,
					      moobl_inode_init_once);
	if (!moobl_inode_cache)
		return -ENOMEM;
	err = register_filesystem(&moobl_fs_type);
	if (err)
		kmem_cache_destroy(moobl_inode_cache);
	return err;
}

static void __exit moobl_exit(void)
{
	unregister_filesystem(&moobl_fs_type);
	rcu_barrier();
	kmem_cache_destroy(moobl_inode_cache);
}

module_init(moobl_init);
module_exit(moobl_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Studio Moob");
MODULE_VERSION("0.3.0");
MODULE_DESCRIPTION("MoobFS memory layer: stacked filesystem with a typed attribute index");
