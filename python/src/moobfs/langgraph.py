# SPDX-License-Identifier: LGPL-2.1-or-later
"""A LangGraph store on the library (roadmap C5, phase 2; journal 0207).

MoobStore implements langgraph.store.base.BaseStore on a directory, with the files of the G1
adapter (bench/client/langgraph_store.py, journal 0089): one JSON file per item,
<namespace...>/<key>.json holding {value, created_at, updated_at}; user.moob.v.<key> for each
value field that can be an attribute (every number a double, so 3 and 3.0 are one value, as
for LangGraph's filters); user.moob.lg.updated (time). A store written by either reads with
the other.

Differences from that adapter: publishing and queries go through moobfs.publish and
moobfs.Domain, so without the module a search answers what QUERY2 would answer (ADR-0023),
not every file the caller can open; and the owner or root gives a new file the domain's group
even when the domain grants its group nothing.

What the kernel does not answer stays here, as in the adapter: $ne, lists, nested objects,
names that cannot be attributes, offsets, disjunctions, counts, full text (client contract
section 8). Results are checked again against the whole filter after the read.

Needs the optional dependency: pip install "./python[langgraph]".
"""
import json
import os
import stat
import struct
from datetime import datetime, timezone
from urllib.parse import quote, unquote

from langgraph.store.base import (BaseStore, GetOp, InvalidNamespaceError, Item,
                                  ListNamespacesOp, PutOp, SearchItem, SearchOp)

from . import _uapi as u
from .attrs import Value, name_ok
from .domain import Domain, layer_mounted
from .publish import file_mode_for, publish
from .query import RESOURCE_ERRNO, QueryRefused, Term

T_STRING = u.T_STRING
MoobQueryError = QueryRefused          # the adapter's name for a resource refusal

__all__ = ["MoobStore", "MoobQueryError", "PublicationStopped", "RESOURCE_ERRNO", "T_STRING",
           "file_mode_for", "layer_mounted"]


class PublicationStopped(Exception):
    """A test stopped the publication sequence. The final name was not left mixed."""

    def __init__(self, step):
        super().__init__(step)
        self.step = step


def _enc(label):
    return quote(label, safe="")


def _check_labels(labels):
    """LangGraph's rule for namespace labels: strings, not empty, without a period. BaseStore
    applies it in get, put, search and delete, not to the operations given to batch(), and
    here a label is a directory name, so "." or ".." would leave the root."""
    for label in labels:
        if not isinstance(label, str) or not label or "." in label:
            raise InvalidNamespaceError(f"invalid namespace label {label!r} in {tuple(labels)}")


def _attr_name(key):
    """The attribute name of a value key, or None when the key cannot be an attribute name."""
    name = "v." + key
    return name if key and name_ok(name) else None


def _typed(value):
    """The Value of a field that can be indexed, else None. Every number is a double."""
    if isinstance(value, bool):
        return Value(u.T_BOOL, bytes([value]))
    if isinstance(value, (int, float)):
        f = float(value)
        if f != f or f in (float("inf"), float("-inf")) or (isinstance(value, int) and int(f) != value):
            return None
        return Value(u.T_DOUBLE, struct.pack("<d", f))
    if isinstance(value, str):
        b = value.encode()
        if len(b) > u.STRING_MAX or b"\0" in b:
            return None
        return Value(u.T_STRING, b)
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
    if op in ("$gt", "$gte", "$lt", "$lte"):
        a, b = float(value), float(op_value)
        return {"$gt": a > b, "$gte": a >= b, "$lt": a < b, "$lte": a <= b}[op]
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

    # ---- paths ---------------------------------------------------------------------------
    def _dir(self, namespace):
        return os.path.join(self.root, *[_enc(x) for x in namespace])

    def _file(self, namespace, key):
        return os.path.join(self._dir(namespace), _enc(key) + ".json")

    def _prepare_dirs(self, namespace):
        """Create missing namespace directories, 0700 at the domain and not more private than
        the domain below it. Returns the domain directory (the namespace's own directory when
        it is above the domain depth)."""
        cur, domain_dir, domain_mode = self.root, None, 0o700
        for index, label in enumerate(namespace, start=1):
            cur = os.path.join(cur, _enc(label))
            if not os.path.isdir(cur):
                os.mkdir(cur, 0o700)
                if index > self.depth:
                    os.chmod(cur, domain_mode)
            if index == self.depth:
                domain_dir = cur
                domain_mode = stat.S_IMODE(os.stat(cur).st_mode)
        return domain_dir or cur

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

    def _checkpoint(self, step):
        if self.stop_after == step:
            raise PublicationStopped(step)

    def _put(self, op):
        path = self._file(op.namespace, op.key)
        if op.value is None:
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass
            return
        domain_dir = self._prepare_dirs(op.namespace)
        now = datetime.now(timezone.utc)
        old = self._load(path, op.namespace, op.key)
        doc = {"value": op.value, "created_at": (old.created_at if old else now).isoformat(),
               "updated_at": now.isoformat()}
        attrs = {"lg.updated": Value.time_ns(int(now.timestamp() * 1e9))}
        for k, v in op.value.items():
            name, typed = _attr_name(k), _typed(v)
            if name and typed:
                attrs[name] = typed
        publish(os.path.dirname(path), os.path.basename(path), json.dumps(doc).encode(), attrs,
                domain=domain_dir, replace=True, _step=self._checkpoint)

    # ---- search ----------------------------------------------------------------------------
    def _terms(self, flt):
        """Up to four Terms implied by the filter; the rest is checked after the read."""
        terms, left = [], 0
        for key, cond in (flt or {}).items():
            name = _attr_name(key)
            conds = cond.items() if isinstance(cond, dict) and any(k.startswith("$") for k in cond) \
                else [("$eq", cond)]
            for op, v in conds:
                sym = {"$eq": "==", "$gt": ">", "$gte": ">=", "$lt": "<", "$lte": "<="}.get(op)
                typed = _typed(v)
                usable = (name and sym and typed is not None and len(terms) < u.MAX_TERMS
                          and (sym == "==" or typed.type == u.T_DOUBLE))
                if usable:
                    terms.append(Term(name, sym, typed))
                else:
                    left += 1
        self.stats["terms_indexed"] += len(terms)
        self.stats["terms_not_indexed"] += left
        return terms or [Term("lg.updated", "exists")]

    def _matches(self, item, flt):
        return all(_compare(item.value.get(k), v) for k, v in flt.items())

    def _take(self, domain_dir, terms, below, op):
        if not os.path.isdir(domain_dir):
            return []
        d = Domain(domain_dir)
        matches = d.query(*terms)
        self.stats["query2_calls"] += d.stats["query2_calls"]
        self.stats["fallback_domains"] += d.stats["scans"]
        items = []
        for m in matches:
            self.stats["candidates"] += 1
            if m.path is None:
                self.stats["truncated_paths"] += 1
                continue
            rel = m.path
            if not rel.endswith(".json") or "/.tmp." in "/" + rel:
                continue
            if below and not (rel == below or rel.startswith(below + "/")):
                continue
            try:
                item = self._load(os.path.join(domain_dir, rel))
            except (PermissionError, IsADirectoryError):
                continue
            if item is None:
                continue
            if op.filter and not self._matches(item, op.filter):
                self.stats["post_filtered_out"] += 1
                continue
            items.append(item)
        return items

    def _readable(self, path):
        try:
            fd = os.open(path, os.O_RDONLY)
        except (PermissionError, FileNotFoundError):
            return False
        os.close(fd)
        return True

    def _search(self, op):
        prefix = tuple(op.namespace_prefix)
        terms = self._terms(op.filter)
        items = []
        if len(prefix) >= self.depth:
            domains = [self._dir(prefix[:self.depth])]
            below = "/".join(_enc(x) for x in prefix[self.depth:])
        else:
            # files above the domain depth are no domain's; read the ones the caller can open
            level, below = [self._dir(prefix)], ""
            for _ in range(self.depth - len(prefix)):
                nxt = []
                for dpath in level:
                    try:
                        entries = list(os.scandir(dpath))
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
            domains = level
        for dpath in domains:
            self.stats["domains_visited"] += 1
            items.extend(self._take(dpath, terms, below, op))
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
        ops = list(ops)
        for op in ops:      # every operation is checked before any runs
            if isinstance(op, (GetOp, PutOp)):
                _check_labels(op.namespace)
            elif isinstance(op, SearchOp):
                _check_labels(op.namespace_prefix)
            elif isinstance(op, ListNamespacesOp):
                for cond in op.match_conditions or ():
                    _check_labels(cond.path)
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
