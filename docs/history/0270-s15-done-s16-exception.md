# 0270 — S15 done; a freeze exception for S16; a weekly S18 schedule

Date: 2026-11-07, about 15:25–15:45 Brasília time. Status: **done**. Calendar time and effort
about the same (records, one ADR and a schedule change).

## Goal

After S18 (journal 0248), plan the phases toward stage 2 (ADR-0030). The maintainer accepted
the recommended order: close S15 on the evidence of the S18 campaigns, allow S16 by a narrow
exception to the kernel freeze, and stop running a campaign every night.

## Batch 13 merged

PR 186 (S18 done, `hookrace renameover`, ADR-0028 accepted, the UAPI comment) passed its 15
checks, the KASAN/lockdep suite included, and was merged by the maintainer's approval on
2026-11-07 at 16:36 Brasília time with a merge commit (`26d18a8`). The post-merge runs on
`main` (CI, static analysis, TLA+ model, CodeQL, OpenSSF Scorecard) all passed. This branch was
rebased on it.

## Decisions

- **S15 done.** Its "done when" is "a suite step with a fixed seed set; a longer run in the
  nightly job of S18". The step exists since journal 0250; the open part was a successful
  longer run inside a complete S18 report. Each of the seven complete campaigns c01–c07 ran the
  longer profile (24 seeds, 96 steps, all three lowers) under KASAN/UBSAN/lockdep before its
  fuzzing: 72 `PERMCHURN_PASS` lines per campaign, 248,832 `QUERY2` checks per campaign,
  1,741,824 in all, no line with a non-zero mismatch count, no `PERMCHURN_FAIL`, and
  `MOOBFS_KASAN_ONLY_PASS` in each (counted from the campaigns' permission consoles on the test
  host).
- **S16: ADR-0033.** The maintainer chose a narrow exception over moving S16 to stage 3: the
  self-check is a module parameter, off by default and writable only by root, that only reads
  and warns, under the rules journal 0221 (M8.6) set for the module's other test-only parameters.
  A first draft of the ADR put it behind a test-only build; it was changed before publication
  because M8.6 keeps test parameters in the shipped module so that the suite tests the binary
  users install. Alternative kept in the ADR: S16 in stage 3, which
  closes stage 2 sooner without the in-kernel comparison of record and lower.
- **Weekly schedule.** The test host's crontab line `0 23 * * *` became `0 23 * * 0` (Sunday
  23:00); the `@reboot` resume line is unchanged; the old crontab is kept on the host. With the
  kernel frozen and seven clean campaigns on the same code, a nightly campaign mostly held the
  host. It still runs the snapshot `9dce137`, which differs from the head only in documents and
  one UAPI comment.
- **Dependabot PR 185** (`actions/setup-java` 6.1.0 → 6.2.0 in `tla.yml`, the only workflow
  that uses it): the pinned commit is the `v6.2.0` tag's commit, the release is not a
  prerelease, and all of the PR's checks passed, `tla.yml` included. The change is taken into
  this batch with its `docs/references.md` entry, as with the Dependabot updates of journal
  0228, and PR 185 is closed when the batch is merged.

## Changes

ADR-0033; dated note in ADR-0022; `docs/security-roadmap.md` (S15 done, S16 allowed);
`docs/testing.md` (schedule); `docs/references.md` §48; `CHANGELOG.md`; old wordings in
`docs/retired-claims.txt`.

## Limitations

- S15's evidence is the same host and kernel as S18 (Mac mini, Debian 6.12, x86-64).
- S16 is allowed, not implemented.

## Next step

Implement S16 within ADR-0033, with its negative round; then check the remaining deliverables
of `docs/distribution-strategy.md` §8 for stage 2.
