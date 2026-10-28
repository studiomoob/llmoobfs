/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ext_acl() of layer/index.c from user space, shared by moobfs-index and moobfs-check
 * (journal 0222). An access ACL counts as extended when it may decide otherwise than the mode
 * bits: it has a named user, named group or mask entry (posix_acl_equiv_mode() in
 * fs/posix_acl.c returns 1), or its three minimal entries differ from the mode bits, or it
 * cannot be read or parsed (the kernel treats an unreadable ACL as restrictive). No ACL, or a
 * filesystem without ACLs, is not extended. The bytes are the VFS's xattr form
 * (include/uapi/linux/posix_acl_xattr.h): a little-endian version 2 header, then 8-byte
 * entries {tag, perm, id}.
 */
#ifndef MOOBFS_EXT_ACL_H
#define MOOBFS_EXT_ACL_H
#include <errno.h>
#include <sys/stat.h>
#include <sys/xattr.h>

static int moob_ext_acl(const char *path, mode_t mode)
{
	static unsigned char buf[65536];	/* XATTR_SIZE_MAX: getxattr cannot return ERANGE */
	unsigned int bits = 0, i;
	int not_equiv = 0;
	ssize_t n = getxattr(path, "system.posix_acl_access", buf, sizeof buf);

	if (n < 0)
		return !(errno == ENODATA || errno == EOPNOTSUPP);
	if (n < 4 || (n - 4) % 8 || buf[0] != 2 || buf[1] || buf[2] || buf[3])
		return 1;
	for (i = 4; i < (unsigned int)n; i += 8) {
		unsigned int tag = buf[i] | buf[i + 1] << 8, perm = (buf[i + 2] | buf[i + 3] << 8) & 07;

		switch (tag) {
		case 0x01: bits |= perm << 6; break;			/* ACL_USER_OBJ */
		case 0x04: bits |= perm << 3; break;			/* ACL_GROUP_OBJ */
		case 0x20: bits |= perm; break;				/* ACL_OTHER */
		case 0x10: bits = (bits & ~070U) | perm << 3; not_equiv = 1; break;	/* ACL_MASK */
		case 0x02: case 0x08: not_equiv = 1; break;		/* ACL_USER, ACL_GROUP */
		default: return 1;					/* -EINVAL in the kernel */
		}
	}
	return not_equiv || bits != (mode & 0777);
}
#endif
