# 0242 — Plan for S14 to S21; static analysis in CI (S5, S19); mutants of the homogeneity rule (S14)

Date: 2026-10-29. Status: implemented on branch `claudiofp/b11-s14`, run locally on an arm64
workstation with Docker; `.github/workflows/static.yml` has not run on GitHub, and no mutant has
run in the VM. Work started: 2026-10-29 23:09 UTC; ended about 23:45 UTC. Wall-clock time,
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
`WERROR` off with `scripts/config` and `olddefconfig` after `defconfig`; the baseline, produced
on arm64, is checked by the next amd64 run.

## Limitations

- The baseline was produced on arm64. The CI run is on amd64, where the clang analyzer or
  Coccinelle may report a different set (architecture headers differ); the first GitHub run
  decides, and its `findings.txt` artifact is the input for an update. The amd64 image was not
  built here: it needs about 4 GB more disk, and the workstation had 5 GB free.
- The image is about 4.2 GB (the configured upstream tree, clang, OCaml); building it takes a
  few minutes on each CI run. Caching it is a later improvement.
- Keys use the text of the reported line, so two findings with the same message on identical
  lines of one file count once.
- No mutant has run in the VM. The `steps` of each mutant are the steps expected to kill it,
  from reading the suite, not from a run; S14 is not done until a recorded run kills each one.
- S15 to S18 and S21 are planned only; S16 waits for a decision on the kernel freeze.

## Next steps

1. Push the branch with its batch and read the first `static.yml` run on GitHub; update the
   baseline from its artifact if amd64 differs, then propose it as a required check.
2. On the test host (KVM): `python3 scripts/mutants.py run`; record killed and surviving
   mutants here; write a test for each survivor; then a weekly mutation job in CI.
3. S15 on the existing `permmatrix` step; S17 as a CI job with KVM; S18 on the test host.
