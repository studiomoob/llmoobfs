# 0110 — Projection of values accepted (ADR-0014); effort estimates from now on

Date: 2026-10-14. Status: decisions recorded; nothing implemented.

## Projection

`docs/decisions/0014-query-value-projection.md`, accepted by the maintainer on 2026-10-14: a
version 3 of the `QUERY2` request may name up to four attributes whose values, taken from the
index, come back with each result, in a second user buffer copied after `query_sem` is released.
The interface items left open by the review of journal 0105 are decided in the same revision:
no implicit padding in the version 3 term, and a new ioctl letter for the version 3 commands
only, since changing the letter of the existing commands would change their numbers and break
version 2 callers. The third use case journal 0097 waited for is the measurement of journal 0108
(a `getxattr` per row, about 10 µs). Prior art: PostgreSQL's index-only scans and `INCLUDE`
columns, with the caveat it documents (the index's value is not the row's when they disagree;
here, a change made behind the layer); XFS's bulk ioctls (`XFS_IOC_BULKSTAT`,
`XFS_IOC_ATTRMULTI_BY_HANDLE`). Order: after the syzkaller round (journal 0109), before packaging.
Estimated effort: M.

## Effort estimates

Asked whether planned and actual time can be compared, the answer was: only for the community
plan (S/M/L) and the 30/60/90-day goals of the visibility plan; no technical plan carried an
estimate. Decided with the maintainer: every new ADR and roadmap item gets S, M or L, and each
journal records when its work started and ended (`AGENTS.md`). Calendar time is not effort, and
the work is done with an AI assistant while the scale was written for one human maintainer; the
comparison is to be read with both caveats.

## Next step

The six-hour syzkaller run; then ADR-0014.
