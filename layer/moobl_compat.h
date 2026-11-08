/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel interface adaptations of the MoobFS memory layer (ADR-0034). The module builds on two
 * series: 6.12, the forms it was written for, and 7.3, read against the 7.3.3 sources and their
 * Documentation/filesystems/porting.rst with fs/overlayfs as the model (journal 0275). Only forms
 * change here, never behavior; the 6.12 branch of every helper is the call the module made before.
 * Releases between the two are not ported: their forms are a mix of both, so they are refused
 * with a message instead of a list of type errors.
 */
#ifndef MOOBL_COMPAT_H
#define MOOBL_COMPAT_H

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/fileattr.h>
#include <linux/filelock.h>
#include <linux/mnt_idmapping.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 3, 0)
#define MOOBL_VFS_73 1
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
#error "moobfs_layer is ported to Linux 6.12 and 7.3 only (ADR-0034): see docs/compatibility.md"
#else
#define MOOBL_VFS_73 0
#endif

#if MOOBL_VFS_73
/*
 * vfs_mkdir() returns the dentry and, on error, has already unlocked the parent and dropped the
 * dentry (porting.rst; fs/namei.c, vfs_mkdir()). moobl_mkdir() relies on both; a form that
 * returns an int again would make its error path release them a second time.
 */
static_assert(__same_type(vfs_mkdir,
			  struct dentry *(struct mnt_idmap *, struct inode *, struct dentry *,
					  umode_t, struct delegated_inode *)),
	      "moobfs_layer needs the 7.3 vfs_mkdir(): see journal 0275");
static_assert(__same_type(start_renaming_dentry,
			  int (struct renamedata *, int, struct dentry *, struct qstr *)),
	      "moobfs_layer needs the 7.3 start_renaming_dentry(): see journal 0275");
#else
/* on 6.12 vfs_mkdir() leaves the parent locked and the dentry referenced on error */
static_assert(__same_type(vfs_mkdir,
			  int (struct mnt_idmap *, struct inode *, struct dentry *, umode_t)),
	      "moobfs_layer needs the 6.12 vfs_mkdir(): see journal 0275");
#endif

/* struct fileattr became struct file_kattr; the fields the layer reads are the same */
#if MOOBL_VFS_73
#define moobl_kattr file_kattr
#else
#define moobl_kattr fileattr
#endif

/*
 * Lookups in the lower directory. lookup_one_len*() became lookup_noperm*(), which skip the
 * parent's search permission; lookup_one*() keep it, as lookup_one_len*() did, and take a qstr
 * (porting.rst). The lower mount is never idmapped (moobl_fill_super), hence nop_mnt_idmap.
 */
static inline struct dentry *moobl_lookup_one(const char *name, struct dentry *base, int len)
{
#if MOOBL_VFS_73
	return lookup_one(&nop_mnt_idmap, &QSTR_LEN(name, len), base);
#else
	return lookup_one_len(name, base, len);
#endif
}

static inline struct dentry *moobl_lookup_one_unlocked(const char *name, struct dentry *base,
						       int len)
{
#if MOOBL_VFS_73
	return lookup_one_unlocked(&nop_mnt_idmap, &QSTR_LEN(name, len), base);
#else
	return lookup_one_len_unlocked(name, base, len);
#endif
}

/* i_state is read through helpers (fs.h, inode_state_read_once()), as ecryptfs does after iget5 */
static inline bool moobl_inode_is_new(struct inode *inode)
{
#if MOOBL_VFS_73
	return inode_state_read_once(inode) & I_NEW;
#else
	return inode->i_state & I_NEW;
#endif
}

/* generic_drop_inode() was renamed inode_generic_drop(); the test is the same */
static inline int moobl_generic_drop(struct inode *inode)
{
#if MOOBL_VFS_73
	return inode_generic_drop(inode);
#else
	return generic_drop_inode(inode);
#endif
}

/* s_d_op is set through set_default_d_op() (porting.rst) */
static inline void moobl_set_d_op(struct super_block *sb, const struct dentry_operations *ops)
{
#if MOOBL_VFS_73
	set_default_d_op(sb, ops);
#else
	sb->s_d_op = ops;
#endif
}

/*
 * The lower's own ->d_revalidate(), in ref-walk only. On 7.3 it takes the parent's inode and the
 * name, both of which must stay stable during the call (porting.rst): the parent is pinned and
 * the name is a snapshot, as ovl_revalidate_real() does in its ref-walk branch
 * (fs/overlayfs/super.c).
 */
static inline int moobl_lower_d_revalidate(struct dentry *lower, unsigned int flags)
{
#if MOOBL_VFS_73
	struct dentry *parent = dget_parent(lower);
	struct name_snapshot n;
	int rc;

	take_dentry_name_snapshot(&n, lower);
	rc = lower->d_op->d_revalidate(d_inode(parent), &n.name, lower, flags);
	release_dentry_name_snapshot(&n);
	dput(parent);
	return rc;
#else
	return lower->d_op->d_revalidate(lower, flags);
#endif
}

/*
 * Name operations on the lower. 7.3 adds a delegated_inode argument, NULL here as in every
 * overlayfs call (fs/overlayfs/overlayfs.h): a delegation on the lower directory is broken
 * without waiting, as vfs_unlink() and vfs_link() already did with NULL on 6.12. vfs_create()
 * takes the parent from the dentry and no longer has excl (.create lost it): the parent is the
 * locked lower_dir, and ext4, btrfs and XFS never read excl.
 */
static inline int moobl_vfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
				   bool excl)
{
#if MOOBL_VFS_73
	return vfs_create(&nop_mnt_idmap, dentry, mode, NULL);
#else
	return vfs_create(&nop_mnt_idmap, dir, dentry, mode, excl);
#endif
}

static inline int moobl_vfs_rmdir(struct inode *dir, struct dentry *dentry)
{
#if MOOBL_VFS_73
	return vfs_rmdir(&nop_mnt_idmap, dir, dentry, NULL);
#else
	return vfs_rmdir(&nop_mnt_idmap, dir, dentry);
#endif
}

static inline int moobl_vfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
#if MOOBL_VFS_73
	return vfs_symlink(&nop_mnt_idmap, dir, dentry, symname, NULL);
#else
	return vfs_symlink(&nop_mnt_idmap, dir, dentry, symname);
#endif
}

#endif /* MOOBL_COMPAT_H */
