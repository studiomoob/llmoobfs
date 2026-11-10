# 0282 — The one-page matrix and limitations of stage 2

Date: 2026-11-10, 17:48–17:56 Brasília time. Status: **done locally**, not published (branch
`claudiofp/b21-matrix-page`, on `main` `6a711f4`). Estimated effort S (hours); the time recorded
here is calendar time, close to the effort for this unit.

## Goal

Stage 2 of `docs/distribution-strategy.md` §8 is done when "a third party reproduces
everything, the matrix and the limitations fit on one page and no leak is known". The matrix and
the limitations were spread over `docs/compatibility.md` §1, `docs/testing.md` §9,
`docs/when-not-to-use.md` §3, the README's Limitations and the journals. This unit writes the one
page.

## Decisions

- **A new page, `docs/matrix-and-limits.md`**, not a section replacing one of the existing
  places: `compatibility.md` keeps the rules and their dated notes, `testing.md` §9 what no step
  covers, `when-not-to-use.md` the explanations. The page states each fact in one line and links
  to where it is explained; it adds no fact of its own.
- **No copied fact without a check.** The page has to name the supported releases exactly, and
  they will change. `scripts/check-matrix-page.sh` (new, run in CI's lint job) fails when the
  releases between the page's `supported-kernels` markers differ from `scripts/supported-kernels`,
  when the upstream releases between its `kasan-pins` markers differ from `KERNEL_VERSION` and
  `KERNEL73_VERSION` of `.github/workflows/kasan.yml`, when a relative link names a missing file,
  or when the page exceeds 70 lines (one printed page). The page is also added to the documents
  `scripts/check-retired-claims.sh` reads. Alternative discarded: generating the table from the
  list, as `scripts/site-faq.py` does for the FAQ page; the "what runs on it" column cannot be
  generated, and a check is enough for the releases.
- **arm64 and s390x are rows marked exploratory**, with the journals of their runs (0124, 0137,
  0280); rule 5 of `compatibility.md` §1 is unchanged.
- **Ubuntu**: a row "not supported, nothing yet" and an HTML comment marking it as the place for
  the Ubuntu port, which another branch is preparing; this page claims nothing about it.
- **"No leak is known"** is stated as "none known open", with links to the published advisories
  (all twelve published on 2026-11-10, checked with `gh api`) and to the register of review
  findings. The third-party reproduction has not happened; `distribution-strategy.md` §8 says so
  in a dated note.

## Changes

- `docs/matrix-and-limits.md` (new): the kernel table (4 supported Debian releases, the two
  upstream KASAN pins, the earlier pin 6.12.112 of the KCSAN and syzkaller rounds, arm64 and
  s390x exploratory, Ubuntu and anything else unsupported), lowers, packaging, leaks, and 14
  one-line limitations with links (no human review, not for production, NFS/FUSE and the
  GHSA-756j-cgwq-gvwm limits, ext4 without `metadata_csum`, issue 117, the build bound on slow
  debug kernels, the s390x `ixlock` threshold, a very slow disk, index memory, stale paths and
  lower changes, depth 64, LSMs, what the suite does not cover, mounting privileges).
- `scripts/check-matrix-page.sh` (new); `.github/workflows/ci.yml` runs it in the lint job;
  `scripts/check-retired-claims.sh` reads the page; `scripts/supported-kernels` names the check in
  its header.
- Links to the page: `README.md` and `README.pt-BR.md` (documentation map, as for
  `compatibility.md`), `docs/README.md`, `docs/compatibility.md`, `docs/testing.md` §9,
  `docs/distribution-strategy.md` §8 (dated note), `docs/public-surfaces.md` (inventory row and the
  list of CI checks).
- `docs/references.md`: unchanged; nothing new is cited.

## Checks (on the maintainer's Mac, 2026-11-10, about 17:52–17:55 BRT)

- `sh scripts/check-matrix-page.sh`: `MATRIX_PAGE_OK`. Negative rounds on a copy of the page,
  each restored afterwards: a release changed to `6.12.114+deb13-amd64` (supported releases
  differ), the 7.3 pin changed to `7.3.2` (KASAN pins differ), a link changed to a missing journal
  (broken link): each failed with its message.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`; `python3 scripts/site-faq.py --check`:
  matches; `python3 scripts/check-site.py`: 12 pages valid; `sh scripts/check-pins.sh --offline`:
  `PINS_OK`; `sh scripts/check-spdx.sh`: exit 0; `git diff --check`: clean.
- Not run here: shellcheck on the new script (`make lint`'s first half): shellcheck is not
  installed on this Mac. It runs in CI's lint job and in the batch's pre-push checks on the test
  host.

## Limitations

- The check compares releases, pins, file links and length; the other cells (which suite ran
  where, the limitations) are kept in step by hand, by walking `docs/public-surfaces.md`.
  Link fragments are not checked.
- "One printed page" is approximated by a line count; the long table rows wrap when printed.
- Nothing was run on a kernel for this unit; the page reports earlier runs only.

## Next step

Shellcheck of `scripts/check-matrix-page.sh` with the batch's pre-push checks; when the Ubuntu
port merges, its journal fills the Ubuntu row (and `scripts/supported-kernels`, which the check
then requires in the page).
