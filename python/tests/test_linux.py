# SPDX-License-Identifier: LGPL-2.1-or-later
"""Linux, without the module: publishing, attributes and the scan's homogeneity rule on a
plain directory that supports user extended attributes. Some cases need root (chown, a second
uid); they are skipped otherwise. MOOBFS_TEST_DIR picks the directory (default: a temporary
one)."""
import os
import shutil
import stat
import sys
import tempfile
import threading
import unittest
from unittest import mock

if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src"))

import moobfs  # noqa: E402
from moobfs import Domain, Order, Term  # noqa: E402
from moobfs import scan  # noqa: E402

NOBODY = 65534


def read(path):
    with open(path, "rb") as f:
        return f.read()


def xattrs_work(path):
    probe = os.path.join(path, ".probe")
    open(probe, "w").close()
    try:
        os.setxattr(probe, "user.moob.p", b"\x04\x01")
        return True
    except OSError:
        return False
    finally:
        os.unlink(probe)


@unittest.skipUnless(sys.platform.startswith("linux"), "Linux only")
class Plain(unittest.TestCase):
    def setUp(self):
        base = os.environ.get("MOOBFS_TEST_DIR") or tempfile.gettempdir()
        self.root = tempfile.mkdtemp(prefix="moobfs-pylib-", dir=base)
        os.chmod(self.root, 0o755)
        if not xattrs_work(self.root):
            shutil.rmtree(self.root)
            self.skipTest("no user extended attributes here")
        self.dom = os.path.join(self.root, "alice")
        os.mkdir(self.dom, 0o700)

    def tearDown(self):
        for dirpath, dirnames, _ in os.walk(self.root):
            for d in dirnames:
                os.chmod(os.path.join(dirpath, d), 0o700)
        shutil.rmtree(self.root)

    def test_publish_and_read_back(self):
        p = moobfs.publish(self.dom, "a.json", b"{}", {"kind": "note", "n": 3})
        self.assertEqual(stat.S_IMODE(os.stat(p).st_mode), 0o600)
        got = moobfs.get_attrs(p)
        self.assertEqual({k: v.python() for k, v in got.items()}, {"kind": "note", "n": 3})
        self.assertEqual(read(p), b"{}")
        with self.assertRaises(FileExistsError):
            moobfs.publish(self.dom, "a.json", b"x", {"kind": "other"})
        self.assertEqual(read(p), b"{}")
        moobfs.publish(self.dom, "a.json", b"[]", {"kind": "other"}, replace=True)
        self.assertEqual(moobfs.get_attrs(p)["kind"].python(), "other")
        self.assertNotIn("n", moobfs.get_attrs(p))
        self.assertEqual([n for n in os.listdir(self.dom) if n.startswith(".tmp.")], [])
        with self.assertRaises(ValueError):
            moobfs.publish(self.dom, "b.json", b"", {"bad-name": 1})
        self.assertFalse(os.path.exists(os.path.join(self.dom, "b.json")))

    def test_scan_answers(self):
        for i in range(20):
            moobfs.publish(self.dom, f"m{i:02d}", b"", {"kind": "a" if i % 2 else "b", "n": i})
        d = Domain(self.dom)
        self.assertEqual(d.last_backend, None)
        got = d.query(Term("kind", "==", "a"), Term("n", ">=", 10))
        self.assertEqual(d.last_backend, "scan")
        self.assertEqual(sorted(m.path for m in got), [f"m{i:02d}" for i in range(11, 20, 2)])
        top = d.query(order=Order("n", "int64", descending=True))[:3]
        self.assertEqual([m.path for m in top], ["m19", "m18", "m17"])
        a, b = d.batch([((Term("n", "==", 1),), None), ((Term("n", "==", 2),), None)])
        self.assertEqual(([m.path for m in a], [m.path for m in b]), (["m01"], ["m02"]))
        with self.assertRaises(moobfs.KernelUnavailable):
            Domain(self.dom, backend="kernel").query(Term("n", "exists"))

    def test_homogeneity_rule(self):
        moobfs.publish(self.dom, "shared", b"", {"k": 1})
        hidden = moobfs.publish(self.dom, "tighter", b"", {"k": 1})
        os.chmod(hidden, 0o200)                   # the domain's owner can enter but not read
        linked = moobfs.publish(self.dom, "linked", b"", {"k": 1})
        os.link(linked, os.path.join(self.root, "elsewhere"))   # two names: left out
        sub = os.path.join(self.dom, "sub")
        os.mkdir(sub, 0o700)
        moobfs.publish(sub, "deep", b"", {"k": 1}, domain=self.dom)
        closed = os.path.join(self.dom, "closed")
        os.mkdir(closed, 0o700)
        moobfs.publish(closed, "x", b"", {"k": 1}, domain=self.dom)
        os.chmod(closed, 0o600)                   # cannot be traversed by the domain's owner
        os.setxattr(self.dom + "/shared", "user.moob.bad", b"\x04\x07")   # invalid: not indexed
        got = sorted(m.path for m in Domain(self.dom).query(Term("k", "==", 1)))
        self.assertEqual(got, ["shared", "sub/deep"])
        self.assertNotIn("bad", moobfs.get_attrs(self.dom + "/shared"))

    def test_scan_through_a_link_to_the_domain(self):
        # 0700 domain, 0600 file of the same owner: homogeneous for the directory itself, not
        # for the 0777 mode of a symbolic link to it
        moobfs.publish(self.dom, "own", b"", {"k": 1})
        link = os.path.join(self.root, "alice-link")
        os.symlink(self.dom, link)
        self.assertEqual([m.path for m in Domain(link, backend="scan").query(Term("k", "==", 1))],
                         ["own"])

    def test_no_helper_process(self):
        import subprocess
        from moobfs.domain import layer_mounted

        def refuse(*args, **kwargs):
            raise AssertionError(f"started {args[0] if args else kwargs.get('args')}")
        saved = subprocess.Popen
        subprocess.Popen = refuse
        try:
            moobfs.publish(self.dom, "quiet", b"", {"k": 1})
            self.assertFalse(layer_mounted(self.dom))
        finally:
            subprocess.Popen = saved

    def test_search_only_directory(self):
        # a 0755 domain: a 0711 directory in it hides its names from group and others, who
        # can enter the domain, so a file under it is left out even when every class reads it
        os.chmod(self.dom, 0o755)
        for name, mode in (("open", 0o755), ("searchonly", 0o711), ("ownerread", 0o751)):
            d = os.path.join(self.dom, name)
            os.mkdir(d)
            os.chmod(d, mode)
            p = moobfs.publish(d, "f", b"", {"k": 1}, domain=self.dom)
            os.chmod(p, 0o644)
        got = sorted(m.path for m in Domain(self.dom, backend="scan").query(Term("k", "==", 1)))
        self.assertEqual(got, ["open/f"])

    # ---- the walk by descriptor (journal 0231): a name swapped for a link mid-walk --------
    def _outside(self):
        """A private directory outside the domain with a file the domain's owner may read."""
        outside = tempfile.mkdtemp(prefix="moobfs-pylib-out-")
        self.addCleanup(shutil.rmtree, outside)
        os.chmod(outside, 0o700)
        moobfs.publish(outside, "secret", b"private", {"k": 2})
        return outside

    def _paths(self):
        return sorted(p for p, _, _ in scan.candidates(self.dom))

    def test_scan_keeps_a_directory_swapped_for_a_link(self):
        """The directory the walk judged is the one it lists, even when someone swaps its
        name for a link to a directory outside between the two steps."""
        outside = self._outside()
        sub = os.path.join(self.dom, "sub")
        os.mkdir(sub, 0o700)
        moobfs.publish(sub, "deep", b"", {"k": 1}, domain=self.dom)
        real, calls = os.scandir, []

        def swapping(target):
            calls.append(target)
            if len(calls) == 2:               # about to list sub: swap it
                os.rename(sub, sub + ".moved")
                os.symlink(outside, sub)
            return real(target)
        with mock.patch.object(scan.os, "scandir", swapping):
            got = self._paths()
        self.assertEqual(len(calls), 2)
        self.assertNotIn("sub/secret", got)
        self.assertEqual(got, ["sub/deep"])     # the directory itself has no attributes

    def test_scan_reads_the_file_it_judged(self):
        """The attributes reported for a file are the ones of the inode the rule judged, not
        of a link someone put in its place before they were read."""
        outside = self._outside()
        f = moobfs.publish(self.dom, "f", b"", {"k": 1})
        real = scan.get_attrs

        def swapping(target, *args, **kwargs):
            if os.path.lexists(f) and not os.path.islink(f):
                os.rename(f, f + ".moved")
                os.symlink(os.path.join(outside, "secret"), f)
            return real(target, *args, **kwargs)
        with mock.patch.object(scan, "get_attrs", swapping):
            got = {p: {k: v.python() for k, v in a.items()} for p, _, a in scan.candidates(self.dom)}
        self.assertEqual(got, {"f": {"k": 1}})

    def test_scan_never_follows_links(self):
        """Links planted below the domain, to a directory and to a file outside it, are left
        out (the walk only ever opens with O_NOFOLLOW)."""
        outside = self._outside()
        moobfs.publish(self.dom, "own", b"", {"k": 1})
        os.symlink(outside, os.path.join(self.dom, "dirlink"))
        os.symlink(os.path.join(outside, "secret"), os.path.join(self.dom, "filelink"))
        os.mkfifo(os.path.join(self.dom, "pipe"))
        self.assertEqual(self._paths(), ["own"])

    def test_scan_under_a_racing_swap(self):
        """Someone sharing the domain exchanges a directory and a link to a private one over
        and over (renameat2 RENAME_EXCHANGE) while the walk runs: no answer ever names a file
        of the private directory."""
        import ctypes
        outside = self._outside()
        real = os.path.join(self.dom, "notes")
        os.mkdir(real, 0o700)
        moobfs.publish(real, "x", b"", {"k": 1}, domain=self.dom)
        link = os.path.join(self.dom, "notes.link")
        os.symlink(outside, link)
        try:
            exchange = ctypes.CDLL(None, use_errno=True).renameat2
        except AttributeError:
            self.skipTest("no renameat2 in the C library")
        stop, swaps, errors = threading.Event(), [0], []

        def swap():
            while not stop.is_set():
                if exchange(-100, os.fsencode(real), -100, os.fsencode(link), 2):
                    errors.append(ctypes.get_errno())
                    return
                swaps[0] += 1
        t = threading.Thread(target=swap)
        t.start()
        seen = set()
        try:
            for _ in range(2000):
                seen.update(self._paths())
        finally:
            stop.set()
            t.join()
        self.assertEqual(errors, [])
        self.assertGreater(swaps[0], 100)
        self.assertFalse({p for p in seen if p.endswith("secret")}, sorted(seen))
        self.assertTrue({"notes/x", "notes.link/x"} & seen, "no walk went through")

    @unittest.skipUnless(hasattr(os, "geteuid") and os.geteuid() == 0, "needs root")
    def test_group_domain_and_other_caller(self):
        team = os.path.join(self.root, "team")
        os.mkdir(team)
        os.chown(team, 0, NOBODY)
        os.chmod(team, 0o2770)
        ok = moobfs.publish(team, "ok", b"", {"k": 1})
        self.assertEqual(stat.S_IMODE(os.stat(ok).st_mode), 0o660)
        bad = moobfs.publish(team, "bad", b"", {"k": 1})
        os.chmod(bad, 0o604)                      # others may read, the group may not (0116)
        self.assertEqual([m.path for m in Domain(team).query(Term("k", "==", 1))], ["ok"])
        pid = os.fork()
        if pid == 0:                              # a caller who cannot enter alice's domain
            code = 1
            try:
                os.setgid(NOBODY + 1 if NOBODY + 1 < 65536 else 1)
                os.setuid(NOBODY)
                try:
                    Domain(self.dom).query(Term("k", "exists"))
                except PermissionError:
                    code = 0
            finally:
                os._exit(code)
        self.assertEqual(os.waitpid(pid, 0)[1], 0)


if __name__ == "__main__":
    unittest.main()
