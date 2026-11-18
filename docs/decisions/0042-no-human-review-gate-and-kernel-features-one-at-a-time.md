# ADR-0042 — No human-review gate for the public release; kernel features one at a time, each by its own ADR

Date: 2026-11-18. Status: **accepted by the maintainer on 2026-11-18**, about 09:40 Brasília time
(journal 0305). Estimated effort: S (hours) for the decision and the documents.

Amends: [ADR-0030](0030-stage-2-review-criterion.md) (its stage-3 precondition) and
[`docs/distribution-strategy.md`](../distribution-strategy.md) §8, stage 3. Replaces decision 1 of
[ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md) (the kernel freeze); its decisions
2 to 5 stand. The earlier text of each is kept, with a dated note pointing here.

## Context

ADR-0030 (2026-10-29) removed the human review from the stage-2 gates, because no reviewer was
available, and moved a targeted one to stage 3: "at least one independent human review, read by
hand by someone who knows the VFS, targeted at the credential paths and at the `QUERY2`
authorization". ADR-0022 (2026-10-25) kept the kernel, the UAPI and the format frozen, and named the
missing outside review first among its reasons for not lifting the freeze.

Three weeks later the situation is the same. Review requests have been open since 2026-10-13
(journal 0084), the call for reviewers is public (`docs/help-wanted.md`), and the maintainer is
looking for reviewers on several fronts; no one outside the project has read the kernel code by
hand. In the meantime stage 2 was declared (ADR-0039, 2026-11-15), its packages are published
(ADR-0041), and the security roadmap's runtime items are complete (S8 with KMSAN, journal 0304).
A gate that depends on a volunteer who may never come stops the project for as long as that lasts;
the freeze has already needed five recorded exceptions to fix defects (ADR-0033, ADR-0034,
ADR-0037, ADR-0038, ADR-0040).

## Decision

1. **Stage 3 no longer requires an independent human review.** In its place, besides the other
   stage-3 preconditions of §8, which do not change:
   1. **Independent AI-assisted reviews of the credential paths and the `QUERY2` authorization**,
      the scope ADR-0030 gave the human review: at least two, made with two different tools (for
      example Claude Code and Codex), each started without the other's findings, each recorded in
      a journal that names the tool and who directed it.
   2. **ADR-0030's criterion (2) applies to them**: every finding triaged, reproduced and turned
      into a regression test that fails without its fix, or recorded as an exception with its
      reason, as in ADR-0030's amendment of 2026-11-11.
   3. **The surfaces keep saying it.** README (both languages), the status pages and
      `docs/when-not-to-use.md` state that there has been no independent human review of the
      kernel code, as long as that is true. Nothing may call an AI-assisted review a human one.
2. **A human review stays welcome.** `docs/help-wanted.md` keeps asking for it. If one comes, its
   findings follow criterion (2), and the journals record it as the first independent human
   review. It is not a gate of anything.
3. **The kernel freeze of ADR-0022 ends; kernel features enter one at a time.** A new behavior of
   `layer/`, a UAPI change or a format change needs:
   1. its own ADR, accepted by the maintainer, with the compatibility decision a UAPI or format
      change requires (AGENTS.md, "Scope and validation");
   2. one such feature in flight at a time, in its own batch;
   3. the kernel-correction rules of AGENTS.md: `make checkpatch`, the suite steps it touches in a
      dedicated VM, a negative round showing its tests fail without it, KASAN and lockdep at once
      if it touches locking or object lifetime, and the full suite and KASAN suite before the
      batch's pull request;
   4. an AI-assisted review of the change before its pull request, under criterion (2).
   Fixes go on as before. The maintainer chooses which feature comes first; the accepted, never
   implemented projection of attribute values (ADR-0014) is the first candidate, not a commitment.
4. **The recorded exceptions reopen.** ADR-0030's amendment says a change to the freeze reopens
   its exceptions to criterion (2): each gets a regression test or a renewed reason. The four left
   (0114 item 3, M7, M8.2, M8.3) are reviewed in a later batch; until then they stand with their
   reasons, which this decision does not change, except that "it would need a kernel change" is no
   longer a reason by itself.

## Alternatives

- **Keep the human review as a stage-3 gate.** Rejected by the maintainer: the project would wait
  on a volunteer it cannot schedule, with no date.
- **Drop the gate with nothing in its place.** Rejected: the scope ADR-0030 gave the human review
  (credential paths, `QUERY2` authorization) is where a defect would leak data across users; two
  independent AI-assisted reviews of it, with every finding reproduced, are the most that can be
  done without a human reviewer, and the AI-assisted reports so far found real defects there (F15,
  F16, GHSA-756j-cgwq-gvwm).
- **A paid audit.** Rejected for the reason in ADR-0030: no budget.
- **Lift the freeze entirely, with features in roadmap order.** Rejected by the maintainer: one
  feature at a time, each accepted by its own ADR, keeps each change small enough to test and to
  undo.
- **Keep the freeze until a human review comes.** Rejected: the same indefinite wait.

## Consequences

- Stage 3's remaining preconditions are those of §8 (M4 completed and published, the security
  roadmap items listed there, the matrix green) plus decision 1 above.
- The risk ADR-0030 named stays and grows with each new kernel feature: a defect class that
  neither the tests nor an AI-assisted review can see. One feature at a time, its own tests with a
  negative round, and the public "no independent human review" statement are the mitigations.
- `ROADMAP.md`, `docs/distribution-strategy.md` §8, `docs/security-roadmap.md` S1,
  `docs/help-wanted.md` and the status page of the site (both languages) change in the same unit
  of work; the old wordings go to `docs/retired-claims.txt`.
- The evolution track continues with G6 (journal 0305); the corporate-directory items D1 to D3
  stay as planned.
