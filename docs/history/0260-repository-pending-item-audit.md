# 0260 — Repository-wide pending-item audit

Date: 2026-11-03. Status: audit complete; remediation and publication remain separate.
Started about 15:00 Brasília; inventory completed at 15:04, recorded afterwards.
These are calendar times, not measured engineering effort. Estimated effort: S.

## Goal and scope

Reconcile old pending items across source comments, plans, ADRs, current-state surfaces,
GitHub issues/checks/settings and the existing local coordination list. Preserve active
batch-11 work and the other sessions. This is a backlog/evidence audit, not a new
line-by-line kernel review or an execution of the full test matrix.

## Repository and remote evidence

Published main is e59a244 (PR181 merged), clean. The local follow-up was 7ea5fbb before
this documentation unit. All seven registered worktrees were checked with branch, last
commit and status: clean. The old three batch-11 worktrees are preserved checkpoints,
not three newly active unfinished changes. G6 is 884c012; corporate pilot is de01160.
No worktree, branch or evidence was deleted or integrated.

GitHub returned four open issues: 16,17 (human VFS/credential/QUERY2 review), 68
(other-hardware benchmark), and117 (remaining index lock on inodes with records).
There are no open PRs and no open Dependabot alerts. Five code-scanning alerts remain:
four Scorecard signals and CodeQL49 at tests/query.c:182. The latter flags cursor
creation with fopen(...,"wb") and potentially permissive mode before umask; it needs
contextual triage, not automatic dismissal or classification as a kernel vulnerability.
The SLSA tag-pin exception is already documented in security-operations.md.

The active main ruleset requires Tools and sanitizers, Native Linux VM and Layer suite
under KASAN/lockdep. Static analysis is not required by that ruleset. No setting changed.
Fuzz schedule41193336695 and benchmark schedule41454936109 report success through the
API; this audit did not revalidate their old raw artifacts.

## Actual gaps versus historical checkpoints

Current work remains the corrected S18 campaigns and their audit, with S15 integration.
Full native and KASAN validation of the local partition fix is in journal0259; the active
long campaign has no final verdict. S16 needs a bounded freeze decision, S21 is unstarted,
and G6 stays after security batches11/12. The specification-only coldlink fixture of0259
is a concrete candidate for S20: verify its receipt and the checklist before claiming closure.

Remaining work also includes issue117's residual, independent human review for stage3,
other physical-hardware/reference comparisons, direct-build kernel matrix refusal,
KMSAN, residual SELinux/Smack work, appropriately scoped S11, S22 subscriptions/access
review, and the still-unreviewed ADR0028 design. New features, publication and Azure
retain their existing authorization boundaries. Codespaces and OSS-Fuzz are optional,
not automatic next actions merely because an old plan mentions them.

Several current-state summaries need reconciliation rather than repeated implementation:
ROADMAP still lists fair shares and S3 coverage as remaining; its G2 chronology contains
an obsolete incomplete statement before the completion; first static/fuzz runs are already
recorded, fstests auto reruns are complete in0248, PR181 is merged, client-test-plan's
opening says nothing ran despite its later results, and status.md calls ADR0031 accepted
and still awaiting acceptance. The old DX plan's qsort/checkpatch/sanitizer/grep queue is
resolved by0050 and current code. Dated historical records must remain historical; do not
blindly delete every occurrence of pending. The local list's stale current host/notifier
summary is reconciled in the same unit outside Git.

Public surfaces are inventoried in docs/public-surfaces.md. No existing public surface
was edited or published during this read-only audit; their identified stale summaries
are deliberately queued for a coordinated local documentation unit and later authorized
publication. No newly discovered bug or corrected runtime claim is presented as public main.

## Checks actually run

On main e59a244: check-retired-claims passed; site-faq --check passed; check-site passed
for12 HTML pages and security.txt valid until2027-04-26; check-pins --offline passed.
The first site-journey invocation incorrectly used --check and failed with KeyError0007:
that script takes a root directory, not that option. Corrected validation generated its
eight HTML files in a temporary copy with history/decisions; exit0, all byte-identical
to tracked pages. No generated source was changed. Source-comment searches found no
explicit TODO/FIXME/TBD backlog in first-party code; variable names and quoted upstream
TODOs are not pending tasks. This does not establish defect absence.

No VM suite repeated, active job interrupted, alert dismissed, issue closed, remote
configuration changed, push/PR/merge/tag/release or Azure action.

## Next steps

Keep the existing corrected campaign running. Triage cursor creation locally; prepare a
separate coherent documentation reconciliation. Verify S20 evidence, then seek a narrow
batch12/S16 decision when the current gate permits it. Existing long work does not need
another approval to continue. Detailed private coordination inventory and API receipts
remain outside Git; no outreach or personal account data is included here.
