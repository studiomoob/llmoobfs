# SPDX-License-Identifier: LGPL-2.1-or-later
"""The answer of a query without the module: a walk of the domain directory that applies the
kernel's rules (client contract section 7, ADR-0013's fallback).

A file is a candidate only where the kernel would index it (docs/spec-query-v2.md section 3,
layer/index.c reach_ok and target_ok): every directory between the domain and the file can be
listed and traversed by everyone who can traverse the domain, the file is readable by all of
them, it has one
name, and neither carries an extended access ACL (one that may decide otherwise than the mode
bits). So the scan returns what QUERY2
returns on a mounted layer, never a file the domain does not share with all who can enter it,
even when the caller could open it. The caller must be able to read and search the domain
directory, as QUERY2 requires.

Below the domain the walk never resolves a path: each entry is opened relative to its
directory's descriptor without following a symbolic link, and is judged and read through that
descriptor (journal 0231). Someone who shares the domain and swaps a directory or a file for a
link while the walk runs cannot make it list, judge or read anything outside the domain.

Known differences from the kernel, all at the edges: a file whose attribute list exceeds 64 KiB
is left out by the kernel and read here; the kernel stops at 64 levels below the mount root; a
domain the kernel answers in fallback mode after a directory's permissions changed, until the
next mount (spec-query-v2 section 3, Q2a).
"""
import errno
import os
import stat

from .attrs import get_attrs
from .query import Match, check_request, sort_key

_ACL_ACCESS = "system.posix_acl_access"


def _ext_acl(path, mode):
    """ext_acl of layer/index.c: an access ACL that may decide otherwise than the mode bits,
    because it has a named or mask entry (posix_acl_equiv_mode() is not 0) or its three entries
    differ from mode (journal 0222). An ACL that cannot be read or parsed counts as extended,
    as in the kernel; no ACL, or no ACL support, does not."""
    try:
        raw = (os.getxattr(path, _ACL_ACCESS) if isinstance(path, int)
               else os.getxattr(path, _ACL_ACCESS, follow_symlinks=False))
    except OSError as err:
        if err.errno in (errno.ENODATA, errno.EOPNOTSUPP, errno.ENOTSUP):
            return False
        return True
    if len(raw) < 4 or (len(raw) - 4) % 8 or raw[:4] != b"\x02\x00\x00\x00":
        return True
    bits, not_equiv = 0, False
    for off in range(4, len(raw), 8):
        tag, perm = raw[off] | raw[off + 1] << 8, (raw[off + 2] | raw[off + 3] << 8) & 0o7
        if tag == 0x01:         # ACL_USER_OBJ
            bits |= perm << 6
        elif tag == 0x04:       # ACL_GROUP_OBJ
            bits |= perm << 3
        elif tag == 0x20:       # ACL_OTHER
            bits |= perm
        elif tag == 0x10:       # ACL_MASK
            bits, not_equiv = (bits & ~0o70) | perm << 3, True
        elif tag in (0x02, 0x08):   # ACL_USER, ACL_GROUP
            not_equiv = True
        else:
            return True
    return not_equiv or bits != mode & 0o777


def _reach_ok(st, ext, dom, dom_ext, want_read):
    """reach_ok for an lstat result st (with its ACL flag) against the domain's: read for a
    file, read and search for an intermediate directory (a search-only one keeps its names
    from those who cannot list it, journal 0215)."""
    cls = (0o400, 0o040, 0o004) if want_read else (0o500, 0o050, 0o005)
    mode = st.st_mode
    if not ext and all(mode & b == b for b in cls):
        return True
    if ext or dom_ext:
        return False
    if st.st_uid != dom.st_uid or st.st_gid != dom.st_gid:
        return False
    for search, bits in zip((0o100, 0o010, 0o001), cls):
        if dom.st_mode & search and mode & bits != bits:
            return False
    return True


# Every name below the domain is opened relative to its directory's descriptor and never
# through a symbolic link (journal 0231), as moobfs.mcp walks a path (journal 0227).
# O_NONBLOCK: an entry swapped for a FIFO after it was listed must not hold the walk.
_DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
_ENTRY_FLAGS = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_NOCTTY | os.O_CLOEXEC
# an entry gone, swapped for a link or another type, or closed to the caller is left out, as a
# path the old walk could not list or read was
_SKIP = frozenset((errno.ENOENT, errno.ENOTDIR, errno.ELOOP, errno.EACCES, errno.EPERM,
                   errno.ENXIO, errno.EWOULDBLOCK, errno.EAGAIN))


def _open_entry(name, dir_fd, flags):
    """A descriptor of name under dir_fd, or None when the walk leaves it out."""
    try:
        return os.open(name, flags, dir_fd=dir_fd)
    except OSError as err:
        if err.errno in _SKIP:
            return None
        raise


def _visit(dir_fd, rel, dom, dom_ext, out):
    """Add the candidates listed in dir_fd to out; return (name, (dev, ino)) of the
    directories under it the kernel would descend into. Each entry is opened without
    following a link and every decision is taken on what the descriptor holds (fstat, the
    ACL and the attributes read through it), so a name swapped between two steps cannot make
    the walk judge one inode and report another."""
    try:
        entries = list(os.scandir(dir_fd))
    except OSError as err:
        if err.errno in _SKIP:
            return []
        raise
    subdirs = []
    for e in entries:
        try:
            if not (e.is_dir(follow_symlinks=False) or e.is_file(follow_symlinks=False)):
                continue
        except OSError:
            continue
        fd = _open_entry(e.name, dir_fd, _ENTRY_FLAGS)
        if fd is None:
            continue
        try:
            st = os.fstat(fd)
            is_dir, is_reg = stat.S_ISDIR(st.st_mode), stat.S_ISREG(st.st_mode)
            if not (is_dir or is_reg):
                continue
            sub = os.path.join(rel, e.name) if rel else e.name
            ext = _ext_acl(fd, st.st_mode)
            if is_dir and _reach_ok(st, ext, dom, dom_ext, False):
                subdirs.append((e.name, (st.st_dev, st.st_ino)))
            if is_reg and st.st_nlink > 1:
                continue
            if not _reach_ok(st, ext, dom, dom_ext, True):
                continue
            try:
                attrs = get_attrs(fd)
            except (FileNotFoundError, PermissionError):
                continue
            except OSError as err:
                if err.errno in (errno.ENOTSUP, errno.EOPNOTSUPP):
                    continue
                raise
            if attrs:
                out.append((sub, st.st_ino, attrs))
        finally:
            os.close(fd)
    return subdirs


def candidates(domain_dir):
    """(path relative to the domain, inode, {name: Value}) of every file and directory below
    the domain that the kernel would index, with its indexed attributes.

    The domain is opened once by its path (a link to it is followed: it is the caller's
    name for the directory QUERY2 would be issued on); everything below it is reached through
    directory descriptors, never by path and never through a symbolic link. A directory is
    opened again by name when the walk descends into it and must still be the inode that was
    judged, and still pass the rule; otherwise it is left out."""
    domain_dir = os.path.abspath(domain_dir)
    top = os.open(domain_dir, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    frames = []
    try:
        dom = os.fstat(top)         # the directory QUERY2 would be issued on, not a link to it
        dom_ext = _ext_acl(top, dom.st_mode)
        effective = os.access in os.supports_effective_ids
        if os.access in os.supports_dir_fd:
            ok = os.access(".", os.R_OK | os.X_OK, dir_fd=top, effective_ids=effective)
        else:
            ok = os.access(domain_dir, os.R_OK | os.X_OK, effective_ids=effective)
        if not ok:
            raise PermissionError(errno.EACCES, os.strerror(errno.EACCES), domain_dir)
        out = []
        frames.append((top, "", _visit(top, "", dom, dom_ext, out)))
        while frames:
            fd, rel, pending = frames[-1]
            if not pending:
                frames.pop()
                if fd != top:
                    os.close(fd)
                continue
            name, ident = pending.pop()
            child = _open_entry(name, fd, _DIR_FLAGS)
            if child is None:
                continue
            sub = os.path.join(rel, name) if rel else name
            frames.append((child, sub, []))     # closed by the loop or by finally
            cst = os.fstat(child)
            # still the inode that was judged, and still one to descend into
            if ((cst.st_dev, cst.st_ino) == ident
                    and _reach_ok(cst, _ext_acl(child, cst.st_mode), dom, dom_ext, False)):
                frames[-1] = (child, sub, _visit(child, sub, dom, dom_ext, out))
        return out
    finally:
        for fd, _, _ in frames:
            if fd != top:
                os.close(fd)
        os.close(top)


def run(cands, terms, order=None):
    """The answers of one request over candidates(): matching as the kernel matches, ordered
    by (value, inode) when ordered, else by inode (the kernel's order is then unspecified)."""
    terms = list(terms)
    check_request(terms, order)
    hits = [c for c in cands if all(t.matches(c[2].get(t.name)) for t in terms)]
    if order is None:
        hits.sort(key=lambda c: c[1])
    else:
        hits = [c for c in hits if c[2].get(order.name) is not None
                and c[2][order.name].type == order.type]
        hits.sort(key=lambda c: (sort_key(c[2][order.name]), c[1]), reverse=order.descending)
    return [Match(os.fsdecode(rel), ino) for rel, ino, _ in hits]


def scan_query(domain_dir, terms, order=None):
    return run(candidates(domain_dir), terms, order)


def scan_batch(domain_dir, requests):
    cands = candidates(domain_dir)
    return [run(cands, t, o) for t, o in requests]
