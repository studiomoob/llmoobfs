# 0253 — Batch 11 local integration and complete-suite validation

Date: 2026-10-31–2026-11-02. Status: **PR 181 merged with maintainer approval; S14/S17 validated; remaining nightly work is open**.
Work started about 13:17 Brasília time (UTC-3); this validation unit ended about 15:29. Calendar time, not measured engineering effort. Estimated effort: S
for integration, plus machine time for the complete suites and long S2 runs.

## Goal and decisions

Consolidate the three batch-11 branches locally and validate the combined head before
publication: S14/S17 CI (`7a967d5`, journal 0247), randomized permissions (`e6b0a96`,
journal 0250), and long-run/replay/nightly harnesses (`e839590`, journal 0248).
The maintainer authorized these local merges and the complete VM and KASAN suites.
Push, PR, merge to main and remote public edits remain subject to separate approval.
S18's seven reports are its completion criterion, not a prerequisite for publishing its
harness with batch 11; S18 remains open while reports accumulate.

## Integration

Dedicated worktree `MoobFS-b11`, branch `claudiofp/BATCH11`, based on main `302bff0`.
Local merge commits preserve all three histories. Conflicts affected only CHANGELOG,
the history index, references and security-roadmap status. Keep all contributions, references
43–45 in sequence, and journals 0247, 0248 and 0250. The separate corporate worktree has
journals 0246, 0249, 0251 and 0252 and is preserved, not integrated in this batch.

An initial automatic resolution truncated documentation and failed while ordering the
reference sections. The following merge restored the four files from the preserved main
and branch heads, keeping the original changelog body, full history index and all references.
The intermediate local merge remains in history; it is not a validated head. Final checks
confirmed no conflict markers and no shortened base documents.

Comparisons on the consolidated head: `layer/`, `kernel/`, `include/` and `common/`
unchanged from main; permission test and executable VM harnesses identical to S15; replay,
nightly and classifier code identical to the long-run branch. No production kernel change.

## Evidence before complete suites

- Focused S15 KASAN on `0512db7` finished at 12:54:47 Brasília time, exit 0,
  `MOOBFS_KASAN_ONLY_PASS` without kernel diagnostics according to the harness (journal 0250).
- Consolidated workstation: `make test` (60 attribute checks, DIAG_TEST_PASS, nine image
  tests), three replay controls, retired-claims, offline pins, FAQ and site checks passed.
- Full VM and full local KASAN suites on the consolidated snapshot: pending. They run
  sequentially on CPUs 0–3, preserving the existing syzkaller build and XFS on CPUs 4–7.

## Remaining work and limits

Inspect complete-suite results and final consoles; preserve logs outside Git. S2 auto
results, real replay smoke and nightly timer installation remain pending. The dedicated
host already has one-shot queues for replay then ext4, and btrfs after 05:15 on November 1
and the XFS run. First GitHub mutation and gcov jobs require an authorized PR. Seven nightly
reports remain necessary for S18; no night is counted from an offline control or schedule.

## Complete-suite launch and scheduling preparation

Frozen source: `569ec2a`, transferred by a Git bundle and cloned into two independent
working copies on the dedicated host (`b11full/vm-src`, `b11full/kasan-src`). The bundle
was verified from the cloned repository; the first verification outside a repository was
rejected by Git, then rerun correctly. Both copies retain the exact branch head and tracked
file manifests. No running worktree was overwritten.

At 13:18:58 Brasília time, a detached driver began the full
`MOOB_VM_KVM=1 MOOB_DEBIAN_KERNEL=6.12.113 make vm-test`. An external Docker wrapper adds
`--cpuset-cpus=0-3` at container creation, leaving the tracked `vm/run.sh` unchanged; the
container's CPU set was inspected. The full local `sh vm/kasan-local.sh` is queued after
that run, also pinned to CPUs 0–3. The driver records separate exit codes, start/end times,
and post-run checks of all tracked source hashes. No success is claimed until both finish.

A host-only installer is queued behind those two successful suites and the real replay
smoke. It uses an independent clean clone of `569ec2a` for nights, preserves the existing
crontab in a private backup, and adds named 23:00/reboot entries only after validation.
Every entry acquires both daytime slot locks; the wrapper sets America/Sao_Paulo and
CPUs 4–7. Cron is active. At this checkpoint the installer is waiting, and the nightly
schedule is still not installed. Host scripts passed shell syntax and ShellCheck.

Actionlint 1.7.7 passed the two new CI workflows after consolidation. The complete `make lint`
on the dedicated snapshot is being captured separately. No workflow has run on GitHub.

Checkpoint at about 13:22 Brasília time: complete `make lint` on the frozen dedicated-host
snapshot passed (ShellCheck over all tracked VM/scripts shell files, then SPDX; exit 0).
The full Debian guest reached `QUERY2_PASS` and `ISOLATION_PASS` on ext4; it is still running.
The existing XFS auto guest reached `generic/127`. No final complete-suite, fstests or replay
verdict exists yet. The later integration commits update only this journal; executable code
remains identical to the frozen source `569ec2a`.

## SQLite observation during the complete Debian run

At about 13:40 Brasília time the rollback-journal concurrent reader reported
`database is locked (5)` once. All six final variants had 200 rows, `integrity=ok`,
no missing rows and status 0; `SQLITE_CONCURRENT_OK` and `ACCEPTANCE_CLIENTS_PASS` followed.
The helper configures `.timeout 10000`, prints reader errors without propagating them,
and returns the final row-count test; rollback and unix-excl variant statuses are informational
by the existing acceptance contract. No read-latency or zero-contention-error guarantee is
claimed. The timeout duration of this individual read was not measured.

SQLite's official `sqlite3_busy_timeout()` and `sqlite3_busy_handler()` documentation
(Context7 `/websites/sqlite_docs`, two commands including resolution) permits SQLITE_BUSY
after the configured wait and, to avoid a deadlock, sometimes without invoking the handler.
This observation is compatible with documented lock contention; it does not establish a
module defect. References and testing limits clarified; no executable change or running
snapshot modification. The kernel build is linking, and the complete VM suite continues.

The marker table's former wording also overstated the helper's return status as checking
`integrity_check`. A dated correction now says that it checks the final row count and prints
integrity/missing-row diagnostics for separate inspection; the old claim is retired. The
actual six outputs in this run were all `integrity=ok` and `missing=[]`.

## Focused KASAN evidence archived

The final focused logs and source manifest were copied outside Git to the local review
archive `s15-focused-kasan-final.tar.gz`. All 1,796 tracked files match snapshot `0512db7`
byte for byte after the run. Console inspection confirms three `PERMCHURN_STEP_PASS`
markers, 6,912 QUERY2 checks per lower, `MOOBFS_KASAN_ONLY_PASS`, and no VM failure/environment
failure marker. These counts belong to the focused run, not to the still-running full suite.
The full-suite timeout description in testing.md was also corrected from 2,400 to 3,600
seconds to match the existing `vm/in-container.sh`; executable code is unchanged.

## Full Debian suite passed; replay smoke passed (14:19 checkpoint)

On 2026-10-31 the full Debian KVM run finished at 14:09:18 Brasília time, exit 0, after
about 50 minutes including builds (guest time about 48 minutes). The console has all three
required final markers: MOOBFS_VM_PASS, MOOBFS_ACCEPTANCE_PASS and MOOBFS_LAYER_PASS.
All 1,810 tracked source hashes match snapshot `569ec2a` after execution. The full local
KASAN run began at 14:09:19 and is still running; its result is not inferred from this run.

The syzkaller build finished at 14:02:29 Brasília time, exit 0. Real replay smoke finished
at 14:05:13, exit 0: all three seeds replayed, no failed/unreached program, 911 module-text
PCs and 341/781 handler call sites (43.7%). This is a three-seed smoke, not a corpus-coverage
measurement and not a change to the S3 result. ext4 auto started at 14:05:41; XFS auto had
reached generic/166 at 14:17. The nightly installer still waits for the full KASAN result.

Checkpoint, 2026-10-31 15:20 Brasília time: full KASAN remains active. The console records
RELEASE_PASS and the six-default-seed PERMCHURN_STEP_PASS on all three lowers (5,184 QUERY2
checks per lower), followed by IXLOCK_PASS. No VM_FAIL/ENV_FAIL marker was found; the final
KASAN marker is still pending. XFS auto was running generic/187 and ext4 generic/133; no
`.out.bad` files were present in either results directory at inspection. This is progress,
not a final fstests verdict. The nightly installer and btrfs queue remain waiting.

## Coordination continuity (2026-10-31, about 15:27 Brasília time)

The maintainer authorized the three existing instances to communicate and continue their
assigned work. Batch 11 retains ownership of its dedicated host, frozen test copies and
journals 0247/0248/0250/0253. The corporate campaign retains its separate shared host and
journal 0255; G6 will renumber its isolated colliding records before any integration.
No additional workload was started on either reserved host by this coordination.

A local read-only marker notifier was prepared outside Git to prompt the existing batch-11
terminal after the complete suites, each fstests lower and the first nightly report settle.
It records one send receipt per event; an uncertain send stops instead of submitting twice.
The first local nohup attempt did not stay alive; it was replaced by a persistent Orca shell
terminal, observed running Python. This is a notification helper, not another test runner or
AI instance. Test verdicts still require inspection of actual logs and source hashes.
No push, PR, merge to main or public edit is authorized by the communication decision.

## Full KASAN passed and nightly schedule installed

The full KASAN/UBSAN/lockdep suite on frozen snapshot `569ec2a` finished at 15:27:27
Brasília time on 2026-10-31, exit 0. Required markers: MOOBFS_ACCEPTANCE_PASS,
MOOBFS_LAYER_PASS and MOOBFS_KASAN_PASS. Fault injection: 22 operations, 325 calls,
303 injected failures, unexpected=0; RENAMEPATH_FAULT_PASS and DEADSCAN_PASS. The saved
dmesg passed the diagnostic scan. All 1,810 tracked source hashes match before/after in
each full-suite copy and match the Git snapshot. The final archive and verification JSON
are outside Git under the local review directory, `full-suites-569ec2a-final.tar.gz` and
`full-suites-569ec2a-verification.json`.

Instrumentation limits: CONFIG_KASAN, CONFIG_UBSAN and CONFIG_PROVE_LOCKING are enabled;
CONFIG_MEMCG is absent in this instrumented kernel. Its MEMCG, LIMITS_MEMCG and DIAG_MEMCG
cases therefore skipped. The full Debian run separately passed MEMCG_ACCOUNT_PASS,
LIMITS_MEMCG_PASS and DIAG_MEMCG_PASS. The Python backend step passed in KASAN
(MOOBFS_PYLIB_PASS), and was skipped in the Debian initramfs without Python as documented.
No skipped case is counted as exercised by the instrumented run.

At 15:28:17 the gated installer added and verified the requested 23:00/reboot crontab
entries, with a private backup and byte comparison. Nights use an independent clean Git
clone of `569ec2a`, CPUs 4–7, America/Sao_Paulo, and both daytime slot locks. The source
is frozen; the first night has not run. Scheduling does not satisfy S18's seven-report
criterion or S15's longer nightly campaign.

Only the ext4 and XFS fstests guests remain active on the dedicated host. CPUs 0–3 are
no longer used by full KASAN, but the host disk remains reserved to S2. Other instances
were informed; no workload was started on the separate corporate host. The notifier's
first prompt was delivered and its receipt retained; it continues waiting for remaining
events. No publication or remote repository change. Local validation took about 2 h 12 min
of calendar time, dominated by serial complete suites; engineering effort was not measured.


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


## Coordinated unattended continuity (2026-11-01)

The maintainer authorized continued use of the two local project hosts during his absence,
with resource coordination across the existing fronts. Azure decisions are explicitly
postponed; the inventoried cloud VM remains deallocated. No cloud start/create/resize,
publication authorization or implicit change to the freeze/batch order follows from this.

Btrfs retains slotB and the pending-corpus replay retains slotA. At 10:51 the same btrfs
QEMU remained active (console generic/476), and the replay guest was up since 10:40:54.
G6 was actively resumed in its existing session for nonintrusive recovery/identification
of the new Debian host, verified G5 assets, launcher adaptation and offline preparation
that can progress while access is unavailable. The prepared guest and corporate evidence
remain preserved; no mounted G6 experiment is authorized ahead of the security batches.
The corporate front retains its separately authorized research/materials work and will
coordinate any new host workload rather than restarting its old guests independently.

The same sole marker observer was updated to keep watching later per-date nightly reports,
besides btrfs/daytime replay, throughout a bounded 48-hour window instead of exiting after
the initial events. Existing receipts prevent duplicate submissions. Its old process was
stopped, free lock checked and one replacement PID 16326 confirmed in the existing helper
terminal. No additional observer or test runner was added. Each new report triggers review,
not automatic acceptance. Local rules, pending list, host registry and HTML map were updated
under the agreed lock. Next: verify real completion evidence, record findings and keep the
next available authorized work ready for maintainer review without publishing.


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


## Local validation checkpoint complete (2026-11-01 17:22 Brasília time)

The complete corpus replay passed: 1,261/1,261 programs, 1,001 newly replayed and 260 reused,
zero failures/unreached, 1,843 module-text PCs, 688/781 call sites (88.1%). All 1,812 source
hashes match `06e4a65` before/after; cache union and call-site reports were recomputed, and
all 260 old cached entries match their earlier archive. Journal 0248 records raw evidence,
the known mand-option warning and binary-console decoding limitation. Both slots are free;
future scheduled jobs and the existing observer are preserved. Daytime continuation does
not turn the first incomplete report into an accepted night.

Local batch matrix: full Debian VM and full KASAN passed on `569ec2a`; focused expanded and
long permissions passed; negative controls retained; quick/auto exclusion reruns passed on
all three lowers; five replay controls, two negative controls, real bounded/resumed replay,
ShellCheck and this full corpus replay passed. After `06e4a65`, local changes are records,
site content/generator data and per-test result artifacts; no production/module or replay
runtime source changed. Required local evidence is ready for review, not a claim that new
GitHub CI ran. S14/S17 GitHub jobs remain unexecuted; S18 complete-night accumulation and
nightly permission integration remain open as explicitly documented.

The local validation checkpoint spans about 28 h 5 min of calendar time from integration
start (mostly machine time/waits); engineering effort was not measured. No push, PR, main
merge or publication occurred. The next maintainer decision is publication of the prepared
batch, with remaining CI/nightly limitations visible; Azure and S16/freeze decisions remain
separate. A concrete review synopsis is retained outside Git in the local review directory.


## Publication authorized: PR 181 (2026-11-01)

After the local review checkpoint, the maintainer authorized pushing this branch and opening
one batch PR; merge approval remains separate. origin/main was fetched and remains `302bff0`.
`git rebase --rebase-merges origin/main` preserved final head `4f2e134`; the worktree was clean,
all commits carried DCO, and layer/kernel/include/common matched main. Prepublication portable
`make test`, checkpatch, five replay controls, retired claims, FAQ/site and whitespace checks
passed. A mistaken journal filename in the private draft was caught before publication and
corrected; the public links were checked against actual files.

Branch claudiofp/BATCH11 was pushed without force and PR 181 opened against main:
https://github.com/studiomoob/llmoobfs/pull/181 . Scope excludes the separate G6/corporate branches.
The new Mutation testing and Branch coverage workflows started for the first time on the PR,
along with required CI/KASAN and existing analyses. Initial run links:
- Mutation: https://github.com/studiomoob/llmoobfs/actions/runs/41347762160
- Coverage: https://github.com/studiomoob/llmoobfs/actions/runs/41347762118
- KASAN: https://github.com/studiomoob/llmoobfs/actions/runs/41347762042
- CI: https://github.com/studiomoob/llmoobfs/actions/runs/41347762103

These are started executions, not passed-check claims. Live/latest results are on the PR's
Checks tab; documentation-only publication records may supersede initial runs through the
existing concurrency settings. No protection setting, issue closure, merge, Azure operation
or unrelated publication was performed. The PR explicitly retains all local evidence limits,
first-night incompleteness, S18 open status and pending nightly permission integration.
Next: inspect exact-head checks/logs, correct any batch-scoped failure, and obtain separate
maintainer approval before merge. Local source-validation snapshots remain immutable.


## First PR checks and coverage correction (2026-11-01, about 18:52 Brasília time)

On head `865ef4d`, 18/19 checks passed; only Branch coverage failed. Mutation artifacts
confirm baseline plus all 23 kills; the separate full KASAN artifact confirms its final pass
marker and no kernel diagnostic under the harness rules. Journal 0247 records exact run links,
artifact limits, the instrumented suite's successful exit and LCOV's negative counter failure.
The reporting failure is not attributed to a filesystem defect.

A bounded user-space counter control loses updates with `single` and counts every call with
`atomic`. The fix changes only coverage instrumentation: atomic profile updates for that
job's module, retained compiler notes/config/toolchain/hashes, and a non-zero branch-total
check. No production source or normal build changed. Actionlint, pin checks and the concurrent
positive/negative control passed; updated CI remains pending. S17 and S18 stay open, and the
first incomplete night stays 0/7 accepted. Existing host jobs and observer were preserved.
Push/update of this branch and PR 181 is authorized; merge still requires separate approval.


## Saved-data coverage recapture; pinned tool awaiting CI (2026-11-01, about 19:33 Brasília)

Head `d5f2b30` again passed 18/19 checks; KASAN and mutation artifacts were downloaded and
verified, including all 23 kills and baseline. The coverage suite passed and its raw GCC JSON
has no negative line/branch counts, but LCOV 2.0 failed to merge TRACE_EVENT branches at the
same header line. Notes, tested synthetic-merge parents and ten available source/notes/config
hashes were checked; absent binary/generated-source bytes remain an explicit artifact limit.

The same saved inputs reproduce the old parser failure and complete capture/summary/list/HTML
with the official LCOV 2.6 release: 2,241/3,120 branches, 3,264/3,461 lines. This is an offline
recapture of CI data, not a successful CI reporting job. No exclusion, ignored error or counter
rewrite is added. Journal 0247 records the failed alternative, remaining warning and exact pin.
The coverage workflow now installs that SHA-256-pinned release. Offline checks passed before
updating the same authorized PR; new CI validation remains pending, as do S17 and S18. Existing
host jobs/observer are untouched; no main merge or unrelated publication is authorized.


## CI validation accepted; final documentation synchronization (2026-11-01, 20:29 Brasília)

All 19 checks on `1071845` passed. Journal 0247 records independently checked artifacts:
mutation baseline plus 23/23 kills, full KASAN/lockdep, and valid CI LCOV/HTML output with
2,250/3,120 branches (72.1%) and 3,274/3,461 lines (94.6%). The real tested synthetic-merge
parents, ten available source/notes/config hashes, 95 artifact hashes and actual markers
were checked. Explicit limitations include absent binary/generated-source bytes and LCOV's
retained non-zero unexecuted-block warning. No ignored errors, exclusions or counter rewrite.

S14 CI and S17's report acceptance are complete. The first incomplete night remains 0/7;
S15 nightly integration/S18, S16's freeze decision and the later items retain their existing
status. This final record changes documentation only. Its head must pass required checks
before requesting the separate main-merge approval; no further documentation-only push is
needed solely to record the unchanged validation payload's new check IDs. Existing host
jobs/observer are preserved. Push/PR181 updates remain authorized; merge is not authorized.


## Second night reviewed; daytime continuation active (2026-11-02, about 05:07 Brasília)

The November 1 report is partial, not an accepted night. Scheduled long permissions passed
all three lowers under KASAN/UBSAN/lockdep (72 seeds, 248,832 checks); fuzzing exited 0 with
459,608 executions and no crash directory. Replay now saves a correct partial verdict before
the cutoff: 259/1,116, failed=0, not_reached=857. Original evidence preserved and archived;
source post-hashes, cache union and call-site report checked, with no pre-night manifest.
Journal 0248 records exact identities and limitations. S18 remains 0/7; broader S15 complete-
night integration stays open despite the successful permission phase.

A single daytime continuation started 05:04:43, both reserved slots, two guests verified
booted, 259 cache hits and only 857 queued. No existing test/guest was interrupted or completed
program intentionally repeated. Original night remains unchanged; source/binary/cache hashes
will be checked after continuation. Existing sole observer extended in place to dated daytime
markers, receipts retained, one process verified. Peer fronts informed; 23:00 schedule intact.
This new experiment record changes documentation only; the last tested PR head `ca9321e`
passed 19/19, and any subsequently published record head must pass its own required checks.
Main merge and Azure remain unauthorized. Next: inspect daytime completion evidence and
obtain the separate maintainer decision on PR 181; no automatic approval inferred.


## Authorized merge completed; nightly continuation preserved (2026-11-02, 07:03:03 Brasília)

The maintainer explicitly approved merging PR 181 using a merge commit, with nightly
limitations still open. A fresh preflight confirmed head `8686b116b9bd8030ac29fc29d97b1858c363a72b`,
base `302bff0`, all 19 successful checks, and OPEN/MERGEABLE/CLEAN. The merge used
`gh pr merge 181 --merge --match-head-commit 8686b116b9bd8030ac29fc29d97b1858c363a72b`
with the technical validation summary in its body, without admin bypass, squash or branch
deletion. GitHub confirms MERGED at 10:03:03 UTC (07:03:03 Brasília), merge commit
`e59a244f1282994ed1a8cd21d53fb34dbe235f3c`. Its two parents are the checked base and head;
its tree is identical to the reviewed head. The clean local main checkout was fast-forwarded
to that commit. No shared history was rewritten.

The same existing observer now follows checks on the authorized merge commit as well as
nightly/daytime markers; exact commit identity is enforced. Its prior process was stopped,
the single-instance lock verified free, and the same helper terminal reused. No running VM
or test was interrupted and no second monitor was created. Automatic main workflows, including
Pages publication, are consequences of the approved merge; no separate deployment command,
release, outreach or new PR was issued.

The daytime replay remains active in both dedicated-host slots. At 06:57:48, 500 new programs
had completed in addition to 259 reused entries, with no final verdict. S18 remains 0/7 complete
nightly reports accepted; the separate continuation does not rewrite either incomplete night.
S14/S17 acceptance remains met. Kernel freeze, format/UAPI and all later decision gates remain.

This post-merge journal/status record is a small local follow-up commit prepared at about 07:07
Brasília. The current-state introduction of `docs/security-roadmap.md` and this journal's
status heading are updated locally; their dated pre-merge source checkpoints in main remain
until the next authorized integration. No follow-up PR or direct main push is authorized.
The public merge outcome is recorded immediately in PR 181 and the merge commit itself;
local pending list, handoff and HTML map are synchronized under their shared lock. Existing
G6/corporate fronts are informed of integration without changing their priorities or reservations.


## Post-merge checks and Pages verified (2026-11-02, about 07:17 Brasília)

All 13 automatic checks on merge commit `e59a244f1282994ed1a8cd21d53fb34dbe235f3c`
completed successfully. Exact-head run metadata was checked for CI
[41425610258](https://github.com/studiomoob/llmoobfs/actions/runs/41425610258), static analysis
[41425610114](https://github.com/studiomoob/llmoobfs/actions/runs/41425610114), CodeQL
[41425610188](https://github.com/studiomoob/llmoobfs/actions/runs/41425610188), Scorecard
[41425610100](https://github.com/studiomoob/llmoobfs/actions/runs/41425610100), and Pages
[41425610101](https://github.com/studiomoob/llmoobfs/actions/runs/41425610101).
Both native-VM artifacts contain VM/ACCEPTANCE/LAYER pass markers and no VM/environment
failure or captured kernel diagnostic under the harness rules. This is the main-push
workflow set, distinct from the 19 pre-merge PR checks; no manual test rerun was started.

Pages build/deploy succeeded automatically. The downloaded Pages tar's 18 files, including
`.well-known/security.txt`, match all corresponding source files at the merge commit by
SHA-256, with no extra or missing file. The public homepage returned HTTP 200 and exactly
matched the merged `site/index.html` hash. Artifact hashes, console markers, run metadata,
public response hash and Pages log are retained locally in github-merge181-e59a244, with a
verification JSON. No separate publication or deploy command was issued.

The existing observer's now-complete GitHub watch is archived by moving its configuration;
its loop and receipts are preserved and it continues the dedicated-host day/night markers.
No second observer, new guest or host workload was started. At 07:15:18 both replay slots
were still held; the latest milestone was 550 new programs at 07:09:04 plus 259 reused.
There is no final replay verdict and no resource release. S18 remains 0/7 accepted nights.

This and the history index's corrected merge status remain local follow-up records under
the previously stated publication limit. `docs/history/README.md`, this journal's updated
status and `docs/security-roadmap.md`'s post-merge introduction will accompany the next
authorized integration; no new PR/direct main push is authorized merely to record these
results. The existing merged PR's summary and local map/pending list are updated now.


## Daytime replay approved; lab access restored (2026-11-02)

The November 2 pending-only continuation completed 08:19:20 Brasília, exit 0/hashes 0.
The independent audit confirms 1,116/1,116 programs (857 new, 259 unchanged cached entries),
zero failed/unreached, 1,827 module-text PCs and 689/781 call sites (88.2%). All 1,812 source
hashes are identical before/after; the sole difference from the Git snapshot is the prior
generated constants file. Cache union, program membership, archived module/kernel hashes,
image before/after checks and call-site report were reviewed. Journal 0248 records archive,
raw-console decoding and diagnostic limits. No full nightly report was retroactively accepted:
S18 remains 0/7. Both slots are free and no test VM remains; peer fronts were informed.

Separately, the second laboratory host's SSH access was restored without changing its
firewall, sshd, routes or Wi-Fi connection. A bounded packet investigation isolated failed
broadcast ARP delivery: source requests are seen by the wired management host but not at
the destination station. Genuine unicast ARP requests between the verified endpoints are
answered and restore direct SSH with the existing host key. The precise source of broadcast
filtering (upstream AP/router versus receive driver) remains unconfirmed; no permanent
network-policy fix is claimed. Power saving was off and firewall/ARP checks found no rule
explaining the failure. The only diagnostic package installed was tcpdump; captures were
bounded and recorded ARP headers only. A private SSH fallback through the existing management
host was tested, without forwarding an agent or copying key material. No VM/workload was
started on that host. Detailed addresses, packets, screenshot and private access procedures
stay outside Git; existing campaign priorities and authorizations are unchanged.

These result records remain local on the follow-up branch until a new integration is
explicitly authorized. No new PR, additional merge, release or Azure action is inferred.
The existing merged PR summary and local map/pending list reflect the new verified result.
