# 0305 — No human-review gate for stage 3; the kernel freeze ends, features one at a time (ADR-0042); G6 starts

Date: 2026-11-18. Work started at 09:40 Brasília time and ended at 10:30. Status: **done** for the
decision and the documents. Estimated effort S (hours); the time recorded is calendar time.

## Goal

With S8 closed (journal 0304), decide what the project does next, and whether two rules set when
the module had not been tested as it is now still hold: the independent human review that
ADR-0030 left as a precondition of stage 3, and the kernel freeze of ADR-0022.

## Decisions

The maintainer decided on 2026-11-18, about 09:40:

- **G6, the end-to-end evaluation, is the current roadmap item**, as the order of 2026-10-27 set
  (security, then C6, then G6). Its final evaluation has never run; its scope, with the numeric
  targets the protocol leaves open, is prepared for the maintainer's approval before any run.
- **Stage 3 no longer waits for an independent human review** (ADR-0042). In its place: two
  independent AI-assisted reviews of the credential paths and the `QUERY2` authorization, made with
  two different tools, every finding reproduced with a regression test or recorded as an
  exception. The surfaces keep stating that there has been no independent human review of the
  kernel code. A human review stays welcome and gates nothing.
- **The kernel freeze ends** (ADR-0042, replacing decision 1 of ADR-0022). A kernel, UAPI or
  format feature enters one at a time, each by its own ADR accepted by the maintainer, under the
  kernel-correction rules of AGENTS.md plus an AI-assisted review before its pull request. Fixes
  go on as before. The first feature is the maintainer's choice; ADR-0014 (projection) is the
  first candidate.
- Work that does not conflict runs in parallel.

Reason given: no human reviewer has come since the first request (2026-10-13), the maintainer is
looking on several fronts, and the project cannot wait on it.

## Alternatives

Offered to the maintainer for stage 3: an automated criterion in place of the review
(recommended, chosen), or dropping the requirement with nothing in its place. For the freeze: one
feature at a time, each by its own ADR (recommended, chosen); keep the freeze for now; or lift it
entirely, features in roadmap order. ADR-0042 records why the others were rejected.

## Changes

- `docs/decisions/0042-no-human-review-gate-and-kernel-features-one-at-a-time.md` (new).
- Dated notes pointing to it in ADR-0022 (decision 1 replaced) and ADR-0030 (stage-3
  precondition replaced; the four exceptions to criterion (2) reopened by the freeze's end, as its
  amendment of 2026-11-11 requires).
- `docs/distribution-strategy.md` §8, stage 3 (amendment) and the issue 117 note; `ROADMAP.md`
  (agreed sequence, consumable path, "Public release" row); `docs/security-roadmap.md` S1;
  `docs/help-wanted.md`; `scripts/site-journey.py` and the status page it generates, in English
  and Portuguese; `CHANGELOG.md`; four retired wordings in `docs/retired-claims.txt`.
- Left as they are, because they stay true: every statement that there has been no independent
  human review of the kernel code (README in both languages, status pages, `when-not-to-use.md`,
  `matrix-and-limits.md`, `help-wanted.md`, `architecture-overview.md`, `CONTRIBUTING.md`).

## Checks

On this Mac, since the test host did not answer (`ssh: ... Host is down`) and the change touches
only documents and the site generator: `scripts/check-retired-claims.sh`, `scripts/check-pins.sh`,
`scripts/check-matrix-page.sh`, `scripts/check-site.py`, `scripts/site-faq.py --check`,
`python3 scripts/site-journey.py .` (pages regenerated, no further change), `make test` and
`git diff --check main...HEAD`: all pass. `make lint` and `make checkpatch` were not run: no shell
script and no kernel code changed.

## Limitations

- Nothing about the kernel code changed. The two AI-assisted reviews that stage 3 now needs have
  not been made; the earlier ones (journals 0111, 0114, 0144, 0151, 0155 and the external
  reviewer's reports) were not made under this criterion and are not counted as them.
- The four reopened exceptions keep their reasons until the batch that reviews them.

## Next step

The scope of G6's final evaluation for the maintainer's approval; then, separately, the
maintainer's choice of the first kernel feature, and a batch for the four reopened exceptions.
