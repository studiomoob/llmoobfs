# 0233 — Suite gaps of issue #168: splice in the set-user-ID check, permmatrix on three lowers, rootpath under KVM

Date: 2026-10-29. Status: on the branch `claudiofp/b7-vm-gaps` (from `main` at `7ac52cd`), for
the coordinator of batch 7 to merge; not pushed. Work started about 12:25 UTC and ended about
13:32 UTC. Wall-clock time, with the test-host runs, not an engineering-effort measurement.
Estimated effort: S; actual: S.

## Goal

Close the test-suite items of issue #168 that need only test changes (ADR-0022: no change to
`layer/`, `include/` or the format):

1. Journal 0215: the `permmatrix` cases ran on ext4 only.
2. Journal 0218: the set-user-ID check of GHSA-6xf4-r246-92r6 did not exercise splice.
3. Journals 0225 and 0228 (batch 5 comment on #168): under `MOOB_VM_KVM=1` the `rootpath`
   step stopped twice at `MOOBFS_VM_FAIL: rootpath-race-inconclusive-btrfs`.
4. Optional: the per-result check over a lower labelled by SELinux (journal 0215, S12).

## Decisions

- **Splice and sendfile join the three set-user-ID blocks of `tests/layer.c`.** `as_user()`
  gains `how 8`, `splice(2)` of four bytes from a pipe into the descriptor, and `how 9`,
  `sendfile(2)` of 4 KiB from the source file. Both reach the layer's `->splice_write`
  (`moobl_splice_write()`), not `->write_iter`: `sendfile` to a file goes through
  `do_splice_direct()` and `do_splice_from()` (references §31). They run in the block where the
  bit is visible to the layer, in the file-capability block, and in the block where the bit is
  given on the lower behind the layer, with and without an earlier write that caches `S_NOSEC`
  (the case of the advisory). `copy_file_range` was already covered (`how 3`). A kept bit now
  prints which call kept it (`suid: call <n> left mode ...`, `suidlow: call <n> ...`), since the
  `CHECK` line alone does not tell the calls of a loop apart. Splice and sendfile run first in
  the first block, because `sendfile` writes at the descriptor's position and the block's
  closing content check expects the `S` block that `copy_file_range` writes at offset 0.
- **`permmatrix` runs once per lower.** The whole step (twelve seeds in partition and fallback
  mode, and the three search-only fixtures after it) is wrapped in the `for fs in ext4 btrfs
  xfs` loop the other steps use; every failure names the lower, and `PERMMATRIX_STEP_PASS`
  is printed once per lower with its name. The step's own guards (`pm_wrong > 0`, `pm_prev > 0`)
  now hold per lower, so each lower must meet cases the earlier rules got wrong.
- **`rootpath` starts A when B is in the domain's build, not after a fixed wait.** What
  "inconclusive" means (exit 3 of `tests/rootpath-race.c`): A, the QUERY2 that took the domain's
  name, was not seen waiting in `D` for 5 ms, or B, the mount-wide QUERY holding the build mutex,
  had returned before the rename, or the build counter shows no build by A after B's. The test
  started A 100 ms after B; under KVM B's whole btrfs build took 0.10 s (journal 0225), so A
  arrived after it. A now starts once `GET_INFO`'s `domain_builds` shows B's first build (the
  part outside the domains, `domain_ensure()` of domain 0, which `domain_ensure_all()` runs
  before the domain loop) done, polled every millisecond for at most 2 s, plus 2 ms for B to take
  the domain's mutex. The conclusive conditions are unchanged, so what a pass proves is
  unchanged; the negative round below shows the step still catches issue 110 on btrfs under
  KVM.
- **An inconclusive attempt plants twice the files for the next.** Up to four attempts
  (previously three on the same tree): each starts from a fresh tree and a cold mount, and after
  an inconclusive one the file count doubles (10 000 to 80 000) when `statfs` shows the free
  inodes for it, or reports none (btrfs). The 64 MiB ext4 image has 16 384 inodes, so ext4 stays
  at 10 000. Four inconclusive attempts still fail the step. `ROOTPATH_PASS` now names the size
  and the attempt.
- **SELinux per-result check (S12): not done.** `vm/selinux-local.sh` boots a virtme-ng guest of
  the host's own kernel and needs the SELinux policy packages installed on the host and modules
  inserted into its policy store with `sudo semodule`; there is no quick path in the `make
  vm-test` harness, and the dedicated host is shared. The item stays open on issue #168.

## Alternatives

- For `rootpath`, only raising `ROOTPATH_FILES`: rejected alone, because the fixed wait was the
  cause on btrfs and a larger tree slows every TCG run; kept as the fallback for slower or
  faster guests than seen here.
- For `rootpath`, starting A as soon as B is forked: rejected, because A could then take the
  domain's mutex before B and the test would race a different interleaving whose `D` state
  could be A's own disk reads.
- Widening the window from the kernel (a delay in `domain_ensure()`): rejected, frozen by
  ADR-0022 and against the test's rule that the module is not changed for it.

## Changes

- `tests/layer.c`: `how 8` (splice) and `how 9` (sendfile) in `as_user()`; added to the three
  set-user-ID blocks; messages naming the call that kept a bit. Commit `664ef61`.
- `vm/suite.sh`: `permmatrix` over ext4, btrfs and XFS. Commit `9434d24`.
- `tests/rootpath-race.c` and `vm/suite.sh`: A started from the build counter; four attempts,
  doubling where the lower has room; `ROOTPATH_PASS: <fs> (<n> files, attempt <k>)`. Commit
  `b0c8e59`.
- `docs/testing.md`: the `LAYER_PASSTHROUGH_TESTS_PASS` row (splice, sendfile); a new section
  for `permmatrix` with its markers; the `rootpath` paragraph and `ROOTPATH_PASS` row.
  `docs/retired-claims.txt`: "Up to three attempts per lower". `docs/references.md` §31,
  `CHANGELOG.md`, this journal and its row in `docs/history/README.md`.

## Evidence

All on the dedicated host (Debian 13, KVM), Debian guest kernel 6.12.111, from a git copy of
the branch in `~/b7b/src`; times UTC.

| Run | Command (`make vm-test` in the copy) | Result |
| --- | --- | --- |
| first try, 12:31 | `MOOB_VM_KVM=1 MOOB_ONLY="ext4 btrfs xfs permmatrix rootpath"` | `FAIL line 746: pread(fd, buf, 1, 0) == 1 && buf[0] == 'S'`, `MOOBFS_VM_FAIL: layer-tests-ext4`: an error of the new test, not of the layer (sendfile, then last in the loop, wrote at the descriptor's position and not at offset 0); fixed by the order above |
| positive, 12:32–12:38 | same | exit 0, `MOOBFS_ONLY_PASS`; `LAYER_PASSTHROUGH_TESTS_PASS` (six lines), `LAYER_LOWER_PASS` on ext4 (`reflink=no`), btrfs and XFS (`reflink=yes`), so the bit was cleared after each splice and sendfile; `PERMMATRIX_STEP_PASS: ext4`, `: btrfs` and `: xfs`, each with 281 answers the rule before 0116 gave wrongly and 12 the rule before 0215, and `QUERY_BUILD_MOVED_PASS` on each; `ROOTPATH_PASS` on the three lowers at 10 000 files, attempt 1. No kernel diagnostic |
| negative, splice, 12:39 | same tests over a module whose `moobl_splice_write()` passed the mounter's credentials (`ctx.cred = get_cred(ctx.cred)` in place of `moobl_wcred_get()`, copy only), `MOOB_ONLY="ext4 btrfs"` | `suidlow: call 8 (after a write 0) left mode 4666`, `FAIL line 790`, `MOOBFS_VM_FAIL: layer-tests-ext4`, exit 2. The block where the layer sees the bit passed: the layer's own `file_remove_privs()` clears it there, as journal 0218 explains |
| negative, sendfile, 12:40 | same module, sendfile ordered before splice in that block (copy only), `MOOB_ONLY=ext4` | `suidlow: call 9 (after a write 0) left mode 4666`, `FAIL line 790`, exit 2 |
| negative, rootpath, 12:41 | `MOOB_VM_KVM=1 MOOB_ONLY=rootpath` over a module that records `ix->top_gen` instead of the sample taken before the name (the defect of issue 110, one line of `domain_ensure_dom()`, copy only), btrfs first in the loop | A in `D` after 6 ms, rename at 0.016 s, B 0.100 s, 3 builds; `0 results (expected 50)`, `ROOTPATH_RACE_FAIL`, `MOOBFS_VM_FAIL: rootpath-race-btrfs`, exit 2: on btrfs under KVM, where the old step was inconclusive, the step now concludes and catches the defect |
| KVM ×3, 12:42–12:46 | `MOOB_VM_KVM=1 MOOB_ONLY=rootpath`, three runs | exit 0 each; `ROOTPATH_PASS` on ext4, btrfs and XFS, all at 10 000 files, attempt 1. btrfs: A started 0–1 ms after B, rename at 0.014, 0.016 and 0.021 s, B's build 0.096, 0.113 and 0.119 s. ext4: A started after 120 to 320 ms (the cold outside part), rename at 0.153 to 0.389 s, B 0.282 to 0.526 s. XFS: rename at 0.012–0.013 s, B 0.184 to 0.400 s. 4 builds during each race; no kernel diagnostic |
| TCG, 12:46–12:50 | `MOOB_ONLY="permmatrix rootpath"` (no `MOOB_VM_KVM`) | exit 0, `MOOBFS_ONLY_PASS`; `PERMMATRIX_STEP_PASS` on ext4, btrfs and XFS; `ROOTPATH_PASS` on the three at 10 000 files, attempt 1 (B 1.56 to 1.85 s, rename at 0.040 to 0.238 s) |
| doubling, 12:51–12:52 | `MOOB_VM_KVM=1 MOOB_ONLY=rootpath` with the starting size lowered to 300 files (copy only) | the fallback path: ext4 inconclusive at 300, 600 and 1 200 files, `ROOTPATH_PASS: ext4 (2400 files, attempt 4)`; btrfs and XFS inconclusive at 300 and 600, pass at 1 200 (attempt 3); exit 0 |
| final tree, 13:10–13:29 | `MOOB_VM_KVM=1 MOOB_ONLY="ext4 btrfs xfs"` on the committed code (with the messages naming the call) | exit 0, `MOOBFS_ONLY_PASS`; `LAYER_PASSTHROUGH_TESTS_PASS` six times, `LAYER_LOWER_PASS` on ext4 (`reflink=no`), btrfs and XFS (`reflink=yes`); slower than the 12:32 run because other guests shared the host |

The negative rounds' changes were made with `sed` in the host copy only and never committed;
the copy was then replaced by a fresh one of the branch. `make lint` on the host (`shellcheck`
and the SPDX check): clean. `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
`make checkpatch` was not run: no kernel source changed.

## Limitations

- The SELinux per-result check (S12) did not run (see Decisions).
- The doubling of `rootpath` was not needed in any run here: every positive attempt concluded
  at 10 000 files on the first attempt. The doubling path was exercised only with a lowered starting size (the last row of the
  table).
- `permmatrix` on btrfs and XFS ran under KVM and TCG with the Debian 6.12.111 guest only, not
  under the KASAN/lockdep kernel; the batch's full KASAN run covers it.
- The splice and sendfile cases ran inside the `ext4`, `btrfs` and `xfs` steps under KVM
  (`LAYER_PASSTHROUGH_TESTS_PASS` six times in that log). They did not run under
  TCG here (the `ext4`, `btrfs` and `xfs` steps were not part of the TCG run).
- Issue #168 itself was not edited; its items 0215 (btrfs/XFS variants), 0218 (splice) and the
  `rootpath` comment can be checked off by the coordinator when this branch is on `main`.

## Next step

The coordinator merges the branch into batch 7; the batch's full `make vm-test` (TCG and
`MOOB_VM_KVM=1`) and KASAN/lockdep suite run once before publication. S12 stays open.
