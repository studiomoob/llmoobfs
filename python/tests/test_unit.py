# SPDX-License-Identifier: LGPL-2.1-or-later
"""Tests that need neither Linux nor the module: encoding, validation, terms and the matching
and ordering rules of the scan. python3 -m unittest discover -s python/tests"""
import errno
import os
import struct
import sys
import unittest
from unittest import mock
from datetime import datetime, timedelta, timezone

if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, __file__.rsplit("/tests/", 1)[0] + "/src")

import moobfs  # noqa: E402
from moobfs import Order, Term, Value  # noqa: E402
from moobfs import _uapi as u  # noqa: E402
from moobfs.query import sort_key  # noqa: E402
from moobfs import scan  # noqa: E402
from moobfs.scan import run  # noqa: E402


class Encoding(unittest.TestCase):
    def test_types(self):
        self.assertEqual(Value.of("a").encode(), b"\x01a")
        self.assertEqual(Value.of(-2).encode(), b"\x02" + struct.pack("<q", -2))
        self.assertEqual(Value.of(1.5).encode(), b"\x03" + struct.pack("<d", 1.5))
        self.assertEqual(Value.of(True).encode(), b"\x04\x01")
        self.assertEqual(Value.of(b"x").encode(), b"\x06x")
        t = datetime(2026, 10, 26, 12, 0, 0, 123456, tzinfo=timezone.utc)
        v = Value.of(t)
        self.assertEqual(v.type, u.T_TIME)
        self.assertEqual(v.python(), t)
        self.assertEqual(Value.of(datetime(1969, 12, 31, 23, 59, 59, tzinfo=timezone.utc)).python(),
                         datetime(1969, 12, 31, 23, 59, 59, tzinfo=timezone.utc))
        with self.assertRaises(ValueError):
            Value.of(datetime(2026, 1, 1))
        with self.assertRaises(TypeError):
            Value.of([1])

    def test_validate_matches_common_attr_c(self):
        ok = [b"\x01", b"\x01" + "é".encode(), b"\x01" + b"a" * 1024, b"\x02" + bytes(8),
              b"\x03" + struct.pack("<d", float("inf")), b"\x04\x00", b"\x04\x01",
              b"\x05" + bytes(8), b"\x06" + bytes(4096)]
        bad = [b"", b"\x00", b"\x07", b"\x01" + b"a" * 1025, b"\x01a\x00", b"\x01\xc0\x80",
               b"\x01\xed\xa0\x80", b"\x01\xf4\x90\x80\x80", b"\x01\xff", b"\x02" + bytes(7),
               b"\x03" + struct.pack("<d", float("nan")), b"\x04\x02", b"\x04", b"\x06" + bytes(4097)]
        for raw in ok:
            self.assertIsNone(moobfs.validate(raw), raw)
        for raw in bad:
            self.assertIsNotNone(moobfs.validate(raw), raw)

    def test_names(self):
        for n in ("a", "kind", "v.kind", "a_b.c9", "x" * 64):
            self.assertTrue(moobfs.name_ok(n), n)
        for n in ("", ".a", "a.", "a..b", "A", "a-b", "x" * 65, "é"):
            self.assertFalse(moobfs.name_ok(n), n)
        with self.assertRaises(ValueError):
            Term("format", "==", 1)


class Terms(unittest.TestCase):
    def test_refused(self):
        for args in (("a", "~", 1), ("a", "prefix", 1), ("a", "==", b"raw"),
                     ("a", "==", float("nan")), ("a", "exists", 1)):
            with self.assertRaises(ValueError, msg=args):
                Term(*args)
        with self.assertRaises(ValueError):
            Term("a", "exists", missing_ok=True)
        with self.assertRaises(ValueError):
            Order("a", "bool")

    def test_term_checks_a_value_built_by_hand(self):
        # the payload is copied into a 1024-byte field of the request, whatever its type says
        for v in (Value(u.T_INT64, b"x" * 4000), Value(u.T_INT64, b"x" * 7),
                  Value(u.T_BOOL, b"\x02"), Value(u.T_TIME, b""), Value(u.T_STRING, b"a" * 1025),
                  Value(9, b"x")):
            with self.assertRaises(ValueError, msg=v.payload[:8]):
                Term("a", "==", v)
        self.assertEqual(Term("a", "==", Value(u.T_INT64, bytes(8))).value, Value.of(0))

    def test_fill_bounds_a_payload_replaced_after_the_term(self):
        from moobfs.query import _fill
        t = Term("a", "==", "x")
        t.value.payload = b"a" * (u.VALUE_MAX + 1)
        with self.assertRaises(ValueError):
            _fill(u.Query2(), [t], None, 1, 0)

    def test_int64_range(self):
        self.assertEqual(Value.of(2**63 - 1).python(), 2**63 - 1)
        self.assertEqual(Value.of(-2**63).python(), -2**63)
        for bad in (2**63, -2**63 - 1):
            with self.assertRaises(ValueError):
                Value.of(bad)
            with self.assertRaises(ValueError):
                Value.time_ns(bad)
        with self.assertRaises(ValueError):       # past year 2262 in nanoseconds
            Value.of(datetime(9999, 1, 1, tzinfo=timezone.utc))

    def test_matching(self):
        def m(term, value):
            return term.matches(None if value is None else Value.of(value))
        self.assertTrue(m(Term("a", "==", 3), 3))
        self.assertFalse(m(Term("a", "==", 3), 3.0))        # int64 and double are two types
        self.assertFalse(m(Term("a", "<", 3), "2"))
        self.assertTrue(m(Term("a", "<", 3), -5))
        self.assertTrue(m(Term("a", "prefix", "ab"), "abc"))
        self.assertFalse(m(Term("a", "prefix", "ab"), "a"))
        self.assertTrue(m(Term("a", "exists"), False))
        self.assertFalse(m(Term("a", "exists"), None))
        self.assertTrue(m(Term("a", ">", 1, missing_ok=True), None))
        self.assertFalse(m(Term("a", ">", 1, missing_ok=True), 0))
        self.assertFalse(m(Term("a", "==", 0.0), -0.0))     # double_key keeps the sign
        self.assertTrue(m(Term("a", "<", 0.0), -0.0))
        self.assertTrue(m(Term("a", "<", "b"), "a\xff"))    # bytes of UTF-8, not code points
        self.assertTrue(m(Term("a", ">", "é"), "z") is False)

    def test_double_order(self):
        vals = [float("-inf"), -2.5, -0.0, 0.0, 1e-300, 3.0, float("inf")]
        keys = [sort_key(Value.of(v)) for v in vals]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(len(set(keys)), len(keys))

    def test_request_rules(self):
        with self.assertRaises(ValueError):
            run([], [Term("a", "==", 1, missing_ok=True)])
        with self.assertRaises(ValueError):
            run([], [])
        with self.assertRaises(ValueError):
            run([], [Term("a", "exists")] * 5)
        self.assertEqual(run([], [], Order("a", "int64")), [])


class ScanRun(unittest.TestCase):
    cands = [("x", 5, {"n": Value.of(2), "k": Value.of("a")}),
             ("y", 3, {"n": Value.of(2), "k": Value.of("b")}),
             ("z", 9, {"n": Value.of(1.0)}),
             ("w", 1, {"n": Value.of(7), "k": Value.of("a")})]

    def test_unordered_by_inode(self):
        self.assertEqual([m.path for m in run(self.cands, [Term("k", "exists")])], ["w", "y", "x"])

    def test_ordered_ties_by_inode_both_ways(self):
        asc = run(self.cands, [], Order("n", "int64"))
        self.assertEqual([m.path for m in asc], ["y", "x", "w"])   # z is a double: not ordered
        desc = run(self.cands, [], Order("n", "int64", descending=True))
        self.assertEqual([m.path for m in desc], ["w", "x", "y"])

    def test_missing_ok_with_order(self):
        got = run(self.cands, [Term("k", "==", "a", missing_ok=True)], Order("n", "double"))
        self.assertEqual([m.path for m in got], ["z"])


def _acl(*entries):
    """The VFS's xattr form of an access ACL: version 2, then (tag, perm, id) entries."""
    return struct.pack("<I", 2) + b"".join(struct.pack("<HHI", t, p, i) for t, p, i in entries)


class ExtendedAcl(unittest.TestCase):
    """scan._ext_acl against the kernel's ext_acl (journal 0222): extended when it may decide
    otherwise than the mode bits."""
    U, G, O, M, NU = 0x01, 0x04, 0x20, 0x10, 0x02
    ANY = 0xFFFFFFFF

    def ext(self, raw, mode, err=None):
        def fake(path, name, follow_symlinks=True):
            if err is not None:
                raise OSError(err, os.strerror(err))
            return raw
        with mock.patch("moobfs.scan.os.getxattr", fake, create=True):
            return scan._ext_acl("f", mode)

    def test_minimal(self):
        same = _acl((self.U, 6, self.ANY), (self.G, 4, self.ANY), (self.O, 4, self.ANY))
        self.assertFalse(self.ext(same, 0o100644))       # equal to the bits: not extended
        self.assertTrue(self.ext(same, 0o100640))        # other entry grants what the bits do not
        other0 = _acl((self.U, 6, self.ANY), (self.G, 4, self.ANY), (self.O, 0, self.ANY))
        self.assertTrue(self.ext(other0, 0o100644))      # the ACL refuses others: extended

    def test_named_and_mask(self):
        named = _acl((self.U, 6, self.ANY), (self.NU, 4, 1002), (self.G, 4, self.ANY),
                     (self.M, 4, self.ANY), (self.O, 4, self.ANY))
        self.assertTrue(self.ext(named, 0o100644))
        mask = _acl((self.U, 6, self.ANY), (self.G, 4, self.ANY), (self.M, 4, self.ANY),
                    (self.O, 4, self.ANY))
        self.assertTrue(self.ext(mask, 0o100644))        # a mask alone is not equivalent

    def test_absent_unreadable_malformed(self):
        self.assertFalse(self.ext(None, 0o100600, errno.ENODATA))
        self.assertFalse(self.ext(None, 0o100600, errno.EOPNOTSUPP))
        self.assertTrue(self.ext(None, 0o100600, errno.EACCES))
        self.assertTrue(self.ext(b"\x02\x00\x00", 0o100600))
        self.assertTrue(self.ext(b"\x01\x00\x00\x00" + _acl((self.U, 6, self.ANY))[4:], 0o100600))
        self.assertTrue(self.ext(_acl((0x40, 6, self.ANY)), 0o100600))


class Modes(unittest.TestCase):
    def test_file_mode_for(self):
        self.assertEqual(moobfs.file_mode_for(0o700), 0o600)
        self.assertEqual(moobfs.file_mode_for(0o2770), 0o660)
        self.assertEqual(moobfs.file_mode_for(0o755), 0o666)
        self.assertEqual(moobfs.file_mode_for(0o711), 0o666)


class Installed(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("MOOBFS_TEST_INSTALLED"), "testing python/src")
    def test_imports_the_installed_package(self):
        self.assertNotIn(os.sep + "python" + os.sep + "src" + os.sep, moobfs.__file__)
        self.assertIn("site-packages", moobfs.__file__)


class Times(unittest.TestCase):
    def test_round_trip_microseconds(self):
        base = datetime(2001, 2, 3, tzinfo=timezone.utc)
        for d in (timedelta(0), timedelta(microseconds=1), timedelta(days=-20000, microseconds=7)):
            self.assertEqual(Value.of(base + d).python(), base + d)


if __name__ == "__main__":
    unittest.main()
