# SPDX-License-Identifier: LGPL-2.1-or-later
"""Capability detection and the Domain object (client contract section 7).

Domain(path).query(...) asks the kernel when the directory is on a mounted layer that serves
QUERY2, and otherwise walks the directory with the kernel's rules (moobfs.scan). Both give the
same answers; the kernel is the fast path, the scan the fallback.
"""
import ctypes
import fcntl
import os
import sys

from . import _uapi as u
from ._libc import libc
from . import query as q
from . import scan

BACKENDS = ("auto", "kernel", "scan")


def _statfs_f_type(raw, machine=None, byteorder=sys.byteorder):
    """f_type from the bytes of a struct statfs that statfs(2) filled on machine (default: this
    one). glibc starts the struct with f_type, a __fsword_t (a long) on every Linux ABI but
    s390x, where f_type and f_bsize are each an unsigned int (bits/statfs.h); reading a long
    there gave f_type << 32 | f_bsize (journal 0280)."""
    if machine is None:
        machine = os.uname().machine
    if machine == "s390x":
        return int.from_bytes(raw[:4], byteorder, signed=False)
    return int.from_bytes(raw[:ctypes.sizeof(ctypes.c_long)], byteorder, signed=True)


def layer_mounted(path):
    """True when path is on a mounted moobfs_layer (statfs type 0x4d4f4f42). False on systems
    other than Linux. Does not open the mount root."""
    if not sys.platform.startswith("linux"):
        return False
    buf = ctypes.create_string_buffer(256)   # struct statfs; f_type is its first word
    if libc().statfs(os.fsencode(path), buf) != 0:
        e = ctypes.get_errno()
        raise OSError(e, os.strerror(e), path)
    return _statfs_f_type(buf.raw) == u.FS_MAGIC


def get_info(mount_root):
    """MOOB_IOC_GET_INFO on the mount root, as a dict. The index counters are zero unless the
    caller has CAP_SYS_ADMIN (flags has FLAG_COUNTERS then)."""
    info = u.Info()
    fd = os.open(mount_root, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.ioctl(fd, u.IOC_GET_INFO, info)
    finally:
        os.close(fd)
    out = {name: getattr(info, name) for name, _ in u.Info._fields_ if name != "reserved"}
    out["lower_fs"] = info.lower_fs.decode(errors="replace")
    return out


def usable_info(info):
    """Contract section 7 step 2: domains on, index not off, not marked incomplete (older
    modules set that flag)."""
    f = info["flags"]
    return bool(f & u.FLAG_DOMAINS) and not f & (u.FLAG_INDEX_OFF | u.FLAG_INDEX_INCOMPLETE)


class Domain:
    """A domain directory. backend is "auto" (kernel when served, else scan), "kernel" (never
    scan) or "scan" (never ask the kernel). With on_refusal="scan", a QueryRefused from the
    kernel is answered by the scan instead of raised. last_backend says which one answered;
    stats counts the QUERY2 and QUERY2_BATCH calls ("query2_calls") and the walks ("scans")."""

    def __init__(self, path, backend="auto", on_refusal="raise"):
        if backend not in BACKENDS:
            raise ValueError(f"backend is one of {', '.join(BACKENDS)}")
        if on_refusal not in ("raise", "scan"):
            raise ValueError('on_refusal is "raise" or "scan"')
        self.path = os.path.abspath(path)
        self.backend, self.on_refusal = backend, on_refusal
        self.last_backend = None
        self.stats = {"query2_calls": 0, "scans": 0}
        self.mounted = layer_mounted(self.path) if backend != "scan" else False

    def _kernel(self, call):
        fd = os.open(self.path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            return call(fd)
        finally:
            os.close(fd)

    def _scan(self, scan_call):
        self.last_backend = "scan"
        self.stats["scans"] += 1
        return scan_call()

    def _answer(self, kernel_call, scan_call):
        if self.backend == "scan" or (self.backend == "auto" and not self.mounted):
            return self._scan(scan_call)
        try:
            out = self._kernel(kernel_call)
        except q.KernelUnavailable:
            if self.backend == "kernel":
                raise
            return self._scan(scan_call)
        except q.QueryRefused:
            if self.on_refusal != "scan":
                raise
            return self._scan(scan_call)
        self.last_backend = "kernel"
        return out

    def query(self, *terms, order=None):
        """Every answer of the terms (all must hold), as a list of Match. Without order the
        order is unspecified."""
        return self._answer(lambda fd: q.kernel_query(fd, terms, order, stats=self.stats),
                            lambda: scan.scan_query(self.path, terms, order))

    def batch(self, requests):
        """Answers of up to 8 (terms, order) requests, one list each: "in a set" is one request
        per value, and the union is the caller's (contract section 6)."""
        requests = [(tuple(t), o) for t, o in requests]
        return self._answer(lambda fd: q.kernel_batch(fd, requests, stats=self.stats),
                            lambda: scan.scan_batch(self.path, requests))
