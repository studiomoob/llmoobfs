# 0282 — The one-page matrix and limitations of stage 2

Date: 2026-11-10, 17:48–17:56 Brasília time; second round 17:56–17:58. Status: **done locally**, not published (branch
`claudiofp/b21-matrix-page`, on `main` `6a711f4`). Estimated effort S (hours); the time recorded
here is calendar time, close to the effort for this unit.

*Note, 2026-11-11:* folded into batch b21 (journal [0281](0281-port-ubuntu-7-0.md)). The Ubuntu
row now states the supported series `7.0.0-*-generic`, tested on `7.0.0-43-generic`. The KASAN
block has the 7.0.14 pin, and `scripts/check-matrix-page.sh` checks three pins (`KERNEL70_VERSION`
too) and takes the first code span of each supported row, so the series entry counts. The
limitations gain generic/798 (cachestat).

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
  installed on this Mac. It ran in the second round, on the test host (below).

## Limitations

- The check compares releases, pins, file links and length; the other cells (which suite ran
  where, the limitations) are kept in step by hand, by walking `docs/public-surfaces.md`.
  Link fragments are not checked.
- "One printed page" is approximated by a line count; the long table rows wrap when printed.
- Nothing was run on a kernel for this unit; the page reports earlier runs only.

## Second round, 2026-11-10, 17:56–17:58 BRT: shellcheck, and issue 118 in the current-state documents

### Shellcheck on the test host

The script, `scripts/supported-kernels`, `kasan.yml` and `docs/` were copied to a scratch
directory on the test host (Debian 13, ShellCheck 0.10.0), removed afterwards. The first pass
reported two notes: SC2016 (backticks inside a single-quoted `grep` pattern, meant literally) and
SC2013 (a `for` over `$(grep ...)` splits words). The pattern now takes the backtick from a
variable (`printf '\140'`, braced, after SC1087 on the next pass) and the links are read line by
line from a here-document, which keeps `bad()` in the same shell. Third pass:
`shellcheck --shell=sh scripts/check-matrix-page.sh` clean. Under `dash` there:
`MATRIX_PAGE_OK`, and with a release changed in the copy of the page the check failed with its
message. Locally (macOS `sh`) the negative rounds of a changed release and a broken link were
repeated with the final script and failed as before; the unchanged page gives `MATRIX_PAGE_OK`.

### Issue 118 stated as open

`docs/when-not-to-use.md` §3 still said that issue 118 "remains open for slow lower-device and
larger real-client evidence". GitHub shows it closed on 2026-10-27 at 16:56 BRT, as completed,
by the merge of PR 160 ("layer: drop a domain record when its directory is removed (#113); close
#118 with a slow-lower run (journal 0210)"); journal 0210 records the run over a lower slowed by
`dm-delay`. Current-state documents walked: the READMEs (both state it closed), `docs/status.md`
and its mirror and `ROADMAP.md` (a dated update says closed after the older checkpoint), the site
(`journey.html` in both languages lists the 2026-10-27 closure after the dated milestone of
2026-10-25; `guide.html` links the measurements only), `docs/security-roadmap.md` (no mention in
its current-state section) and `SECURITY.md`, which said "Issue 118 remains open for a real slow
lower device ... while this issue is open" a few lines after saying it was closed.

Changes: `docs/when-not-to-use.md` and `SECURITY.md` state that the issue is closed (2026-10-27,
journal 0210) and that a real slow device and a larger real client were not validated;
`SECURITY.md` keeps its advice against untrusted multi-user query workloads, now with its reason
(the limits bound a build's work, not its CPU time or I/O). `docs/spec-query-v2.md`, whose
paragraph is a dated checkpoint, gains a dated note instead. `docs/retired-claims.txt` gains the
three old wordings; with one of them put back in `when-not-to-use.md` the check failed, and
passed again once it was removed. The README Status box is left as it is: the Ubuntu batch
refreshes it.

## Next step

When the Ubuntu port merges, its journal fills the Ubuntu row (and `scripts/supported-kernels`,
which the check then requires in the page).
