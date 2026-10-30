# 0242 — Plan for S14 to S21; static analysis in CI (S5, S19); mutants of the homogeneity rule (S14)

Date: 2026-10-29. Status: implemented on branch `claudiofp/b11-s14`, run locally on an arm64
workstation with Docker; `.github/workflows/static.yml` has not run on GitHub. *Update,
2026-10-30:* the mutants ran in a dedicated VM; tests were added for the five that survived, and
a run of the whole catalogue killed all 23 (section "Mutants in the VM"). Work started:
2026-10-29 23:09 UTC; ended about 23:45 UTC; VM runs and tests 2026-10-30 11:18 to 17:35 UTC,
with a power cut of the test host from 14:27 to 15:58 UTC that interrupted none of them. Wall-clock time,
mostly image builds and checker runs; not an engineering-effort measurement. Estimated effort:
S19 M, S14 M (of which this journal does the offline half); actual: about 40 minutes of attended
work for both, the rest of S14 still open.

## Goal

`docs/security-roadmap.md` lists S14 to S21 ("making the tests find what reviews found",
journal 0156) as planned and not started. Give each item a concrete plan, effort and done check,
and implement the items that need no VM and no kernel change and can be validated on a
workstation or in GitHub's CI: S19 (static analysis in CI), which also closes the open part of
S5, and the offline part of S14 (the mutant catalogue and its checks).

## State found

- `.github/workflows/`: `checkpatch.pl` in the `Lint` job (S5, journal 0050); CodeQL; no sparse,
  smatch, Coccinelle, clang analyzer, `W=1` or kernel-doc job. Journal 0056 had run sparse and
  `W=1` once by hand, with nothing reported.
- `tests/permmatrix.c` (journals 0172, 0233): seeded random trees and identities compared with
  what the lower allows, on static trees; no sequences of changes between queries (S15).
- The negative rounds of T2 to T4 (journals 0172 and later) removed checks by hand; no
  catalogue of mutants, no harness (S14).
- `vm/syzkaller-local.sh` exists (S3); no nightly schedule (S18). No gcov build (S17), no
  consistency mode (S16, a kernel change), no model (S21). `fuzz.yml` (journal 0237) is S7.

## Decisions

- **One pinned image for every checker, used by `make static` and by CI.** `tests/static/Dockerfile`
  starts from the `vm/Dockerfile` base image (same digest) and installs Debian's headers of
  6.12.113, the kernel of the `Native Linux VM (Debian 6.12.113)` job, for the gcc-based checks,
  and the upstream 6.12.112 tarball of the KASAN job (same SHA-256), configured with
  `LLVM=1 defconfig` and `modules_prepare`, for the clang analyzer and for the kernel's own
  Coccinelle scripts, which Debian's header package does not ship. Debian's headers refuse clang
  (`-fmin-function-alignment=8`, `-fconserve-stack`), and the kernel's `clang-analyzer` target
  exists only in a tree configured for clang. sparse and Coccinelle are pinned to their Debian
  versions, smatch to a commit (no Debian package), and `scripts/check-pins.sh` checks the
  copies of the kernel pin, the base image and the Debian kernel. The image builds on amd64
  (CI) and arm64. Rejected: Ubuntu's runner headers (a kernel the project does not support);
  separate jobs per tool (four image builds for 80 s of checks).
- **The kernel's own entry points.** `W=1` (gcc and clang), `C=2 CHECK=sparse`,
  `C=2 CHECK="smatch -p=kernel"`, `make coccicheck MODE=report`, `make clang-analyzer` and
  `scripts/kernel-doc -Wall -none`, each on a fresh copy of the sources, over `layer/`,
  `kernel/` and `include/` (the UAPI header, which S5 asked kernel-doc for). A checker whose
  build fails fails the run.
- **A baseline with reasons, both ways.** `scripts/static-triage.py` turns each finding into a
  key: tool, path, message with line numbers removed, and the text of the reported line, so a
  key survives edits that only move lines. A finding inside a kernel header is attached to the
  last frame of its trace in the repository, so that a baselined finding in `list.h` does not
  hide a new one elsewhere. The run fails on a finding not in `tests/static/baseline.txt`, on a
  baseline entry no checker reports any more, and on an entry without a justification comment.
  The compiler's own warnings in the sparse and smatch builds are counted once, under `w1`.
- **A canary per checker.** `tests/static/canary/canary.c`, never loaded and outside the
  modules, holds one defect for each checker (an unused-but-set variable, a `__user`
  dereference, a write past an array, a hand-written `ARRAY_SIZE`, a use after `kfree`, an
  undocumented kernel-doc parameter); `tests/static/canary-expected.txt` names the finding each
  checker must report. A checker that stops working (a package missing, a command line changed,
  an output format the parser no longer reads) fails the run instead of passing with nothing.
- **Findings justified, none fixed.** ADR-0022 freezes the kernel code, and none of the six is a
  defect (below). The `min()` suggestion waits for the next change of `scan_child()`.
- **The workflow is not a required check** until it has run green on `main`, as with the KASAN
  job before it.
- **S14 offline first.** `tests/mutation/homogeneity.mut` holds 23 mutants over `reach_ok()`,
  `target_ok()`, `ext_acl()`, `place_dentry_above()`, `index_load()` and the scan's and the
  rename's `inner_ok`, each with the `MOOB_ONLY` steps expected to fail it, written as exact
  lines and their replacement with the same line count. `make mutants-check` (same image)
  checks that each applies at exactly one place, builds, and changes the object code of
  `layer/index.o`; an unchanged object is an equivalent mutant (trivial compiler equivalence,
  Papadakis et al., ICSE 2015). Original and mutants are built at one fixed path with the debug
  sections stripped, because `WARN_ON` keeps the file name and the line table moves with the
  code. `scripts/mutants.py run` runs each mutant's steps with `make vm-test` from a copy of
  `HEAD` (never the working tree) and calls a mutant killed only when the console has a
  `MOOBFS_VM_FAIL:` marker other than the module or loop load; it has not been run, because
  this session has no VM. Rejected: a generic mutation tool (Mull, Dextool) over the whole
  module: it would mutate code with no bearing on the rule and needs the module built as a
  user-space target.
- **S20's checklist added now** (`CONTRIBUTING.md`): it is text, and the procedure itself waits
  for the next change of the rule, which the freeze defers.

## Plan per item

Recorded in `docs/security-roadmap.md`, "Plan per item", with the done checks. In short:

| Item | Effort | Next step | Needs |
| --- | --- | --- | --- |
| S14 | M | `scripts/mutants.py run` on the test host; a test for each survivor; a weekly CI job | VM (KVM) |
| S15 | M | seeded sequences of chmod, chown, setfacl, rename, link and unlink in `permmatrix`, with and without `scan_delay_us` | VM |
| S16 | M | a test-only consistency parameter; a kernel change, so a decision on ADR-0022 first | VM, decision |
| S17 | M | gcov kernel and layer in a CI job with KVM, `lcov` branch report as an artifact | CI |
| S18 | S + a week | nightly `vm/syzkaller-local.sh` on the test host | test host |
| S19 | M | first green run of `static.yml` on GitHub, then a required check | CI |
| S20 | S + one change | one rule or protocol change through the procedure | — |
| S21 | L | TLA+ model of the loader and hooks, TLC in CI, after S14 to S16 | CI |

## Findings and their triage (none is a defect)

| Checker | Where | Finding | Why it is not a defect |
| --- | --- | --- | --- |
| clang analyzer | `scan_run()`, twice (the loop and `scan_list_free()` through `list_del()` in `list.h`) | use after free of `cur` | The analyzer does not keep the circular-list invariant: after `list_del(&cur->list)` it still takes `head->next` for the freed entry. `list_del()` of a well-formed list moves `head->next` before `scan_dir_free()`. |
| clang analyzer | `rec_set_partition()` → `entry_free()` | use after free of `e` | Same model gap: the entry freed when `name_get()` fails was unlinked from `rec->entries` first, so the loop that empties the list never meets it. |
| clang analyzer | `attr_buf_add()` | `memcpy` to a NULL `b->data` | `data` is NULL only while `cap` is 0 (`attr_buf_init()`), and `need` is at least 3, so the first call always allocates; the analyzer assumes `need <= cap` with `data` NULL. |
| Coccinelle `d_find_alias.cocci` | `result_path()` | missing `dput()` | The reference goes to the caller through `*held` and is dropped with `dput(cand[i].held)` after the index lock is released. |
| Coccinelle `minmax.cocci` | `scan_child()` | `err = err < 0 ? err : 0;` could be `min()` | Style; the code is frozen. |

sparse, smatch, `W=1` (gcc and clang) and kernel-doc report nothing on `layer/`, `kernel/` or
`include/`. The layer has no `/**` comment, so kernel-doc checks the UAPI header only in
practice; it now runs on every change.

## Evidence

Host: Apple Silicon workstation, Docker Desktop (arm64 Linux VM). Image built from
`tests/static/Dockerfile`; `build/static/versions.txt` of the run: gcc 14.2.0, Debian clang
19.1.7, sparse 0.6.4, Coccinelle (spatch) 1.3 with OCaml 5.3.0, smatch at `5189e3fb`, the gcc
tree 6.12.113+deb13-arm64 headers, the clang tree 6.12.112.

```
make static
STATIC_TOOL clang: findings=4 canary=10
STATIC_TOOL clang-w1: findings=0 canary=9
STATIC_TOOL coccinelle: findings=2 canary=2
STATIC_TOOL kerneldoc: findings=0 canary=1
STATIC_TOOL smatch: findings=0 canary=2
STATIC_TOOL sparse: findings=0 canary=7
STATIC_TOOL w1: findings=0 canary=9
STATIC_OK tools=7 findings=6 ...                      (about 80 s once the image is built)
```

Negative control of the triage (baseline and expected list changed by hand, then restored):
one baseline entry removed, one bogus entry added, one impossible canary text added;
`MOOB_STATIC_TOOLS="coccinelle sparse kerneldoc" make static` printed `NEW` for the removed
entry, `STALE` for the bogus one, `CANARY_MISSED` for the impossible text, `STATIC_FAIL`, exit 2.
The first runs found two faults of the harness, fixed before the result above: spatch, run in
parallel by `coccicheck`, writes its per-job files in the tree's top directory, which the image
user could not write (the top directory is now writable); and `run()` of the shell script reused
the loop's variable names (POSIX `sh` has no local variables), so the clang analyzer step ran
under a wrong name.

```
make mutants-check
MUTANT_OK M01 ... MUTANT_OK M23
MUTANTS_CHECK_OK mutants=23                         (39 s)
```

Negative control: a 24th mutant that only changes a comment printed
`MUTANT_BAD M99: same object code as the original (equivalent)` and `MUTANTS_FAIL`, exit 2.

Lint: `shellcheck --shell=sh` over `vm/*.sh` and `scripts/*.sh` (the `koalaman/shellcheck:stable`
image, as `make lint` runs it; shellcheck is not installed on the host), `scripts/check-spdx.sh`,
`scripts/check-retired-claims.sh`, `sh scripts/check-pins.sh --offline` (`PINS_OK`); actionlint
1.7.7 (`rhysd/actionlint` image) reports nothing on `static.yml` (its two notes are on the
existing `fuzz.yml`).

*Added 2026-10-30, on PR 179:* the first run of `static.yml` on GitHub (amd64) failed before
any triage: the x86-64 defconfig sets `CONFIG_WERROR=y` (`arch/x86/configs/x86_64_defconfig`,
Linux 6.12.112) and arm64's does not, so on amd64 the clang W=1 build of the canary stopped at
its first warning (`STATIC_FAIL: clang-w1 on canary did not complete`). The image now turns
`WERROR` off with `scripts/config` and `olddefconfig` after `defconfig`; the next amd64 run
matched the baseline except one clang analyzer entry (the `attr_buf_add()` `memcpy` NullArg),
reported on arm64 only. `scripts/static-triage.py` now reads an `@arm64 ` or `@amd64 ` prefix on a
baseline entry: the entry is accepted everywhere and stale only on that architecture; checked
offline with empty logs on both machine names.

## Mutants in the VM (2026-10-30)

Host: the dedicated test host (x86-64, Debian 13, KVM), Debian kernel 6.12.113 in the guest
(`MOOB_DEBIAN_KERNEL=6.12.113 MOOB_VM_KVM=1`). The tree is copied without `.git`, so a local
snapshot commit gives `mutants.py run` the `HEAD` it archives. One change on the host copies only,
not committed: `vm/run.sh` passes `--cpuset-cpus=$MOOB_CPUSET` to `docker run`, so that each
guest stayed on host CPUs not used by another run (pairs 0-1, 2-3 or 6-7; an unrelated run held
4-5).

1. Baseline, unmutated: `MOOB_ONLY="permmatrix minacl ext4 xattrread permgaps" make vm-test`:
   `MOOBFS_ONLY_PASS`.
2. First run, `python3 scripts/mutants.py run` (51 min, 08:23 to 09:14 BRT): **16 killed, 7
   survived** (M02, M11, M16, M19, M21, M22, M23); each survivor's console ends in
   `MOOBFS_ONLY_PASS`, so its steps ran and passed.
3. Each survivor alone under the full default suite (`make vm-test` of the mutated tree):
   - M21 was killed by `pm-mv-leak-ext4` (step `permmatrix`), and M02 by
     `t3-acl-on-domain-directory-leaks-ext4` (step `ext4`, shown with `mutants.py run M02` and
     the steps line `permmatrix ext4`): their steps lines were incomplete. Fixed in the catalogue.
   - M19 and M22 passed the full suite (`MOOBFS_LAYER_PASS`). M11, M16 and M23 first reached the
     3600 s guard of `vm/in-container.sh` in the btrfs release step while four guests shared the
     host (no verdict); rerun with the guard at 7200 s on the host copy only, all three passed the
     full suite (35, 38 and 26 min). Five real gaps.
4. Tests added (commit "regression checks for the five mutants"), each run on the unmutated
   module first (`MOOBFS_ONLY_PASS` for `ext4 minacl`, then for `permmatrix`, which passes on
   ext4, btrfs and XFS):
   - M19 (the scan files a directory's own attributes below a failing directory in the
     partition): domain `ds14` with an attributed `pv/d` (0755) below `pv` (0700), read by the
     build; 1001's answer must equal the oracle. Killed by `q2-mismatch-s14-build-ext4`.
   - M16 (a path too deep to evaluate is not marked heterogeneous): a file 68 levels deep given an
     attribute after a mount-wide `QUERY` built the outside part must raise `heterogeneous` by
     one. A first version without the mount-wide `QUERY` failed on the unmutated module: a write
     is recorded only when its part is built, and such a path is placed outside every domain.
     The mutant changes no answer, only that count. Killed by `s14-deep-not-heterogeneous-ext4`.
   - M11 (an ACL that cannot be read counts as permissive): in step `minacl`, debugfs writes
     `x/bad`'s access ACL raw (`ea_set -r`) in ext4's on-disk format with version 2 instead of 1,
     so `ext4_acl_from_disk()` refuses every read with `EINVAL` (checked: uid 1001's `open` fails
     with `Invalid argument`); renamed into the built domain through the layer, a write path that
     passes no error pointer, it must stay out of 1001's answer. A build that meets that ACL
     fails instead, which the old steps already covered. Killed by `ma-unreadable-acl-answered`.
   - M22 and M23 (a directory moved during the build below a failing directory, its content and
     its own attributes): a rename after the build makes the next query build the domain again,
     so the first test, in `ds14`, did not kill them; `dir_moved_prepare()` serves a move during
     a build. In step `permmatrix`, `query-build-moved` (now with optional `OLD NEW` paths)
     moves `a/s` (0755, an attribute, 600 files) to `pv/s` during the first build. Both killed by
     `pm-mvp-leak-ext4`.
5. Whole catalogue on the final tree, split in two runs (M01 to M11 on CPUs 2-3, M12 to M23 on
   6-7, 14:10 to 14:30 BRT): `MUTANTS_RUN killed=11 survived_or_error=0 of 11` and
   `MUTANTS_RUN killed=12 survived_or_error=0 of 12`. All 23 killed, each by a check of its
   steps line.


*Added 2026-10-30:* `fuzz.yml`'s first scheduled run on GitHub (run 40953229237, started
2026-10-29 20:55 UTC) passed for its three targets (`attr`, `image`, `tools`), so S7's workflow
has run (journal 0237 recorded it as not yet run). `static.yml` passed on PR 179 (run
41093502826) after the two fixes above.

## Limitations

- The baseline was produced on arm64. The CI run is on amd64, where the clang analyzer or
  Coccinelle may report a different set (architecture headers differ); the first GitHub run
  decides, and its `findings.txt` artifact is the input for an update. The amd64 image was not
  built here: it needs about 4 GB more disk, and the workstation had 5 GB free.
- The image is about 4.2 GB (the configured upstream tree, clang, OCaml); building it takes a
  few minutes on each CI run. Caching it is a later improvement.
- Keys use the text of the reported line, so two findings with the same message on identical
  lines of one file count once.
- The mutants run on one kernel (Debian 6.12.113) and, for the steps that loop over them, on
  ext4, btrfs and XFS; a mutant is killed by the first failing check, which is always on ext4.
  The weekly CI job of S14 is not written.
- S15 to S18 and S21 are planned only; S16 waits for a decision on the kernel freeze.

## Next steps

1. Push the branch with its batch and read the first `static.yml` run on GitHub; update the
   baseline from its artifact if amd64 differs, then propose it as a required check.
2. A weekly mutation job in CI with KVM (`scripts/mutants.py run`, about 40 to 50 minutes for the
   catalogue on two vCPUs); the VM run, the tests for its survivors and a run killing all 23 are
   done (2026-10-30).
3. S15 on the existing `permmatrix` step; S17 as a CI job with KVM; S18 on the test host.
