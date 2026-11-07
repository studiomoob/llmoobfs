# 0271 — S16: the index compared with the lower after every change

Date: 2026-11-07, 15:45–17:15 Brasília time. Status: **done** locally, not published. Calendar
time about 1 h 30 min, most of it VM runs; estimated effort M (one to three days).

## Goal

S16 of the security roadmap, within the freeze exception of ADR-0033: "a test-only module
parameter that, after each update hook and build, re-reads the touched files from the lower and
warns on a difference", done when "the suite runs with it on and stays silent; a mutant of S14
makes it warn".

## Design

- `selfcheck` (bool, 0644, off) and `selfcheck_counts` (read-only: checks, differences, readings
  not conclusive) in `layer/index.c`, under the rules of journal 0221 (M8.6) for test-only
  parameters. `moobl_index_selfcheck()` in `layer/moobl.h` is one predicted branch while off.
- Where it runs: after the update hooks main.c calls (typed attribute set or removed, rename,
  link, chmod/chown, access ACL), outside the lower locks, and at the end of every successful
  `index_load()` (each file a build or an adoption reads). An unlink that leaves other names is
  not followed: no dentry of the remaining names is at hand.
- What it reads: the file's typed attributes, with `attrs_read()` (which gained a flag so that
  only a reading for the index takes the fault of `scan_getxattr_errno_once`), and its partition
  computed from the lower tree alone: the chain of lower dentries up to the lower root, taken
  under `rename_lock` like `where_is()`, the directory at `domain_depth` on it, and
  `reach_ok()`/`target_ok()` applied to the directories between and to the file. It does not
  use `place_dentry_above()`, the scan's `inner_ok` or the rename's placement, so a defect in
  those can show.
- Concurrency: it registers a loader like `index_load()`, so an update of the same file marks
  it, and samples `move_gen`; either makes the reading not conclusive. A difference counts only
  when three readings in a row, a millisecond apart, show one, since a chmod of a directory
  reaches the lower before its hook marks the domain.
- What it compares, under the index write lock: nothing in a domain that is not live, lost or
  restarting; a missing record only in a built domain for a file with typed attributes; a
  record holding part of its file (a write during a build, F1) not at all; the partition only in
  the unsafe direction (a file the rule fails in its domain's partition, unless the domain is in
  fallback mode), and only while the domain's placements are current (see below); every typed
  attribute, both ways.
- Suite: step `selfcheck` (the permchurn workload, seeds 1 to 3, 24 steps per mount, on ext4,
  btrfs and XFS, failing on any difference and requiring conclusive checks), and
  `MOOB_SELFCHECK=1` through `make vm-test` and `vm/kasan-local.sh` for the whole suite, which
  then ends in `SELFCHECK_SUITE_PASS` or fails, and prints the counts at each step.

## A false difference, found and corrected

The first full suite with the check on (16:19) failed: 1,800 differences in 1,708,108 checks,
all of the kind "a file that fails the homogeneity rule is in its domain's partition". The
counts per step put 1,797 of them in `permmatrix`; a run of that step with the record's path
printed placed them in the `pm-mvp` fixture, where `query-build-moved` renames the directory
`d/a/s` (600 files) below the private `d/pv` during the build. The index handles that by design:
the rename hook marks the domain `paths_stale` and queues the directory on `dom->moved`, the
build scans it again before it declares the domain built, and no answer comes from the partition
meanwhile (`domain_fresh()`). The check compared placements inside that window. It now compares
the partition only when the domain has no rename pending (`paths_stale`, `top_gen`, `moved`),
and the attributes always. The one difference per lower in the `ext4`, `btrfs` and `xfs` steps
went with the same change. The module's behavior was not wrong; the check was.

## Evidence (test host, Debian 6.12.113 KVM guest unless said)

- `make checkpatch`, `make layer W=1` (no warning), ShellCheck on the changed scripts: clean.
- `MOOB_ONLY=selfcheck`: `SELFCHECK_PASS`, 3,024 checks, no difference, none not conclusive.
- Full native suite with `MOOB_SELFCHECK=1` (`vm/run.sh`, CPUs 0–1): `SELFCHECK_SUITE_PASS`
  `checks=1683190 differences=0 inconclusive=24037`, `MOOBFS_ACCEPTANCE_PASS`,
  `MOOBFS_LAYER_PASS`, exit 0.
- Negative rounds, `MOOB_ONLY=selfcheck` with one S14 mutant each (`scripts/mutants.py apply`):
  M17 (`index_load()` does not check the file against its domain) fails with
  `sc-difference-ext4-1`, 16 differences in the first seed; M20 (a file under a failing directory
  goes to the partition) `sc-difference-ext4-1`, 4; M14 (the directories between the domain and
  the name are not checked) `sc-difference-ext4-3`, 1 in the third seed. M01 (an extended ACL no longer stops the
  shortcut in `reach_ok()`) is not seen by the check, which shares `reach_ok()`, and fails the
  step through permchurn's own oracle (`sc-churn-ext4-1`).
- `MOOB_ONLY=selfcheck` under KASAN/UBSAN/lockdep (`vm/kasan-local.sh`, kernel 6.12.112):
  `MOOBFS_KASAN_ONLY_PASS`, no kernel diagnostic.

## Batch validation (2026-11-07)

On `97f9f88`, the head of the batch, in separate copies of the tree on the test host:

- Full native suite (`make vm-test`, Debian 6.12.113 KVM guest, CPUs 0–1), 17:11–17:32: exit 0,
  `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, no `_FAIL`; `SELFCHECK_PASS` (2,978 checks);
  `HOOK_RACE_PASS` and `HOOK_RACE_RENAMEOVER_PASS` on all three lowers.
- Full KASAN/UBSAN/lockdep suite (`vm/kasan-local.sh`, 6.12.112, CPUs 2–3), 17:11–18:12:
  `MOOBFS_KASAN_PASS`, no kernel diagnostic in the console, the same step markers.
- Pre-push checks, 18:12–18:24: `make lint`, `make checkpatch`, `make test`, the same under clang
  ASan/UBSan, `make hostile-test`, `make pylib-test`, `scripts/check-retired-claims.sh`,
  `make mutants-check` (the 23 mutants still apply once each and change the object code) and the
  build of the suite's test programs on Ubuntu 24.04: all pass.

## Limitations

- The check shares `reach_ok()`, `target_ok()` and `ext_acl()` with the code it checks: a defect
  inside them (mutants M01 to M13) is invisible to it; the suite's oracles cover those.
- It follows the updates main.c applies and the files builds read; it does not follow an unlink
  that leaves other names, nor changes made to the lower behind the layer, which are outside the
  index's contract.
- 24,037 of the full suite's checks (1.4%) were not conclusive, mostly readings overtaken by
  an update of the same file.

## Next step

The batch's pull request, opened with the maintainer's approval; its merge waits for his. With S16, the S14–S21 block of ADR-0030 is done; the remaining
deliverables of `docs/distribution-strategy.md` §8 decide stage 2.
