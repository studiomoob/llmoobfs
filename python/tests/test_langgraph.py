# SPDX-License-Identifier: LGPL-2.1-or-later
"""moobfs.langgraph on a plain Linux directory: the same answers as LangGraph's InMemoryStore
for the filters of the G1 protocol, and files the G1 adapter (bench/client/langgraph_store.py)
reads and writes too. Skipped without langgraph or without user extended attributes. The
mounted comparison is G1's gate run with --store package (journal 0207)."""
import os
import random
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))

try:
    from langgraph.store.memory import InMemoryStore
    import moobfs.langgraph as pkg
    HAVE = sys.platform.startswith("linux")
except ImportError:
    HAVE = False

FILTERS = [None, {"kind": "decision"}, {"kind": "fact", "flag": True}, {"score": {"$gt": 0.5}},
           {"score": {"$gte": 0.25, "$lt": 0.75}}, {"kind": {"$ne": "fact"}}, {"n": 3},
           {"n": 3.0}, {"tags": ["a", "b"]}, {"meta": {"lang": "pt"}}, {"missing": 1},
           {"kind": "decision", "flag": False, "score": {"$lte": 0.5}, "n": {"$gt": 2}}]


def corpus(rng, n):
    out = []
    for i in range(n):
        v = {"kind": rng.choice(["decision", "fact", "note"]), "flag": rng.random() < 0.5,
             "score": rng.choice([0.0, 0.25, 0.5, 0.75, 1.0]), "n": rng.randint(0, 5),
             "note": f"n{i}"}
        if rng.random() < 0.3:
            v["tags"] = rng.choice([["a", "b"], ["b"], []])
        if rng.random() < 0.3:
            v["meta"] = {"lang": rng.choice(["pt", "en"])}
        out.append(v)
    return out


def identity(item):
    return (item.namespace, item.key, repr(sorted(item.value.items())))


@unittest.skipUnless(HAVE, "needs Linux and langgraph")
class Store(unittest.TestCase):
    def setUp(self):
        base = os.environ.get("MOOBFS_TEST_DIR") or tempfile.gettempdir()
        self.root = tempfile.mkdtemp(prefix="moobfs-lg-", dir=base)
        probe = os.path.join(self.root, "p")
        open(probe, "w").close()
        try:
            os.setxattr(probe, "user.moob.p", b"\x04\x01")
        except OSError:
            shutil.rmtree(self.root)
            self.skipTest("no user extended attributes here")
        os.unlink(probe)

    def tearDown(self):
        shutil.rmtree(self.root)

    def test_batched_labels_stay_inside_the_root(self):
        from langgraph.store.base import GetOp, InvalidNamespaceError, PutOp, SearchOp
        store = pkg.MoobStore(os.path.join(self.root, "store"))
        os.mkdir(store.root)
        outside = os.path.join(self.root, "outside")
        os.mkdir(outside)
        with open(os.path.join(outside, "k.json"), "w") as f:
            f.write('{"value": {}, "created_at": "2026-10-27T00:00:00+00:00", '
                    '"updated_at": "2026-10-27T00:00:00+00:00"}')
        for op in (PutOp(("..", "outside"), "w", {"v": 1}), PutOp(("..", "outside"), "k", None),
                   GetOp(("..", "outside"), "k"), SearchOp(("..",)), PutOp(("a", ""), "k", {}),
                   PutOp((".", "a"), "k", {})):
            with self.assertRaises(InvalidNamespaceError, msg=op):
                store.batch([op])
        with self.assertRaises(InvalidNamespaceError):    # checked before any operation runs
            store.batch([PutOp(("ok",), "k", {"v": 1}), PutOp(("..",), "k", {"v": 1})])
        self.assertEqual(sorted(os.listdir(outside)), ["k.json"])
        self.assertEqual(os.listdir(store.root), [])

    def test_equal_to_in_memory_store(self):
        rng = random.Random(1)
        store, oracle = pkg.MoobStore(self.root, domain_depth=1), InMemoryStore()
        for ns in (("alice",), ("alice", "sub"), ("team",)):
            for i, v in enumerate(corpus(rng, 60)):
                store.put(ns, f"k{i:03d}", v)
                oracle.put(ns, f"k{i:03d}", v)
        for i in range(0, 60, 7):
            store.delete(("alice",), f"k{i:03d}")
            oracle.delete(("alice",), f"k{i:03d}")
        for prefix in (("alice",), ("alice", "sub"), ("team",), ()):
            for flt in FILTERS:
                got = store.search(prefix, filter=flt, limit=1000)
                exp = oracle.search(prefix, filter=flt, limit=1000)
                self.assertEqual({identity(x) for x in got}, {identity(x) for x in exp},
                                 f"{prefix} {flt}")
        self.assertEqual(store.stats["query2_calls"], 0)
        self.assertGreater(store.stats["fallback_domains"], 0)
        page = store.search(("team",), filter={"kind": "fact"}, limit=5, offset=5)
        whole = store.search(("team",), filter={"kind": "fact"}, limit=1000)
        self.assertEqual([x.key for x in page], [x.key for x in whole[5:10]])
        self.assertEqual(sorted(store.list_namespaces()), sorted(oracle.list_namespaces()))
        with self.assertRaises(NotImplementedError):
            store.search(("team",), query="text")

    def test_publication_stopped_leaves_the_old_file(self):
        store = pkg.MoobStore(self.root, domain_depth=1)
        store.put(("alice",), "p", {"note": "v1"})
        for step in ("content", "attributes", "file_fsync"):
            store.stop_after = step
            with self.assertRaises(pkg.PublicationStopped):
                store.put(("alice",), "p", {"note": "v2"})
            self.assertEqual(store.get(("alice",), "p").value["note"], "v1")
        store.stop_after = None
        self.assertEqual([n for n in os.listdir(os.path.join(self.root, "alice"))
                          if n.startswith(".tmp.")], [])

    def test_files_shared_with_the_g1_adapter(self):
        sys.path.insert(0, os.path.join(REPO, "bench", "client"))
        try:
            import langgraph_store as adapter
        finally:
            sys.path.pop(0)
        old, new = adapter.MoobStore(self.root, 1), pkg.MoobStore(self.root, 1)
        old.put(("alice",), "a", {"kind": "decision", "n": 3, "score": 0.5, "flag": True})
        new.put(("alice",), "b", {"kind": "decision", "n": 3.0, "score": 0.5, "flag": True})
        for store in (old, new):
            got = store.search(("alice",), filter={"kind": "decision", "n": 3}, limit=10)
            self.assertEqual(sorted(x.key for x in got), ["a", "b"])
        a, b = (os.path.join(self.root, "alice", f"{k}.json") for k in "ab")
        for name in ("user.moob.v.kind", "user.moob.v.n", "user.moob.v.score", "user.moob.v.flag"):
            self.assertEqual(os.getxattr(a, name), os.getxattr(b, name), name)
        self.assertEqual(new.get(("alice",), "a").value, old.get(("alice",), "a").value)


if __name__ == "__main__":
    unittest.main()
