# ADR-0014 — QUERY2 can return attribute values with each result (projection)

Date: 2026-10-14. Status: **accepted** by the maintainer on 2026-10-14, as a version 3 of the
request with the open interface items decided in the same revision; to be done after the
syzkaller round (S3) and before packaging. Nothing implemented yet. Estimated effort: M.

## Context

A `QUERY2` result is an inode number and a path. A client that needs an attribute of each result
(to show it, sort by it, group by it) reads it with `getxattr`, one call per file. Journal 0097
kept returning values with results ("projection") for a third use case. Journal 0108 measured it:
through the SQLite virtual table of ADR-0013, statements that read a column of every row
(`GROUP BY kind`, sorting by two columns, sorting a whole IN list) take 16 to 78 ms on a domain of
7 459 files, against 1 to 3 ms on an ordinary SQLite table; the difference is one `getxattr` per
row, about 10 µs. The LangGraph adapter (journal 0089) and the ai-memory stages (journals 0054,
0055) read every result's file anyway, so they gain less; the SQL front-end gains most.

The index already holds every typed value in kernel memory, entry by entry (`struct
moobl_ix_entry`): it is, in database terms, a covering index whose values are never returned.

## Decision (proposed)

1. A request may name up to four attributes to return (`proj[4]`, names as in terms); for each
   result the kernel returns, for each of them, the typed value from the index, or "absent".
2. Values go to a second user buffer the caller supplies (`values`, `values_size`), packed one
   after the other (type byte, length, payload); each result record says where its values are.
   A result whose values do not fit ends the page (`truncated`), as the result limit does.
3. Copied after `query_sem` is released, like the results (finding F7): the kernel gathers them in
   its own buffer, bounded by `values_size` and by a fixed ceiling (1 MiB per call).
4. A new request version (`MOOB_QUERY2_VERSION` 3) with its own result record, so version 2
   callers are untouched. The interface items left open by the review of journal 0105 are
   decided in the same revision rather than in a second one: the version 3 term has no implicit
   padding, and the version 3 commands use an ioctl letter not shared in
   `Documentation/userspace-api/ioctl/ioctl-number.rst`. The existing commands keep `'M'`, since
   changing their letter changes their numbers and breaks version 2 callers; retiring them is a
   later, separate decision.
5. The SQLite extension asks for the columns a statement uses (`colUsed`), up to four, and keeps
   `getxattr` for the rest.

## Prior art this builds on

- PostgreSQL, "Index-Only Scans and Covering Indexes": "return values directly out of each index
  entry instead of consulting the associated heap entry"; `INCLUDE` columns carried in the index
  for that purpose; and the caveat that the shortcut holds only when the index entry can be
  trusted (there, the visibility map).
- XFS: `XFS_IOC_BULKSTAT` and `XFS_IOC_ATTRMULTI_BY_HANDLE` (`fs/xfs/libxfs/xfs_fs.h`, Linux
  6.12.112), ioctls that return metadata, and several attributes, of many files in one call.

## What has to be got right

- **What the value means.** It is the index's value, current for every change made through the
  layer. A change made behind the layer (non-promise 2 of ADR-0011) reaches the index at the next
  build; until then `getxattr` reads the file's current value and the projection does not. To be
  stated in the specification, as PostgreSQL states its visibility caveat.
- **Authorization unchanged**: values only for results the caller already gets, in a homogeneous
  partition or after the per-result check.
- **Bounds**: four names, the value buffer bounded, nothing copied under the semaphore.
- **Tests**: every projected value equal to `getxattr` on the same file in the suite; a buffer too
  small; absent attributes; a raw attribute (never indexed, so "absent").

## Alternatives considered

- **Keep `getxattr` per row** (today): costs one system call per row, about 10 µs measured.
- **One `getxattr` per row in a batch, via io_uring**: fewer transitions, still a lookup of each
  file; and the caller would need io_uring.
- **Return every attribute of each result**: simpler request, much larger replies for files with
  many attributes; rejected in favour of naming what is wanted, as `INCLUDE` does.

## Decision of the maintainer (2026-10-14)

Accepted, as a version 3 of the request with the open interface items decided in the same
revision, after the syzkaller round and before packaging.
