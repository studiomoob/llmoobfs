# 0247 — CI jobs for the mutants of S14 and the branch coverage of S17

Date: 2026-10-31. Status: **written, not yet run.** Both workflows exist on branch
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
