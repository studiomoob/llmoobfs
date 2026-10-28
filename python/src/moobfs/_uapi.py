# SPDX-License-Identifier: LGPL-2.1-or-later
"""The structures and constants of include/moobfs_ioctl.h and include/moobfs_attr.h.

Mirrored by hand with ctypes; tests/test_uapi.py checks the sizes against the header's
layout. The UAPI is frozen (ADR-0022), so these change only with a new UAPI version.
"""
import ctypes

PREFIX = "user.moob."
NAME_MAX, STRING_MAX, RAW_MAX = 64, 1024, 4096
VALUE_MAX, PATH_MAX, MAX_TERMS, MAX_LIMIT, CURSOR, BATCH_MAX = 1024, 256, 4, 4096, 128, 8
RESERVED_NAMES = frozenset({"format"})
FS_MAGIC = 0x4D4F4F42

T_STRING, T_INT64, T_DOUBLE, T_BOOL, T_TIME, T_RAW = 1, 2, 3, 4, 5, 6
OP_EQ, OP_LT, OP_LE, OP_GT, OP_GE, OP_PREFIX, OP_EXISTS = 1, 2, 3, 4, 5, 6, 7
TERM_MISSING_OK = 0x1
RES_PATH_UNKNOWN, RES_PATH_TRUNCATED = 0x1, 0x2
ORDER_ASC, ORDER_DESC = 1, 2
QUERY2_VERSION = 2

CAP_REFLINK, CAP_DATA_CHECKSUMS, CAP_SNAPSHOTS, CAP_XATTR = 0x1, 0x2, 0x4, 0x8
FLAG_NOLINKS, FLAG_INDEX_OFF, FLAG_PATHS_STALE, FLAG_INDEX_INCOMPLETE = 0x1, 0x2, 0x4, 0x8
FLAG_DOMAINS, FLAG_COUNTERS, FLAG_ON_DEMAND = 0x10, 0x20, 0x40


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


class Query2Batch(ctypes.Structure):
    _fields_ = [("version", ctypes.c_uint32), ("nqueries", ctypes.c_uint32),
                ("queries", ctypes.c_uint64), ("reserved", ctypes.c_uint64 * 2)]


class Info(ctypes.Structure):
    _fields_ = [("uapi_version", ctypes.c_uint32), ("format_version", ctypes.c_uint32),
                ("attr_name_max", ctypes.c_uint32), ("attr_string_max", ctypes.c_uint32),
                ("attr_raw_max", ctypes.c_uint32), ("attr_max_per_file", ctypes.c_uint32),
                ("caps", ctypes.c_uint32), ("flags", ctypes.c_uint32),
                ("index_budget", ctypes.c_uint64), ("index_used", ctypes.c_uint64),
                ("index_entries", ctypes.c_uint32), ("index_inodes", ctypes.c_uint32),
                ("lower_fs", ctypes.c_char * 16), ("domain_depth", ctypes.c_uint32),
                ("heterogeneous", ctypes.c_uint32), ("fallback_domains", ctypes.c_uint32),
                ("domains_built", ctypes.c_uint32), ("domain_builds", ctypes.c_uint32),
                ("domain_releases", ctypes.c_uint32), ("reserved", ctypes.c_uint64 * 1)]


def _ioc(direction, nr, size):
    return (direction << 30) | (size << 16) | (ord("M") << 8) | nr


_READ, _READ_WRITE = 2, 3
IOC_GET_INFO = _ioc(_READ, 2, ctypes.sizeof(Info))
IOC_QUERY2 = _ioc(_READ_WRITE, 3, ctypes.sizeof(Query2))
IOC_QUERY2_BATCH = _ioc(_READ_WRITE, 4, ctypes.sizeof(Query2Batch))
