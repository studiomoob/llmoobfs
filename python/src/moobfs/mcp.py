# SPDX-License-Identifier: LGPL-2.1-or-later
"""An MCP server for one agent's memories (roadmap C7, ADR-0024, journal 0208).

    python -m moobfs.mcp --root /srv/memory [--read-only]

The agent's host starts it as a subprocess and talks MCP over its stdin and stdout. It runs
with the agent's own credentials and no others: it refuses to start as root or setuid, offers
no network transport and cannot switch users, so every permission decision is the kernel's,
made for the agent. Every path is a domain under --root and a path relative to it; absolute
paths, "..", NUL and symbolic links are refused, so the tools stay inside the root. That is a
scope guard, not a boundary against the agent itself, which can open with its own uid whatever
the kernel lets it.

Tools: info, query, read, write, delete (the last two absent with --read-only). Needs the
optional dependency: pip install "./python[mcp]".
"""
import argparse
import base64
import contextlib
import errno
import os
import stat
import struct
import sys
from datetime import datetime
from typing import Any

from . import _uapi as u
from .attrs import Value, get_attrs
from .domain import Domain, layer_mounted
from .publish import publish
from .query import ORDER_TYPES, Order, QueryRefused, Term

TYPES = {"string": u.T_STRING, "int64": u.T_INT64, "double": u.T_DOUBLE, "bool": u.T_BOOL,
         "time": u.T_TIME}
READ_MAX = 1 << 20


class ToolError(Exception):
    """A refusal the agent sees as the tool's error, with no partial result."""


def _effective_capabilities():
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("CapEff:"):
                    return int(line.split()[1], 16)
    except OSError:
        pass
    return 0


def check_identity():
    """Refuse to be a deputy: not root, not setuid or setgid, no capability (ADR-0024
    decision 3). A capability such as CAP_DAC_READ_SEARCH would let the kernel's checks pass for
    files the agent's uid cannot read, and a saved uid of 0 lets a process become root again."""
    ruid, euid, suid = os.getresuid()
    rgid, egid, sgid = os.getresgid()
    if 0 in (ruid, euid, suid):
        raise SystemExit("moobfs.mcp: refusing to run as root; start it as the agent's user")
    if len({ruid, euid, suid}) > 1 or len({rgid, egid, sgid}) > 1:
        raise SystemExit("moobfs.mcp: refusing to run setuid or setgid")
    if _effective_capabilities():
        raise SystemExit("moobfs.mcp: refusing to run with capabilities; start it as the agent's "
                         "user without any")


# ---- JSON values -------------------------------------------------------------------------
def to_value(value, type_=None):
    """The Value of a JSON value; an explicit type wins (ADR-0024 decision 5)."""
    if type_ is None:
        if isinstance(value, bool):
            return Value(u.T_BOOL, bytes([value]))
        if isinstance(value, int):
            return Value.of(value)
        if isinstance(value, float):
            return Value.of(value)
        if isinstance(value, str):
            return Value.of(value)
        raise ToolError(f"no attribute type for {type(value).__name__}; give a type")
    if type_ not in TYPES:
        raise ToolError(f"type is one of {', '.join(TYPES)}")
    try:
        if type_ == "string":
            if not isinstance(value, str):
                raise TypeError
            return Value.of(value)
        if type_ == "int64":
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError
            return Value(u.T_INT64, struct.pack("<q", value))
        if type_ == "double":
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise TypeError
            return Value(u.T_DOUBLE, struct.pack("<d", float(value)))
        if type_ == "bool":
            if not isinstance(value, bool):
                raise TypeError
            return Value(u.T_BOOL, bytes([value]))
        if isinstance(value, bool):
            raise TypeError
        if isinstance(value, int):
            return Value.time_ns(value)
        if isinstance(value, str):
            t = datetime.fromisoformat(value.replace("Z", "+00:00"))
            if t.tzinfo is None:
                raise ToolError("a time needs a time zone, for instance 2026-10-27T12:00:00Z")
            return Value.of(t)
        raise TypeError
    except (TypeError, struct.error, OverflowError):
        raise ToolError(f"{value!r} is not a valid {type_}") from None
    except ValueError as err:
        raise ToolError(str(err)) from None


def from_value(v):
    """{type, value} of a stored Value, as JSON."""
    name = {code: n for n, code in TYPES.items()}.get(v.type, "raw")
    py = v.python()
    if v.type == u.T_TIME:
        py = py.isoformat()
    elif v.type == u.T_RAW:
        py = base64.b64encode(py).decode()
    return {"type": name, "value": py}


# ---- paths -------------------------------------------------------------------------------
_DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
_FILE_FLAGS = os.O_NOFOLLOW | os.O_CLOEXEC


def _file_error(domain, path, err):
    if err.errno == errno.ELOOP:
        return ToolError(f"{domain}/{path}: symbolic links are refused")
    if err.errno in (errno.EACCES, errno.EPERM):
        return ToolError(f"{domain}/{path}: permission denied")
    return ToolError(f"{domain}/{path}: {os.strerror(err.errno)}")


class Memory:
    def __init__(self, root, read_only=False):
        self.root = os.path.realpath(root)
        if not os.path.isdir(self.root):
            raise SystemExit(f"moobfs.mcp: {root} is not a directory")
        self.read_only = read_only

    def domain_dir(self, domain):
        if (not isinstance(domain, str) or not domain or "/" in domain or "\0" in domain
                or domain in (".", "..")):
            raise ToolError(f"invalid domain {domain!r}: one directory name under the root")
        path = os.path.join(self.root, domain)
        try:
            st = os.lstat(path)
        except FileNotFoundError:
            raise ToolError(f"no domain {domain!r}") from None
        if not stat.S_ISDIR(st.st_mode):
            raise ToolError(f"{domain!r} is not a directory")
        return path

    def _parts(self, domain, path):
        if not isinstance(path, str) or not path or "\0" in path or path.startswith("/"):
            raise ToolError(f"invalid path {path!r}: relative to the domain")
        parts = [p for p in path.split("/") if p not in ("", ".")]
        if not parts or ".." in parts:
            raise ToolError(f"invalid path {path!r}: no '..'")
        return parts

    def _open_dir(self, name, dir_fd, what):
        """A directory under dir_fd, never through a symbolic link."""
        try:
            return os.open(name, _DIR_FLAGS, dir_fd=dir_fd)
        except FileNotFoundError:
            raise ToolError(f"{what}: no such directory") from None
        except PermissionError:
            raise ToolError(f"{what}: permission denied") from None
        except OSError as err:
            if err.errno == errno.ELOOP:
                raise ToolError(f"{what}: symbolic links are refused") from None
            if err.errno == errno.ENOTDIR:
                raise ToolError(f"{what}: not a directory") from None
            raise ToolError(f"{what}: {os.strerror(err.errno)}") from None

    def _walk(self, stack, domain, path, create=False):
        """Descriptors of the domain and of the directory that holds path, and its last name.
        Each component is opened relative to the one before with O_NOFOLLOW, so a component
        that someone sharing the domain swaps for a symbolic link is refused, not followed.
        With create, missing directories are made with the domain's mode, setgid included."""
        self.domain_dir(domain)
        parts = self._parts(domain, path)
        root = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
        stack.callback(os.close, root)
        dom = self._open_dir(domain, root, repr(domain))
        stack.callback(os.close, dom)
        mode = stat.S_IMODE(os.fstat(dom).st_mode)
        cur = dom
        for part in parts[:-1]:
            if create:
                try:
                    os.mkdir(part, 0o700, dir_fd=cur)
                    made = True
                except FileExistsError:
                    made = False
                except PermissionError:
                    raise ToolError(f"{domain}/{path}: permission denied") from None
            nxt = self._open_dir(part, cur, f"{domain}/{path}")
            stack.callback(os.close, nxt)
            if create and made:
                os.fchmod(nxt, mode)
            cur = nxt
        return dom, cur, parts

    # ---- tools ---------------------------------------------------------------------------
    def info(self):
        domains = []
        try:
            entries = sorted(os.scandir(self.root), key=lambda e: e.name)
        except PermissionError:
            entries = []
        for e in entries:
            if e.is_dir(follow_symlinks=False) and os.access(e.path, os.R_OK | os.X_OK):
                domains.append(e.name)
        return {"root": self.root, "layer_mounted": layer_mounted(self.root), "uid": os.getuid(),
                "gid": os.getgid(), "read_only": self.read_only, "domains": domains}

    def query(self, domain, terms=None, order=None, limit=100):
        base = self.domain_dir(domain)
        built = []
        for t in terms or []:
            if not isinstance(t, dict) or "name" not in t or "op" not in t:
                raise ToolError("a term is {name, op, value, type?, missing_ok?}")
            try:
                value = None if t["op"] == "exists" else to_value(t.get("value"), t.get("type"))
                built.append(Term(t["name"], t["op"], value, missing_ok=bool(t.get("missing_ok"))))
            except ValueError as err:
                raise ToolError(f"term {t}: {err}") from None
        ordering = None
        if order:
            if not isinstance(order, dict) or order.get("type") not in ORDER_TYPES:
                raise ToolError(f"order is {{name, type: {'|'.join(ORDER_TYPES)}, descending?}}")
            try:
                ordering = Order(order.get("name", ""), order["type"], bool(order.get("descending")))
            except ValueError as err:
                raise ToolError(str(err)) from None
        if not isinstance(limit, int) or limit < 1:
            raise ToolError("limit is a positive integer")
        d = Domain(base)
        try:
            found = d.query(*built, order=ordering)
        except ValueError as err:
            raise ToolError(str(err)) from None
        except QueryRefused as err:
            raise ToolError(f"query refused: {errno.errorcode.get(err.errno, err.errno)} "
                            "(a resource limit; nothing returned)") from None
        except PermissionError:
            raise ToolError(f"{domain}: permission denied") from None
        return {"backend": d.last_backend, "count": len(found), "truncated": len(found) > limit,
                "matches": [{"path": m.path, "ino": m.ino} for m in found[:limit]]}

    def read(self, domain, path, max_bytes=65536):
        if not isinstance(max_bytes, int) or not 0 < max_bytes <= READ_MAX:
            raise ToolError(f"max_bytes is 1 to {READ_MAX}")
        with contextlib.ExitStack() as stack:
            _, parent, parts = self._walk(stack, domain, path)
            try:
                # O_NONBLOCK: a FIFO someone else put there must not hold the server forever
                fd = os.open(parts[-1], _FILE_FLAGS | os.O_RDONLY | os.O_NONBLOCK, dir_fd=parent)
            except FileNotFoundError:
                raise ToolError(f"{domain}/{path}: no such file") from None
            except OSError as err:
                raise _file_error(domain, path, err) from None
            stack.callback(os.close, fd)
            st = os.fstat(fd)
            if not stat.S_ISREG(st.st_mode):
                raise ToolError(f"{domain}/{path}: not a regular file")
            data = os.read(fd, max_bytes + 1)
            attrs = get_attrs(fd, indexed_only=False)
        return {"path": path, "size": st.st_size, "truncated": len(data) > max_bytes,
                "content": data[:max_bytes].decode("utf-8", "replace"),
                "attributes": {k: from_value(v) for k, v in sorted(attrs.items())}}

    def write(self, domain, path, content, attributes=None, replace=False):
        if self.read_only:
            raise ToolError("read-only server")
        self._parts(domain, path)
        if not isinstance(content, str):
            raise ToolError("content is a string")
        attrs = {}
        for name, spec in (attributes or {}).items():
            if isinstance(spec, dict) and "value" in spec:
                attrs[name] = to_value(spec["value"], spec.get("type"))
            else:
                attrs[name] = to_value(spec)
        with contextlib.ExitStack() as stack:
            dom, parent, parts = self._walk(stack, domain, path, create=True)
            where = os.path.join(self.root, domain, *parts[:-1])
            try:
                publish(where, parts[-1], content.encode(), attrs, domain=dom,
                        replace=bool(replace), dir_fd=parent)
            except FileExistsError:
                raise ToolError(f"{domain}/{path} exists; pass replace to replace it") from None
            except ValueError as err:
                raise ToolError(str(err)) from None
            except OSError as err:
                raise _file_error(domain, path, err) from None
        return {"path": path, "replaced": bool(replace)}

    def delete(self, domain, path):
        if self.read_only:
            raise ToolError("read-only server")
        with contextlib.ExitStack() as stack:
            _, parent, parts = self._walk(stack, domain, path)
            try:
                st = os.stat(parts[-1], dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                raise ToolError(f"{domain}/{path}: no such file") from None
            if not stat.S_ISREG(st.st_mode):
                raise ToolError(f"{domain}/{path}: not a regular file")
            try:
                os.unlink(parts[-1], dir_fd=parent)
            except OSError as err:
                raise _file_error(domain, path, err) from None
        return {"path": path, "deleted": True}


def build_server(memory):
    """The MCPServer with the tools of ADR-0024 bound to memory."""
    from mcp.server import MCPServer
    from mcp.server.mcpserver.exceptions import ToolError as SDKToolError

    server = MCPServer("moobfs", instructions=(
        "Memories are files under one root, grouped in domain directories. query finds files by "
        "typed attributes; read returns content and attributes; write publishes a memory. Every "
        "access uses this process's own user, so the kernel decides what it may see."))

    def guard(fn, *args):
        try:
            return fn(*args)
        except ToolError as err:
            raise SDKToolError(str(err)) from None     # the agent sees it as the tool's error

    @server.tool()
    def info() -> dict[str, Any]:
        """The root, whether the MoobFS layer is mounted there, this server's uid and gid, and
        the domains this user can enter."""
        return guard(memory.info)

    @server.tool()
    def query(domain: str, terms: list[dict[str, Any]] | None = None,
              order: dict[str, Any] | None = None, limit: int = 100) -> dict[str, Any]:
        """Files of one domain whose typed attributes match every term. A term is
        {"name": ..., "op": "==|<|<=|>|>=|prefix|exists", "value": ..., "type":
        "string|int64|double|bool|time" (optional), "missing_ok": bool (optional)}; at most
        four. order is {"name", "type": "string|int64|double|time", "descending"}. Without
        order the order is unspecified."""
        return guard(memory.query, domain, terms, order, limit)

    @server.tool()
    def read(domain: str, path: str, max_bytes: int = 65536) -> dict[str, Any]:
        """Content (UTF-8) and typed attributes of one file, path relative to the domain."""
        return guard(memory.read, domain, path, max_bytes)

    if not memory.read_only:
        @server.tool()
        def write(domain: str, path: str, content: str,
                  attributes: dict[str, Any] | None = None, replace: bool = False) -> dict[str, Any]:
            """Publish one memory: content and typed attributes ({name: value} or
            {name: {"value", "type"}}) written in one sequence; an existing path is refused
            unless replace is true."""
            return guard(memory.write, domain, path, content, attributes, replace)

        @server.tool()
        def delete(domain: str, path: str) -> dict[str, Any]:
            """Remove one file, path relative to the domain."""
            return guard(memory.delete, domain, path)

    return server


def main(argv=None):
    ap = argparse.ArgumentParser(prog="python -m moobfs.mcp",
                                 description="MCP server for one agent's MoobFS memories (stdio)")
    ap.add_argument("--root", required=True, help="the memory root; domains are its directories")
    ap.add_argument("--read-only", action="store_true", help="leave out write and delete")
    args = ap.parse_args(argv)
    check_identity()
    build_server(Memory(args.root, args.read_only)).run("stdio")


if __name__ == "__main__":
    main(sys.argv[1:])
