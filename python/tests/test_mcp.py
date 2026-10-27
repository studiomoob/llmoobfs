# SPDX-License-Identifier: LGPL-2.1-or-later
"""moobfs.mcp on a plain Linux directory, through an in-process MCP client: the tools, the
path confinement, read-only mode and the identity check. Skipped without the mcp package or
user extended attributes. The mounted, two-user acceptance is mcp_mounted.py (journal 0208)."""
import asyncio
import ctypes
import os
import shutil
import sys
import tempfile
import threading
import unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))

try:
    from mcp import Client
    import moobfs.mcp as srv
    HAVE = sys.platform.startswith("linux")
except ImportError:
    HAVE = False


def call(server, name, args):
    async def go():
        async with Client(server) as client:
            r = await client.call_tool(name, args)
            return r.is_error, (r.structured_content or {}), " ".join(
                getattr(c, "text", "") for c in r.content)
    return asyncio.run(go())


def tools(server):
    async def go():
        async with Client(server) as client:
            return sorted(t.name for t in (await client.list_tools()).tools)
    return asyncio.run(go())


@unittest.skipUnless(HAVE, "needs Linux and the mcp package")
class Server(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="moobfs-mcp-")
        probe = os.path.join(self.root, "p")
        open(probe, "w").close()
        try:
            os.setxattr(probe, "user.moob.p", b"\x04\x01")
        except OSError:
            shutil.rmtree(self.root)
            self.skipTest("no user extended attributes here")
        os.unlink(probe)
        os.mkdir(os.path.join(self.root, "alice"), 0o700)
        self.server = srv.build_server(srv.Memory(self.root))

    def tearDown(self):
        shutil.rmtree(self.root)

    def test_write_read_query_delete(self):
        err, out, _ = call(self.server, "write", {
            "domain": "alice", "path": "notes/a.md", "content": "use staging",
            "attributes": {"kind": "decision", "n": 3, "score": 0.5, "ok": True,
                           "t": {"value": "2026-10-27T12:00:00Z", "type": "time"},
                           "d": {"value": 2, "type": "double"}}})
        self.assertFalse(err, out)
        err, out, _ = call(self.server, "read", {"domain": "alice", "path": "notes/a.md"})
        self.assertFalse(err)
        self.assertEqual(out["content"], "use staging")
        self.assertEqual(out["attributes"]["n"], {"type": "int64", "value": 3})
        self.assertEqual(out["attributes"]["d"], {"type": "double", "value": 2.0})
        self.assertEqual(out["attributes"]["t"]["type"], "time")
        err, out, _ = call(self.server, "query", {"domain": "alice", "terms": [
            {"name": "kind", "op": "==", "value": "decision"}, {"name": "n", "op": ">", "value": 2}]})
        self.assertFalse(err)
        self.assertEqual([m["path"] for m in out["matches"]], ["notes/a.md"])
        self.assertEqual(out["backend"], "scan")
        err, _, text = call(self.server, "write", {"domain": "alice", "path": "notes/a.md",
                                                   "content": "x"})
        self.assertTrue(err)
        self.assertIn("exists", text)
        err, out, _ = call(self.server, "delete", {"domain": "alice", "path": "notes/a.md"})
        self.assertFalse(err)
        err, out, _ = call(self.server, "query", {"domain": "alice",
                                                  "terms": [{"name": "kind", "op": "exists"}]})
        self.assertEqual(out["count"], 0)

    def test_paths_stay_inside_the_root(self):
        outside = tempfile.mkdtemp(prefix="moobfs-mcp-out-")
        try:
            with open(os.path.join(outside, "secret"), "w") as f:
                f.write("no")
            os.symlink(outside, os.path.join(self.root, "alice", "link"))
            os.symlink(outside, os.path.join(self.root, "escape"))
            bad = [("alice", "../alice/x"), ("alice", "/etc/passwd"), ("alice", "link/secret"),
                   ("../tmp", "x"), ("escape", "secret"), ("alice/..", "x"), ("", "x"),
                   ("alice", ""), ("alice", "a\0b")]
            for domain, path in bad:
                err, out, _ = call(self.server, "read", {"domain": domain, "path": path})
                self.assertTrue(err, (domain, path))
                self.assertNotIn("content", out)
                err, _, _ = call(self.server, "write", {"domain": domain, "path": path,
                                                        "content": "x"})
                self.assertTrue(err, (domain, path))
            self.assertEqual(os.listdir(outside), ["secret"])
        finally:
            shutil.rmtree(outside)

    def test_bad_terms_are_tool_errors(self):
        for terms in ([{"name": "Bad-Name", "op": "==", "value": 1}],
                      [{"name": "k", "op": "~", "value": 1}],
                      [{"name": "k", "op": "==", "value": "x", "type": "int64"}],
                      [{"name": "k", "op": "==", "value": [1]}],
                      [{"name": "k", "op": "==", "value": 1}] * 5,
                      [{"op": "=="}]):
            err, _, _ = call(self.server, "query", {"domain": "alice", "terms": terms})
            self.assertTrue(err, terms)

    def test_read_only(self):
        ro = srv.build_server(srv.Memory(self.root, read_only=True))
        self.assertEqual(tools(ro), ["info", "query", "read"])
        self.assertEqual(tools(self.server), ["delete", "info", "query", "read", "write"])

    def test_info(self):
        err, out, _ = call(self.server, "info", {})
        self.assertFalse(err)
        self.assertEqual(out["domains"], ["alice"])
        self.assertFalse(out["layer_mounted"])
        self.assertEqual(out["uid"], os.getuid())

    def test_identity_refused(self):
        me, g = os.getuid(), os.getgid()
        for ids in ((0, 0, 0), (me, 0, me), (me, me, 0), (me, me + 1, me)):
            with mock.patch("os.getresuid", return_value=ids):
                with self.assertRaises(SystemExit, msg=ids):
                    srv.check_identity()
        with mock.patch("os.getresuid", return_value=(me, me, me)), \
                mock.patch("os.getresgid", return_value=(g, g + 1, g)):
            with self.assertRaises(SystemExit):
                srv.check_identity()
        with mock.patch("os.getresuid", return_value=(me, me, me)), \
                mock.patch("os.getresgid", return_value=(g, g, g)), \
                mock.patch.object(srv, "_effective_capabilities", return_value=1 << 2):
            with self.assertRaises(SystemExit):     # CAP_DAC_READ_SEARCH
                srv.check_identity()

    def test_component_swapped_for_a_link(self):
        """Someone sharing the domain swaps a directory of the path for a link to a private
        one, over and over, while the agent reads, writes and deletes there."""
        outside = tempfile.mkdtemp(prefix="moobfs-mcp-out-")
        dom = os.path.join(self.root, "alice")
        memory = srv.Memory(self.root)
        stop = threading.Event()
        try:
            os.chmod(outside, 0o700)
            with open(os.path.join(outside, "x"), "w") as f:
                f.write("private")
            real = os.path.join(dom, "notes")
            os.mkdir(real)
            with open(os.path.join(real, "x"), "w") as f:
                f.write("shared")
            link = os.path.join(dom, "notes.link")
            os.symlink(outside, link)
            exchange = ctypes.CDLL(None, use_errno=True).renameat2
            swaps, errors = [0], []

            def swap():
                while not stop.is_set():   # notes and notes.link trade places atomically
                    if exchange(-100, os.fsencode(real), -100, os.fsencode(link), 2):
                        errors.append(ctypes.get_errno())
                        return
                    swaps[0] += 1
            t = threading.Thread(target=swap)
            t.start()
            seen = set()
            for i in range(600):
                for fn, args in ((memory.read, ("alice", "notes/x")),
                                 (memory.write, ("alice", "notes/x", "agent", None, True)),
                                 (memory.write, ("alice", f"notes/n{i}", "agent")),
                                 (memory.delete, ("alice", f"notes/n{i}"))):
                    try:
                        out = fn(*args)
                    except (srv.ToolError, FileNotFoundError):
                        continue
                    if isinstance(out, dict) and "content" in out:
                        seen.add(out["content"])
            stop.set()
            t.join()
            self.assertEqual(errors, [])
            self.assertGreater(swaps[0], 1000)
            self.assertNotIn("private", seen)
            self.assertTrue(seen, "no read went through")
            self.assertEqual(sorted(os.listdir(outside)), ["x"])
            with open(os.path.join(outside, "x")) as f:
                self.assertEqual(f.read(), "private")
        finally:
            stop.set()
            shutil.rmtree(outside)

    def test_fifo_does_not_hang_read(self):
        os.mkfifo(os.path.join(self.root, "alice", "pipe"))
        done = []
        t = threading.Thread(target=lambda: done.append(
            call(self.server, "read", {"domain": "alice", "path": "pipe"})))
        t.daemon = True
        t.start()
        t.join(10)
        self.assertTrue(done, "read of a FIFO did not return")
        self.assertTrue(done[0][0])


if __name__ == "__main__":
    unittest.main()
