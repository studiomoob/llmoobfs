# SPDX-License-Identifier: LGPL-2.1-or-later
"""The answer of a query without the module: a walk of the domain directory that applies the
kernel's rules (client contract section 7, ADR-0013's fallback).

A file is a candidate only where the kernel would index it (docs/spec-query-v2.md section 3,
layer/index.c reach_ok and target_ok): every directory between the domain and the file can be
listed and traversed by everyone who can traverse the domain, the file is readable by all of
them, it has one
name, and neither carries an access ACL beyond the minimal one. So the scan returns what QUERY2
returns on a mounted layer, never a file the domain does not share with all who can enter it,
even when the caller could open it. The caller must be able to read and search the domain
directory, as QUERY2 requires.

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


def _ext_acl(path):
    """ext_acl: an access ACL with more than the three minimal entries. An ACL that cannot be
    read counts as extended, as in the kernel."""
    try:
        raw = (os.getxattr(path, _ACL_ACCESS) if isinstance(path, int)
               else os.getxattr(path, _ACL_ACCESS, follow_symlinks=False))
    except OSError as err:
        if err.errno in (errno.ENODATA, errno.EOPNOTSUPP, errno.ENOTSUP):
            return False
        return True
    if len(raw) < 4 or (len(raw) - 4) % 8:
        return True
    return (len(raw) - 4) // 8 > 3


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


def candidates(domain_dir):
    """(path relative to the domain, inode, {name: Value}) of every file and directory below
    the domain that the kernel would index, with its indexed attributes."""
    domain_dir = os.path.abspath(domain_dir)
    fd = os.open(domain_dir, os.O_RDONLY | os.O_DIRECTORY)
    try:
        dom = os.fstat(fd)          # the directory QUERY2 would be issued on, not a link to it
        dom_ext = _ext_acl(fd)
    finally:
        os.close(fd)
    effective = os.access in os.supports_effective_ids
    if not os.access(domain_dir, os.R_OK | os.X_OK, effective_ids=effective):
        raise PermissionError(errno.EACCES, os.strerror(errno.EACCES), domain_dir)
    out = []
    stack = [""]
    while stack:
        rel = stack.pop()
        here = os.path.join(domain_dir, rel) if rel else domain_dir
        try:
            entries = list(os.scandir(here))
        except (FileNotFoundError, NotADirectoryError, PermissionError):
            continue
        for e in entries:
            path = e.path
            try:
                st = os.lstat(path)
            except FileNotFoundError:
                continue
            sub = os.path.join(rel, e.name) if rel else e.name
            is_dir, is_reg = stat.S_ISDIR(st.st_mode), stat.S_ISREG(st.st_mode)
            if not (is_dir or is_reg):
                continue
            ext = _ext_acl(path)
            if is_dir and _reach_ok(st, ext, dom, dom_ext, False):
                stack.append(sub)
            if is_reg and st.st_nlink > 1:
                continue
            if not _reach_ok(st, ext, dom, dom_ext, True):
                continue
            try:
                attrs = get_attrs(path)
            except (FileNotFoundError, PermissionError):
                continue
            except OSError as err:
                if err.errno in (errno.ENOTSUP, errno.EOPNOTSUPP):
                    continue
                raise
            if attrs:
                out.append((sub, st.st_ino, attrs))
    return out


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
