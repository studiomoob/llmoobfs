# Roadmap

A sequence of research milestones, not a delivery schedule. Items are grouped by
their state of evidence. "Done, with evidence" means a journal entry records the
tests that actually ran, on which kernel and lower filesystem; it never means
production readiness. Nothing here is a compatibility commitment.

## Done, with evidence

The early rows describe the milestone when it was tested; their "What was not" cells are
historical gaps, some closed by later rows or the completed work below. Current security gaps
are tracked in [security-roadmap](docs/security-roadmap.md).

| Item | What was tested | What was not | Evidence |
| --- | --- | --- | --- |
| M0 — Project memory | Versioned journal, ADRs, shared AI instructions, Git history, GitHub replica | — | [0001](docs/history/0001-origin.md), [0002](docs/history/0002-native-read-milestone.md), [0003](docs/history/0003-open-source-github.md) |
| M1 — Native shared reads (read-only image) | Image creation, mount in QEMU, distinct inodes sharing physical bytes (FIEMAP), reads, read-only enforcement, 15 malformed images, remount; kept as a regression in every `make vm-test` | Writes, other kernels, KASAN on the Debian kernel | [0002](docs/history/0002-native-read-milestone.md), [format](docs/format-m1.md), [ADR-0003](docs/decisions/0003-readonly-milestone.md) |
| M1 hardening | Frozen as a completed experiment by [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) | Its original exit criteria (fuzzing of the image parser, lifecycle tests) were not completed before freezing | [0010](docs/history/0010-adr-0007-stacked-layer.md) |
| M2 phase 2a — Minimal stacked pass-through | `moobfs_layer.ko` over ext4 in the VM; pass-through acceptance test; 23,000 `fsx` operations | btrfs, XFS | [0011](docs/history/0011-phase-2a-stacked-passthrough.md), [spec](docs/spec-m2.md) |
| M2 phase 2b — Full pass-through | Rename, links, symlinks, `nolinks`, opaque xattr, delegated reflink/dedupe/`copy_file_range`, FIEMAP over ext4, btrfs and XFS | xfstests | [0012](docs/history/0012-phase-2b-full-passthrough.md) |
| M2 phase 2c — Typed attributes | `user.moob.*` validated in the kernel (shared `common/attr.c`), `moobfs-attr`, `moobfs-check`, host unit tests, libFuzzer target | Fuzzing beyond 30 s per CI run | [0013](docs/history/0013-phase-2c-typed-attributes.md) |
| M2 phase 2d — In-memory index and `QUERY` | Index built at mount and kept current (since ADR-0012, per domain on first query); `QUERY`/`GET_INFO` (UAPI v1); kernel answers identical to the `moobfs-index` oracle over the three lower filesystems, after rename, unlink, removexattr and remount; `noindex`, `index_budget` | Budget exhaustion on large trees; persistence | [0014](docs/history/0014-phase-2d-index-query.md) |
| M2 phase 2e — Acceptance with real clients | git, SQLite WAL with concurrent reader (six variants), own memory client, power-loss replay with dm-log-writes, I/O errors with dm-flakey; two layer bugs found and fixed (stale lower dentry losing WAL frames; `set_nlink` race) | Acceptance over btrfs and XFS; full xfstests | [0015](docs/history/0015-phase-2e-investigation.md) |
| KASAN/UBSAN/lockdep harness | Upstream 6.12.112 built with `vm/kasan.config` under virtme-ng, same `vm/suite.sh`; required on every pull request that touches the module, weekly and on demand; a third defect found (uninitialized `rwsem`/xarray on early mount failure) | KMSAN, KCSAN, KFENCE | [0016](docs/history/0016-kasan-lockdep-harness.md) |
| M3 phase 3b — POSIX authorization in `QUERY` | Per-candidate authorization with the caller's credentials; two users, two namespaces, counts and chunking exact; attribute changes never grant; chmod and POSIX ACLs honoured immediately and across remount; idmapped mounts refused (`EINVAL`), user-namespace mounts refused (`EPERM`); 5-second race of chmod/rename/unlink against `QUERY`; passed in the VM and under KASAN on 6.12.112 | Timing channels; M3 phase 3a identity convention (only the `moobfs-check` duplicate detection exists); phase 3c | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 |
| M3 timing and change-stream verifications | Change stream (fanotify and inotify) and Landlock checked against the layer; the v1 `QUERY` timing channel measured (about 1 µs per hidden entry) | — | [0030](docs/history/0030-verification-notify-landlock-timing.md) |
| ADR-0009 — Per-domain `QUERY2` (phases Q2a to Q2e) | UAPI v2 issued on a domain directory, partitions by domain with the homogeneity rule, ordering and authenticated cursor, equality plan, "missing or" terms, `QUERY2_BATCH`, `EXDEV` for directories renamed across domains; answers equal to the oracle on ext4, btrfs and XFS; a 5-second race per lower filesystem of mode changes, renames, unlink/recreate and cross-domain moves against paged `QUERY2`; passed in the VM and under KASAN | Timing channels other than the one of 0043; persistence | [0034](docs/history/0034-adr-0009-accepted.md) to [0038](docs/history/0038-phase-q2d-missing-batch.md), [0052](docs/history/0052-phase-q2e-concurrency.md), [spec](docs/spec-query-v2.md) |
| M4 — Harness and first campaigns | `bench/` (dataset generator, loader, SQLite client and baselines, equivalence check against the oracle, report); Azure pilots, then a dedicated Mac mini host: `QUERY2` against v1 (24 to 30 times faster on `namespace AND agent`) and against SQLite with composite indexes (faster on the five synthetic shapes measured, on ext4, btrfs and XFS; slower on a real client's long ordered traversal, see the [M4 report](docs/history/0088-m4-report.md)); timing channel closed (10 to 23 µs while another domain grows); cold cache, mount time at 60 000 files, write cost of domains | The M4 report itself (below); POSIX latency per operation and index memory; reflink space savings; other hosts | [0025](docs/history/0025-m4-harness-first-slice.md), [0029](docs/history/0029-query-scale-series.md), [0031](docs/history/0031-remount-time.md), [0042](docs/history/0042-bench-host-macmini.md) to [0049](docs/history/0049-q2-btrfs-xfs.md), [0055](docs/history/0055-measurement-gaps.md) |
| First real-client test, stage 1 (ai-memory, unchanged) | Its own 1 147 tests pass inside the layer as on ext4, after a layer bug they exposed was fixed (rename over a just-deleted name); LongMemEval-S retrieval, 50 questions, identical results per question | Repeated runs and the 500 questions; `QUERY2` with its data (stage 2) | [0054](docs/history/0054-real-client-plan.md), [0057](docs/history/0057-rename-over-deleted-name.md), [plan](docs/client-test-plan.md) |
| Tooling and static analysis | checkpatch clean and failing the CI Lint job on any finding; sparse and `W=1` report nothing on both modules; CodeQL triaged; KASAN job reproducible on a local KVM host | sparse and `W=1` in CI | [0045](docs/history/0045-codeql-triage.md), [0050](docs/history/0050-dx-code-items.md), [0056](docs/history/0056-sparse-w1.md), [0058](docs/history/0058-kasan-local.md) |

## Agreed sequence: completed work and follow-ups

Updated on 2026-10-21. This keeps the order agreed on 2026-10-12 and records what subsequently
finished. New features wait for project review and the maintainer's decision.

| Item | Current evidence | Remaining work |
| --- | --- | --- |
| 1. S1 — Threat and credential model | [ADR-0011](docs/decisions/0011-threat-and-credential-model.md), findings F1 to F12 fixed except fair sharing of the index budget | Outside human review; S9 fair shares |
| 2. Real-client test, stage 2 | Structured query equivalence and latency from 150 to 60 000 memories, live mirror cost ([0068](docs/history/0068-client-stage2-scale.md), [0071](docs/history/0071-client-stage2-live-mirror.md)) | Current-module rerun with intervals; broader client workloads |
| 3. Remaining M3 | Client contract ([0070](docs/history/0070-client-contract.md)); batch content read dropped, change cursor stays in the client ([0072](docs/history/0072-m3-phase-3c.md)) | Attribute-value projection accepted in ADR-0014 but not implemented |
| 4. S4 and S2 | Hostile-lower tests ([0073](docs/history/0073-s4-hostile-lower.md)); fstests `quick` passes on ext4, btrfs and XFS with exclusions ([0091](docs/history/0091-s2-fstests-clean-with-exclusions.md)) | Full fstests `auto` group |
| 5. S6 and test infrastructure | Compatibility policy and `MOOB_ONLY` ([0084](docs/history/0084-s6-moob-only-kasan-required.md)); required KASAN job runs the suite when module/test inputs change | Other kernel series and distribution work remain under the security roadmap |
| Review round 2, R2-5 and R2-7 | Domain-rename and concurrent index-budget corrections reproduced negatively and validated in the complete native VM suite on ext4, btrfs and XFS; the unchanged kernel/layer code passed local KASAN/UBSAN/lockdep ([0174](docs/history/0174-root-path-race.md), [0175](docs/history/0175-over-budget-build-race.md)); all 11 checks on [PR 126](https://github.com/studiomoob/llmoobfs/pull/126) passed and it was merged on 2026-10-21 | Issues 110 and 111 were closed after the merge. This does not implement S9 fair budget shares or the broader S14–S21 test plan |
| M4 report | Pilot complete ([0088](docs/history/0088-m4-report.md)); user-space index comparison measured ([0094](docs/history/0094-userspace-baseline-measured.md)); module comparisons with 40 repetitions and intervals ([0143](docs/history/0143-syzkaller-fifth-run-threshold-met.md)) | Fresh SQLite/client comparisons with intervals, other profiles/hosts, concurrency scaling and default-budget saturation |
| ADR-0012 — On-demand, reclaimable index | Domain build ([0098](docs/history/0098-index-on-demand.md)), releases ([0101](docs/history/0101-domains-released.md)), tracepoints ([0102](docs/history/0102-index-tracepoints.md)); steady-state cost measured ([0139](docs/history/0139-query-buffer-regression.md)) | Fair budget shares; performance residual investigation |
| S3 — syzkaller | Five six-hour runs; fifth has no report and 88.7 % aggregate handler coverage ([0143](docs/history/0143-syzkaller-fifth-run-threshold-met.md)) | `moobl_xattr_get` was at 0 PCs in the fifth run's report: the corpus kept almost no attribute read inside a mounted layer. Replayed with per-call coverage, the seeds reach 3 of its 4 PCs ([0154](docs/history/0154-xattr-get-coverage-explained.md)); the next run, with three new seeds, has to show it in the report |

Stabilization update, 2026-10-20: a third finding, from the review of
[0151](docs/history/0151-review-d110d4e.md) (a chmod during the first build of a domain left the
file in `QUERY2` answers), is fixed in [0152](docs/history/0152-chmod-during-build-fixed.md).
The two correctness findings of the
[independent review](docs/history/0144-independent-review.md) have corrections
in [0149](docs/history/0149-sync-backing-file-flags.md) and
[0150](docs/history/0150-propagate-index-xattr-read-errors.md). Their integrated
full VM suite and local KASAN/UBSAN/lockdep run passed at `49098aa`;
pull-request CI is still a separate pre-merge gate, and publication awaits the
maintainer's approval. This
does not close the S3 per-function coverage gap or the performance residual.

Pause checkpoint, 2026-10-21: the combined correction of issues 110 and 111
had been published as [PR 126](https://github.com/studiomoob/llmoobfs/pull/126),
and new implementation was paused. At that point, exact-head CI, review, a
merge decision and issue reconciliation were the next steps.
Issue 112's batch-error contract was merged by PR 125 and its issue closed;
issue 114 remains partial because unprivileged allocation-fault coverage
(T9) is deferred.
The separate Dependabot PR 75 updated `ossf/scorecard-action`; at that
checkpoint, its old checks had passed but the branch was behind `main` and
needed refresh and recheck before a merge decision. That dependency update
was not part of the correction batch.

Integration update, 2026-10-22: PR 127 merged after all eleven checks passed,
including KASAN/lockdep. It completed issue 114's remaining T9 test and the
concurrent-budget follow-up of issue 111; issue 114 was reconciled and closed.
Issue 116 is being validated separately in journal 0180, while issue 118 and
the broader S9 fair-sharing policy remain open. The pause checkpoint above
describes its earlier date, not the present publication state.

Local validation update, 2026-10-22: issue 116's memcg correction passed
separate negative controls, the full native-KVM suite on ext4, btrfs and XFS
after PR 127 integration, and the full KASAN/UBSAN/lockdep suite. Its
accounting policy is accepted in ADR-0018. No controlled performance
comparison or pull-request CI has run for issue 116; its branch remains local
and the issue open. S9 fair sharing remains separate.

Publication update, 2026-10-22: the maintainer authorized push and
[PR 128](https://github.com/studiomoob/llmoobfs/pull/128) was opened for issue
116. Its exact-head CI and review are the next gates; no check or merge is
claimed at this checkpoint. The issue remains open, and the unmeasured
performance overhead remains disclosed in ADR-0018 and journal 0180.

CI correction, 2026-10-22: PR 128's `Lint` job rejected four documentation
commits without DCO `Signed-off-by` trailers. The original branch remains
preserved; [draft PR 129](https://github.com/studiomoob/llmoobfs/pull/129)
replays the same final code and tests from `main` in signed-off commits.
Its complete exact-head CI, review and integration are still pending.

*Status correction, 2026-10-22 ([0177](docs/history/0177-merged-pr-state-reconciled.md)):*
The checkpoint above records the state before merge. The exact published head
of [PR 126](https://github.com/studiomoob/llmoobfs/pull/126) passed its 11
reported checks; the maintainer merged it on 2026-10-21 and issues 110 and
111 were closed. [PR 75](https://github.com/studiomoob/llmoobfs/pull/75)
also passed its nine reported checks and was merged separately that day.
There are no open pull requests at this checkpoint. Resume with issue 114's
remaining T9 test gap, then investigate the resource-cost issues 116 and 118;
the pause did not imply production readiness or complete S9 fair sharing.

*Local progress, 2026-10-22 ([0178](docs/history/0178-unprivileged-allocation-fault-queries.md)):*
The T9 fault test found that an allocation failure during a per-result
permission lookup could produce a successful but incomplete QUERY2 answer.
The local correction makes that query return `ENOMEM` and adds unprivileged
fault cases. The full local KASAN and native Docker VM suites passed;
publication and issue reconciliation are pending. Issues 116 and 118 remain the next
resource-cost investigations after this step. A separate direct KVM run on the
dedicated Mac mini repeatedly returned `ENOMEM` in the issue 111 concurrent
budget test on ext4, although the reference Docker VM and full KASAN runs
passed. Investigate that KVM finding before treating the budget race as fully
stabilized; journal 0178 records the exact results and limits.

*Local follow-up, 2026-10-22 ([0179](docs/history/0179-kvm-overbudget-race-followup.md)):*
The dedicated-host KVM failure reproduced in an isolated run. The local
correction tries the next eligible built domain if the oldest one's build
mutex is busy. The targeted KVM case and the full Docker VM, native KVM and
KASAN/UBSAN/lockdep suites passed on the changed module, including budget
checks on ext4, btrfs and XFS. This remains unpublished; PR CI and integration
are separate gates. Issues 116 and 118 remain next in the stabilization order.

*Integration update, 2026-10-23 ([0181](docs/history/0181-merged-memcg-state-reconciled.md)):*
The exact head `90b3e68` of
[PR 129](https://github.com/studiomoob/llmoobfs/pull/129) passed all eleven
reported checks, including KASAN/lockdep, and was merged into `main` as
`40645e2` with the maintainer's approval. Issue 116 closed automatically.
The earlier checkpoints above retain their publication-time meaning. No
controlled before/after performance comparison was run for the memcg change;
issue 118 is the next resource-cost investigation, while S9 fair sharing
remains separate.

Planned from ADR-0017 (accepted 2026-10-20, journal 0156), the answers to the design objections of
review round 2:
- **Per-result checks as the default?** Measure every query answered with per-result permission
  checks (the fallback mode) against the homogeneous partition, by the method of
  `docs/benchmark-plan.md`. If the difference is small, per-result checks become the default.
- **The user-space comparison, extended.** The `udx` baseline (journals 0092, 0094) under
  concurrent writes and renames, right after permission changes, and with a `fanotify` mark;
  publish what each side gets right and wrong.

## Depends on validation

These are deliberately **not** scheduled: each is conditioned on M4 results or
on the security roadmap, and a negative result may cancel it.

| Item | Condition | Reference |
| --- | --- | --- |
| Index persistence (surviving remount) | M4 must show that rebuild time or memory at the target scale justifies an on-disk structure, and a format and compatibility ADR must exist | [spec-m3](docs/spec-m3.md) §1, [value-assessment](docs/value-assessment.md) §9 |
| Attribute values with query results | Measured in journal 0072: reading the attributes of results costs 2 to 16 µs per file against 0.9 µs per result for the query. Projection is accepted in ADR-0014, not implemented; it remains paused pending project review and the maintainer’s decision. The batch read of content (`READ_BATCH`) is dropped | [spec-m3](docs/spec-m3.md) §4, journal 0072 |
| Change cursor ("changes since revision") | Decision deferred; needs a client with a measured benefit and answers to the open questions in spec-m3 §5 | [spec-m3](docs/spec-m3.md) §5 |
| Own filesystem as a lower (the pre-ADR-0007 path) | Returns only as an alternative implementation of the same interface, and only with evidence that the mature lower filesystems are the bottleneck | [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) |
| Public release (DKMS, `.deb`, kernel matrix) | Stage 2 (technical preview) needs S1, S4, S5, S6, S2 results and S10; stage 3 (public) needs M4 published and S1–S6, S8–S10 done | [distribution-strategy](docs/distribution-strategy.md) §8 |
| Security roadmap, remaining items | S3 remaining function-coverage investigation, S7 continuous fuzzing, S8 runtime hardening, S9 resource limits and denial of service, S10 module signing and distribution, S11 M3/M4 surfaces | [security-roadmap](docs/security-roadmap.md) |

The [original architecture](docs/native-mvp.md) explores the full transactional
MVP; the [M1 format](docs/format-m1.md) intentionally does not implement it, and
ADR-0007 moved the project to a stacked layer. Any extension starts with an ADR,
a format or UAPI compatibility decision and measurable tests.
