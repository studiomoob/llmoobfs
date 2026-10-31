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
