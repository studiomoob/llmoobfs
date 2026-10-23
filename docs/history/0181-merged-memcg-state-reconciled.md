# 0181 — Reconcile the merged memcg correction across current-state documentation

Date: 2026-10-23. Status: local documentation update, not published.
Estimated effort: S (hours). Work started and ended on 2026-10-23; exact
clock time and labor duration were not measured.

## Goal and decision

The maintainer asked for the first post-merge step: make current-state
documentation reflect PR 129's integration and issue 116's closure. Preserve
dated pre-merge checkpoints as observations of their own time and add dated
updates rather than silently rewriting them. This is a documentation-only
reconciliation; no new feature, kernel behavior, UAPI, format change, tag or
architectural decision is part of this unit.

## Evidence and changes

Read-only GitHub queries on 2026-10-23 confirmed that PR 129's exact head
`90b3e6805f38c022e7a3385dc390ec60ea61901e` completed all eleven
reported checks successfully, including KASAN/lockdep. With the
maintainer's approval, it merged on 2026-10-23 at 09:03:36 UTC as
`40645e23ffca1bce0c665f77a7d36702371d0813`, the current `main` head
at the start of this unit. Issue 116 closed automatically at 09:03:37 UTC.
GitHub listed no open pull requests at this observation. These are remote
state observations, not new tests performed by this documentation unit.

Both READMEs, the roadmap, security roadmap, ADR-0018, the final addendum to
journal 0180, the changelog and history index now state the completed result.
The README status summaries keep issue 118 and S9 fair sharing open and keep
the controlled before/after performance comparison explicitly unmeasured.
Previous publication-time observations remain in journal 0180 and in dated
roadmap checkpoints. Two superseded README phrases are recorded in
`docs/retired-claims.txt`; the site and FAQ did not state the old issue/PR
publication status, so their content does not change.

## Validation and limitations

Documentation-only change. No module or VM test was run for this unit.
`git diff --check`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/check-site.py .` and `python3 scripts/site-faq.py --check`
all exited successfully. The retired-claims check reported
`RETIRED_CLAIMS_OK`; the site check validated links and structure on 12 HTML
pages; the FAQ page matched its source. These checks do not validate module
behavior.
This reconciliation does not quantify the performance overhead of memcg
charging, prove S9 fair sharing or close issue 118.

## Next step

Review and commit this documentation unit. Publication and merge remain
separate maintainer decisions. The next resource-cost investigation is
issue 118, starting with bounded CPU and I/O measurements before choosing a
mitigation.
