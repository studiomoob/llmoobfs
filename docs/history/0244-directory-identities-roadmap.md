# 0244 — Roadmap: an applicability track for corporate directory identities

Date: 2026-10-30. Status: done on branch `claudiofp/b10-review-criterion` (batch 10),
documentation only, not pushed. Work started: about 15:05 UTC; ended about 15:20 UTC.
Wall-clock time, not an engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

Record, as planned roadmap items, the maintainer's proposal of 2026-10-30 to check MoobFS on
hosts whose users and groups come from a corporate directory service, and to treat it as a
separate applicability after the current batch.

## Decisions

- **A new roadmap section, D1 to D3,** after the current batch, each with its own go-ahead:
  D1 directory-service identities (P2 / M), D2 network lower filesystems (P2 / M, investigation),
  D3 a positioning page (P3 / S, after D1 and D2's outcome).
- **The claim to check:** `QUERY2` answers with the caller's credentials, so on a host that
  resolves users and groups through SSSD (Active Directory, FreeIPA) or LDAP the results follow
  the directory without a second copy of permissions in an index. Nothing in the layer is
  specific to a directory service; the items test what already exists.
- **Limits stated in advance:** group changes reach a process at its next login (Linux
  credentials are fixed per process); the NFS client leaves `i_generation` 0, so a reused inode
  number is not detected over it ([compatibility](../compatibility.md) item 4), and D2 may end in
  a refusal or a documented limit rather than support; no claim about timing or count leaks.
- **Kept within ADR-0022 and the client-neutral rule:** no change to `layer/`, the UAPI or the
  format; the items are tests, documents and, if needed, refusals found by D2 that would go to the
  core front as fixes.

## Changes

`ROADMAP.md` (new section), `CHANGELOG.md`, this journal and the history index.

## Evidence

None: planned items, nothing run.

## Next steps

Finish batch 10; then ask the maintainer for the go-ahead on D1 and D2.
