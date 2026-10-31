# 0248 — The corpus replay tool, nightly syzkaller (S18) and the repeat of fstests `auto` (S2)

Date: 2026-10-31 onward. Status: **in progress**, updated per round, on branch
`claudiofp/b11-night`. Work started: 2026-10-31 15:18 UTC. Wall-clock times are calendar time,
not engineering effort. Estimated effort: S for the replay tool and the nightly job (S18's own
estimate), plus machine time: seven nights of fuzzing and about 25 guest-hours of fstests.

## Goal

1. Commit the per-call KCOV replay that journal 0239 and ADR-0032 used to count the S3
   handlers' call sites (688 of 781), which ran from host-only scripts.
2. S18: syzkaller every night on the test host with the S3 descriptions and seeds, each
   night's report kept, with the call-site figure of the replay. Done when seven nightly reports
   are recorded.
3. S2's remainder: fstests `auto` on ext4, btrfs and XFS with the two exclusion lists of journal
   0239, and a make target for the long run.

## Decisions

(filled in below as the work goes)

## Rounds

(one entry per round)

## Takeover, 2026-10-31 (12:41 Brasília time onward)

The CI front is committed separately (`7a967d5`), and S15 remains in progress (journal 0250).
No push, PR, merge or remote public edit is authorized by this takeover.

### Decisions and implementation

- `make fstests-auto` runs the long group with the common quick/auto exclusion lists and
  btrfs's additional list. `vm/fstests/results.sh` extracts the final per-test classification
  and refuses a log without a final `Ran:` list.
- `vm/syzkaller-replay.sh`, `vm/syzkaller/replay.py`, `callsites.py` and `handlers.txt` keep
  ADR-0032's per-call replay reproducible. Disposable snapshot guests verify the module's
  SHA-256; cache entries are keyed by module/kernel and program content. The handler set and
  both manager and call-site figures are retained, with no invented percentage threshold.
- `vm/syzkaller-nightly.sh` retains phases and per-night reports; intended window is
  23:00–05:00 Brasília time on CPUs 4–7, after exclusive slot locks. It includes S15's longer
  KASAN campaign (`24,96,YYYYMMDD` by default, at most 20 minutes), then syzkaller with the
  corpus preserved and time reserved for replay. GNU timeout bounds build and replay phases
  at the window end, with a 15-second final kill grace. An interrupted/incomplete phase is
  reported as such; a scheduled job is not a completed night.
- Scheduling is not installed yet. The source for the schedule must be the integrated batch,
  containing S15. Existing daytime runs are preserved. Seven nightly reports remain required
  before S18 closes, independently of publication of its harness.

### Errors found and corrected before the first replay

1. The awk braces in the guest shell template were interpreted by Python `str.format`, which
   would raise an exception before any program ran. Escape the braces; the rendered script now
   passes `sh -n`.
2. The initial parser accepted a timeout/nonzero syz-execprog result as successful cached
   coverage. It now requires `EXIT 0` and the final transport marker.
3. A module mismatch or unexpected worker exception could leave a guest alive. The mismatch
   stops the guest and the worker always cleans up in `finally`, recording an error.
4. Duplicate program contents could race on the same cache temporary path across guests.
   Queue each content hash once. Missing call sites or missing manager handler PCs now fail
   explicitly instead of printing a misleading zero-denominator percentage.
5. A long build could leave a negative fuzz duration; it now records that the fuzz window was
   consumed. The phase exit status is retained in the report. Note that the reused low-level
   syzkaller harness itself suppresses the manager's timeout status; its summary and real
   execution counters must still be inspected to assess a night.

### Checks actually run

- `python3 tests/syzkaller-replay-test.py`: three offline controls passed (rendered guest script,
  failed/incomplete program output rejected, worker exception cleans up). No guest is started
  by these controls.
- `sh -n` of the new shell scripts, `py_compile` of both Python helpers, SPDX and
  `git diff --check`: passed on the workstation.
- ShellCheck of the three new shell scripts on the dedicated host: passed before the latest
  permission-stage addition; final recheck recorded in the continuation below.
- Exact syzkaller source inspected on the host, revision `8fb0ae898`: `dumpCallCoverage()`
  already calls `PreviousInstructionPC()`, which subtracts five bytes on amd64. Replay offsets
  therefore already name the call instructions; no second subtraction. References §45.

### Running jobs and limitations

At 12:41 Brasília time XFS auto and the syzkaller kernel build were active. XFS holds slotB
(CPUs 6–7), the build holds slotA (CPUs 4–5); neither was interrupted. The XFS guest was in
`generic/017`, a loop of roughly ten thousand collapse-range operations, at 12:45. No final
fstests verdict is claimed. The kernel build has not emitted `prep.done` yet. ext4 and btrfs
reruns, a real replay smoke, nightly installation and seven reports remain pending, as do
integrated full VM/KASAN and first CI mutation/gcov runs. External surfaces are unchanged
pending the maintainer's publication authorization.

Final offline recheck, 2026-10-31: ShellCheck passed after the permission stage and time
limits were added; all three replay controls also passed on the dedicated Debian host.

Continuation, 2026-10-31 about 12:49 Brasília time: a detached one-shot continuation waits
for the existing kernel build's successful `prep.done`, takes slotA, runs a real replay smoke
on `xattr-get-security`, `xattr-get-fd` and `query-long-path`, and checks nonempty module
coverage. Only on success does it release the slot and start ext4 auto on CPUs 4–5, in a
separate copy. This does not claim either run has started or passed yet. XFS progressed to
`generic/037`; it is not stuck in `generic/017`.

The final result classifier now exits nonzero if any test failed, while retaining every
classification in the output file. Three fixture controls passed on the workstation: a pass
plus skip is successful; a failed test preserves `fail` and returns nonzero; an interrupted
log without `Ran:` returns nonzero. ShellCheck of that final script passed on the host.
The reboot example takes both slot locks just like the normal nightly entry.

## Real replay smoke and ext4 start (2026-10-31)

Build exit 0 at 14:02:29 Brasília time. Smoke exit 0 at 14:05:13: xattr-get-security,
xattr-get-fd and query-long-path, one disposable guest pinned to CPUs 4–5. All three
programs replayed, zero failed/unreached, nonempty coverage: 911 module-text PCs,
341 of 781 handler call sites (43.7%). This is only a smoke; it does not measure the full
corpus or replace S3's 688/781. ext4 auto started at 14:05:41 in its separate copy.
Full Debian integration passed at 14:09; the nightly installer awaits full KASAN.
