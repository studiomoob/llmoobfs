# 0253 — Batch 11 local integration and complete-suite validation

Date: 2026-10-31. Status: **local integration and full-suite validation passed; batch publication pending**.
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
