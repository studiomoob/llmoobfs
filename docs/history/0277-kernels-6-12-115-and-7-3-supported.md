# 0277 — Debian 6.12.115 and 7.3.3 become supported kernels (in progress)

Date: started 2026-11-09, about 06:50 Brasília time. Status: **in progress**: the list, the CI
jobs and the documents are changed on the batch branch; the new CI jobs have not run on GitHub;
the btrfs `quick` rerun on 6.12.115 with the new exclusion list passed (Evidence).
Estimated effort S (hours); the time recorded below is calendar time.

## Goal

Add the two kernels whose evidence journals 0272 and 0275 gathered to the supported list, as
`docs/compatibility.md` §1 rules 1 and 3 and ADR-0034 point 4 require: the release in
`scripts/supported-kernels` (and so in `make install` and DKMS), a native CI job for each, the
KASAN suite on upstream 7.3.3 in CI, fstests `quick` recorded per lower, and every public surface
that states the supported kernels in step.

## Decisions (maintainer, 2026-11-09)

1. **Linux 7.3.3 is supported**, as Debian's `7.3.3+deb14-amd64` (sid on that date), for the
   layer module only. The M1 module (`kernel/`) stays 6.12-only (journal 0275).
2. **`generic/301` is excluded on btrfs in fstests `quick`**, as it already was in `auto`
   (`vm/fstests/exclude-quick-btrfs.txt`, commit `fa88181`; `make fstests-auto` uses it on btrfs).
   fstests skips its fragmentation threshold when `FSTYP` is btrfs; over the layer `FSTYP` is
   `moobfs_layer` and the threshold is applied, and on 6.12.115 the count crossed it in three of
   four runs (journal 0272). With it, Debian's `6.12.115+deb13.1-amd64` joins the list.
   Alternative not taken: keep `quick` without the exclusion and leave 6.12.115 out, which would
   leave every updated Debian 13 host outside the list.
3. **Rule 2 of `docs/compatibility.md` §1 is amended**: the refusal of an unlisted kernel is split
   by tool. `make install` (`scripts/install.sh`, unless `MOOB_ALLOW_UNSUPPORTED=1`) and DKMS
   (`BUILD_EXCLUSIVE_KERNEL`, generated from `scripts/supported-kernels`) refuse every release
   outside the list; `make layer` refuses by API series only, through the `#error` of
   `layer/moobl_compat.h` (releases from 6.13 up to before 7.3 do not build). Reason: a refusal by
   exact release in `make layer` would break the builds that compile the module against kernels
   deliberately not on the list (the upstream KASAN kernels 6.12.116 and 7.3.3, the KCSAN and
   coverage kernel 6.12.116, the static-analysis headers, the compile probes of journal 0272), about ten builds. This
   closes the open point about rule 1.2. Kernels older than 6.12 still stop at the first missing
   header (`linux/backing-file.h`, journal 0272), not at a message.

## Changes

- `scripts/supported-kernels`: `6.12.115+deb13.1-amd64` and `7.3.3+deb14-amd64` added; the header
  says DKMS follows the list and that 7.3 covers the layer module only.
  `packaging/debian/rules` escapes `.` and `+` for `BUILD_EXCLUSIVE_KERNEL`; the generated
  alternation was checked by hand to match both releases and not near misses
  (`6.12.115+deb13x1-amd64`, `7.3.3+deb14-amd64x`). `scripts/install.sh` and
  `vm/install-test/guest.sh` read the list; nothing else in `scripts/`, `tools/` or `packaging/`
  hardcodes it, except `scripts/repro-ko.sh` (below).
- `scripts/repro-ko.sh` (`make repro-test`, not in CI): pins the headers of 6.12.115
  (`6.12.115-2`, trixie-security) and, by default, leaves the 7.x releases out with a message:
  its trixie image cannot install sid's 7.3.3 headers. Without this it would stop at "no pinned
  headers for 7.3.3+deb14-amd64".
- `.github/workflows/ci.yml`: jobs `native_115` "Native Linux VM (Debian 6.12.115)" and
  `native_733` "Native Linux VM (Debian 7.3.3)", like `native_113`, with artifacts
  `native-vm-612115-evidence` and `native-vm-733-evidence`. Existing job names are unchanged.
- `.github/workflows/kasan.yml`: the job becomes a two-leg matrix. The 6.12 leg keeps the name
  "Layer suite under KASAN/lockdep" (the required check) and the artifact `kasan-evidence`; the
  7.3 leg is "Layer suite under KASAN/lockdep (7.3.3)" with `kasan-733-evidence`. `KERNEL_VERSION`
  and `KERNEL_SHA256` stay 6.12.116; `KERNEL73_VERSION: 7.3.3` and `KERNEL73_SHA256` (kernel.org's
  `v7.x/sha256sums.asc`, `79c66dde…f3d1`) are new. The cache key carries the leg's version, and
  the tarball URL takes `v6.x` or `v7.x` from it. The timeout stays 150 minutes: on the test host
  the 7.3.3 suite under lockdep took about 95 minutes, as 6.12.116 did there, and the 6.12 leg
  takes about 45 on a hosted runner with KVM.
- `scripts/check-pins.sh`: prints the 7.3 pin, checks that it is a 7.3 release and, online,
  checks its SHA-256 against kernel.org's `v7.x` list. It also compares `vm/Dockerfile`'s default
  base image (`ARG MOOB_BASE=`) with `tests/static/Dockerfile` and with the trixie image of
  `vm/run.sh`, and checks that `vm/run.sh` has a sid image for 7.3.3. Since `75f3451` gave
  `vm/Dockerfile` a `FROM $MOOB_BASE`, the old comparison of the `FROM` lines failed ("different
  base images"); this fixes that failure of the Lint job on the branch.
- `docs/compatibility.md` §1: the table has rows for Debian 6.12.115, Debian 7.3.3 and upstream
  7.3.3 under KASAN; "Today" says two series; dated notes under rules 2 and 3 and under the open
  point about rule 1.2. Old notes are unchanged.
- `vm/fstests/README.md`: a section for 6.12.115 and 7.3.3 that names
  `exclude-quick-btrfs.txt` and the move of `generic/301`; the `auto` command line adds the list.
- Current-state surfaces (`docs/public-surfaces.md`): `README.md`, `README.pt-BR.md`,
  `SECURITY.md`, `CONTRIBUTING.md`, `docs/status.md`, `docs/status.pt-BR.md`, `docs/faq.md` (and
  `site/faq.html` regenerated), `docs/testing.md`, `docs/operations.md`,
  `docs/when-not-to-use.md`, `docs/architecture-overview.md`, `docs/security-operations.md`,
  `docs/distribution-strategy.md`, `docs/development.md` (pins), `ROADMAP.md`, `site/index.html`,
  `site/guide.html`, `site/plan.html` and, through `scripts/site-journey.py`, `site/status.html`
  and `site/pt/status.html`. Dated records (the README's "Facts as of 2026-10-25", the result
  tables of earlier runs, journals) keep their wording. The old current-state wordings are in
  `docs/retired-claims.txt`.
- `docs/references.md` §49: kernel.org's `v7.x/sha256sums.asc` for the 7.3 pin, and the Debian
  archive entries of the two packages.

## Evidence

Nothing was run on a kernel in this unit; the evidence is that of the earlier journals.

- 6.12.115 (journal 0272, 2026-11-08): full native suite PASS (`MOOBFS_LAYER_PASS`,
  `MOOBFS_ACCEPTANCE_PASS`); fstests `quick` with `exclude-quick.txt`: ext4 and XFS 631 passed;
  btrfs failed only `generic/301`.
  Rerun on btrfs with `exclude-quick.txt` and `exclude-quick-btrfs.txt`, test host, KVM,
  2026-11-09 06:49–08:55 BRT (calendar time): passed all 630 tests, check exit 0, 166 not run.
- 7.3.3 (journal 0275, 2026-11-08/09): full KASAN/UBSAN/lockdep suite PASS on upstream 7.3.3 with
  `vm/kasan.config` (`MOOBFS_KASAN_PASS`, 284 pass markers); full native suite PASS on Debian's
  `7.3.3+deb14-amd64` (layer and acceptance; M1 skipped); fstests `quick` 631/631 on ext4, btrfs
  and XFS over kernel.org 7.3.3 built with `vm/fstests.config`.
- Local checks of this unit, macOS: `sh scripts/check-pins.sh` (online, both tarballs agree with
  kernel.org) and `--offline`: `PINS_OK`; `sh scripts/check-retired-claims.sh`:
  `RETIRED_CLAIMS_OK`; `python3 scripts/site-faq.py --check`: up to date; shellcheck (via
  `uvx --from shellcheck-py`) on `scripts/check-pins.sh` and `scripts/repro-ko.sh`: clean;
  actionlint (via `uvx --from actionlint-py`) on `ci.yml` and `kasan.yml`: clean. `make lint`
  did not run: shellcheck is not installed on the host.

## The batch's full suites (6.12)

Test host, KVM, at `1cfd8de` (this branch rebased on `main` `fc50fe3`, before the btrfs rerun
note), started 2026-11-09 07:40 BRT, calendar time:

- `make vm-test` (Debian 6.12.111, CPUs 0-1), 07:40–08:10: exit 0, `MOOBFS_VM_PASS`,
  `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, 302 pass markers; only `PYLIB_SKIPPED`, which every
  native run prints.
- `sh vm/kasan-local.sh` (upstream 6.12.116 with the new `vm/kasan.config`, rebuilt because
  `CONFIG_LOCKDEP_CHAINS_BITS=18` changed it; CPUs 2-3), 07:40–09:36: exit 0, `MOOBFS_KASAN_PASS`
  with no kernel diagnostic, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_PYLIB_PASS`;
  285 pass markers.

## Merges of PR 188 and PR 189

With the maintainer's agreement of 2026-11-09, PR 188 (the kernel matrix, journal 0272) was
merged at 06:40 BRT (`403f4a0`) and PR 189 (the workspace commands, journal 0273), rebased on it
and pushed with `--force-with-lease` after its pre-push checks passed (06:47–06:59, 12 of 12), at
07:40 BRT (`fc50fe3`) with 17 of 17 checks green. The post-merge runs on `main` passed: CI,
CodeQL, static analysis, OpenSSF Scorecard and Pages on `403f4a0`; CI, CodeQL, static analysis,
the TLA+ model and Scorecard on `fc50fe3`. The rebase of PR 189 renumbered its
`docs/references.md` section to 50; this branch's port section became 51.

## Limitations

- The new CI jobs (`Native Linux VM (Debian 6.12.115)`, `Native Linux VM (Debian 7.3.3)`,
  `Layer suite under KASAN/lockdep (7.3.3)`) have not run on GitHub. Their first runs are the
  pull request of this batch; until they pass, rule 1's "pass in CI" is met by the local runs of
  journals 0272 and 0275 only. Whether the 7.3 native job fits in 35 minutes under TCG on a
  hosted runner is not measured.
- fstests `quick` for 7.3 ran on kernel.org's 7.3.3 built with `vm/fstests.config`, not on
  Debian's `7.3.3+deb14-amd64`; the two share the upstream release. Debian's build ran the native
  suite.
- The M1 module (`kernel/`) does not build on 7.3; the 7.3 entries cover the layer module only.
- `make install-test` and `make package-test` (DKMS under Secure Boot) ran on Debian 13 only;
  neither has run on a sid host with 7.3.3. `make repro-test` does not cover 7.3.3.
- `vm/kasan-local.sh` still reads only the 6.12 pin and fetches from `v6.x`; journal 0275 ran 7.3.3
  with an edited copy.
- Debian's 7.3.3 is in sid on this date; when it reaches forky or a later 7.3 build replaces it,
  the list needs another entry under the same rule.

## Next step

The batch's pull request, with the maintainer's agreement, whose
CI runs the three new jobs for the first time, and close this journal and 0275 when they pass.
