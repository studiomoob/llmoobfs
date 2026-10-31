# 0245 — S3's coverage threshold counts KCOV call sites (ADR-0032); S3 done

Date: 2026-10-31. Status: done on branch `claudiofp/b11-s3-count`, documentation only.
Work started: about 14:45 UTC; ended about 15:10 UTC. Wall-clock time, not an
engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

Record the maintainer's decision of 2026-10-31 on how S3's handler coverage is counted, after
the seventh syzkaller run (journal 0239) stood at 84.9 % of the manager's 809 PCs.

## Decisions

- **ADR-0032, accepted:** the threshold counts the KCOV call sites of the handler functions
  (781 on the current module and kernel) instead of the manager's 809 PCs, 28 of which, in
  `moobl_xattr_get` and `moobl_xattr_set`, are not call sites and cannot be covered. The value
  stays 85 %, and the other conditions of S3 stay.
- **S3 done:** the replay of journal 0239 reached 688 of 781 call sites (88.1 %), with seven
  six-hour runs and no open report, and every handler function exercised.
- **Alternatives rejected:** an eighth run under the old count, a lower value, leaving S3 open
  (ADR-0032).
- **Reversal stated:** on 2026-10-30 the maintainer kept the manager's count for the seventh run,
  so as not to change the metric after a near miss; the change made here follows the evidence
  that the gap is made of PCs no program can reach.

## Changes

`docs/decisions/0032-s3-threshold-counts-kcov-call-sites.md`; `docs/security-roadmap.md` (S3 row
and section); both READMEs, `CHANGELOG.md`, `scripts/site-journey.py` and the generated site
pages; `docs/references.md` §42; `docs/retired-claims.txt`; this journal and the index.

## Evidence

Journal 0239: the call-site listing (`objdump -dr`, `addr2line`) and the per-call KCOV replay
that reached 688 of 781. No new run.

## Limitations

- The replay script is not in the repository yet (ADR-0032, consequences).
- Why the manager counts 14 extra PCs in each xattr handler is not established.

## Next steps

Commit the replay script before the next syzkaller run; S18 (nightly fuzzing) records both
counts.
