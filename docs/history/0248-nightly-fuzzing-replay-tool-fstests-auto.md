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

## Schedule installed (2026-10-31 15:28:17 Brasília time)

After full Debian and full KASAN passed on `569ec2a` and the real replay smoke passed,
the gated host installer added the 23:00 and reboot entries. It preserved the prior crontab
and verified the installed content. A clean frozen clone under `nightly-src/` is used,
with both daytime slot locks and CPUs 4–7, window 23:00–05:00 America/Sao_Paulo.
No nightly report exists yet; S18 and S15's longer campaign remain open. ext4 and XFS
auto continue; btrfs remains queued after 05:15 on November 1 and XFS completion.


## ext4 auto rerun verified (2026-10-31)

This verification round ran from about 18:40 to 18:41 Brasília time (calendar time).
The ext4 exclusion-list rerun began at 14:05:41 and finished at 18:39:42, exit 0.
The actual guest console contains MOOBFS_FSTESTS_CHECK_EXIT: 0 and the ext4 DONE marker.
Of 778 selected cases, the per-test classifier reports 229 pass, 549 notrun, zero fail;
"Passed all 778 tests" is fstests' summary and does not mean that skipped cases ran.
The classifier was re-executed against the saved log, returned 0 and reproduced the saved
classifications byte for byte. No .out.bad artifact was present. The saved console passed
vm/kdiag.sh's diagnostic scan. No test or monitor was restarted for this verification.

Post-run hashes of 1,803 tracked paths were compared with `e839590`: all executable code,
module sources, harnesses and exclusion lists match; six documentation files differ
(CHANGELOG, journal 0248, history index, references, security roadmap and testing guide).
There is no pre-run source manifest for this inherited copy, so this is a post-run source
comparison, not proof of unchanged files throughout the run. fstests HEAD is
`22348afe338c0f6d540c0d7ef0db749eaa51b218`; its common/config and common/rc have the
harness filesystem adaptation. Built module SHA256:
`36dff256c0877a25301354dd86fc027aee1647412e552468fd5e18ccbdabbaa1`.
Per-test results are retained in `vm/fstests/results-ext4-auto-exclusions-6.12.113.txt`.
The local evidence archive `ext4-auto-final.tar.gz` and source verification JSON are in
the batch-11 local review directory. Archive SHA256:
`0fdc65b1d08356f2b0fc45bb790a85ba6f725766813f7457483371a763138762`.

slotA (CPUs 4–5) is free, confirmed by a nonblocking lock probe. XFS remains running on
CPUs 6–7, and the host disk remains reserved to S2. The existing nightly schedule takes
both slots at 23:00; btrfs remains queued after 05:15 on November 1 and XFS completion.
G6 and the corporate instance were notified. Public surfaces outside this local branch
remain unchanged pending publication approval; no push, PR or merge to main occurred.


## XFS auto rerun verified (2026-10-31)

The inherited XFS run started at 12:22:07 and ended at 19:26:08 Brasília time,
exit 0, about 7 h 4 min of machine/calendar time. Verification completed at 19h27 Brasília time.
The full guest log has MOOBFS_FSTESTS_CHECK_EXIT: 0 and the XFS DONE marker. Among
778 selected cases: 372 pass, 406 notrun, zero fail; skipped cases were not executed.
No .out.bad file was present. vm/kdiag.sh found no kernel diagnostic in the console.
The inherited classifier predates the failure-exit fix: its exit status alone was not
accepted. The current batch classifier was independently run against the saved console,
returned 0, and reproduced the saved per-test classifications byte for byte.

Post-run hashes were collected for 1,803 tracked paths and compared with `e839590`.
All 24 relevant module/fstests paths checked match, including production module sources,
Makefile, host/guest harness, filesystem patch, diagnostic helper and exclusion lists.
Six documentation paths and seven classifier/fuzzing paths differ; the verification JSON
records each difference. This is not a fully identical snapshot, and no inherited pre-run
manifest exists. fstests HEAD is `22348afe338c0f6d540c0d7ef0db749eaa51b218`, with the
expected common/config and common/rc adaptation. The built module SHA256 equals the ext4
run: `36dff256c0877a25301354dd86fc027aee1647412e552468fd5e18ccbdabbaa1`.

Per-test results: `vm/fstests/results-xfs-auto-exclusions-6.12.113.txt`. Console, results,
build log and exclusions are archived outside Git in the batch-11 review directory as
`xfs-auto-final.tar.gz`, SHA256
`ce4f1ec8fc873aa68a4203cac16ae13d4bfd6566550386fc64a2f6b5e985c4c1`;
`xfs-auto-source-verification.json` records hashes and the independent verdicts.
Both daytime slot locks are free and no QEMU guest remained at inspection. The existing
23:00 nightly and November 1 btrfs queue after 05:15 are preserved; no workload or monitor
was duplicated. S2's exclusion-list rerun is now verified for ext4 and XFS; btrfs remains
pending. Resource notices were sent to the existing G6 and corporate instances.
Public surfaces outside this local branch remain unchanged pending approval.
No push, PR, merge to main or publication.


## First scheduled night incomplete; deadline corrections (2026-11-01)

Inspection began at 05:00 Brasília time. The October 31 night ran 23:00:02–05:00:00.
Fuzzing exited 0 after 16,690 seconds: corpus 1,244, 593,680 executions, manager coverage
16,737, zero crash directories. Manager handler count 687/809 (84.9%) is not ADR-0032's
call-site metric and does not replace S3. Build/run logs and phase markers were inspected.

The extended permissions stage exited 124 at its 1,200-second limit. Its console has all
24 ext4 seed passes and its ext4 step marker (82,944 QUERY2 checks), followed by 20 btrfs
seed passes; no completed btrfs/XFS step or final KASAN marker. The full S15 nightly
campaign did not pass. There is no final diagnostic capture from this interrupted guest.
The captured output includes QEMU redirection/image-format warnings; replay guest 0 also
reports the kernel's deprecated mand mount option. These are retained, not presented as a
completed clean instrumented suite. No kernel correction is inferred from this timeout.

Replay started at 04:00:02 with 1,261 unique programs and no prior entries. It cached 260
programs before the outer 05:00 timeout killed aggregation: replay.done, replay.txt,
union.txt and callsites.txt are absent. Report existence is not completion; zero of seven
complete nightly reports are accepted. The original script did not save replay's exit
status, so it cannot be reconstructed as a measured code after the fact. No automatic
repeat of this night was started, and the original cache/corpus remain intact.

Post-run comparison: 1,810 tracked paths against `569ec2a`; only the generated
vm/syzkaller/moobfs.txt.const differs (getxattr syscall constant replaces chdir). Source
comparison is not a pre/post proof: no per-night pre-run manifest existed. The replay cache
is scoped to module SHA256 a0f6ff09405e8a521c5852f2c80a40065c5c7bf0dc23bb6453fa88832437a85c
and its kernel. Evidence was copied locally and SHA256-verified as
night-2026-10-31-final.tar.gz:
b2b0e5c04c326ae102dce1a2047dc78fff3ef75024fdd4323820f27d91811e48.
The adjacent verification JSON lists source hashes, partial markers and cache count.

Harness corrections: allow up to 3,600 seconds for 24 permission seeds on all lowers;
retain the nightly deadline. End replay work 30 seconds before the hard window cutoff,
cap each guest SSH operation and boot loop by that deadline, save replay.exit explicitly,
and include a dequeued unfinished program in not_reached. Expiry preserves cache entries
and is not mislabeled as a completed/failed program. Five offline controls passed; removing
the SSH deadline cap and interrupted-program accounting makes two fail. Shell syntax and
whitespace checks passed. Real bounded replay and deployment validation are pending below.
Both slots were free with no QEMU at inspection; the existing btrfs queue after 05:15 and
the scheduled next night are preserved. No duplicated test/monitor and no publication.


### Corrected deadline validated and installed (2026-11-01 05:05 Brasília time)

Real bounded replay on snapshot `06e4a65`, one guest on CPUs 4–5 under slotA: at the
75-second deadline it retained 2/3 programs, failed=0, not_reached=1, exit 1. It saved
replay.txt, union.txt and CALLSITES 340/781 before returning; the unfinished program was
not cached as successful. Resuming that same test cache ran only the remaining program:
3/3 replayed, failed=0, not_reached=0, exit 0, 911 module-text PCs and 341/781 call sites.
This is a three-seed harness control, not the nightly corpus or a new S3 measurement.
Completed at 05:05:13. ShellCheck and all five offline controls also passed on the host.
The clone was clean at launch; its 1,812 source hashes were collected during the control,
matched the Git snapshot and were checked again after execution. No source file changed.

The first remote launch transport timed out while the detached command was already
running. Processes/logs were inspected; no second launch or duplicate guest was created.
Control evidence and hashes are archived locally as replay-check-06e4a65-final.tar.gz,
SHA256 ab260cea9c73ae8e642c9bbb9550aebf96ef5c1e0171723bc6e8848fc32d4fee,
with its verification JSON. Original nightly corpus/cache/report remain preserved separately.

At 05:05:28 both daytime slot locks were acquired and the host wrapper was atomically
switched to a separate clean clone, nightly-next-06e4a65; old wrapper backed up. Crontab
and the btrfs queue are unchanged. Next scheduled night uses the corrected harness and
60-minute maximum permission-stage budget. That longer campaign has not yet passed;
the first night is still incomplete, zero of seven complete nights accepted. No new
full module suite is claimed for this harness-only correction. The original full-suite
snapshot remains `569ec2a`. No QEMU remained from the replay control after completion.
