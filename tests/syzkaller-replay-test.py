#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline controls for the KCOV replay harness; no kernel or guest is started."""
import importlib.util
from pathlib import Path
import queue
import subprocess
import threading
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location(
    'replay', Path(__file__).resolve().parents[1] / 'vm/syzkaller/replay.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


class ReplayControls(unittest.TestCase):
    def test_guest_script_formats_and_parses(self):
        script = replay.GUEST_SCRIPT.format(timeout=60)
        self.assertEqual(subprocess.run(['sh', '-n'], input=script, text=True).returncode, 0)

    def test_failed_execution_is_not_cached_as_coverage(self):
        a = SimpleNamespace(port=10000, key='unused', timeout=60)
        g = replay.Guest(a, 0, '')
        g.lo, g.hi = 0x1000, 0x2000
        for output, expected in [
                (b'EXIT 0\n0x1010\nREPLAY_OK\n', [0x10]),
                (b'EXIT 0\nREPLAY_OK\n', []),
                (b'EXIT 124\n0x1010\nREPLAY_OK\n', None),
                (b'EXIT 1\nREPLAY_OK\n', None),
                (b'EXIT 0\n0x1010\n', None)]:
            g.run = lambda *args, **kwargs: SimpleNamespace(returncode=0, stdout=output)
            self.assertEqual(g.one(__file__), expected)

    def test_worker_exception_stops_its_guest(self):
        class BrokenGuest:
            idx = 0
            stopped = False

            def boot(self):
                raise subprocess.TimeoutExpired('ssh', 20)

            def stop(self):
                self.stopped = True

            def log(self, message):
                pass

        g = BrokenGuest()
        q = queue.Queue()
        q.put(('seed', __file__, 'key'))
        stats = {'run': 0, 'failed': []}
        replay.worker(g, q, SimpleNamespace(deadline=0), stats, threading.Lock())
        self.assertTrue(g.stopped)
        self.assertEqual(stats['run'], 0)
        self.assertEqual(len(stats['failed']), 1)


if __name__ == '__main__':
    unittest.main()
