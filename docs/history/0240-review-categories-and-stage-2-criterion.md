# 0240 — Review categories corrected; the review criterion of the technical preview (ADR-0030)

Date: 2026-10-29. Status: done on branch `claudiofp/b10-review-criterion` (batch 10),
documentation only, not pushed. Work started: about 2026-10-29 21:00 UTC; ended about 21:25 UTC.
Wall-clock time, not an engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

Two decisions of the maintainer of 2026-10-29:

1. Correct how the documents classify the external reviewer's private reports, which were
   produced with AI assistance, and use three review categories consistently.
2. Record a new review criterion for stage 2 (technical preview) and stage 3 (public release) of
   `docs/distribution-strategy.md` §8, as ADR-0030.

## Decisions

- **Three categories.** (a) *Automated tools*: the VM suite under KASAN, UBSAN and lockdep,
  KCSAN, syzkaller, fstests, fuzzing and fault injection. (b) *AI-assisted reviews*: the two
  source reviews by AI assistants (journals 0111 and 0114), the later review rounds made by
  assistants, and the external reviewer's private reports: F15, F16 and GHSA-756j-cgwq-gvwm in
  the kernel; GHSA-4gr9-rvgr-44qj, GHSA-3wx4-fh9w-2v66 and the scan-walk pointer behind
  GHSA-f769-xjf5-54wc in user space. The reviewer chose where to look, reproduced the defects and
  wrote (F16) or checked the fixes; the analysis was AI-assisted. (c) *Independent human review*:
  none yet.
- **The credit stays.** The reports found real defects; each was reproduced, fixed and given a
  regression test. The correction is about classification only. The reviewer is named by role.
- **Stage 2** no longer waits for a human review: it requires strong automated coverage (suite,
  KASAN/UBSAN/lockdep, KCSAN, syzkaller at the S3 threshold, fstests, fuzzing), every AI-assisted
  finding triaged, reproduced and turned into a regression test, and S14 to S21 of the security
  roadmap done. **Stage 3** requires at least one independent, hand-read human review by a
  volunteer who knows the VFS, targeted at the credential paths (`override_creds()` and the
  lower-access paths) and the `QUERY2` authorization; a hand-read review of all of the kernel
  code stays a stage-3 goal.
- **Alternatives** (ADR-0030): keeping the full human review as a stage-2 gate was rejected
  (no reviewer available; the preview would stall indefinitely); a paid audit, for example through
  OSTIF (published range roughly US$30k–200k for initial audits, references §38), was rejected
  for lack of budget; counting the AI-assisted reports as a partial human review was rejected.

## What the documents said

Before this change, several current-state documents described the reports as a partial
independent human review: `docs/when-not-to-use.md` ("no independent human review of the whole
kernel code (the external reviewer's reports and the fix of F16 aside)"), `docs/help-wanted.md`
(the credential-model review "Partly answered" by the external reviewer), and the repeated
qualifier "no independent human review of the *whole* kernel code", which implied that part of
it had been reviewed by a human. The gate itself: §8 of `docs/distribution-strategy.md` asks
stage 2 for "S1 (threat ADR) ... completed"; S1's done-when criterion was met by ADR-0011, but
`ROADMAP.md` kept "Independent human review of the whole kernel code" as S1's remaining work and
made stage 2 wait for S1.

## Changes

- New `docs/decisions/0030-stage-2-review-criterion.md`.
- Dated notes, old text kept: `docs/distribution-strategy.md` §8 (stage 2 and stage 3),
  `docs/security-roadmap.md` S1 (after the done-when criterion, after the 2026-10-18 status note
  and after the 2026-10-28 update of the three reports), `docs/adoption-risks.md` §14,
  ADR-0011 (GHSA-756j-cgwq-gvwm and F16).
- Current-state sentences updated in place: `README.md` and `README.pt-BR.md` (status block,
  "How to help", limitations), `docs/status.md` and `docs/status.pt-BR.md`, `SECURITY.md`,
  `CONTRIBUTING.md`, `docs/faq.md` (and `site/faq.html`), `docs/help-wanted.md` (the
  credential-model row now asks for the stage-3 review), `docs/when-not-to-use.md`,
  `docs/architecture-overview.md`, `ROADMAP.md` (item 1 and the "Public release" row), the
  current-state tables of `docs/security-roadmap.md` (S1, S14 to S21) and
  `docs/adoption-risks.md` (risks 1 and 14), `scripts/site-journey.py` (the 2026-10-28 milestone
  and the "Security" status answer, both languages) and the hand-written `site/index.html` and
  `site/guide.html`.
- `docs/retired-claims.txt`: "human review of the whole kernel code", "human has reviewed the
  whole kernel code", "human audit of the whole code", "the external reviewer's reports and the
  fix of F16 aside", "Partly answered: an external reviewer" and "revisão humana independente de
  todo o código do kernel".
- `docs/references.md` §38: OSTIF's audit page and the AWS–OSTIF program post.
- `CHANGELOG.md` under Unreleased; this journal and its index row.

## Evidence

- `python3 scripts/site-journey.py .` regenerated `site/journey.html`, `site/status.html`,
  `site/pt/journey.html` and `site/pt/status.html` (`ok`).
- `python3 scripts/site-faq.py .` then `python3 scripts/site-faq.py --check .`: "site/faq.html:
  64 elements from docs/faq.md", "site/faq.html matches docs/faq.md".
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`. Its first run after the new
  claims were added failed on `site/guide.html` and on the generated status pages (not yet
  regenerated), which were then corrected.
- `make lint` was not run: it needs `shellcheck`, not installed locally, and no shell script was
  changed.

## Limitations

- Surfaces on GitHub outside the tree (the review-request issues 16 and 17, the "Call for
  reviewers" discussion, the advisories' credits) and outside GitHub were not touched: this unit
  does not touch GitHub. They should say the same three categories when next edited.
- Journals 0215, 0216, 0218, 0219, 0227 and 0231 do not claim a human review and were left as
  they are; the dated notes are in ADR-0011 and the security roadmap.

## Next steps

- Publish with batch 10; then update the review-request issues and the call for reviewers on
  GitHub to ask for the stage-3 targeted review (credential paths, `QUERY2` authorization).
- Stage 2 now waits for the S2 `auto` group results, the S3 per-function criterion (journal
  0154), S14 to S21 and the remaining §8 deliverables.
