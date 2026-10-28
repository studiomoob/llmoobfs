# 0156 — Round 2 of the independent review: findings verified, fixed, tested or recorded

Date: 2026-10-20. Status: done; pull request 109, merge awaiting the maintainer.

*Dated clarification, 2026-10-20 (journal 0170):* the table and limitations below
retain earlier checkpoints. ADR-0017 was subsequently accepted in `7a5adda`,
and the fail-nth tests subsequently ran in CI. The follow-up at the end records
the later local validation and supersedes the earlier next-step order. None of
this follow-up has been pushed yet.

## Goal

Take in the round-2 review of `3627172` (journal 0155, branch `review-round2`), verify it, fix
what is confirmed, close the test gaps the maintainer named, record the rest, and draft the
project's position on the design objections. All under the batch cadence: one suite and one
KASAN run for the whole batch.

## How the review entered

- `review-round2` merged without rewriting its commits (`9d74080`). Its journal was written as
  0153, but `main` had given 0153 and 0154 to other work meanwhile. It became 0155, with a
  dated note (`8d96942`). The comments of its three test files still say 0153; they were not
  changed, as asked.
- The reproducer `tests/review-3627172-vm.sh` was run before any change, once at `3627172`
  and once at `main` (`02381fb`, which has the round-1 fix). Both runs showed the defect lines
  of R2-1 and R2-2, with the controls as expected.

## Findings

| Finding | Verdict | Commit | Test | Limitation |
| --- | --- | --- | --- | --- |
| R2-1 (F14), names deeper than 64 components | confirmed, reproduced; fixed | `dd4ee3a` | reproducer with expectations inverted: the deep runs give `before=0 exists=0`, control unchanged | deeper names are not indexed (documented limit) |
| R2-2, same-directory rename during a reading | confirmed, reproduced; fixed | `dd4ee3a` | reproducer: the race now names `b` | — |
| R2-3, partial record carried into another domain | confirmed from the code; fixed | `281a719` | `failnth` setxattr-then-move | test not run yet (KASAN harness) |
| R2-4, failed upper inode wedges the name; link skips hooks | confirmed from the code; fixed | `d7f244d` | `failnth` create, mkdir, link agreement | test not run yet (KASAN harness) |
| R2-5, build root path before `top_gen` | confirmed from the code; deferred | — | — | issue proposed |
| R2-6, failed path recording keeps the old name | confirmed from the code; fixed | `281a719` | none (allocation of one page under the lock) | untested |
| R2-7, `over_budget` mount-wide | likely; deferred | — | — | issue proposed |
| R2-8, emptied record never freed | confirmed from the code; fixed | `281a719` | `failnth` chmod-file counters | test not run yet (KASAN harness) |
| R2-9, batch partial write | confirmed from the code; deferred | — | — | issue proposed |
| R2-10, dead directory fails a build | confirmed against 6.12 (`IS_DEADDIR`); fixed | `fb602d8` | none (needs a directory removed between listing and lookup) | untested |
| R2-11, domain records never freed | confirmed from the code; deferred | — | — | issue proposed |
| R2-12, oversized attribute list left out | decision: keep the exception, write it in spec §9; `moobfs-check` made to report it, as the code's comment already claimed | `3237804` | none | — |
| T2, cursor MAC never deciding | gap closed | `8c70ac6` | three bytes changed outside the hash give `ESTALE`; negative round without the MAC fails | — |
| T3, ACL on the domain directory | gap closed | `c0aeac3` | `HOMOGENEITY_GAPS_PASS`; negative round without `ext_acl(dom)` fails | — |
| T4, writes under a private directory | gap closed | `c0aeac3` | `HOMOGENEITY_GAPS_PASS`; negative round without the intermediate walk fails | links not covered (they are heterogeneous anyway) |
| T5–T11 | recorded | — | — | issue proposed |
| V1, V3, V5, V7 | recorded | — | — | issue proposed |
| V2 | documented (README limits) | `cfed44b` | — | not fixable without the lower's mapping |
| V4, M5 | fixed with R2-4: ref-walk revalidation checks what the RCU path checks | `d7f244d` | the suite, end to end | — |
| V6, `RENAME_WHITEOUT` | fixed: refused with `EINVAL` | `fb602d8` | `query whiteout` in the fs steps | — |
| V8, unmasked `i_flags` copy | fixed at all five sites, not only the rename one | `fb602d8` | the suite, end to end | no dedicated test |
| V9 | comment corrected | `cfed44b` | — | — |
| M1–M4, M6–M8 | recorded | — | — | issues proposed |
| D1–D3 | position proposed in ADR-0017, for the maintainer | `aee15b4` | — | not accepted yet |

## Decisions

- **R2-1.** The scan leaves out what lies beyond the depth the write paths can place. The
  alternative was to mark the domain lost, or to set the fallback, when a placement fails. It
  does not work: with the walk failed, the domain is unknown, and the only safe fallback would
  be mount-wide, which any user could trigger with a deep tree. The limit applies only with
  domains on; with `domain_depth=0` the hooks place nothing by depth.
- **R2-2.** `index_load()` gives up when the dentry's name is no longer the last component of
  the listed path, as it already did for a dentry that left its parent; the rename hook has
  adopted the file under its new name. The review's second suggestion ("a loader that stored a
  fresher reading makes older ones give up") alone does not close it: if the scan stores after
  the adoption, it still overwrites the right name.
- **R2-3, R2-8.** Any failure while updating a record drops the whole record (`rec_drop()`),
  and the two hooks that call `rec_set_partition()` free a record it emptied. With no record,
  `rec_whole()` cannot vouch for a partial one, and a rename or link reads the file again from
  the lower.
- **R2-4.** The ecryptfs pattern at 6.12: `create` unlinks the lower file when the upper inode
  cannot be made; `mkdir`, `symlink` and `link` drop the negative upper dentry. `link` also
  updates the link count and calls the permission hook whenever the lower link exists.
- **R2-12.** Failing the build on an oversized list would let anyone able to write such a file
  in a shared domain make the domain unanswerable; the exception is kept and written down.
- **F13.** The round-1 finding had never entered the ADR-0011 list; it is recorded there now,
  with a dated note, beside F14.

## Things found on the way

- The negated checks of the inverted reproducer (`! grep -q …`) do not stop a `set -e` script;
  they became `if grep -q …; then exit 1; fi`.
- `mkdir -m` in busybox runs `chmod` after `mkdir`, and a directory chmod sets the fallback. The
  T4 test takes its modes from `umask`.
- The first T2 negative round ran the old suite: the merge into the negative worktree had
  failed on a pending change. It was redone after restoring the file; only the second run is
  evidence.
- `fsstack_copy_attr_all()` was also used at inode creation, not only in the rename the review
  named (V8).

## Evidence

- `tests/review-3627172-vm.sh`: at `3627172` and `02381fb`, defect reproduced; with the fixes,
  passes with the expectations inverted.
- `MOOB_ONLY=ext4 make vm-test`: `HOMOGENEITY_GAPS_PASS`, `QUERY2_ORDER_PASS` (with T2) and the V6
  check pass.
- Negative rounds, branch `neg/homogeneity-gaps`: without `ext_acl(dom)`,
  `t3-acl-on-domain-directory-leaks-ext4`; without the intermediate walk,
  `t4-private-directory-leaks-ext4`; without the cursor MAC, `t2-tampered-cursor-accepted-at-5-ext4`.
- `make checkpatch` clean; the module builds with `W=1` and no warning; `tests/failnth.c` and
  `tests/query.c` build with `-Wall -Wextra -Werror`.
- Full `make vm-test` of the batch (Docker/QEMU, Debian 6.12.111): exit 0 in 516 s.
  `HOMOGENEITY_GAPS_PASS` and `FCNTL_FLAGS_PASS` on ext4, btrfs and XFS; `BUILD_RACE_PASS` with
  the chmod check on all three; `BIGDIR_PASS`, `XATTRREAD_PASS`, `MOOBFS_ACCEPTANCE_PASS`,
  `MOOBFS_LAYER_PASS`. The Debian harness has no `fail-nth`, so `FAULT_INJECTION_PASS` is not
  part of it.
- KASAN/UBSAN/lockdep in the CI of pull request 109 (run 39466977662): `MOOBFS_KASAN_PASS`,
  `HOMOGENEITY_GAPS_PASS` on the three filesystems, and `FAULT_INJECTION_PASS: operations=22
  calls=322 faults=300 unexpected=0`, the first run of the extended `failnth`. The new checks
  saw faults: `create` 8, `link` 4, `chmod-file` 1, `setxattr-then-move` 2; no state check
  reported a difference.

## Limitations

- The `failnth` state checks for R2-3, R2-4 and R2-8 passed, but had no negative round: they
  were not run with the fixes undone. With one or two faults per operation, nothing shows that
  the injected failure landed on the fixed path.
- R2-6 and R2-10 have no test of their own.
- The F14 advisory exists as a private draft (GHSA-qgjj-2f2p-q82m), to be published after the
  merge.
  *Note, 2026-10-28:* it was not published after that merge; it was published on 2026-10-28
  (https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-qgjj-2f2p-q82m), without a CVE
  (journal 0225).
- Deferred to issues, opened with the pull request: R2-5 #110, R2-7 #111, R2-9 #112, R2-11
  #113, T5 to T11 #114, V1, V3, V5, V7 #115, M1 #116, M2 #117, M3 #118, M4, M6, M7, M8 #119.
- ADR-0017 is a proposal.

## Next step

Merge, then publish the F14 advisory. Then a negative round for the new `failnth` checks in the
KASAN harness, and the round-3 review request (filled in outside the repository).

## 2026-10-20 — Validation follow-up in a separate worktree

The latest CI on head `09fc102`, run `39472813146`, executed the sanitizer suite:
`MOOBFS_KASAN_PASS`, homogeneity checks on ext4/btrfs/XFS, and 22 fail-nth
operations, 322 calls, 300 faults, `unexpected=0`. Its create fault count is 9,
not the 8 in the earlier run above. These are separate executions.

The later local work is recorded in journal 0170:

- R2-3: removing the whole-record cleanup makes `setxattr-then-move` miss the
  moved file in its new domain; the negative run fails.
- R2-4: removing recovery/revalidation makes create and mkdir leave names
  visible on only one side; the negative run fails. This does not independently
  isolate link's recovery branch.
- R2-8: the original shared-domain test missed the mutation. A fresh built
  domain per chmod attempt requires a new partition node and detects the leaked
  empty record when cleanup is removed.
- R2-6 and R2-10 now have dedicated VM regressions and negative mutations.
  The former checks rename after a confirmed allocation failure and dentry
  eviction; the latter removes a directory after its names were listed, using
  function tracing to synchronize the race. Both are ext4/root scenarios.
- ADR-0017 is accepted; its measurement, consistency-mode and differential-test
  commitments remain planned. Issues 110–119 remain deferred and open.

The historical table's "test not run yet", "untested" for R2-6/R2-10 and
"ADR-0017 is a proposal" no longer describe this local preparation. Journal
0170 distinguishes all positive, negative and complete-suite results and their
limits. No new production-code change is made to the PR's module.

Current next step: review the local continuation before authorizing push, then
run the required PR CI on the published head. Merge and advisory publication
still need their own authorization; the earlier merge-before-negative order
does not apply to this preparation.
