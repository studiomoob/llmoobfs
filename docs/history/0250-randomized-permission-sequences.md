# 0250 — Randomized permission sequences against the lower kernel (S15)

Date: 2026-10-31. Status: **in progress**, branch `claudiofp/b11-s15`.
Work started in the preceding session before 12:31 Brasília time (UTC-3); coordination
resumed at about 12:41 Brasília time. End pending. These are calendar times, not measured
engineering effort. Estimated effort: M (journal 0242).

## Goal and decisions

Extend `permmatrix` with seeded chmod, chown, access-ACL changes, rename, link and unlink
through the layer, as root and unprivileged identities. Check QUERY2 after each operation
against permission decisions made by the lower kernel as each of nine identities. Four
mounts per seed alternate ordinary builds and builds slowed by `scan_delay_us`.
No production kernel, UAPI or format changes.

Every returned result must have a reachable name and the correct inode. Completeness is
checked for reachable homogeneous files except documented history-dependent cases: a file
seen with multiple names, a changed ancestor, or a change during a build. These exceptions
still receive leak checks. This is a bounded differential test, not proof of general equality.
The default is six seeds and 24 steps per mount; `MOOB_PERMCHURN=SEEDS,STEPS[,FIRST]` selects
longer campaigns through both the Debian and KASAN harnesses. References: §43.

## Changes

`tests/permmatrix.c`, `vm/suite.sh` (new default step `permchurn`), `vm/run.sh`,
`vm/in-container.sh`, `vm/init`, `vm/kasan-local.sh`, `docs/testing.md`, `docs/references.md`.

## Inherited evidence, inspected during takeover

Dedicated host, Debian 6.12.113, KVM; containers pinned to CPUs 0–3. Logs copied outside Git
under the local review directory `2026-10-31-batch11-takeover`.

- `pc1`: `MOOB_ONLY=permchurn`, six default seeds, all three lowers passed, ending with
  `MOOBFS_ONLY_PASS` and `EXIT 0`. Each lower made 5,184 QUERY2 checks.
- `pc2-knob`: `MOOB_ONLY="permmatrix permchurn" MOOB_PERMCHURN=3,64,101`.
  The static matrix passed on all lowers. Churn failed on ext4 seed 101: `l14` was reachable
  and homogeneous but missing after unlink of `f6` (round 1, step 12). Exit 2;
  `MOOBFS_VM_FAIL: pc-mismatch-ext4-101`. This is not an approved expanded run.
- S14 M14 and M15 were killed by the new churn step: respectively an unreachable
  `s0/t0/r10` for uid 1002 (seed 3) and `f44` for uid 1001 (seed 2), with
  `MOOBFS_VM_FAIL: pc-mismatch-ext4-*` and exit 2.
- M10 survived the default campaign (`MOOBFS_ONLY_PASS`, exit 0). The random test does
  not replace S14's targeted catalogue. M17 and M04 results await final inspection.
- The inherited driver's cleanup encounters root-owned build files and prints permission
  errors. Those are cleanup errors, distinct from the guest verdicts; no global cleanup.

## Investigation and changes after takeover

The extended failure follows removal of one hard-link name. The specification §3 permits a
file returning to one name to remain heterogeneous. The oracle recorded multiple names only
after operations; at the next mount, an unlink during the initial delayed build could remove
the penultimate name before that observation. The candidate correction records all multiply
linked files before the initial build and before each operation, including build phases.
This changes only the test's history tracking; confirmation by repeating seed 101 is pending.
Flush stdout before checker forks to prevent earlier mismatch text from being duplicated.

An isolated source copy `b11s15/review-src` was queued behind the inherited driver, with the
same expanded configuration. Existing running snapshots were preserved.

## Limitations and next steps

Expanded confirmation, strict compilation/lint, the final negative control and full integrated
VM and local KASAN/lockdep suites remain pending. No full batch suite is claimed from a
focused run. Finish and commit S15 locally, then integrate with the CI and nightly fronts.
Push, PR, merge and remote public edits require the maintainer's explicit approval.

Numbering note, 2026-10-31: S15 was assigned journal 0246 in the handoff. The separate
corporate pilot already committed journals 0246 and 0249, so S15 uses 0250. That worktree
and its commits are preserved; no corporate work is integrated in batch 11.

## Expanded confirmation after the oracle correction

2026-10-31, finished about 12:47 Brasília time, dedicated KVM guest, Debian 6.12.113,
CPUs 0–3, isolated `review-src` snapshot:
`MOOB_ONLY="permmatrix permchurn" MOOB_PERMCHURN=3,64,101 make vm-test`
(with `MOOB_VM_KVM=1 MOOB_DEBIAN_KERNEL=6.12.113`). Exit 0;
`MOOBFS_ONLY_PASS: steps permmatrix permchurn`. The static matrix passed all three lowers.
Churn passed seeds 101–103, 6,912 QUERY2 checks on each lower, zero mismatching steps:

| Lower | Operations done | By users | During builds | Delayed builds | Expected files checked |
| --- | ---: | ---: | ---: | ---: | ---: |
| ext4 | 602 | 280 | 53 | 12 | 6,852 |
| btrfs | 579 | 248 | 41 | 9 | 4,419 |
| XFS | 602 | 280 | 53 | 12 | 6,852 |

This confirms the history-tracking correction for the previously failing seed. The explicit
completeness exceptions remain; this is not proof that an arbitrary missing result is harmless.
The inherited M17 and M04 controls also finished with `pc-mismatch-ext4-1` and exit 2, giving
four mutants detected by the new default step (M04, M14, M15, M17); M10 survived.

Offline checks: workstation `make test` passed 60 attribute checks, DIAG_TEST_PASS and nine
image tests; shell syntax, SPDX, retired claims and diff checks passed. Dedicated host
`cc -static -Wall -Wextra -Werror -Iinclude tests/permmatrix.c` and ShellCheck of the five
changed shell harnesses passed. Snapshot comments still said 0246 when copied; renumbering
those comments to 0250 changes no executable behavior. Full integrated suites, the longer
nightly campaign and a fresh negative control after the oracle correction remain pending.
