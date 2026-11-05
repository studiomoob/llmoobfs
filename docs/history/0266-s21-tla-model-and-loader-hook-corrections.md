# 0266 — S21: TLA+ model of the index loader, and corrections of its two findings

Date: 2026-11-04. Status: **done; merged in PR 183 (`d6ce35e`, journal 0267).** The model, the
tooling and both kernel corrections are implemented. TLC passes on the corrected model, and the
focused native VM runs passed with their negative controls. The focused KASAN run, the full
native suite and the full KASAN/UBSAN/lockdep suite passed, and the first GitHub run of
`tla.yml` checked every configuration as expected (journal 0267). Calendar time: the model work started about 07:15 and its last commit was at 07:52
Brasília time. The corrections started about 09:40 after the maintainer's decision, and the
last focused native run ended at 10:51. Effort and calendar time are close for this work,
because nothing waited on other jobs except the VM runs.

## Goal

Security roadmap S21: a TLA+ model of `index_load()`'s registration and retry against the
update hooks, checked by TLC for every interleaving of a bounded configuration, with TLC
pinned and run in CI. The acceptance has two parts: TLC finds no violation in the
configuration of the code as it is, and it finds one when the retry is removed (the negative
control).

## The model (07:15–07:52)

A separate agent wrote `formal/IndexLoad.tla`, the pinned runner `formal/tlc.sh` (tlaplus
`v1.7.4`, jar SHA-256 `936a2620…e050e88`, refused on mismatch), `make tla-check`, the CI job
`.github/workflows/tla.yml` and `formal/README.md`. The README holds the code-to-model mapping,
the reductions with their argument from the code, and what is left out. The coordinator
reproduced every result on the same machine. Six configurations: three must pass
(`IndexLoad`, `IndexLoadSafety`, `IndexLoadNoLinks`) and three must fail (`IndexLoadNoRetry`,
the S21 control; `IndexLoadPre0152`; `IndexLoadPreR22`). The failing three put back defects
fixed in journals 0152 and 0155/0156.

The controls failed as required, and the safety invariants held. `IndexLoad`, the code as it
was, violated `NoLostUpdate`. With that invariant left out, it violated `FreshPath`. Both are
completeness defects of the code, not modeling errors, and neither discloses anything; the
confidentiality invariant `NoStaleHomogeneity` held in every configuration. The traces are in
`formal/README.md`:

- **F1.** During a domain's first build, a write creates a record with one attribute, and a
  rename leaves the file to the rename hook (`index_load()` sees the name changed). If the
  build ends before the hook takes the index lock, `rec_whole()` sees the domain `BUILT`, and
  the hook keeps the partial record instead of reading the file. A query on the other
  attribute then misses the file until the domain is built again.
- **F2.** An unlink that leaves other names called no hook. A loader that read the file through
  the removed name stored the record under it. After the inode leaves the cache, the result
  cannot be resolved and is silently omitted (the R2-2 effect).

## Decision

The kernel is frozen to fixes, tests and test infrastructure (ADR-0022), and these are fixes.
The maintainer chose to correct both now and to count the S18 campaigns again from the
corrected code. The campaigns that ran on the code with F1 and F2 stay recorded and are not
counted toward the seven. Alternative not taken: finish S18 on the old code and correct in a
later batch.

## Corrections

- **F1** (`layer/index.c`): `struct moobl_ix_inode` gains `whole`. It is set when
  `index_load()` stores what it read, and when `moobl_index_set()` makes a new record in a
  domain that is already built: a built domain read every file, so a file with no record had no
  typed attribute. `rec_whole()` now asks `rec->whole` as well as the state of the domain.
  A record made by a write during a build stays not whole, and a later rename or link reads the
  file from the lower filesystem. That is at worst a reading more, the behavior before the
  domain was built.
- **F2** (`layer/main.c`, `layer/index.c`): `moobl_unlink()`, and `moobl_rename()` over a
  target that keeps other names, call the new `moobl_index_unlinked()`. It calls
  `index_touched()`, so a loader reading the file through the removed name reads again and
  finds the name gone. If the record already carries the removed name, because the store came
  between the lower unlink and the hook, the build in progress starts again (`restart`) or a
  built domain is built again by its next query (`paths_stale`), as a directory rename does.
  If the name cannot be computed (no memory), the record is treated as carrying it.
  Cost: a rebuild of the domain when the removed name is the recorded one.
- Values computed before a lock and used after it (the `CONTRIBUTING.md` check):
  - The removed name is computed before the index lock. The caller holds the parent's
    `i_rwsem` (unlink) or the rename locks, so the name cannot change meanwhile.
  - `made` in `moobl_index_set()` and `rec->whole` are read under the same lock section that
    uses them.
- Test infrastructure:
  - `rename_hook_delay_us`, a module parameter (root-only, 0 by default, one predicted branch
    while 0), holds the file branch of the rename hook between the lower rename and the index
    lock. It is listed by `moobfs diagnose` like the other test-only parameters.
  - `tests/hookrace.c`, with the suite step `hookrace` on ext4, btrfs and XFS.
  - The `rename` mode reproduces F1, and checks that the build had ended when the hook ran.
  - The `unlink` mode reproduces F2. It times a twin domain first, then unlinks the second
    name in the middle of the last reading. It checks the answer of the mount-wide `QUERY`
    after the dentry cache is dropped. The suite requires a repeated reading in the tracepoint,
    or the unlink missed the window.
- Specification `docs/spec-query-v2.md` §9 states both cases. The model gains the switches
  `WholeFlag` and `UnlinkHook`, and the negative controls `IndexLoadPreF1` and
  `IndexLoadPreF2`.

## Evidence

- `make tla-check` (TLC 2.19, Java 17, 12 workers, Apple M-series laptop): all eight
  configurations as expected. `IndexLoad` passed with the state graph exhausted: 826,876
  distinct states, depth 59. `IndexLoadPreF1` violated `NoLostUpdate` and `IndexLoadPreF2`
  violated `FreshPath`, as required. The other controls failed as before.
- Two-file run of `IndexLoad` with all four invariants (`Files = {f1, f2}`, `MaxOps = 2`,
  outside CI, 10:35–10:59): no violation, 267,198,325 distinct states, depth 82.
- `make checkpatch`: exit 0. `gcc -static -O2 -Wall -Wextra -Werror` of the test: clean.
  `shellcheck` of `formal/tlc.sh` and `vm/suite.sh`: clean.
- Focused native VM runs: Mac mini, Debian 6.12.113 KVM guest, `MOOB_ONLY=hookrace`, CPUs 0–3
  (S18 r05 holds 4–7).
  - **Corrected:** `HOOK_RACE_PASS` on ext4, btrfs and XFS, each with
    `readings_repeated=1`, `MOOBFS_ONLY_PASS`, exit 0, source hashes unchanged (10:51).
  - **Without the F2 correction** (only the two calls of `moobl_index_unlinked()` removed):
    `rename` passed, and `unlink` failed with `QUERY on x does not return the file: its record
    names the removed n2` (10:51).
  - **Without the F1 correction** (only `rec_whole()` reverted to the domain state): `HOOKRACE_FAIL: [rename] a query on x does not return the renamed file: its record holds
  only y`, first at 10:36 and again with the final test bytes at 10:52.
- Focused KASAN/UBSAN/lockdep run (`vm/kasan-local.sh`, kernel 6.12.112 with the pinned
  config, Mac mini CPUs 2–3, 10:52–11:13): `MOOB_ONLY="hookrace buildrace permchurn permgaps"`
  ended in `MOOBFS_KASAN_ONLY_PASS` with exit 0 and unchanged source hashes. Every step passed
  on ext4, btrfs and XFS (`HOOK_RACE_PASS` each with `readings_repeated=1`; `BUILD_RACE_PASS`;
  six `PERMCHURN_PASS` seeds per lower; `PERMISSION_GAPS_PASS`). The console has no `BUG:`,
  KASAN, UBSAN, lockdep or call-trace line; its only `Warning:` is the firmware notice at boot.
- Full native suite (`make vm-test`, Debian 6.12.113 KVM guest, Mac mini CPUs 0–1, source
  `baf00ef`, 10:54–11:16): exit 0, `MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS`,
  `HOOK_RACE_PASS` on all three lowers, no kernel diagnostic line, source hashes unchanged.
- Full KASAN/UBSAN/lockdep suite (`vm/kasan-local.sh`, kernel 6.12.112 with the pinned
  config, Mac mini CPUs 2–3, source `407a3d0`, same kernel and test code as `baf00ef`,
  11:14–13:15): exit 0, `MOOBFS_KASAN_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `HOOK_RACE_PASS` on all
  three lowers, no `_FAIL` marker, no `BUG:`, KASAN, UBSAN, lockdep, call-trace or `WARNING: CPU`
  line, source hashes unchanged. The only warnings in the console are the expected JSON
  notices of `moobfs diagnose` (no memory limit; a test parameter set by the step under test).
- Pre-push checks (Mac mini, a clone of `24a04ce`, at low priority beside the S18 campaigns,
  13:34–13:40): `make lint`, `make checkpatch`, `make test` (9 tests), the same under clang
  with address and undefined-behavior sanitizers, `make hostile-test` under sanitizers (50
  trees, `HOSTILE_USER_PASS`), `make pylib-test` (43 tests, 13 skipped for missing root or
  optional packages, as before) and the retired-claims check all passed, with no sanitizer
  report. `tests/hookrace.c` and `tests/coldmove.c` build with the suite's flags on Ubuntu
  24.04, the CI KASAN job's userland.
- Earlier attempts, kept in `~/b11night/f12/attempt*` on the test host, are errors of the test,
  not of the kernel:
  - (1) `-Wformat-truncation` stopped the build of the test.
  - (2) A fixed delay missed the reading of the second name; the window check caught it.
  - (3) `reap()` cleared `scan_delay_us` before waiting, so the timing build did not sleep.
  - (4, 5) The test queried with `QUERY2`, which by design never answers a file with two
    names (heterogeneous, spec §3), so it failed with and without the correction. The
    negative log of attempt 4 was overwritten by attempt 5, which ran the same code with more
    diagnostics.

## Branch

`claudiofp/b12-records` (not pushed) was rebased on its own base `a1f313b` with `--signoff`,
which added the missing DCO trailers; every tree is unchanged, and the commit hashes cited by
earlier local records changed (journal 0264 notes the one it cites).

## Limitations

- F2 for a homogeneous record needs the unlink between the loop-head check and the
  classification in `index_load()`, a window of microseconds that the test does not reach;
  the model covers it (`IndexLoadPreF2`). The test covers the heterogeneous case, where the
  mount-wide `QUERY` exposes the stale name.
- The model's bounds are those of `formal/README.md`: one file in CI, and a two-file run once,
  outside CI.
- Rename over a file that keeps other names calls the new hook but has no test of its own.
  (2026-11-05: tested since journal 0268, mode `renameover`.)

## Next steps

The S18 campaigns on the corrected code (journal 0248); the pull request of the batch, merged
only with the maintainer's approval after its checks pass.
