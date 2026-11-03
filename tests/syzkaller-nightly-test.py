#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Linux-only orchestration controls; fake phase tools never start a guest or fuzzer."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


@unittest.skipUnless(sys.platform.startswith("linux"), "runner needs GNU date/flock/timeout")
class CampaignWindow(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "vm/syzkaller").mkdir(parents=True)
        shutil.copyfile(Path(__file__).resolve().parents[1] / "vm/syzkaller-nightly.sh",
                        self.root / "vm/syzkaller-nightly.sh")
        self.phase("kasan-local.sh", "echo MOOBFS_KASAN_ONLY_PASS: fixture\n")
        self.phase("syzkaller-local.sh", """mkdir -p build/syzkaller/workdir
if [ "${2:-}" = run ]; then
 printf '%s' "$1" > duration
 printf 'duration_s=%s\ncorpus=1 coverage=1 exec total=1\ncrash_dirs=0\n' "$1" > build/syzkaller/summary.txt
 printf 'fixture' > build/syzkaller/workdir/corpus.db
 printf 'fixture' > build/syzkaller/funccover.csv
fi
""")
        self.phase("syzkaller-replay.sh", """mkdir -p "$1"
printf '%s' "$REPLAY_DEADLINE" > "$1/deadline"
printf 'programs=1 replayed=1 failed=0 not_reached=0\n' > "$1/replay.txt"
printf 'CALLSITES 1/1 100.0%%\n' > "$1/callsites.txt"
""")
        (self.root / "vm/syzkaller/callsites.py").write_text("print('CALLSITES 1/1 100.0%')\n")
        self.env = dict(os.environ, TZ="UTC", SYZ="fixture", GO="fixture")
        for key in ("NIGHT_RESUME", "NIGHT_DEADLINE", "NIGHT_FUZZ_S"):
            self.env.pop(key, None)
        self.deadline = int(time.time()) + 7200
        self.env.update(NIGHT_DEADLINE=str(self.deadline), NIGHT_REPLAY_S="3600")

    def phase(self, name, body):
        (self.root / "vm" / name).write_text("#!/bin/sh\nset -eu\n" + body)

    def run_campaign(self, **env):
        return subprocess.run(["sh", "vm/syzkaller-nightly.sh", "reports"],
                              cwd=self.root, env=dict(self.env, **env),
                              text=True, capture_output=True, timeout=10)

    def test_explicit_window_and_fuzz_cap(self):
        result = self.run_campaign(NIGHT_FUZZ_S="7")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "duration").read_text(), "7")
        report = next((self.root / "reports").glob("*/report.txt"))
        self.assertNotIn("not complete", report.read_text())
        self.assertEqual((report.parent / "replay/deadline").read_text(),
                         str(self.deadline - 30))

    def test_explicit_window_without_cap(self):
        result = self.run_campaign()
        self.assertEqual(result.returncode, 0, result.stderr)
        duration = int((self.root / "duration").read_text())
        self.assertGreaterEqual(duration, 3580)
        self.assertLessEqual(duration, 3600)

    def test_invalid_deadline(self):
        result = self.run_campaign(NIGHT_DEADLINE="tomorrow")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "duration").exists())

    def test_zero_fuzz_cap(self):
        result = self.run_campaign(NIGHT_FUZZ_S="0")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "duration").exists())

    def test_expired_window_preserves_partial_verdict(self):
        result = self.run_campaign(NIGHT_DEADLINE=str(int(time.time()) - 60))
        self.assertEqual(result.returncode, 0, result.stderr)
        report = next((self.root / "reports").glob("*/report.txt"))
        self.assertIn("replay: not complete", report.read_text())
        self.assertFalse((self.root / "duration").exists())


if __name__ == "__main__":
    unittest.main()
