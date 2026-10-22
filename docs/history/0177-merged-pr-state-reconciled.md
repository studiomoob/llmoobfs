# 0177 — Reconcile the merged review batch and dependency update

Date: 2026-10-22. Status: documentation validated locally. Estimated effort: S
(hours). Work started and ended on 2026-10-22; exact start time was not
recorded, so elapsed time is not claimed as effort.

## Goal and decision

The maintainer asked to resume in the previously agreed order. First, make
current-state documentation match the completed merges without rewriting the
earlier journals, which accurately describe the state at publication. Keep
the experimental and non-production status, the unimplemented S9 fair shares,
and the remaining test and resource-cost issues explicit. No new feature,
release, tag, or architectural decision is part of this unit.

## Evidence and changes

Read-only GitHub queries on 2026-10-22 showed PR 126 merged on 2026-10-21 at
`ca52fa7`, with all 11 reported checks successful on head `c71c164`.
PR 109, which integrated the earlier review batch described in the READMEs,
was also merged with all 11 reported checks successful. Issues 110 and 111 were
closed immediately after that merge. PR 75 merged separately on 2026-10-21
at `d7fb946`, with all nine reported checks successful. GitHub listed zero
open pull requests, and its `main` head was `d7fb946` when checked. These
are observed repository states, not new tests performed for this journal.

The current-state row and a dated status correction in `ROADMAP.md` now
distinguish this result from the 2026-10-21 pre-merge checkpoint. A dated
correction in `docs/security-roadmap.md` does the same. The status summaries
in both READMEs now say that the review corrections are on `main` and that
required pull-request checks passed; the experimental tag remains older.
Superseded current-state wording was added to `docs/retired-claims.txt`.
The next technical order is issue 114's remaining T9 test, then investigation
of the resource-cost issues 116 and 118. Requests for independent human review
remain open and are not a gate for that work.

## Validation and limitations

Documentation-only change. No module or VM test was run for this reconciliation.
`git diff --check`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/check-site.py .`, and
`python3 scripts/site-faq.py --check` all exited successfully. The site check
validated links and structure on 12 HTML pages; the retired-claims check
reported `RETIRED_CLAIMS_OK`. These checks do not validate module behavior.
The worktree started from `origin/main` at `d7fb946`; the primary
checkout's unrelated edits were not touched. Publication and merge are
separate maintainer decisions; this journal records only local preparation.

## Next step

Review and commit this documentation unit, then continue with issue 114 T9
in the agreed order. Keep any new kernel correction in a validated
batch with its own journal and negative test.
