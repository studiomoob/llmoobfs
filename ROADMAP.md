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
| M2 phase 2c — Typed attributes | `user.moob.*` validated in the kernel (shared `common/attr.c`), `moobfs-attr`, `moobfs-check`, host unit tests, libFuzzer target | The first scheduled run of the long fuzzing workflow `fuzz.yml` (45 minutes per target, twice a week; written in journal 0237, run locally only) | [0013](docs/history/0013-phase-2c-typed-attributes.md), [0237](docs/history/0237-s7-fuzzing-and-s8-hardening.md) |
| M2 phase 2d — In-memory index and `QUERY` | Index built at mount and kept current (since ADR-0012, per domain on first query); `QUERY`/`GET_INFO` (UAPI v1); kernel answers identical to the `moobfs-index` oracle over the three lower filesystems, after rename, unlink, removexattr and remount; `noindex`, `index_budget` | Budget exhaustion on large trees; persistence | [0014](docs/history/0014-phase-2d-index-query.md) |
| M2 phase 2e — Acceptance with real clients | git, SQLite WAL with concurrent reader (six variants), own memory client, power-loss replay with dm-log-writes, I/O errors with dm-flakey; two layer bugs found and fixed (stale lower dentry losing WAL frames; `set_nlink` race) | Acceptance over btrfs and XFS; full xfstests | [0015](docs/history/0015-phase-2e-investigation.md) |
| KASAN/UBSAN/lockdep harness | Upstream 6.12.112 built with `vm/kasan.config` under virtme-ng, same `vm/suite.sh`; required on every pull request that touches the module, weekly and on demand; a third defect found (uninitialized `rwsem`/xarray on early mount failure) | KMSAN (KCSAN runs in `kcsan.yml`; KFENCE and the S8 hardening options are in `vm/kasan.config` since journal 0237) | [0016](docs/history/0016-kasan-lockdep-harness.md), [0237](docs/history/0237-s7-fuzzing-and-s8-hardening.md) |
| M3 phase 3b — POSIX authorization in `QUERY` | Per-candidate authorization with the caller's credentials; two users, two namespaces, counts and chunking exact; attribute changes never grant; chmod and POSIX ACLs honoured immediately and across remount; idmapped mounts refused (`EINVAL`), user-namespace mounts refused (`EPERM`); 5-second race of chmod/rename/unlink against `QUERY`; passed in the VM and under KASAN on 6.12.112 | Timing channels; M3 phase 3a identity convention (only the `moobfs-check` duplicate detection exists); phase 3c | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 |
| M3 timing and change-stream verifications | Change stream (fanotify and inotify) and Landlock checked against the layer; the v1 `QUERY` timing channel measured (about 1 µs per hidden entry) | — | [0030](docs/history/0030-verification-notify-landlock-timing.md) |
| ADR-0009 — Per-domain `QUERY2` (phases Q2a to Q2e) | UAPI v2 issued on a domain directory, partitions by domain with the homogeneity rule, ordering and authenticated cursor, equality plan, "missing or" terms, `QUERY2_BATCH`, `EXDEV` for directories renamed across domains; answers equal to the oracle on ext4, btrfs and XFS; a 5-second race per lower filesystem of mode changes, renames, unlink/recreate and cross-domain moves against paged `QUERY2`; passed in the VM and under KASAN | Timing channels other than the one of 0043; persistence | [0034](docs/history/0034-adr-0009-accepted.md) to [0038](docs/history/0038-phase-q2d-missing-batch.md), [0052](docs/history/0052-phase-q2e-concurrency.md), [spec](docs/spec-query-v2.md) |
| M4 — Harness and first campaigns | `bench/` (dataset generator, loader, SQLite client and baselines, equivalence check against the oracle, report); Azure pilots, then a dedicated Mac mini host: `QUERY2` against v1 (24 to 30 times faster on `namespace AND agent`) and against SQLite with composite indexes (faster on the five synthetic shapes measured, on ext4, btrfs and XFS; slower on a real client's long ordered traversal, see the [M4 report](docs/history/0088-m4-report.md)); timing channel closed (10 to 23 µs while another domain grows); cold cache, mount time at 60 000 files, write cost of domains | The M4 report itself (below); POSIX latency per operation and index memory; reflink space savings; other hosts | [0025](docs/history/0025-m4-harness-first-slice.md), [0029](docs/history/0029-query-scale-series.md), [0031](docs/history/0031-remount-time.md), [0042](docs/history/0042-bench-host-macmini.md) to [0049](docs/history/0049-q2-btrfs-xfs.md), [0055](docs/history/0055-measurement-gaps.md) |
| First real-client test, stage 1 (ai-memory, unchanged) | Its own 1 147 tests pass inside the layer as on ext4, after a layer bug they exposed was fixed (rename over a just-deleted name); LongMemEval-S retrieval, 50 questions, identical results per question | Repeated runs and the 500 questions; `QUERY2` with its data (stage 2) | [0054](docs/history/0054-real-client-plan.md), [0057](docs/history/0057-rename-over-deleted-name.md), [plan](docs/client-test-plan.md) |
| Tooling and static analysis | checkpatch clean and failing the CI Lint job on any finding; sparse and `W=1` report nothing on both modules; CodeQL triaged; KASAN job reproducible on a local KVM host | sparse and `W=1` in CI | [0045](docs/history/0045-codeql-triage.md), [0050](docs/history/0050-dx-code-items.md), [0056](docs/history/0056-sparse-w1.md), [0058](docs/history/0058-kasan-local.md) |

## Agreed sequence: completed work and follow-ups

Updated on 2026-10-21. This keeps the order agreed on 2026-10-12 and records what subsequently
finished. New features wait for project review and the maintainer's decision; since
2026-10-25, ADR-0022 keeps that freeze for the kernel, UAPI and format and lets the user-space
"Consumable path" items proceed one by one. *Update, 2026-10-27 (journal 0210):* the maintainer
set the order security, then consumable C6, then evolution G6.

| Item | Current evidence | Remaining work |
| --- | --- | --- |
| 1. S1 — Threat and credential model | [ADR-0011](docs/decisions/0011-threat-and-credential-model.md), findings F1 to F17 fixed, F2's fair shares by ADR-0027 (journal 0228) | Independent human review of the whole kernel code |
| 2. Real-client test, stage 2 | Structured query equivalence and latency from 150 to 60 000 memories, live mirror cost ([0068](docs/history/0068-client-stage2-scale.md), [0071](docs/history/0071-client-stage2-live-mirror.md)) | Current-module rerun with intervals; broader client workloads |
| 3. Remaining M3 | Client contract ([0070](docs/history/0070-client-contract.md)); batch content read dropped, change cursor stays in the client ([0072](docs/history/0072-m3-phase-3c.md)) | Attribute-value projection accepted in ADR-0014 but not implemented |
| 4. S4 and S2 | Hostile-lower tests ([0073](docs/history/0073-s4-hostile-lower.md)); fstests `quick` passes on ext4, btrfs and XFS with exclusions ([0091](docs/history/0091-s2-fstests-clean-with-exclusions.md)) | Full fstests `auto` group |
| 5. S6 and test infrastructure | Compatibility policy and `MOOB_ONLY` ([0084](docs/history/0084-s6-moob-only-kasan-required.md)); required KASAN job runs the suite when module/test inputs change | Other kernel series and distribution work remain under the security roadmap |
| Review round 2, R2-5 and R2-7 | Domain-rename and concurrent index-budget corrections reproduced negatively and validated in the complete native VM suite on ext4, btrfs and XFS; the unchanged kernel/layer code passed local KASAN/UBSAN/lockdep ([0174](docs/history/0174-root-path-race.md), [0175](docs/history/0175-over-budget-build-race.md)); all 11 checks on [PR 126](https://github.com/studiomoob/llmoobfs/pull/126) passed and it was merged on 2026-10-21 | Issues 110 and 111 were closed after the merge. This does not implement S9 fair budget shares or the broader S14–S21 test plan |
| M4 report | Pilot complete ([0088](docs/history/0088-m4-report.md)); user-space index comparison measured ([0094](docs/history/0094-userspace-baseline-measured.md)); module comparisons with 40 repetitions and intervals ([0143](docs/history/0143-syzkaller-fifth-run-threshold-met.md)); a weekly trend run of the on-demand benchmark on GitHub's amd64 runner over ext4, btrfs and XFS, not a reference measurement ([0238](docs/history/0238-weekly-bench-workflow.md)) | Fresh SQLite/client comparisons with intervals, other profiles/hosts, concurrency scaling and default-budget saturation |
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

*Local issue 118 progress, 2026-10-23 ([0182](docs/history/0182-query-build-resource-cost-investigation.md)):*
Repeated KVM probes measured unprivileged build CPU/I/O and cross-user
budget eviction. ADR-0019 records the maintainer's resource-limit direction.
An experimental cumulative-work, elapsed-time and rebuild-frequency guard
is implemented locally. The targeted KVM regression and a pre-fix negative
control passed; the complete KVM suite, KASAN, performance comparison and
publication are separate gates. This does not implement S9 fair index-memory
shares or guarantee strict CPU-time or physical-I/O-byte limits. Journal
0192 later stopped a budget release from dropping another filesystem uid's
domain on the host; the sentences above remain the 2026-10-23 measurement.

*Integration update, 2026-10-25 ([0193](docs/history/0193-merged-per-mount-build-work-policy.md)):*
PR 138 merged the bounded `index_build_work` mount option after twelve green
checks. The 32,768-unit default remains, with an administrative 1,024–65,536
choice. Issue 118 stays open for real slow lower-device and large
single-project client evidence; S9 fair sharing remains separate. The
preceding 2026-10-23 paragraph is the earlier local checkpoint.

*Update, 2026-10-27 ([0210](docs/history/0210-security-batch-1-domain-records-and-slow-lower.md)):*
issue 118 is closed. Over an ext4 lower slowed to 400 ms per I/O the first build of a domain
stopped at its deadline with `ETIMEDOUT` and no partial answer. The statements above that issues
116 and 118 remain next or open are earlier checkpoints; both are closed.

*Update, 2026-10-28 ([0221](docs/history/0221-lookup-errnos-and-m8.md) to
[0224](docs/history/0224-passthrough-gaps-issue-115.md)):* the third batch of the security order
fixes F17 and closes issues 119 and 115; issue 117 is fixed for inodes without an index record and
stays open for inodes with one. S9 fair sharing remains open.

*Update, 2026-10-28 ([0228](docs/history/0228-security-batch-5-fair-shares.md)):* the fifth batch
implements S9 fair sharing ([ADR-0027](docs/decisions/0027-fair-shares-of-the-index-budget.md)): a
uid above its share of the index budget lends its room back to a uid below its own, and a uid
within its share keeps its domains. F2 is fixed. The statements above that S9 fair sharing is
open or separate are earlier checkpoints.

Planned from ADR-0017 (accepted 2026-10-20, journal 0156), the answers to the design objections of
review round 2:
- **Per-result checks as the default?** Measure every query answered with per-result permission
  checks (the fallback mode) against the homogeneous partition, by the method of
  `docs/benchmark-plan.md`. If the difference is small, per-result checks become the default.
- **The user-space comparison, extended.** The `udx` baseline (journals 0092, 0094) under
  concurrent writes and renames, right after permission changes, and with a `fanotify` mark;
  publish what each side gets right and wrong.

## Client integration and retrieval — G1 gate, G3 drill and G4 gate passed

Added on 2026-10-20 at the maintainer's request, following
[assessment 0164](docs/history/0164-roadmap-product-gaps.md) and recorded in
[journal 0165](docs/history/0165-client-roadmap-priorities.md). On 2026-10-24 the
maintainer accepted the first evolution batch as a document: the evaluation
protocol, the G1 scope and the G3 recovery bounds, in
[the protocol](docs/g6-evaluation-protocol.md) and
[journal 0188](docs/history/0188-g6-protocol-and-g1-scope.md). Journal
[0189](docs/history/0189-g1-adapter-package.md) then wrote the G1 adapter
package. The same evening the mounted gate passed on the dedicated Debian
host (ext4, kernel 6.12.111). The counters and the limits are in that
journal. The G3 identity-map drill then passed on that same corpus
([journal 0190](docs/history/0190-g3-restore-drill.md)). G2's fairness property is registered. A host regression then stopped a budget release from dropping another filesystem uid's domain ([ADR-0021](docs/decisions/0021-budget-release-keeps-the-builders-domain.md), [journal 0192](docs/history/0192-same-fsuid-budget-release.md)). A reserved share is not implemented and no latency ceiling is set, so G2 is not complete. On 2026-10-25 a G4 amendment to the protocol was proposed ([journal 0194](docs/history/0194-g4-protocol-amendment.md)): freshness, inventory and revocation bounds for a per-uid derived index in user space. The maintainer accepted it, and the G4 package then met those bounds in three runs on the dedicated host ([journal 0195](docs/history/0195-g4-derived-index-package.md)). A G5 amendment with relevance and latency bounds was then proposed ([journal 0200](docs/history/0200-g5-protocol-amendment.md)) and accepted; on 2026-10-26 the G5 package met those bounds on the dedicated host ([journal 0201](docs/history/0201-g5-hybrid-retrieval-package.md)). On 2026-10-28 ADR-0027 gave the budget fair shares and protocol section 14 set G2's latency ceiling before the measurement; the noisy-neighbour shape met it in three runs on the dedicated host (journal [0228](docs/history/0228-security-batch-5-fair-shares.md)). G2 stays open for its user-visible diagnostics and deployment recipe. The G6 final evaluation stays
unstarted, and the latency, memory and relevance ceilings it needs
stay unbound until a dated amendment sets them before the run. Priorities are
not delivery dates or a production-readiness commitment.

The target experiment is file-backed agent memory on a managed Linux host, with
two distinct OS identities and private/shared domains. Application tenant labels
alone do not provide OS isolation. Reuse existing query, security and distribution
work; these packages do not replace their acceptance criteria or the institutional
collaboration track maintained separately.

**P0 — Existing correctness and security work remains open.** S9 fair sharing is
done since journal 0228 (ADR-0027); S9's remaining caps and the mount cost were tested when hit
and measured in journal 0235, and S9 closes when the maintainer accepts
[ADR-0029](docs/decisions/0029-s9-limits-met-by-existing-bounds.md); the open issues remain. Passing a test gate does not start a feature package. The protocol
batch of journal 0188 does not close P0 and does not contain the G1 package.

Effort is a preliminary engineering estimate: S = hours, M = one to three days,
L = one week or more. Estimates exclude external review and infrastructure wait
time and must be refined when a batch is scoped.

The [known-algorithm assessment](docs/research-algorithm-fit.md) maps existing
techniques to these packages, with evidence, trade-offs and experiment proposals.
It does not select an algorithm or authorize implementation.

| Item | Priority / effort | Scope and dependencies | Validation required before completion |
| --- | --- | --- | --- |
| **G1 — Usable client integration** | P1 / L, mounted gate passed 2026-10-24 | One narrow client package and one adapter, scoped in the protocol: durable publication, typed fields, paging, explicit errors, capability detection from an accessible domain and fallback. Builds on the [client contract](docs/client-contract.md) and [ADR-0013](docs/decisions/0013-user-space-query-library.md). Any new UAPI needs its own decision; projection remains the separately accepted ADR-0014. | Adapter answers equal the reference on the supported query shapes, including multi-page results; test a caller unable to open the mount root, missing/unsupported module, budget exhaustion and heterogeneous files. Fallback must preserve the caller's access restrictions. Crash injection at publication steps yields an old complete or new complete final file (or absence before first publication), never mixed content/attributes. Document concurrent-writer semantics and unsupported operations. |
| **G2 — Predictable shared operation** | P1 / L, cross-user budget release stopped on the host (ADR-0021); fair shares (ADR-0027) and the latency ceiling of protocol §14 met on the dedicated host in 3 of 3 runs on 2026-10-28 (journal 0228); S9 caps hit and mount cost measured in suite steps `limits` and `mountscale` (journal 0235, ADR-0029 accepted); open for the user-visible diagnostics and the deployment recipe, which has to set `memory.max` for the users of a shared mount | Deliver the remaining [S9](docs/security-roadmap.md#s9-resource-limits-and-denial-of-service) work with user-visible diagnostics, workload limits and a supported deployment recipe. Reuse ADR-0012 and tracepoints. Distribution continues to require its existing gates. | In a dedicated VM, exercise two users, a noisy neighbor, cold/warm domains and budget saturation. Verify the configured resource policy and explicit errors, compare answers with the oracle, record actual memory including query buffers, rebuild counts and p50/p95/p99 latency. The declared workload must meet pre-registered resource and latency bounds under contention. No silent partial success. |
| **G3 — Portable backup and recovery** | P1 / M, identity-map drill passed 2026-10-24 | Complete the export/recovery work already proposed in [distribution strategy section 2.7](docs/distribution-strategy.md). Bounds for the identity-map drill are in the protocol. Content, typed attributes, identity and relevant permissions form the recovery unit. It does not require G1 or a persistent kernel index. | Restore into a fresh tree; compare content hashes, typed values, identity and ownership/mode/ACL semantics under a declared UID/GID mapping. Rebuild and compare query answers and denials for both users. Missing/corrupt metadata and incompatible versions must be detected. Refuse overwrite by default; verify content remains readable without the module. Any new export format requires a specification and compatibility decision. |
| **G4 — Derived-index lifecycle** | P2 / L, protocol amendment accepted and mounted gate passed in three runs 2026-10-25 (journals 0194, 0195) | A user-space contract for stable client identity, content version/hash, embedding-model version, reconciliation and protected caches. Depends on G1; shares the restored-data contract of G3. Revisions and change tracking remain in the client under [M3 section 5](docs/spec-m3.md). | Exercise update, rename, delete, permission revocation, watcher interruption, lost events, restart and model change. After reconciliation the derived inventory equals a fresh scan for the declared scope/version; no orphaned or obsolete version is served. Measure freshness against pre-registered bounds. New retrieval after a completed revocation must not deliver forbidden content; define in-flight behavior and test protected vectors/caches as well as files. |
| **G5 — Optional hybrid retrieval** | P2 / L, protocol amendment accepted 2026-10-25 and mounted gate passed 2026-10-26 (journals 0200, 0201) | Integrate structured filters, lexical retrieval and vector ranking in user space. Depends on G1 and G4; use G2's measured envelope for multi-user claims. Select an existing engine only after a bounded comparison. No vector/ANN implementation in the kernel is planned. | Establish an exact-scoring oracle over the complete eligible set, including sets larger than one QUERY2 page. Verify filter/authorization correctness and deterministic tie handling; measure relevance and recall@k, usable-result count, p50/p95/p99, CPU and total RAM against the same search engine over plain files plus a user-space metadata index. Any approximate mode must meet its pre-registered recall threshold. Include lexical-only and vector-only baselines, restrictive filters and revocation tests; no claim of inherited protection from timing/count/ranking leaks. |
| **G6 — End-to-end usefulness** | Cross-cutting: P1 protocol written, P2 final evaluation not run / L | The protocol and the G1 scope are [g6-evaluation-protocol](docs/g6-evaluation-protocol.md) (journal 0188). Final evaluation combines G1–G5 in one real client; the shared scenario needs G2 and restoration needs G3. Extend [M4](docs/benchmark-plan.md), without treating existing pilot results as fresh evidence. | Reproduce write → retrieve usable context → update → revoke/delete → restart/restore with two OS identities and private/shared domains. Publish raw data, pinned versions, repeated runs and intervals. Measure correctness, relevance, time to usable context, freshness, resources, recovery and adapter-specific maintenance burden. Continue only if a preselected performance/resource objective or a concrete reduction in integration burden is demonstrated without weakening access restrictions or the declared relevance floor; publish negative outcomes too. |

### Selection and validation rules

1. P1 foundations are considered before the P2 retrieval extension. G2 and G3
   need not wait for each other; this is a dependency statement, not an instruction
   to start concurrent work. Within P2, G4 precedes G5. G6 defines the method first
   and evaluates the completed integration last.
2. Before authorizing implementation, scope one client/workload and record numeric
   targets for latency, memory, freshness and relevance, corpus size, filter
   selectivity, concurrency, repetitions and the baseline. These are workload
   choices, not universal guarantees. A package cannot pass with unspecified
   targets or targets chosen after seeing results. Predefine how any reduction
   in integration burden will be measured (for example, duplicated metadata fields
   and client-specific synchronization paths).
3. Correctness failures, forbidden-content exposure, irrecoverable metadata loss
   or failure of a declared acceptance bound block completion. Defer, narrow or
   cancel an item when its benefit is not demonstrated; do not move work into the
   kernel merely to retain it on the roadmap. Runtime tests use dedicated VMs;
   kernel changes retain the existing regression, negative-run and sanitizer gates.
4. A fallback or search service must carry the intended credentials and protect
   derived data. Opening a result again does not establish absence of side
   channels. Revocation cannot retract content already delivered to a process.
5. Preserve ADR-0007: MoobFS already is a native stacked filesystem. An independent
   on-disk filesystem remains conditional on a measured lower-filesystem bottleneck.
   Kernel index persistence and a kernel change cursor retain their separate
   evidence gates below. No backend, new disk format or revised UAPI is selected
   by adding this plan.

## Consumable path — planned, user space only (ADR-0022)

Added on 2026-10-25 ([ADR-0022](docs/decisions/0022-kernel-freeze-and-user-space-consumable-track.md),
[journal 0196](docs/history/0196-consumable-path-roadmap.md)). The kernel freeze stays: these items
change no code in `layer/`, no UAPI and no format. They turn what exists into something a
technical user can install, mount and reach from an application, and they reuse stage 2 of
[distribution-strategy](docs/distribution-strategy.md) §8 and risks 5, 6, 10 and 11 of
[adoption-risks](docs/adoption-risks.md). Each needs the maintainer's go-ahead; C1 and C2 were started on 2026-10-25.
Packages are prepared and tested in VMs, not published, until the stage 2 gates pass.

**Definition of done for the path.** On a clean Debian 13 VM with the matrix kernel, someone who
did not write the documents follows them and, within 15 minutes of their own time, installs the module, mounts it,
writes and queries memories from Python as two OS users with no leak between them, and
uninstalls it, leaving an ordinary tree with its attributes. The run is recorded with its time.

Three fronts run in parallel: the core (kernel fixes, S9/G2), the evolution track (G1–G6) and
this path. C5 builds on G1's adapter package and is scheduled with that front.

| Item | Priority / effort | Scope and dependencies | Validation required before completion |
| --- | --- | --- | --- |
| **C1 — First contact** | P1 / S, done 2026-10-25 ([0197](docs/history/0197-first-contact-and-install.md), PR 143) | Both READMEs open with what MoobFS is, for whom, its warning and one path to try it; the running status paragraph moves to a status page that the READMEs link. Same facts, both languages, every public surface in step. | A reader reaches "what it is", "is it for me" and "how to try" without scrolling past status history; `scripts/check-retired-claims.sh`, the site checks and the public-surfaces walk pass. |
| **C2 — Install and mount outside the test harness** | P1 / M, written 2026-10-25; `make install-test` passed in an emulated Debian 13 VM ([0197](docs/history/0197-first-contact-and-install.md)); merged in PR 143; a fresh Debian 13 kernel is outside the list (issue 142) | `docs/operations.md`: build against the running kernel's headers, `make install`/`modprobe`, a mount by `fstab` and by a systemd mount unit, `index_budget` and `index_build_work` choices, upgrade and uninstall. Needs an `install` target, which does not exist today. Matrix kernels only. | Followed step by step on a clean Debian 13 VM: the module loads, mounts at boot, survives a reboot and is removed cleanly; the lower stays readable with `getfattr`. A kernel outside the matrix is refused with a clear message. |
| **C3 — Container recipe, validated** | P1 / M, done 2026-10-25 ([0199](docs/history/0199-container-recipe.md)); `make container-test` passed twice (TCG, then KVM), Docker and Podman, rootless Podman with `--userns=keep-id` included | `docs/container-integration.md` from distribution strategy §2.5: host mounts, container gets a bind mount with aligned uids. Test the untested alternative there (a read-only bind of the whole root for `QUERY2`) and state the outcome. A kernel change found necessary goes to the core front (ADR-0022, decision 3). | In a dedicated VM with Docker or Podman: read, write and `setxattr` work through the bind mount; `QUERY2` works or fails exactly as documented; a second container under another uid sees nothing of the first. Rootless and user-namespace runtimes stated as refused or untested. |
| **C4 — `moobfs` command-line tool** | P1 / M, done 2026-10-25 ([0198](docs/history/0198-moobfs-command-line-tool.md), PR 144); full VM suite (three lower filesystems) and install test passed | `moobfs info`, `moobfs query` and `moobfs check` over existing ioctls and tools (`tools/query-common.h`, `moobfs-check`), with text and JSON output. User space; no new ioctl. | Answers equal `moobfs-index` (the oracle) on the shapes `QUERY2` supports, paging included; explicit errors for no module, wrong directory, budget exhaustion and `EACCES`; tested as two users in the VM suite. |
| **C5 — Supported client library** | P1 / M–L, after G1; phase 1 (library, `pip` install, kernel = scan) done 2026-10-26 ([0204](docs/history/0204-python-client-library.md), [ADR-0023](docs/decisions/0023-python-client-library.md)); phase 2 done 2026-10-27: `moobfs.langgraph`, G1's mounted gate passed three times on it ([0207](docs/history/0207-python-library-langgraph-store.md)); **done** | Promote G1's adapter (`bench/client/`) into an installable, versioned Python package with capability detection and the scan fallback of [ADR-0013](docs/decisions/0013-user-space-query-library.md) and the [client contract](docs/client-contract.md). Scheduled with the evolution front. | G1's validation carried over to the package; installs with `pip` in a clean environment; the fallback returns the same answers without the module and keeps the caller's access restrictions. |
| **C6 — DKMS, packages and signing, prepared** | P2 / L, done 2026-10-29 in a VM, not published: `moobfs-dkms` and `moobfs-tools`, signed by the host's own DKMS key, `make package-test` under Secure Boot ([0229](docs/history/0229-dkms-and-debian-packages.md), [ADR-0028](docs/decisions/0028-dkms-and-debian-packages.md)) | `dkms.conf` that refuses kernels outside the matrix, `moobfs-dkms` and `moobfs-tools` `.deb` packages, MOK signing for Secure Boot, as proposed in distribution strategy §6. Built and tested in VMs only. Publishing stays behind stage 2 and the "Public release" row below. | Stage 2 acceptance 1, 2 and 4 of distribution strategy §8 in a VM: install on clean Debian, upgrade and roll back with `moobfs-check` and `QUERY2` equal to the oracle, kernel outside the matrix refused, signed module loads under lockdown and unsigned one is refused. |
| **C7 — Agent adapter (optional)** | P2 / M, after C5; done 2026-10-27: `moobfs.mcp`, an MCP server over stdio as the agent's uid ([0208](docs/history/0208-mcp-agent-adapter.md), [ADR-0024](docs/decisions/0024-mcp-agent-adapter.md)) | One adapter that exposes the library to agents through a standard protocol (for example a Model Context Protocol server), as an adapter outside `layer/`, client-neutral. Chosen only if C5 is done and a second client is wanted. | Its answers equal the library's; it runs under the agent's own uid and cannot act with another identity (the confused-deputy risk of distribution strategy §2.5); the two-user isolation test passes through it. |

## Depends on validation

These are deliberately **not** scheduled: each is conditioned on M4 results or
on the security roadmap, and a negative result may cancel it.

| Item | Condition | Reference |
| --- | --- | --- |
| Index persistence (surviving remount) | M4 must show that rebuild time or memory at the target scale justifies an on-disk structure, and a format and compatibility ADR must exist | [spec-m3](docs/spec-m3.md) §1, [value-assessment](docs/value-assessment.md) §9 |
| Attribute values with query results | Measured in journal 0072: reading the attributes of results costs 2 to 16 µs per file against 0.9 µs per result for the query. Projection is accepted in ADR-0014, not implemented; it remains paused pending project review and the maintainer’s decision. The batch read of content (`READ_BATCH`) is dropped | [spec-m3](docs/spec-m3.md) §4, journal 0072 |
| Change cursor ("changes since revision") | Decision deferred; needs a client with a measured benefit and answers to the open questions in spec-m3 §5 | [spec-m3](docs/spec-m3.md) §5 |
| Own filesystem as a lower (the pre-ADR-0007 path) | Returns only as an alternative implementation of the same interface, and only with evidence that the mature lower filesystems are the bottleneck | [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) |
| Public release (DKMS, `.deb`, kernel matrix) | Packages may be prepared in VMs under item C6; publishing them follows these gates. Stage 2 (technical preview) needs S1, S4, S5, S6, S2 results and S10; stage 3 (public) needs M4 published and S1–S6, S8–S10 done | [distribution-strategy](docs/distribution-strategy.md) §8 |
| Security roadmap, remaining items | S3 remaining function-coverage investigation, S7 continuous fuzzing (workflow written, first scheduled run pending, journal 0237), S8 runtime hardening (KMSAN left; journal 0237), S9 (done 2026-10-29, ADR-0029), S10 real firmware and hardware (signing under Secure Boot and the reproducible module build are done in VMs and containers, journals 0229 and 0236), S11 M3/M4 surfaces | [security-roadmap](docs/security-roadmap.md) |

The [original architecture](docs/native-mvp.md) explores the full transactional
MVP; the [M1 format](docs/format-m1.md) intentionally does not implement it, and
ADR-0007 moved the project to a stacked layer. Any extension starts with an ADR,
a format or UAPI compatibility decision and measurable tests.
