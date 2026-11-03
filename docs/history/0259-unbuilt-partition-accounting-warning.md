# Partition accounting warning after a permission change

Date: 2026-11-03. Status: **in progress**. Estimated effort: S (hours), revised if the
investigation expands. Investigation started 2026-11-03; exact engineering effort was not
measured. Calendar start/end and individual machine runs are recorded below.

## Goal and observed evidence

Investigate the MoobFS `part_rec`/`part_sub` warnings found by the complete November 2
daytime and scheduled campaigns. Both campaigns completed all three phases with exit 0,
but neither is accepted as a clean S18 result. Replay verification independently confirms
1,145/1,145 and 691/781 call sites (88.5%) during daytime; 1,176/1,176 and 692/781 (88.6%)
in the scheduled campaign. An exit-0 replay does not waive a kernel warning.

Evidence archive SHA-256:
`21bbac7bb3c7c898eb9a9fdf716818faa17c19ebd5404df851aeb9858682bc69`.
All 4,070 archived files were verified against the manifest; corpus content identifiers,
cache unions and generated call-site reports were independently recomputed. The daytime
1,812-source before/after manifest equals the fresh post-night manifest. Only the already
generated syzkaller constants differ from Git `06e4a65`; no separate pre-night source
manifest exists. Private raw crash material stays outside Git. Neither a privilege leak nor
data loss has been demonstrated. The warning originates in this module, not an unrelated
filesystem or an expected environment diagnostic.

## Investigation and proposed behavior

The warning trace passes through permission changes and `rec_set_partition()`. The latter
charges name nodes and moves entry/inode byte counts into the newly computed partition.
Unlike the xattr-write and rename paths, the permission-change path does not check whether
that target domain has an active build/record. This is a source-level hypothesis pending a
deterministic regression; it is not yet a validated fix. The associated teardown underflow
warning is retained, not dismissed. Hard-link aliases in a never-built domain and stale
upper paths following another mount's rename are candidate triggers; neither hypothesis
is recorded as independently reproduced yet.

The candidate correction will retain lazy domain creation: drop an existing record that
would move into an unbuilt domain and invalidate its old live domain so a subsequent query
rebuilds from the lower, rather than charging a missing partition or returning a partial
answer. No UAPI or format change is planned. ADR-0022 permits fixes under the freeze;
the maintainer instructed continuing investigation/correction after the finding.

A different existing agent wrote a functional fixture from the specification and
harness without seeing `layer/` or this implementation proposal. This task is distinct
from that session's blocked fuzzing preparation; no security restriction is bypassed.

## Validation and limits

No kernel correction, negative VM round or corrected VM result is claimed at this
checkpoint. Required: deterministic old-code failure, checkpatch, focused suite and
KASAN/lockdep, then full VM and KASAN batch gates before publication. The runtime clone
used by the campaigns stays immutable; correction runs use a separate copy and slot lock.
Published main, GitHub and site retain their dated checkpoint pending separate publication
authorization. G6 mounted work, new PR/merge, release and Azure remain outside scope.

## Prior art inspected

Existing MoobFS domain-liveness checks in xattr-write and rename paths and the on-demand
write contract in spec-query-v2 §9. Linux 6.12.112 exact build sources, lib/xarray.c
1466–1487 (`xa_load`, absent entries return NULL), and include/linux/lockdep.h
290–291 (`lockdep_assert_held_write`), inspected on the dedicated host. The existing index
write lock protects accounting and lookup; no new locking mechanism is proposed.

## Deterministic negative control (2026-11-03 10:46:18–10:47:19 Brasília)

The specification-only fixture was implemented as `permission-gaps coldlink` in a fresh
mount per lower, before the existing permission-gap fixtures. It builds A alone, adds a
hard-link alias in never-queried B, changes mode through B, checks exclusion while linked
and membership after alias removal/mode restoration. GET_INFO checks that B stays cold,
no domain is released and counters return to their initial shape. The normal guest
diagnostic gate supplies the kernel-warning assertion.

With the original `layer/index.c`, the same suite step failed with exit 1 and
`MOOBFS_VM_FAIL: kernel-diagnostic-layer`. Dmesg records `part_rec` at line 182 with
`permission-gaps` as the triggering process. Functional fixture membership passed,
showing why a zero userspace result alone would miss this accounting defect. Raw consoles,
dmesg and the old module were preserved before copying the proposed correction.
Negative source archive SHA-256:
`3afc0e3cc42ebf711f4a5f1481022b22481fd856c6f6311505d3ded3f9300a88`.

A seven-line candidate guard/drop/invalidation change is now under the same focused
KASAN/UBSAN/lockdep step. `make checkpatch` passed locally. No corrected VM pass or full
batch result is claimed until actual exit markers and diagnostics are reviewed.
The independent test receipt corrected a documentation-section reference: on-demand
domains are spec §9, not §10. This journal and references use the corrected section.

## Focused corrected result (2026-11-03; completed 11:52:01 Brasília)

The same `MOOB_ONLY=permgaps` round with the candidate correction exited 0. All three
lowers (ext4, btrfs, XFS) emitted both `COLD_LINK_PASS` and `PERMISSION_GAPS_PASS`,
followed by `MOOBFS_KASAN_ONLY_PASS` under KASAN, UBSAN and lockdep, with no kernel
diagnostics. The input source hashes passed after execution. This is focused validation,
not the full batch gate or a clean long fuzzing campaign.

The correction checks target-domain liveness while holding the existing index write lock.
When the target is cold, it invalidates the old domain and drops the record instead of
moving its charges into a nonexistent partition. The next query rebuilds the old domain
from the lower, avoiding a falsely complete answer after dropping the record. No new
partition is eagerly allocated. The existing warning assertions remain intact.

Next: full native Debian VM and full local KASAN/lockdep suites on this corrected unit,
in separate source/build copies and dedicated slot reservations. Long campaign acceptance
remains open; prior warning-bearing reports are not reclassified as clean.

## Full batch gates started (2026-11-03 11:53:38 Brasília; in progress)

Local DCO commit `4f17fb4` contains the correction, independent functional fixture and
investigation record. Its Git source archive SHA-256 is
`416292cd4bb53f9858440fe789e0779245217ffa8efe84deae2c06b1619f0323`.
Two source/build copies run in parallel under separate slot locks: full KASAN/UBSAN/lockdep
on CPUs 4–5 and `MOOB_DEBIAN_KERNEL=6.12.113 MOOB_VM_KVM=1 make vm-test` on CPUs 6–7.
A private Docker wrapper supplies the container cpuset/name/label; the repository's VM
runner is unchanged. Only the named container is stopped if its client times out. Both
commands have a 22:00 deadline, source before/after manifests and separate exit markers.
Actual KASAN QEMU and the native container were observed; no complete result is claimed.

Focused old/new evidence archive SHA-256 (verified after local copy):
`fff3ada692ed2b8a8a594b37e725db10914aeb19c00234d4ac2b59ea92fd2d6c`.
The sole existing observer was restarted after adding these two completion paths; its
prior receipts remain intact. No VM/test was interrupted or duplicated. Source corrections
are local only; the immutable original campaign runtime has not been modified.

## Full native Debian VM approved (2026-11-03 12:13:38 Brasília)

The full Debian 6.12.113 KVM suite completed in 20 minutes of calendar machine time,
exit 0, with both `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`. The new cold-link
fixture and existing permission-gap cases passed on all three lowers. No unexpected
kernel diagnostic appears in the saved console. The expected Python-library skip remains
explicit: this native guest lacks Python; its coverage belongs to the separate KASAN run.

All 1,814 before/after source hashes are identical, freshly verified on the host, and
independently matched locally against the tested Git source archive. The archived module
vermagic is `6.12.113+deb13-amd64 SMP preempt mod_unload modversions`. Layer module SHA-256:
`53a28dc0af1fced7dab3cd0b207e958d8ff9d8866fb1f0ce3989fb32a95100c5`; base module:
`6f86f90789df65dab2fd88fc4699e557fea6df3a5d203e22dd2cadce71099d1e`.
The boot kernel image itself is not independently archived in this evidence.

Native evidence archive SHA-256, checked after local copy, including the internal file
manifest, raw console, build log, source manifests and binaries:
`79253e592ceda1795ce97629c04248d10797d53d7d78e01aad078ad18d91bf91`.
Slot B is free and its named container has ended. Slot A remains reserved for the full
KASAN/UBSAN/lockdep suite; no final KASAN result is claimed. Peer fronts were notified
without changing their tasks. The next clean long campaign waits for that gate.

## Full KASAN/UBSAN/lockdep approved (2026-11-03 12:54:00 Brasília)

The full suite on the same corrected snapshot exited 0 after 1 h 00 min 22 s of calendar
machine time. All three lowers passed the cold-link regression and permission cases;
`MOOBFS_KASAN_PASS` reports no kernel diagnostics. Allocation-failure injection completed
22 operations, 326 calls and 304 injected faults, **unexpected=0**. The Python library
passed its kernel/scan comparisons, complementing the native guest's Python skip.

This KASAN kernel lacks the memory controller and Landlock: MEMCG_ACCOUNT, LIMITS_MEMCG,
DIAG_MEMCG and LANDLOCK remain explicit skips here. All four have actual pass markers
in the already-approved full native Debian suite; they were not rerun or silently waived.
All 1,814 source hashes match before/after and the Git source archive independently.
Evidence SHA-256, verified after local copy and internal-manifest checks:
`6753e6bc16fc35fec817c055a7d3da5e7195ec66e10a22d9e70b49ceeb9f7c34`.
KASAN module SHA-256:
`68e37bde7d884eb6d3a14d615f47ab0f0db0c2cd6551a28bbf88f70a209f0eee`;
boot kernel SHA-256:
`06c3ce9a6541864834a2e92a6742013628c5a408ebc592ed35de715a92572ee3`.
Both slots were free with no QEMU/manager when checked. Negative/positive regression,
checkpatch and both full batch gates now pass. Long corrected-campaign validation and
publication remain separate; no old warning-bearing report is reclassified as clean.
