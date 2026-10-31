# 0253 — Batch 11 local integration and complete-suite validation

Date: 2026-10-31. Status: **in progress**. Work started about 13:17 Brasília time
(UTC-3); end pending. Calendar time, not measured engineering effort. Estimated effort: S
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
