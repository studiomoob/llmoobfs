# 0247 — CI jobs for the mutants of S14 and the branch coverage of S17

Current status, 2026-11-01: **S14 and S17 CI validated on 1071845, artifacts checked; two reporting failures and their fixes preserved below.** See the dated follow-up below.

Original implementation checkpoint, 2026-10-31: **written, not yet run.** Both workflows exist on branch
`claudiofp/b11-ci`; neither has run on GitHub, and neither was run locally (no KVM host was
free). Their first run is expected on the batch's pull request, which their path filters
trigger. Work started: 2026-10-31 15:18 UTC; ended about 15:30 UTC. Wall-clock time, not an
engineering-effort measurement. Estimated effort: S14's remainder S, S17 M (journal 0242);
actual so far: about a quarter of an hour of attended work for both, before any run; the runs
and the fixes they need are not counted yet.

## Goal

Close the two CI items of the security roadmap that need KVM on GitHub's runner: the weekly
job of S14 that runs the mutant catalogue and fails when a mutant survives (journal 0242 ran
it on the test host), and S17's gcov branch report of `layer/` under the suite. CI and test
infrastructure only; the kernel, the UAPI and the format stay frozen (ADR-0022).

## Decisions

- **S14: `.github/workflows/mutation.yml` runs the command of the recorded run.**
  `scripts/mutants.py run` with `MOOB_DEBIAN_KERNEL=6.12.113` and `MOOB_VM_KVM=1`, as on the
  test host on 2026-10-30, so a CI verdict means the same as the journal's. KVM comes from the
  udev rule of `kasan.yml` and `bench.yml`, with the `udevadm settle` of PR 179; the job refuses
  to run under TCG.
- **Four shards, round-robin.** Mutant *n* of `mutants.py list` (from 1) goes to shard
  *n* mod 4, so shards hold 5 or 6 mutants. The whole catalogue took 51 minutes on two vCPUs of
  the test host; one job would probably fit in GitHub's 6-hour limit, but four jobs keep each
  far from it (180-minute timeout) and report sooner. One job per mutant was rejected: 23 copies
  of the image build and the in-guest builds for a few minutes of suite each. A mutant added to
  the catalogue joins a shard without a workflow change.
- **A baseline job.** The union of the catalogue's steps (`ext4 minacl permgaps permmatrix
  xattrread`) on the unmutated tree, as step 1 of journal 0242's VM run: if it fails, the
  mutants' kills are not evidence. It runs in parallel with the shards, and its failure fails
  the workflow.
- **Triggers:** weekly (Saturday 03:41 UTC), on demand, and on pull requests that change
  `tests/mutation/**`, `scripts/mutants.py`, `layer/index.c` (the file every mutant changes) or
  the workflow. Not a required check.
- **S17: `.github/workflows/coverage.yml` reuses the KASAN job's kernel path.** The pinned
  upstream 6.12.112 tarball, `vm/kasan.config` followed by the new `vm/gcov.config`
  (`CONFIG_GCOV_KERNEL=y`, `CONFIG_GCOV_PROFILE_ALL` off), concatenated so the later fragment
  wins, and virtme-ng from `vm/kasan-requirements.txt`. KASAN, UBSAN and lockdep stay on, so the
  covered paths are those of the KASAN job and its markers keep their meaning; a lighter
  configuration was rejected because it would change which kernel the report describes.
- **`GCOV_PROFILE=y` on the job's command line only**, never in `layer/Kbuild`: Kbuild's rule
  (`scripts/Makefile.lib`) then adds `-fprofile-arcs -ftest-coverage` to the external module
  only. The job calls Kbuild directly instead of `make layer`, so the Makefile's
  `-ffile-prefix-map` (journal 0236) does not rewrite the paths recorded in the notes, which
  lcov must find.
- **`vm/gcov-guest.sh`** runs `vm/kasan-guest.sh` unchanged, then copies every `.gcda` under
  `/sys/kernel/debug/gcov` whose path contains `/layer/` into `build/gcov/gcda/` with `cat`, and
  exits with the suite's status, so a failing run still leaves its counters. The layer is loaded
  and unloaded many times during the suite; `gcov_persist=1` (the default) accumulates every
  load (`kernel/gcov/fs.c`).
- **A report, not a gate.** `lcov --capture --rc branch_coverage=1 --include "$PWD/layer/*"`,
  then `--summary` and `--list` into the job summary and `genhtml` into the artifact
  `layer-branch-coverage`. The job fails when the suite fails (`MOOBFS_KASAN_PASS` missing) or
  when no counters were collected, never on a percentage: no threshold has been decided.
- **Triggers:** weekly (Wednesday 04:53 UTC), on demand, and on pull requests that change the
  workflow, `vm/gcov.config` or `vm/gcov-guest.sh`. The request was weekly and on demand; the
  path filter on the job's own files was added, as `bench.yml` has, so that its first run
  happens on the pull request that adds it. Not a required check. The kernel is cached under
  its own key, as in `kasan.yml` (journal 0209).
- `scripts/check-pins.sh` checks `coverage.yml`'s copy of the kernel pin; `docs/development.md`
  lists it; the labeler puts both workflows under `area/tests-vm`.

## Changes

`.github/workflows/mutation.yml`, `.github/workflows/coverage.yml`, `vm/gcov.config`,
`vm/gcov-guest.sh`; `scripts/check-pins.sh`, `scripts/mutants.py` (docstring),
`.github/labeler.yml`, `docs/development.md`; `docs/testing.md` (the `make mutants-check` row,
a coverage row, the list of workflows, where two sentences had been merged into one by an
earlier edit); `docs/security-roadmap.md` (current state, S14 and S17 rows);
`docs/retired-claims.txt`; `docs/references.md` §44; `CHANGELOG.md`; this journal and the index.

## Evidence (offline only)

On the Apple Silicon workstation, Docker Desktop:

- `docker run --rm -v "$PWD:/r" -w /r rhysd/actionlint:1.7.7 -no-color`: nothing on the two new
  workflows; the two notes reported are on the existing `fuzz.yml`, as in journal 0242. Exit 0.
- `shellcheck --shell=sh` over `vm/*.sh` and `scripts/*.sh` (`koalaman/shellcheck:stable`, as
  `make lint` runs it), `scripts/check-spdx.sh`: clean.
- `sh scripts/check-pins.sh --offline`: `PINS_OK`; with `coverage.yml`'s `KERNEL_VERSION`
  changed to 6.12.111 it printed `PIN MISMATCH: coverage.yml KERNEL_VERSION differs from
  kasan.yml` (restored).
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- The shard and step selections, run on the current catalogue: shards 0 to 3 are `M04 M08 M12
  M16 M20`, `M01 M05 M09 M13 M17 M21`, `M02 M06 M10 M14 M18 M22`, `M03 M07 M11 M15 M19 M23`
  (all 23, once each); the baseline's steps are `ext4 minacl permgaps permmatrix xattrread`.
- The gcov facts the design rests on were read in the 6.12.112 sources (`gcov.rst`,
  `kernel/gcov/fs.c`, `kernel/gcov/Kconfig`, `scripts/Makefile.lib`; references §44). No gcov
  kernel was built: the test host's eight CPUs were all in use by other work, and the
  workstation has no room for a kernel build.

## Limitations

- Nothing here has run. In particular unverified: that a mutant shard fits its timeout on a
  4-vCPU runner with nested KVM; that root-owned files left by each mutant's container in
  `build/mutation/` do not exhaust the runner's disk; that the gcov kernel builds with the KASAN
  fragment and boots under virtme-ng; that the debugfs path of the module's counters contains
  `/layer/`; that the copied `.gcda` match the `.gcno` (same gcc); and that lcov 2.0 accepts the
  kernel's counters without `--ignore-errors` (kernel gcov data often raises lcov's `mismatch`
  or `negative` errors).
- The coverage report has no threshold and no comparison between runs.
- `mutation.yml` runs on one kernel (Debian 6.12.113), as the recorded run did.

## What the first GitHub runs must show

1. `mutation.yml`: "KVM usable" in each job; the baseline ends in `MOOBFS_ONLY_PASS`; each
   shard's summary lists its mutants, each `KILLED by <marker>`, and `MUTANTS_RUN ...
   survived_or_error=0`; each shard's duration, against its 180-minute timeout.
2. `coverage.yml`: `CONFIG_GCOV_KERNEL=y` in the configure step; `ls layer/*.gcno` lists
   `main`, `attr` and `index`; `MOOBFS_KASAN_PASS` and `MOOBFS_GCOV_COLLECTED: 3 data files`
   (or more) in the console; an lcov summary with a non-zero branch count; the artifact.
3. Record both runs (links, durations, totals) in a dated note here, and S17's report link in
   the batch's journal, which is S17's done check.

## Next steps

Open the batch's pull request (coordinator); read the first runs; fix what they show, with a
dated note in this journal; then update the S14 and S17 rows of the security roadmap from
"written" to their run state.


## First GitHub execution started (2026-11-01)

The maintainer authorized publishing the integrated batch as PR 181. Both new workflows
started there: Mutation testing run 41347762160 and Branch coverage run 41347762118.
This is the first execution, not a passing result. Check the latest PR head and Checks
tab for current outcomes; later synchronization may supersede these initial runs.
https://github.com/studiomoob/llmoobfs/pull/181 . No required gate was added or merge performed.


## First CI artifacts and counter correction (2026-11-01, Brasília time)

Follow-up started about 18:32; implementation/probe checkpoint about 18:52. These are
calendar times; engineering effort was not measured. The initial runs above were superseded
by head `865ef4da1ffccd8c630ad0877d897456063a10d9` after publication records were added.

- [Mutation run 41347899785](https://github.com/studiomoob/llmoobfs/actions/runs/41347899785)
  passed. Downloaded artifacts independently confirm the unmutated baseline's VM/ONLY pass
  markers and all 23 catalogue IDs, each killed once, with zero survivors or missing entries.
- [KASAN run 41347899850](https://github.com/studiomoob/llmoobfs/actions/runs/41347899850)
  passed on that head. Its downloaded console has `MOOBFS_KASAN_PASS`; the dmesg scan has no
  kernel diagnostic under `vm/kdiag.sh`'s rules. The SRSO boot text and QEMU raw-image notices
  are not kernel warnings. Artifacts contain no independent source-hash manifest or hidden
  `.config`; exact-head metadata and SHA-256 of both captured files are retained locally.
- [Coverage run 41347899684](https://github.com/studiomoob/llmoobfs/actions/runs/41347899684)
  passed its instrumented VM suite and collected all three `.gcda` files, with suite exit 0.
  LCOV 2.0 then failed at `layer/main.c:764`, branch 2: negative count `-24`. A warning at
  line 536 also reported a non-zero count in an unexecuted block. No valid branch report was
  accepted. The preserved `.info` is partial, not evidence of coverage. The artifact omitted
  `.gcno` files and the hidden kernel `.config`, preventing an independent recapture.

LCOV's documentation identifies concurrent GCC counter updates as a possible cause and
recommends `-fprofile-update=atomic`, followed by rebuilding and rerunning the tests. GCC's
manual describes atomic updates as protecting profile counters against concurrent corruption.
Linux 6.12.112's Makefile sets `CFLAGS_GCOV` to profile arcs/test coverage without atomic
updates. These sources were inspected (references §44); no filesystem defect is inferred from
this reporting failure. The exact cause remains provisional until the corrected CI run.

A bounded user-space control on the idle dedicated host used GCC/gcov 14.2.0, eight joined
threads pinned to two reserved CPUs, and 300,000 calls per thread. Both binaries returned
correct application results. With explicit `-fprofile-update=single`, gcov counted 1,555,357
of 2,400,000 calls; with `atomic`, exactly 2,400,000. This demonstrates lost counters and its
prevention on this compiler; it does not reproduce the exact negative branch on CI's compiler.
The raw control archive SHA-256 is
`ac9441a6447b78dc4d9c31742466c957224a3f662565249c52f461cfea9843ca`.
No VM or module was started, the resource lock was released, and existing scheduled jobs
were preserved. Source, commands, objects, raw counters and results are retained outside Git.

The coverage workflow now passes `KCFLAGS=-fprofile-update=atomic` only when building its
external module. Normal builds and production source are unchanged; the unprofiled kernel's
cache key is still valid. The artifact now includes `.gcno` notes, a visible `kernel.config`,
compiler/tool versions, source commit and build-input hashes. Report errors remain fatal;
no `--ignore-errors negative`, counter rewriting or percentage threshold was added. A report
must contain a non-zero branch total. The summary no longer mistakes report failure for
missing counters. The original failure evidence is preserved.

Offline actionlint and pin checks passed for this correction. The concurrent control passed
its positive and negative checks. The corrected full CI capture is pending: S17 stays open
until the exact updated head produces an inspectable report with non-zero branch data.


## Second CI failure isolated; saved-data recapture passes (2026-11-01)

Follow-up began at 19:29 Brasília time; this reporting correction took about four minutes of
calendar time, not measured engineering effort. On PR head `d5f2b30`, 18/19 checks passed.
The downloaded mutation artifacts again confirm the baseline and 23/23 unique kills;
[KASAN run 41354183859](https://github.com/studiomoob/llmoobfs/actions/runs/41354183859)
has its full-suite marker with no diagnostic under the harness rules. All evidence was
retained with file hashes outside Git; no second host campaign or monitor was started.

[Coverage run 41354183773](https://github.com/studiomoob/llmoobfs/actions/runs/41354183773)
compiled with atomic updates, passed the complete instrumented suite and collected three
counter files (exit 0). The negative count did not recur. GCC 13.3 JSON of all three data
files contains no negative line or branch count. LCOV 2.0 now fails at `moobl_trace.h:17`:
`mismatched exception tag for id 0, 0: '0' -> '1'`. Raw JSON identifies several distinct
functions expanded from the same TRACE_EVENT line, with different branch/exception shapes.
The partial report is again rejected. A non-zero unexecuted-block warning remains; it is
preserved and not suppressed or rewritten.

The new evidence retention made a suite rerun unnecessary for this investigation. The tested
checkout is GitHub's synthetic merge `e27922a674f6e8c98f471dcb5c87924f81749010`, whose parents
were checked against main and PR head `d5f2b30`; it is not the topic head itself. An initial
verification that assumed checkout equals head was corrected to validate those parents.
Ten available source/notes/config hashes match. The manifest also names generated mod.c,
module and kernel binaries absent from the artifact; those bytes cannot be independently
verified from this artifact and are not described as checked.

In a bounded local Ubuntu 24.04 container with gcov 13.3, the same saved notes/counters and
matching source reproduce LCOV 2.0's mismatch (negative control, exit 1). A trial removing
exception branches produced no branch records and was rejected. No exception exclusion,
source/header exclusion, `--ignore-errors` or counter mutation is used in the correction.

The official LCOV 2.6 release instead completes capture, summary, file listing and genhtml,
all exit 0, on those exact inputs: 2,241/3,120 branches (71.8%) and 3,264/3,461 lines (94.3%).
All four source records, including the trace header, are retained. LCOV documents its newer
branch-signature matching for multiple macro expansions (references §44). The unexecuted
block at `moobl.h:121` is still a reported warning, not an error ignored by configuration.
These percentages describe a local recapture of CI data, not yet a successful CI report.

The initial tag-archive trial reported a beta version string and Perl redefinition notices
when extracted on the workstation filesystem. It was superseded by the official packaged
release extracted inside Linux; that reports `LCOV version 2.6-0`, without those notices,
and reproduces the successful totals. The official release archive is pinned by SHA-256
`67a45db99b8ef0260c3fac9a2e3749fd8fa350a15e235dd3c29b356d97755c75`, verified against the
release asset metadata and downloaded bytes. The workflow uses that package ahead of the
distribution tool on PATH; the distro package supplies its Perl dependencies.

Actionlint, offline pins and documentation checks are rerun before the authorized push to
PR 181. S17 stays open until this updated workflow produces an inspectable CI artifact;
S18 remains open with zero accepted complete nights. No production code, kernel cache,
host scheduling, branch protection or merge authorization is changed.


## S14/S17 CI acceptance (2026-11-01, reviewed about 20:29 Brasília time)

All 19 PR checks passed on head `1071845358e4fc71face87d6f10718a1f936b57d`.
The actual artifacts, not only the check conclusions, were downloaded and reviewed:

- [Branch coverage run 41357772643](https://github.com/studiomoob/llmoobfs/actions/runs/41357772643):
  full instrumented suite passed, collected three gcda files with suite exit 0; the pinned
  release archive passed its SHA-256 check and `lcov --version` reports `2.6-0`. Capture,
  summary, list, genhtml and the non-zero branch check passed. The artifact contains a valid
  report and HTML: **2,250/3,120 branches (72.1%)**, **3,274/3,461 lines (94.6%)**, four source
  records including the trace header. All recorded line/branch hits are non-negative. These
  are this run's measured totals, not a coverage threshold or production-readiness claim.
- [KASAN run 41357772580](https://github.com/studiomoob/llmoobfs/actions/runs/41357772580):
  full-suite pass marker in the console and no diagnostic under `vm/kdiag.sh`'s rules in the
  retained dmesg. The instrumented coverage run has the same successful sanitizer marker.
- [Mutation run 41357772802](https://github.com/studiomoob/llmoobfs/actions/runs/41357772802):
  the unmutated baseline's `MOOBFS_ONLY_PASS` and no VM/environment failure; all 23 distinct
  catalogue IDs killed, each once, with none missing or surviving.

The tested checkout is synthetic merge `66c980b87e0e7107fd22bbf14954a63115b89446`; its parents match
`main` at `302bff0` and topic head `1071845`. Ten available source/notes/config hashes match
and 95 artifact-file SHA-256 values are retained in the private verification JSON. Generated
mod.c, the module and kernel binaries are named by the build manifest but absent from the
artifact; this does not independently verify those bytes. The existing diagnostic warnings
about raw QEMU image formats/SRSO are not kernel warnings under the harness rules.

LCOV still emits an unexecuted-block warning at `index.c:240`. It is preserved in the job
log; counters were not normalized, exception branches were not excluded, and no error was
ignored. This does not invalidate the successful capture, but optimizer/gcov warnings and
macro branch matching limit interpretation of the percentage. The two earlier failed
reports remain failed evidence; the 71.8% offline recapture remains a separate measurement.

S14's CI done check and S17's report/link done check are now met. This follow-up closes their
CI investigation; initial implementation took about 15 minutes, and subsequent attended
engineering effort was not measured separately from the roughly 29-hour calendar span.
S15's complete-night integration and S18's accumulation remain open (zero accepted reports).
No new test runner, host load or observer was started. The maintainer has not authorized merge.

This final documentation synchronization does not change workflow/runtime sources. Its new
PR head must pass its own required checks before a merge decision. Results of that unchanged
validation payload will be reflected in local coordination and the PR rather than repeatedly
pushing documentation-only changes merely to name each new head.


*Calendar correction, 2026-11-01:* the preceding acceptance note's roughly 29-hour span
mixed UTC and Brasília time. From implementation start on 2026-10-31 at 15:18 UTC to the
passing coverage result on 2026-11-01 at 23:27 UTC is about **32 hours 9 minutes of calendar
time**, mostly machine time and waits. Attended engineering effort remains unmeasured.
The recorded run results, source snapshots and acceptance are unchanged.
