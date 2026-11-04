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


## Authorized daytime long permissions campaign (2026-11-01)

The maintainer authorized running the missing longer profile now. Started at 08:05:25
Brasília time in an independent clone of `06e4a65`, under slotA on CPUs 4–5:
MOOB_ONLY=permchurn MOOB_PERMCHURN=24,96,20261031, timeout 3600 seconds including build,
then KILL after 15 seconds. This retains the first night's seed range and 96-step profile.
The existing btrfs auto guest stays on slotB/CPUs 6–7 and was not interrupted. Available
memory and slot locks were checked before launch; no other KASAN campaign was running.
This is a focused daytime validation, not another nightly report or a complete suite.

A unique driver retains source revision, hashes before/after, test exit status and final
marker under the dedicated host's b11night/s15-long-20261101/. Result pending: accept only
after checking all three lower step markers, final KASAN-only marker, real exit status,
source identity and diagnostics. A timeout is not a pass. Latest calendar deadline is
about 09:05 Brasília time, plus the explicit 15-second final kill allowance.

The existing local marker notifier was extended with this run's done path, not duplicated.
Its prior sole process was stopped, its lock confirmed free, then it was restarted in the
same shell terminal; exactly one process observed (PID 94386). Prior send receipts persist,
so completed events are not resubmitted. Both this marker and btrfs completion are watched.
No publication approval was given; push, PR, main merge and public edits remain prohibited.


## Long focused permissions campaign passed (2026-11-01)

Started 08:05:25 and finished 08:30:35 Brasília time, exit 0, 25 min 10 s calendar/machine
time including build. All 24 seeds 20261031–20261054, four mounts of 96 steps, passed on
ext4, btrfs and XFS under KASAN/UBSAN/lockdep. Each lower's final step reports 82,944
QUERY2 checks; total 248,832. Both MOOBFS_ONLY_PASS and MOOBFS_KASAN_ONLY_PASS are present.
The saved final dmesg passes the exact kasan-guest.sh diagnostic regex through vm/kdiag.sh.
Instrumented kernel fragment matches the pinned fragment, with KASAN, UBSAN and
PROVE_LOCKING enabled. The module used by the guest equals the built module (SHA256
f99bbbe9d8a926bc580474dc49949147acc97b67115ffc266e949a5f82633780).
All 1,812 tracked source hashes match Git snapshot `06e4a65` before the run and remain
identical afterward. The completion marker's exit=0 hashes=0 was checked against these
logs and manifests; approval is not inferred from that marker alone.

Archive copied locally and SHA256-verified as s15-long-20261101-final.tar.gz:
34fbb34153a71a77eb085b4256383c72fa34725bc398d4ad56c9529b9ef48303.
The adjacent verification JSON retains the three step markers, hash count and scope.
This passes the longer permission profile in a daytime focused run; it is not a full
suite or a completed nightly report. The first night remains incomplete, zero of seven
complete nights accepted; next scheduled nightly integration remains pending. The
original full VM/KASAN snapshot `569ec2a` and this focused evidence are kept distinct.
slotA/CPUs 4–5 is released; btrfs auto continues alone in slotB (generic/170 at inspection).
Other instances are informed of resource ownership and the result. No duplicated test or
monitor, push, PR, main merge or public edit.


## Dedicated-host access unavailable (2026-11-01, about 09:51 Brasília time)

A status query to the dedicated host did not respond. A separate bounded SSH connection
(7-second connect timeout, one attempt) returned "Operation timed out" on port 22.
The local notifier remains alive and logged marker-read unavailability at 09:50:59.
The current btrfs run state is unknown: the last successful check at 08:52 had no final
marker and console progress at generic/175. This does not establish a failed or stopped
run. No host reboot, test restart, replacement runner or extra monitor was attempted.
Only the old local read-only SSH query was terminated. Peer instances were notified to
preserve possible active work. Previously verified S15/full-suite/ext4/XFS evidence is
unchanged. G6 reports its independent Debian VM prepared and shut down; this access issue
concerns the distinct dedicated host. Next: inspect actual btrfs logs/markers after access
returns, preserving the existing observer and avoiding duplicate execution.


Recovery, 2026-11-01 09:59 Brasília time: bounded SSH succeeded again. Host uptime is
1 day 21 hours and the original btrfs QEMU PID 273025 remains, running for 4 h 43 min.
Console advanced to generic/319 at 09:59:24; no final done marker yet. The run continued
through the access outage. No reboot, test restart or replacement monitor was performed.
The maintainer reported a possible temporary connectivity outage; its exact network cause
was not independently diagnosed. Peer instances were informed that host access is restored
but btrfs retains slotB. Approval remains pending its actual final logs and verdict.


## Idle daytime slot used for pending replay (2026-11-01)

The maintainer clarified that the dedicated host and new Debian host are exclusively for
our project fronts and reminded us of an existing Azure VM. This permits coordinating
available daytime windows; it does not by itself change acceptance criteria or batch order.
Read-only Azure inventory found Linux VM moobfs, size Standard_D4as_v4, deallocated.
No cloud VM was started, resized or created. Infrastructure identities remain outside Git.

At 10:39:38 Brasília time a unique daytime continuation of the first night's corpus replay
started in the unused slotA, CPUs 4–5, while btrfs keeps slotB/CPUs 6–7. Source `06e4a65`,
module SHA256 a0f6ff09405e8a521c5852f2c80a40065c5c7bf0dc23bb6453fa88832437a85c,
same snapshot image/kernel and shared content-addressed replay cache: 1,261 unique programs,
260 already cached, only 1,001 pending. Guest readiness was confirmed at 10:40:54.
This continues pending work instead of replaying completed programs. It is not another
fuzzing campaign, a new S3 result or retroactive approval of the incomplete first night.
Original night artifacts are preserved; the daytime result has its own directory.

The driver captures source hashes before/after, real exit status and final markers and
takes slotA. Work deadline 21:29:30, outer cutoff 21:30 plus 15-second kill allowance, to
release the host before the scheduled 23:00 window. Result pending. The existing sole
notifier was extended for this marker. Initial PID matching failed to identify the old
process and the lock correctly rejected a concurrent observer; terminal input sent while
Python held the foreground did not start the replacement. The old observer was then
identified exactly and stopped, its free lock verified, and the same shell restarted once.
One observer (PID 2122) was confirmed, retaining all previous send receipts. No test was
interrupted and no duplicate observer/guest was left running.

Peer fronts were informed; the local map, rules, pending list and host registry reflect
exclusive project use and the daytime queue. The criterion of seven complete nightly
reports remains recorded; no overnight criterion is silently counted as met by daytime
work. Publication remains unauthorized. Next: inspect actual daytime replay outcomes and
retain separate evidence when the existing notifier reports completion.


Continuity, 2026-11-01: the existing sole observer now watches subsequent per-date
nightly reports as well as pending btrfs/daytime replay for 48 hours, retaining receipts.
One active process PID16326 confirmed. Report existence never implies approval.
Azure operations are explicitly deferred by the maintainer; both local host queues continue.


## Btrfs auto verified; exclusion-list reruns complete (2026-11-01)

Btrfs started 05:15:07 and finished 13:40:10 Brasília time, exit 0: 8 h 25 min 3 s of
machine/calendar time. Actual console markers CHECK_EXIT=0 and DONE lower=btrfs are
present. The current classifier returned 0 and reproduced the saved per-test result byte
for byte: 339 pass, 436 notrun, zero fail among 775 selected cases. No .out.bad file was
present, and vm/kdiag.sh found no diagnostic in the captured console. Skips are not passes.

All 1,803 tracked source hashes match `e839590` after the run. There is no inherited pre-run
manifest, so this is a post-run source comparison rather than a before/after proof. The
fstests checkout is `22348afe338c0f6d540c0d7ef0db749eaa51b218`, with only the expected
common/config and common/rc layer adaptation. Built module SHA256 matches the ext4/XFS runs:
36dff256c0877a25301354dd86fc027aee1647412e552468fd5e18ccbdabbaa1.
Per-test classifications are committed as results-btrfs-auto-exclusions-6.12.113.txt.
Full logs/artifacts were copied locally and verified in btrfs-auto-final.tar.gz, SHA256
444228883808bf524de88d9a91097b69e77ac987a18e3592f9b19b0cc6022f7e;
the adjacent source-verification JSON records hashes and independent check results.

| Lower | Selected | Pass | Not run | Fail |
| --- | ---: | ---: | ---: | ---: |
| ext4 | 778 | 229 | 549 | 0 |
| btrfs | 775 | 339 | 436 | 0 |
| XFS | 778 | 372 | 406 | 0 |

The target and all three exclusion-list reruns of S2 are now locally validated. This is
the quick/auto scope with justified exclusions, not every fstests group. Current-state
README mirrors, FAQ/generated site, roadmap and fstests guide are updated together; the
initial failure evidence and dated history remain. Nothing is published remotely.
slotB/CPUs 6–7 is free (nonblocking lock probe succeeded). Daytime corpus replay continues
in slotA: 400 new programs completed by 13:39, in addition to 260 reused cache entries;
no final replay verdict yet. Peer instances were informed of slot release. No new test or
monitor was launched, no work was interrupted, and Azure remains deferred.


Documentation validation: the first site-journey generation omitted its required root
argument and failed before writing files; the retired-claims check then caught the still
stale generated status pages. Rerunning `python3 scripts/site-journey.py .` regenerated
them correctly. Retired claims, FAQ synchronization, site validation and whitespace
checks passed after regeneration. This changed no runtime source or test outcome.


## Complete corpus replay verified (2026-11-01)

The daytime continuation ran 10:39:38–17:22:05 Brasília time, exit 0: 6 h 42 min 27 s of
machine/calendar time. Final summary: programs=1261, replayed=1261, run_now=1001, failed=0,
not_reached=0, module_text_pcs=1843. No guest boot/worker/retry failure appears in run.log.
Per-call coverage is 688/781 KCOV call sites (88.1%); all 23 named handler functions have
at least one reached site, including moobl_xattr_get 4/4. This reproduces the S3 figure;
it is a replay of the first night's corpus, not another six-hour fuzzing run.

Independent artifact checks: all 1,261 content-hash cache entries exist in the namespace
for module a0f6ff09405e8a521c5852f2c80a40065c5c7bf0dc23bb6453fa88832437a85c and kernel
4026582cc0cd60f7c22f25854fb33a21668aea40eed64eea74f726a769d40ae5. Their recomputed union
matches union.txt exactly; rerunning the call-site classifier reproduces callsites.txt
byte for byte. The 260 entries archived before continuation are byte-identical to the
corresponding final entries. All 1,812 tracked source hashes match Git snapshot `06e4a65`
before execution and remain identical afterward. Test exit, hash exit and final summaries
were checked against raw artifacts, not accepted from the completion marker alone.

The saved configuration has KASAN, UBSAN and PROVE_LOCKING enabled. The diagnostic scan of
the serial capture found only the known mand-option deprecation warning. Exact source
Linux 6.12.112 fs/namespace.c:1994–2000 uses pr_warn_once in warn_mandlock and says the
option is ignored; this is not a WARN_ON report or a layer defect (references §45).
No BUG/Oops/KASAN/UBSAN/lockdep/panic diagnostic matched in the captured output. The first
analysis attempt assumed UTF-8 and failed on binary serial bytes; tolerant decoding for
inspection reports 156 replacement characters. Raw bytes remain in the archive, so this
scan is not presented as a separate complete instrumented kernel suite.

Evidence was copied locally and SHA256-verified as day-replay-20261101-final.tar.gz:
405c490425dd2b576708038c976fb4b0f273b529fa80a02553843e8c88c4f114.
The adjacent verification JSON records manifests, reused cache identity and totals. The
archive retains programs, full cache, raw console, logs, original corpus, module and config.
No private SSH key/image is included. The first night's original report remains incomplete;
this separate successful continuation does not retroactively create a complete night.
Zero of seven complete nightly reports are accepted; S18 and successful nightly permission
integration remain open. Next scheduled window is still 23:00–05:00 with the corrected
harness; source code has not changed after its verified snapshot.

Both slot locks are free and no QEMU/manager remains. G6/corporate fronts were notified;
no new test or observer was started. The existing sole observer continues watching future
reports. Required local batch evidence is assembled for maintainer review; first S14/S17
GitHub executions and all remote publication actions still require the maintainer's approval.
Azure remains explicitly deferred.


## Second scheduled night: permissions pass, replay partial (2026-11-02)

Inspection began at 05:00 Brasília time. The November 1 report covers 23:00:01–04:59:30.
The original report, logs and cache were preserved before continuation; report existence is
not acceptance. **Zero of seven complete nights are accepted.**

The extended permission phase completed at 23:29:56 with exit 0 and its done marker.
The actual guest console contains all 72 seed passes: 20261101–20261124 on ext4, btrfs and
XFS, four mounts of 96 steps, 248,832 QUERY2 checks, three lower-step markers, and
`MOOBFS_KASAN_ONLY_PASS`. Retained boot/final dmesg has no diagnostic under `vm/kdiag.sh`'s
rules. This is a successful scheduled permission phase, not a full suite or complete night;
S15's broader complete-night integration remains pending under the previously stated scope.

Fuzzing ran 16,122 seconds, 23:31:18–04:00:02, exit 0: corpus 1,099, 459,608 executions,
manager coverage 16,502 and zero crash directories. Manager handler count 688/809 (85.0%)
remains distinct from ADR-0032 call-site coverage. Logs and phase markers agree.

Replay ran 04:00:02–04:59:30 and exited 1, with **259/1,116** programs saved, failed=0,
not_reached=857, 1,537 module-text PCs and 615/781 call sites (78.7%). The 30-second cleanup
margin worked: replay.txt, union.txt, callsites.txt and replay.exit were written before the
hard cutoff. No replay.done exists. The two last unanswered programs hit that deadline,
not a recorded completed-program failure. The saved union was independently recomputed
from all 259 cached entries and the call-site report reproduced byte for byte. All cached
program hashes belong to the 1,116 unique corpus/seed programs.

Post-run hashes cover 1,812 tracked files against `06e4a65`; the sole difference is generated
`vm/syzkaller/moobfs.txt.const`, regenerated by syz-extract. There was no per-night pre-run
manifest, so this is not a pre/post source proof. Module SHA-256 is
`299a8521106e6a3f41d88b7e2e9ff68511c82aa18d3b9348fa03fcaceb3f918e`;
kernel SHA-256 remains `4026582cc0cd60f7c22f25854fb33a21668aea40eed64eea74f726a769d40ae5`.
The new full-module hash gives a different cache namespace from the previous `a0f6ff...`
module. The saved old and new `.text` sections are byte-identical; their full module files
are not. No entries were copied across identity namespaces or accepted by weakening that
check. The reason for the full-file difference is not established here.

Raw report, corpus, logs, permission console/dmesg/module/config, replay and old module,
kernel, source manifest and the 259-entry cache were archived and copied locally as
night-20261101-review.tar.gz, SHA-256
`af96b0d8284f699d1e4834e7e9844741253af6bd37878e454222cb34085f34c0`.
The adjacent verification JSON records markers, seed counts, source limitation and independently
recomputed union/report. No private image/key is in this archive.

At 05:04:43 a single separate daytime continuation began on the immutable runtime clone,
under both slot locks (CPUs 4–5 and 6–7, two guests). Both guests booted and verified their
loaded module. The queue reports 259 reused and only 857 pending, not a rerun of completed
programs. Separate output `day-replay-20261102` preserves the original night. Source,
module/kernel/image and prior-cache manifests are captured before/after; the completion
marker is not accepted without checking them. Work deadline is 21:29:30 with outer cutoff
21:30 plus 15 seconds, before the existing 23:00 schedule. No final replay verdict yet.

Both slots were free with no QEMU/manager before reservation; peer fronts were informed.
The existing sole observer was stopped, its lock checked free, and restarted in the same
helper terminal with dated daytime-marker discovery, preserving all receipts. Exactly one
observer was verified; no test or guest was interrupted. No Azure use or main merge.
PR 181 publication/updates are authorized; the separate merge decision remains pending.


## November 2 daytime continuation complete (2026-11-02 08:19:20 Brasília)

The pending-only replay begun at 05:04:43 completed at 08:19:20, exit 0, hash checks 0.
The original November 1 night report is byte-identical to its preserved copy and remains
incomplete. This separate daytime completion does not create an accepted night: **0/7**.

Actual result: **1,116/1,116 programs**, 857 newly executed and 259 reused, zero failed and
zero not reached, 1,827 module-text PCs. The cache union was independently recomputed from
all 1,116 SHA-1-addressed program entries, with exactly the corpus/seed content hashes;
all 259 earlier cache files are unchanged. The call-site report reproduces byte for byte:
**689/781 (88.2%)**, all 23 reported functions positive, `moobl_xattr_get` 4/4. This is a
new corpus result, distinct from the November 1 daytime 1,261-program run and its 88.1%.

All 1,812 source hashes match the before-run manifest and the freshly recomputed post-run
manifest. Compared with Git `06e4a65`, only the already-generated description constants
file differs; no tracked source changed during the daytime run. Module/kernel hashes were
verified against copied binaries; image hash before/after passed on the host, but the private
image is not copied into the archive. The reused-cache checks also passed after execution.
Completion and exit markers were checked against actual logs and data, not accepted alone.

Both guest consoles retain only the known Linux `mand` mount-option deprecation warning
(previous source review in references §45); no other matching kernel/sanitizer/lockdep
finding appears in the capture. Raw serial bytes are preserved. Seventy-four invalid-UTF-8
replacement characters were used only for viewing, not to rewrite the saved console.

Evidence is retained locally as day-replay-20261102-final.tar.gz, SHA-256
`eee715f7daf3542f585b1d1acf53cb6ab9c11d4b236f6178c7894c24f6e4fa3e`, with verification JSON,
corpus/programs/cache, source and binary manifests, module/kernel/config, raw consoles,
original partial night report and independently reproduced union/call-site output. No private
SSH key or guest image is included. The source snapshot remains `06e4a65`, module namespace
`299a8521106e6a3f-4026582cc0cd60f7`; no cache identity check was weakened.

Both slot locks were free and no QEMU/manager remained at 08:20:24. Peer fronts were notified
of actual resource release; the existing 23:00 schedule and sole observer are preserved.
No new test or observer was started. This continuation spans about 3 h 15 min of calendar
machine time; engineering effort was not measured. Next: inspect the next scheduled night,
keeping both incomplete reports and acceptance criteria intact; no new PR is authorized.

## November 2 cache preflight (completed 09:37 Brasília)

A build-only control on the same immutable runtime clone rebuilt the KASAN module and
then the syzkaller module, holding slot B on CPUs 6–7 with bounded commands. Both builds
exited 0, all 1,812 tracked-source hashes stayed unchanged, and the final module is
byte-identical to the original `299a8521...` module. Thus this rebuild sequence preserves
the warmed cache identity; it does not prove that future corpus additions fit the replay
budget. No guest or replay was started, and slot B was free after the control.

A raw comparison of the archived and live corpus databases failed, so the initial control
stopped before its end marker. Follow-up unpacking found exactly the same 1,099 programs,
with identical filenames and SHA-256 contents. Database container bytes differ; the cause
is not established, and byte equality is not claimed. Both program manifests are retained.
The completed evidence archive SHA-256 is
`f5c5731b3ce2e64cb6a23a80319b0ce8fc6358bd5ad1626174989a674d6219e5`.
No cache entries were transferred between module identities. The 23:00 schedule, replay
budget and seven-complete-night criterion are unchanged. This is a build/cache preflight,
not another accepted night or a new runtime test. Calendar completion is recorded; effort
was not measured.

## Additional daytime campaign (started 2026-11-02 10:16:24 Brasília; in progress)

The maintainer requested using idle capacity immediately instead of waiting for cron. A
separate full campaign started on the immutable `06e4a65` runtime, holding both slots,
CPUs 4–7: extended permissions under KASAN (24 seeds starting 20261102, 96 steps), fuzzing
until 16:00, then replay with a six-hour reserve until 22:00. The outer deadline is
22:00:15 plus a 15-second kill grace; the existing 23:00 cron remains unchanged.
Permissions execution was observed; no phase result or overall acceptance is claimed yet.
Source manifests and exit markers are captured by the private host driver.

Output is separate in `day-campaign-20261102/reports`. The unmodified runner assigns
its pre-noon internal ID `2026-11-01`; this does not touch the original night directory
or make this daytime campaign an accepted calendar night. The cache identity and shared
slot locks are preserved. The seven-night criterion is unchanged.
The G6 owner also resumed a complementary second-Debian guest build and offline-tool
validation, after actual host inventory; no module load or mounted G6 campaign is authorized
by this task. Results and completion remain pending. No new publication is authorized.

## Maintainer scheduling/acceptance amendment (2026-11-02)

The original S18 text required a week of nightly reports. The maintainer now directs
acceptance by seven complete campaigns, with comparable workload, at any hour and on
coordinated dedicated hosts in parallel. There is no night-time-dependent property in
these tests; waiting for calendar nights wasted idle capacity. This replaces the earlier
calendar-night restriction, including the statement above excluding daytime campaigns.
The active daytime campaign becomes eligible after audit. Partial original reports remain
unaltered, and prior composite campaigns are not counted without auditing their full
phase/evidence chain. Resumptions, replay continuations and worker shards never add a
campaign. No runtime result is newly accepted by this decision alone. The roadmap, test
guide and local map/rules/pending list reflect the amendment; published main/GitHub/site
remain at their dated checkpoint until a separate publication authorization. No kernel,
UAPI, freeze, Azure or mounted G6 gate changed. The G6 owner is asked to prepare isolated
second-host S18 capacity while the current Mac mini campaign continues unchanged.

## Corrected campaign preparation and explicit windows (2026-11-03)

Journal 0259 records the partition-accounting correction: old-code negative control,
focused corrected KASAN and both full native/KASAN suites passed with reviewed artifacts.
The two earlier complete campaigns remain warning-bearing, not newly approved as clean.

The original calendar-derived end could not start an equivalent daytime campaign after
noon. Two optional inputs now allow an explicit epoch deadline and a positive fuzzing
duration cap, while retaining the original defaults and corpus/cache identity checks.
Five Linux orchestration controls passed using fake phase tools (no guests/fuzzer):
explicit deadline with/without cap, malformed deadline, zero cap and expired-window
partial verdict. ShellCheck and shell syntax passed. This scheduling-only change does
not alter the kernel/UAPI tested in `4f17fb4`. A separate corrected runtime and private
cache/image/syzkaller copies are being prepared; no long corrected result is claimed.

## First corrected long campaign (2026-11-03; started 13:03:36 Brasília)

Snapshot `61ee4f7f40ce688c5c8c9bf8dd191e97e1865d13` contains the explicit-window runner;
its kernel, UAPI, C regression and suite contents are identical to full-suite-tested
`4f17fb4`. Source archive SHA-256:
`b3474a10d32deeb0db6bbfa9e573e0deaef4908c5116fe2e51789e0c7addac96`.
Preparation exited 0 at 13:02:44 in a separate source/syzkaller/cache/image copy. All
1,815 tracked sources were checked; only extracted constants differ from the archive,
matching the prior runtime constants. The original source/image/caches are retained.

The starting archived corpus hash is
`3dd6d3ffbd1a0df8fae9cc2d06e4eac456c6a15436bf5fb19af310ff8203502b`.
No results were copied from the old module's cache namespace. The corrected syzkaller
module SHA-256 is `f95469448e85bf5e751c66415062b40231170330aabedadb85b589f3f917d145`,
with the unchanged kernel `4026582cc0cd60f7c22f25854fb33a21668aea40eed64eea74f726a769d40ae5`.

The extended KASAN permission phase passed at 13:31:41: seeds 2026110301–2026110324
on all three lowers, four mounts of 96 steps, 248,832 QUERY2 comparisons, no kernel
diagnostics. Its raw consoles/dmesg/module/config were copied into the campaign directory
before another run could overwrite the shared build output. Fuzzing began 13:32:43 for
18,000 seconds, ending about 18:32:43, then full corpus replay. The explicit cutoff is
November 4 at 03:00 with a 15-second outer margin and 15-second kill grace. At 14:17:28,
81,265 executions, corpus 1,159 and zero crash directories were observed; no final clean
verdict is inferred. Both slots, CPUs 4–7, remain reserved.

The scheduled wrapper now uses the corrected source and private cache/image. Its previous
bytes are backed up; nonblocking slot locks skip a scheduled start during an active
campaign. The 23:00/@reboot entries remain; the fallback window extends to 07:00 with
three hours reserved for replay and a five-hour fuzz cap. The daytime campaign has its
own absolute deadline. The sole existing observer already discovers the dated campaign
marker; no new monitor or test duplicate was started. No publication is authorized.

### 2026-11-03 — corrected campaign r01 completed and accepted (1 of 7)

The daytime campaign r01 on the Mac mini (source `61ee4f7`, CPUs 4–7, two guests) ended at
23:07:53 Brasília time with `runner_exit=0`; the audit was done before acceptance:

- Phases: permissions exit 0 (13:03–13:31, KASAN/UBSAN/lockdep config verified in the saved
  `kernel.config`, `MOOBFS_KASAN_ONLY_PASS`), fuzzing exit 0 for the full 18,000 s
  (13:32:43–18:32:46), replay exit 0 (18:32:46–23:07:51).
- Fuzzing: corpus 1,190, coverage 16,734, 420,211 executions; the manager work directory has
  no `crashes` directory and the report lists `crash_dirs=0`.
- Replay: 1,207/1,207 programs replayed, run now 1,207, failed 0, not reached 0; 693 of 781
  call sites (88.7 %); 1,853 module text PCs.
- Consoles: no `BUG:`, KASAN, UBSAN, lockdep, Oops or call-trace line in the permission or
  replay consoles. The single `WARNING:` match is the kernel's deprecation notice for the
  `mand` mount option, triggered by a fuzzed `mount` call; it is not a kernel diagnostic.
- Integrity: the source hash lists before and after are byte-identical (1,815 files), and the
  syzkaller module (`f9546944…`) and kernel image (`4026582c…`) hashes are unchanged.

r01 is the first accepted complete corrected campaign: **S18 stands at 1 of 7**. Round r02
runs on the Debian host from `a1f313b` (identical kernel code) with permission profile
`24,96,2026110302` and an absolute deadline of November 4 at 10:30.

### 2026-11-04 — campaign r02 on the Debian host completed and accepted (2 of 7)

r02 (source `a1f313b`, kernel code identical to r01, CPUs 4–7, two guests, permission profile
`24,96,2026110302`) ran from 20:45:19 to 05:18:26 Brasília time with `runner_exit=0`:

- Phases: permissions exit 0 (20:45–20:56; `MOOBFS_KASAN_ONLY_PASS`, kernel config with
  KASAN, UBSAN, LOCKDEP and PROVE_LOCKING; the xfs step alone reports 82,944 QUERY2 answers
  equal to the lower's decision), fuzzing exit 0 for 18,000 s (21:09:59–02:09:59), replay
  exit 0 (02:09:59–05:18:26).
- Fuzzing: corpus 1,288, coverage 16,651, 697,413 executions; no `crashes` directory,
  `crash_dirs=0`.
- Replay: 1,305/1,305 programs, failed 0, not reached 0; 688 of 781 call sites (88.1 %).
- Consoles: the replay consoles' only `WARNING:` lines are the `mand` deprecation notice;
  the permission-phase console had no diagnostic line when scanned at 05:27. That raw
  console was then overwritten by r04, which reuses the same build directory; the phase
  verdict lines remain in the r02 `permchurn.log`.
- Integrity: the driver reported `source_hashes=1`. The only mismatching file of 1,819 is
  `vm/syzkaller/moobfs.txt.const`, which the runner regenerates from the descriptions at
  fuzz start (21:09:59); the regenerated file is byte-identical to r01's runtime constants
  (`35478995…`). The r02 driver recorded no binary hashes; the module (`e5d4f245…`) and kernel
  image were last written at 21:05:58 and 21:05:54, before fuzzing started, and not after.

r02 is accepted with those two recorded limitations: **S18 stands at 2 of 7**. The r04 driver
on the Debian host excludes the generated constants file from the source check, compares it
with the reference hash, and records binary hashes and timestamps before and after.

### 2026-11-04 — round r04 interrupted; restarted as r04b

r04 started on the Debian host at 05:28:10. The host's journal stops at 05:30:34 with no
shutdown sequence, and the host booted again at 05:57; r04 was still building for its
permission phase and left no result. It is not counted. After the restart all source files
matched their reference hashes; r04b started from scratch at 06:24:05 with the same
parameters and a deadline of 18:30. The permission phase rebuilds the module against the
KASAN kernel and the fuzz phase rebuilds it for the syzkaller kernel, so the before/after
module hash of the r04 driver differs by design; the audit compares the binaries' write
times with the fuzz start instead.
