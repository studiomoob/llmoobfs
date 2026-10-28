#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""A LangGraph store kept in a MoobFS layer (second test client, journal 0089).

MoobStore implements langgraph.store.base.BaseStore on a directory. With the layer
mounted, a search whose prefix reaches a domain is one MOOB_IOC_QUERY2 per domain.
Without the layer, the same search reads the files the caller can open. Either way
the results are checked again against the whole filter.

What the kernel does not answer stays in this file and is not a kernel query:
``$ne``, lists, nested objects, invalid attribute names, client offset, concurrent
publishers of one key, disjunctions, counts and full text (client contract section 8).
Two publishers of the same key are last-rename-wins. Each final name is one complete
file, not a mix of two publications.

Nothing here is in the layer or its interface.
"""
import ctypes
import errno
import fcntl
import json
import os
import stat
import struct
import sys
from datetime import datetime, timezone
from urllib.parse import quote, unquote

from langgraph.store.base import (BaseStore, GetOp, Item, ListNamespacesOp, PutOp, SearchItem,
                                  SearchOp)

T_STRING, T_INT64, T_DOUBLE, T_BOOL, T_TIME = 1, 2, 3, 4, 5
OP_EQ, OP_LT, OP_LE, OP_GT, OP_GE, OP_EXISTS = 1, 2, 3, 4, 5, 7
NAME_MAX, VALUE_MAX, PATH_MAX, MAX_TERMS, MAX_LIMIT, CURSOR = 64, 1024, 256, 4, 4096, 128
RES_PATH_UNKNOWN, RES_PATH_TRUNCATED = 1, 2
PREFIX = "user.moob."
NAME_OK = set("abcdefghijklmnopqrstuvwxyz0123456789_.")
RESOURCE_ERRNO = frozenset({errno.ENOMEM, errno.E2BIG, errno.ETIMEDOUT, errno.EDQUOT})
ABSENT_ERRNO = frozenset({errno.ENOTTY, errno.ENODEV})


class MoobQueryError(OSError):
    """QUERY2 refused for a resource limit. No result list from that call is usable."""


class ModuleAbsent(OSError):
    """This descriptor does not serve QUERY2."""


class PublicationStopped(Exception):
    """A test stopped the publication sequence. The final name was not left mixed."""

    def __init__(self, step):
        super().__init__(step)
        self.step = step


class Term(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * (NAME_MAX + 1)), ("op", ctypes.c_uint8),
                ("type", ctypes.c_uint8), ("flags", ctypes.c_uint8), ("len", ctypes.c_uint16),
                ("value", ctypes.c_uint8 * VALUE_MAX), ("pad", ctypes.c_uint8 * 4)]


class Result(ctypes.Structure):
    _fields_ = [("ino", ctypes.c_uint64), ("flags", ctypes.c_uint32),
                ("path_len", ctypes.c_uint32), ("path", ctypes.c_char * PATH_MAX)]


class Query2(ctypes.Structure):
    _fields_ = [("version", ctypes.c_uint32), ("nterms", ctypes.c_uint32),
                ("limit", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                ("results", ctypes.c_uint64), ("count", ctypes.c_uint32),
                ("truncated", ctypes.c_uint32), ("order_by", ctypes.c_char * (NAME_MAX + 1)),
                ("order", ctypes.c_uint8), ("order_type", ctypes.c_uint8),
                ("pad", ctypes.c_uint8 * 5), ("cursor", ctypes.c_uint8 * CURSOR),
                ("reserved", ctypes.c_uint64 * 4), ("terms", Term * MAX_TERMS)]


def _iowr(nr, size):
    return (3 << 30) | (size << 16) | (ord("M") << 8) | nr


IOC_QUERY2 = _iowr(3, ctypes.sizeof(Query2))


def file_mode_for(directory_mode):
    """Mode of a file that is not more private than this directory.

    A class that can enter the directory can read and write the file. Execute stays
    off: a memory is not a program. The setgid bit is not copied onto the file.
    """
    mode = stat.S_IMODE(directory_mode)
    out = 0
    if mode & 0o700:
        out |= 0o600
    if mode & 0o070:
        out |= 0o060
    if mode & 0o007:
        out |= 0o006
    return out


def _enc(label):
    return quote(label, safe="")


def _attr_name(key):
    """The attribute name of a value key, or None when the key cannot be an attribute name."""
    name = "v." + key
    if len(name) > NAME_MAX or not key or any(c not in NAME_OK for c in key):
        return None
    return name


def _typed(value):
    """(type, payload) of a value that can be indexed, else None. Every number is a double, so
    that 3 and 3.0 are one value, as they are for LangGraph's filters."""
    if isinstance(value, bool):
        return T_BOOL, bytes([1 if value else 0])
    if isinstance(value, (int, float)):
        f = float(value)
        if f != f or f in (float("inf"), float("-inf")) or (isinstance(value, int) and int(f) != value):
            return None
        return T_DOUBLE, struct.pack("<d", f)
    if isinstance(value, str):
        b = value.encode()
        if len(b) > 1024 or b"\0" in b:
            return None
        return T_STRING, b
    return None


def _compare(item_value, filter_value):
    """LangGraph's own filter semantics (store/memory/__init__.py of langgraph 1.2)."""
    if isinstance(filter_value, dict):
        if any(k.startswith("$") for k in filter_value):
            return all(_operator(item_value, op, v) for op, v in filter_value.items())
        if not isinstance(item_value, dict):
            return False
        return all(_compare(item_value.get(k), v) for k, v in filter_value.items())
    if isinstance(filter_value, (list, tuple)):
        return (isinstance(item_value, (list, tuple)) and len(item_value) == len(filter_value)
                and all(_compare(a, b) for a, b in zip(item_value, filter_value)))
    return item_value == filter_value


def _operator(value, op, op_value):
    if op == "$eq":
        return value == op_value
    if op == "$ne":
        return value != op_value
    if op == "$gt":
        return float(value) > float(op_value)
    if op == "$gte":
        return float(value) >= float(op_value)
    if op == "$lt":
        return float(value) < float(op_value)
    if op == "$lte":
        return float(value) <= float(op_value)
    raise ValueError(f"Unsupported operator: {op}")


class MoobStore(BaseStore):
    def __init__(self, root, domain_depth=1):
        self.root = os.path.abspath(root)
        self.depth = domain_depth
        self.stop_after = None
        self.stats = {"query2_calls": 0, "candidates": 0, "returned": 0, "files_read": 0,
                      "post_filtered_out": 0, "terms_indexed": 0, "terms_not_indexed": 0,
                      "domains_visited": 0, "truncated_paths": 0, "files_above_domain": 0,
                      "fallback_domains": 0}

    def _checkpoint(self, step):
        if self.stop_after == step:
            raise PublicationStopped(step)

    # ---- paths ---------------------------------------------------------------------------
    def _dir(self, namespace):
        return os.path.join(self.root, *[_enc(x) for x in namespace])

    def _file(self, namespace, key):
        return os.path.join(self._dir(namespace), _enc(key) + ".json")

    def _prepare_dirs(self, namespace):
        """Create missing namespace directories. Return the domain's mode, owner and,
        when the group can enter it, the domain's group id. A directory that already
        exists keeps the mode the test gave it. A directory created below the domain is
        chmod'd to that mode so it is not more private than the domain (journal 0089)."""
        cur = self.root
        domain_mode, domain_uid, domain_gid = 0o700, os.geteuid(), None
        for index, label in enumerate(namespace, start=1):
            cur = os.path.join(cur, _enc(label))
            if not os.path.isdir(cur):
                os.mkdir(cur, 0o700)
                if index > self.depth:
                    os.chmod(cur, domain_mode)
            if index == self.depth and os.path.isdir(cur):
                st = os.stat(cur)
                domain_mode = stat.S_IMODE(st.st_mode)
                domain_uid = st.st_uid
                domain_gid = st.st_gid if domain_mode & 0o070 else None
        return domain_mode, domain_uid, domain_gid

    # ---- single operations -----------------------------------------------------------------
    def _load(self, path, namespace=None, key=None):
        try:
            with open(path, "rb") as f:
                doc = json.loads(f.read())
        except FileNotFoundError:
            return None
        self.stats["files_read"] += 1
        if namespace is None:
            rel = os.path.relpath(path, self.root).split(os.sep)
            namespace, key = tuple(unquote(x) for x in rel[:-1]), unquote(rel[-1][:-5])
        return Item(value=doc["value"], key=key, namespace=tuple(namespace),
                    created_at=datetime.fromisoformat(doc["created_at"]),
                    updated_at=datetime.fromisoformat(doc["updated_at"]))

    def _put(self, op):
        path = self._file(op.namespace, op.key)
        if op.value is None:
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass
            return
        d = os.path.dirname(path)
        domain_mode, domain_uid, domain_gid = self._prepare_dirs(op.namespace)
        mode = file_mode_for(domain_mode)
        now = datetime.now(timezone.utc)
        old = self._load(path, op.namespace, op.key)
        doc = {"value": op.value, "created_at": (old.created_at if old else now).isoformat(),
               "updated_at": now.isoformat()}
        tmp = os.path.join(d, f".tmp.{os.getpid()}.{os.urandom(8).hex()}")
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            os.write(fd, json.dumps(doc).encode())
            self._checkpoint("content")
            os.setxattr(fd, PREFIX + "lg.updated",
                        bytes([T_TIME]) + struct.pack("<q", int(now.timestamp() * 1e9)))
            for k, v in op.value.items():
                name, typed = _attr_name(k), _typed(v)
                if name and typed:
                    os.setxattr(fd, PREFIX + name, bytes([typed[0]]) + typed[1])
            self._checkpoint("attributes")
            self._publish_identity(fd, mode, domain_uid, domain_gid)
            os.fsync(fd)
            self._checkpoint("file_fsync")
        except PublicationStopped:
            raise
        except Exception:
            os.close(fd)
            fd = -1
            try:
                os.unlink(tmp)
            except FileNotFoundError:
                pass
            raise
        finally:
            if fd >= 0:
                os.close(fd)
        os.rename(tmp, path)
        self._checkpoint("rename")
        dfd = os.open(d, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(dfd)
        finally:
            os.close(dfd)
        self._checkpoint("dir_fsync")

    def _publish_identity(self, fd, mode, domain_uid, domain_gid):
        """Mode and owner the homogeneity rule can index.

        reach_ok accepts a file that shares the domain directory's owner and group
        and is readable by every class that can enter that directory. It also
        accepts a file readable by owner, group and other, whatever the owner is.
        A publisher who is not the domain owner cannot chown the file to that
        owner, so the mode then gains other-read. The group is still set when
        this process may do so.
        """
        owns = os.geteuid() in (0, domain_uid)
        if not owns:
            mode |= 0o444
        os.fchmod(fd, mode)
        uid = domain_uid if owns else -1
        gid = domain_gid if domain_gid is not None else -1
        if uid == -1 and gid == -1:
            return
        try:
            os.fchown(fd, uid, gid)
        except PermissionError:
            if owns:
                raise

    # ---- search ----------------------------------------------------------------------------
    def _terms(self, flt):
        """Up to four QUERY2 terms that are implied by the filter, and how many conditions were
        left for the check after the read."""
        terms, left = [], 0
        for key, cond in (flt or {}).items():
            name = _attr_name(key)
            conds = cond.items() if isinstance(cond, dict) and any(k.startswith("$") for k in cond) \
                else [("$eq", cond)]
            for op, v in conds:
                code = {"$eq": OP_EQ, "$gt": OP_GT, "$gte": OP_GE, "$lt": OP_LT, "$lte": OP_LE}.get(op)
                typed = _typed(v)
                usable = (name and code and typed and len(terms) < MAX_TERMS
                          and (code == OP_EQ or typed[0] == T_DOUBLE))
                if usable:
                    terms.append((name, code, typed[0], typed[1]))
                else:
                    left += 1
        self.stats["terms_indexed"] += len(terms)
        self.stats["terms_not_indexed"] += left
        if not terms:
            terms.append(("lg.updated", OP_EXISTS, 0, b""))
        return terms

    def _matches(self, item, flt):
        return all(_compare(item.value.get(k), v) for k, v in flt.items())

    def _query2(self, domain_dir, terms):
        """Every path of the domain that matches the terms, relative to the domain directory.

        A resource refusal raises MoobQueryError and does not return the pages already
        copied. ENOTTY or ENODEV on the first call raises ModuleAbsent so the caller can
        fall back; the same error after a page raises, and that page is not returned.
        """
        try:
            fd = os.open(domain_dir, os.O_RDONLY | os.O_DIRECTORY)
        except FileNotFoundError:
            return []
        q = Query2()
        buf = (Result * MAX_LIMIT)()
        q.version, q.nterms, q.limit, q.results = 2, len(terms), MAX_LIMIT, ctypes.addressof(buf)
        for i, (name, code, typ, payload) in enumerate(terms):
            t = q.terms[i]
            t.name, t.op, t.type, t.len = name.encode(), code, typ, len(payload)
            ctypes.memmove(t.value, payload, len(payload))
        paths = []
        try:
            while True:
                try:
                    fcntl.ioctl(fd, IOC_QUERY2, q)
                except OSError as err:
                    if err.errno in RESOURCE_ERRNO:
                        raise MoobQueryError(err.errno, "QUERY2 resource refusal") from err
                    if err.errno in ABSENT_ERRNO and not paths:
                        raise ModuleAbsent(err.errno, "QUERY2 is not offered here") from err
                    raise
                self.stats["query2_calls"] += 1
                page = []
                for r in buf[:q.count]:
                    if r.flags & RES_PATH_TRUNCATED:
                        self.stats["truncated_paths"] += 1
                    elif not r.flags & RES_PATH_UNKNOWN:
                        page.append(r.path.decode())
                paths.extend(page)
                if not q.truncated:
                    break
            return paths
        finally:
            os.close(fd)

    def _readable(self, path):
        """True when this process can open the file. Used by the no-module walk only.
        It is not a filter on a path QUERY2 already returned."""
        try:
            fd = os.open(path, os.O_RDONLY)
        except (PermissionError, FileNotFoundError):
            return False
        os.close(fd)
        return True

    def _scan_domain(self, domain_dir, below, op):
        items = []
        if not os.path.isdir(domain_dir):
            return items
        self.stats["fallback_domains"] += 1
        for dirpath, dirnames, filenames in os.walk(domain_dir):
            dirnames[:] = [name for name in dirnames if not name.startswith(".")]
            for name in filenames:
                if not name.endswith(".json") or name.startswith(".tmp."):
                    continue
                path = os.path.join(dirpath, name)
                rel = os.path.relpath(path, domain_dir)
                if below and not (rel == below or rel.startswith(below + "/")):
                    continue
                if not self._readable(path):
                    continue
                item = self._load(path)
                if item is None:
                    continue
                if op.filter and not self._matches(item, op.filter):
                    self.stats["post_filtered_out"] += 1
                    continue
                items.append(item)
        return items

    def _take(self, domain_dir, terms, below, op):
        if not os.path.isdir(domain_dir):
            return []
        if layer_mounted(domain_dir):
            try:
                rels = self._query2(domain_dir, terms)
            except ModuleAbsent:
                return self._scan_domain(domain_dir, below, op)
        else:
            return self._scan_domain(domain_dir, below, op)
        items = []
        for rel in rels:
            self.stats["candidates"] += 1
            if not rel.endswith(".json") or "/.tmp." in "/" + rel:
                continue
            if below and not (rel == below or rel.startswith(below + "/")):
                continue
            try:
                item = self._load(os.path.join(domain_dir, rel))
            except PermissionError:
                continue
            if item is None:
                continue
            if op.filter and not self._matches(item, op.filter):
                self.stats["post_filtered_out"] += 1
                continue
            items.append(item)
        return items

    def _domains(self, prefix):
        """Domain directories at or below a namespace prefix shorter than the domain depth."""
        dirs = [self._dir(prefix)]
        for _ in range(self.depth - len(prefix)):
            nxt = []
            for d in dirs:
                try:
                    nxt += [e.path for e in os.scandir(d) if e.is_dir(follow_symlinks=False)]
                except FileNotFoundError:
                    pass
            dirs = nxt
        return dirs

    def _search(self, op):
        prefix = tuple(op.namespace_prefix)
        terms = self._terms(op.filter)
        if len(prefix) >= self.depth:
            domains = [self._dir(prefix[:self.depth])]
            below = "/".join(_enc(x) for x in prefix[self.depth:])
        else:
            domains, below = self._domains(prefix), ""
        items = []
        if len(prefix) < self.depth:
            level = [self._dir(prefix)]
            for _ in range(self.depth - len(prefix)):
                nxt = []
                for d in level:
                    try:
                        entries = list(os.scandir(d))
                    except FileNotFoundError:
                        continue
                    for e in entries:
                        if e.is_dir(follow_symlinks=False):
                            nxt.append(e.path)
                        elif e.name.endswith(".json") and not e.name.startswith(".tmp."):
                            self.stats["files_above_domain"] += 1
                            if not self._readable(e.path):
                                continue
                            try:
                                item = self._load(e.path)
                            except PermissionError:
                                continue
                            if item and (not op.filter or self._matches(item, op.filter)):
                                items.append(item)
                level = nxt
        for d in domains:
            self.stats["domains_visited"] += 1
            items.extend(self._take(d, terms, below, op))
        items.sort(key=lambda it: (it.updated_at, it.namespace, it.key), reverse=True)
        out = items[op.offset:op.offset + op.limit]
        self.stats["returned"] += len(out)
        return [SearchItem(namespace=i.namespace, key=i.key, value=i.value,
                           created_at=i.created_at, updated_at=i.updated_at) for i in out]

    def _list_namespaces(self, op):
        found = set()
        for dirpath, _dirs, files in os.walk(self.root):
            if any(f.endswith(".json") and not f.startswith(".tmp.") for f in files):
                found.add(tuple(unquote(x) for x in os.path.relpath(dirpath, self.root).split(os.sep)))

        def match(ns, cond):
            path = tuple(cond.path)
            if len(ns) < len(path):
                return False
            part = ns[:len(path)] if cond.match_type == "prefix" else ns[len(ns) - len(path):]
            return all(p == "*" or p == n for p, n in zip(path, part))
        out = [ns for ns in found if all(match(ns, c) for c in (op.match_conditions or ()))]
        if op.max_depth is not None:
            out = list({ns[:op.max_depth] for ns in out})
        return sorted(out)[op.offset:op.offset + op.limit]

    # ---- BaseStore -------------------------------------------------------------------------
    def batch(self, ops):
        results = []
        for op in ops:
            if isinstance(op, GetOp):
                results.append(self._load(self._file(op.namespace, op.key), op.namespace, op.key))
            elif isinstance(op, PutOp):
                results.append(self._put(op))
            elif isinstance(op, SearchOp):
                if op.query:
                    raise NotImplementedError("text search stays in the client (contract section 8)")
                results.append(self._search(op))
            elif isinstance(op, ListNamespacesOp):
                results.append(self._list_namespaces(op))
            else:
                raise ValueError(f"unknown operation {op!r}")
        return results

    async def abatch(self, ops):
        return self.batch(ops)


def layer_mounted(path):
    """True when path is inside a mounted moobfs_layer (statfs type 0x4d4f4f42).

    On other platforms the module is absent, so this is false and search uses the
    open-what-you-can-read walk. Detection does not open the mount root.
    """
    if sys.platform != "linux":
        return False
    libc = ctypes.CDLL(None, use_errno=True)
    buf = ctypes.create_string_buffer(256)
    if libc.statfs(os.fsencode(path), buf) != 0:
        raise OSError(ctypes.get_errno(), os.strerror(ctypes.get_errno()), path)
    return struct.unpack_from("l", buf.raw)[0] == 0x4D4F4F42


if __name__ == "__main__":
    print("sizeof(struct moob_query2) =", ctypes.sizeof(Query2), "ioctl =", hex(IOC_QUERY2))
    if len(sys.argv) > 1:
        print("layer mounted at", sys.argv[1], ":", layer_mounted(sys.argv[1]))
