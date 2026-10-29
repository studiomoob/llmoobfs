# ADR-0030 — Review criterion of the technical preview: automated coverage and triaged AI-assisted findings; a targeted human review for the public release

Date: 2026-10-29. Status: **accepted by the maintainer on 2026-10-29** (journal 0240).
Estimated effort: S (hours) for the decision and the documents. No kernel, UAPI or format change
(ADR-0022).

Amends: [`docs/distribution-strategy.md`](../distribution-strategy.md) §8 (stage 2 and stage 3),
[`ROADMAP.md`](../ROADMAP.md) (item 1, S1, of "Agreed sequence" and the "Public release" row) and
[`docs/security-roadmap.md`](../security-roadmap.md) S1. Their earlier text is kept, with dated
notes pointing here. It does not replace an earlier ADR.

## Context

`docs/distribution-strategy.md` §8 lists, for stage 2 (technical preview), "S1 (threat ADR), S4
(hostile lower), S5 (lint), S6 (`SECURITY.md`, versioning policy) completed; S2 (fstests) with
recorded results; signing documented and tested in a VM with lockdown (S10)". S1's own done-when
criterion in `docs/security-roadmap.md` is "ADR published, the module's `fs_flags` reviewed against
it, and `moobfs-check` reports the model version", which ADR-0011 met on 2026-10-12. But
`ROADMAP.md` keeps S1 open with "Independent human review of the whole kernel code" as its
remaining work, and its "Public release" row makes stage 2 wait for S1. Read together, the
preview waited for an independent human review of the whole kernel code.

No such reviewer is available. Review requests have been open since 2026-10-13 (journal 0084);
no one outside the project has read the kernel code by hand. What exists falls into three
categories, used from now on in every document:

1. **Automated tools:** the VM suite under KASAN, UBSAN and lockdep (a required check of `main`),
   KCSAN, syzkaller (five six-hour runs, journal 0143), fstests' `quick` group (journal 0091), the
   libFuzzer targets (journal 0237) and fault injection.
2. **AI-assisted reviews:** the two source reviews made by AI assistants (journals 0111 and 0114)
   and the later review rounds made by AI assistants (journals 0144 and 0155; journal 0151 records no human reviewer either); and the external
   reviewer's private reports of 2026-10-27 and 2026-10-28 (F15, F16 and GHSA-756j-cgwq-gvwm in
   the kernel; GHSA-4gr9-rvgr-44qj, GHSA-3wx4-fh9w-2v66 and the scan-walk pointer behind
   GHSA-f769-xjf5-54wc in user space). Those reports were produced with AI assistance: the reviewer
   chose where to look, reproduced the defects and wrote (F16) or checked the fixes, but the
   analysis was AI-assisted. Every defect they found was real, was reproduced, was fixed and has a
   regression test. Some current documents presented them as a partial independent human review;
   journal 0240 corrects that.
3. **Independent human review:** none yet.

## Decision

**Stage 2 (technical preview)** no longer waits for a human review. It requires:

1. strong automated coverage: the existing suite, KASAN/UBSAN/lockdep, KCSAN, syzkaller meeting
   the S3 threshold, fstests (S2 results recorded) and fuzzing (S7);
2. every finding of an AI-assisted review triaged, reproduced and turned into a regression test;
3. the S14 to S21 block of `docs/security-roadmap.md` ("making the tests find what reviews
   found") done, so that coverage does not depend on who reviewed.

The other stage-2 deliverables and acceptance tests of §8 (S1's ADR, S4, S5, S6, S10, the
packages, the five acceptance steps) are unchanged.

**Stage 3 (public release)** requires, besides its existing preconditions, at least one
independent human review, read by hand by someone who knows the VFS, targeted at the credential
paths (`override_creds()` and the paths that reach the lower filesystem) and at the `QUERY2`
authorization. It is to come from a volunteer reviewer. A hand-read review of all of the kernel
code remains a goal of stage 3, not a gate of stage 2.

S1 is closed by its own done-when criterion (ADR-0011); the human review it carried as remaining
work moves to the stage-3 preconditions.

## Alternatives

- **Keep the human review of the whole kernel code as a stage-2 gate.** Rejected: no reviewer is
  available and none is in sight, so the preview would stall indefinitely, while the risk it
  guards against can be reduced, for the preview's audience of technical users in VMs, by tests
  that find the classes of defect reviews found.
- **A paid audit** (for example through OSTIF, whose published range for initial audits is
  roughly US$30k–200k; in an AWS-funded program of ten audits the average was about US$74k;
  `docs/references.md` §38). Rejected: there is no budget for it.
- **Count the AI-assisted reports as a partial human review.** Rejected: the analysis was
  AI-assisted, so calling it independent human review would overstate what was checked. They are
  credited for what they are.

## Consequences

- Stage 2 now waits for: the S2 results (the `auto` group, beyond `quick`), the S3 threshold
  (the per-function criterion of journal 0154 still open), the S14 to S21 block (planned, not
  started) and the remaining §8 deliverables; packages stay prepared, not published, until then.
- The targeted human review moves to stage 3; `docs/help-wanted.md` asks for it.
- Documents state the three categories; no document may describe the AI-assisted reports as
  human review. Old wordings are in `docs/retired-claims.txt`.
- Risk: a defect class that neither the tests nor an AI-assisted review can see reaches the
  preview's users. The preview stays limited to VMs or dedicated hosts with documented limits.
