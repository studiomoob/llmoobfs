# 0215 — Search-only directories: read and search on every directory on the way (ADR-0025)

Date: 2026-10-27. Status: done on the private branch `sec/ghsa-95h9` (temporary private fork of
the advisory); stays private until the coordinated disclosure. Work started: 2026-10-27 about
17:50 America/Sao_Paulo; ended: 2026-10-27 about 18:15. Wall-clock time, not an
engineering-effort measurement. Estimated effort: S.

## Goal and decision

A private report from an external reviewer showed that `QUERY2` returned the names, inode
numbers and, through probes, the attribute values of files under a search-only directory
(`0711`) inside a domain to every user who could enter the domain, although none of them could
list that directory, on the lower or through the layer. This is finding F15 of ADR-0011 and
broke promise 3.

The maintainer chose fix option 1 of the report: every directory strictly between the domain
directory and the file must be listable and searchable by everyone who can traverse the domain.
Option 2, declaring it a non-promise, was rejected (ADR-0025, which also records the prior art:
the kernel's own split between `MAY_EXEC` on a path walk and `MAY_READ` to list a directory, and
plocate's `R_OK | X_OK` check on every directory above a result).

## Changes

- `layer/index.c`: `reach_ok()` takes the class bits it needs, `MOOBL_REACH_FILE` (read) for a
  file and `MOOBL_REACH_DIR` (read and search) for an intermediate directory, and compares them
  per class. Its three callers for directories (`place_dentry_above()`, the scan's
  `nd->inner_ok`, a directory moved during a build) ask `MOOBL_REACH_DIR`. The per-result check
  `caller_may_read()` now also calls `caller_may_list_above()`, which asks `MAY_READ` of every
  directory strictly between the queried directory and the result and refuses a result whose
  ancestors no longer lead there. No lock, lifetime or format change.
- The mirrors: `tools/moobfs-index.c`, `tools/moobfs-check.c` (the reason stays "more private
  than the domain directory") and `python/src/moobfs/scan.py`.
- Tests: `tests/permmatrix.c` judges a file by `getxattr` and by `opendir()` of the directory
  between, counts the cases the rule before this journal answered wrongly
  (`prev_rule_wrong`), and the suite step runs it again after a chmod through the layer, in
  fallback mode; the step fails if no seed reached such a case. The fallback round meets only
  files the rule already leaves out, so the step also makes a directory search-only through the
  layer after the build (`pm-so-*`): its file stays in the partition, the domain falls back to
  per-result checks, and only `caller_may_list_above()` keeps it from uid 1001. The child of
  `permmatrix check` now flushes its output before `_exit()`, so `MISMATCH` lines reach the log
  (they were lost before; the counts were not). Suite step `ext4`: the ACL case
  now separates `u:1001:x` on the directory (the file is not answered, `acl-search-only-leak`)
  from `u:1001:rx` (it is). `python/tests/test_linux.py`: `test_search_only_directory`.
- Documents: ADR-0025 (new), ADR-0011 (non-promise 3 note, finding F15),
  `docs/spec-query-v2.md` §3 (dated note), `docs/client-contract.md` §3, `docs/faq.md`,
  `docs/references.md` §16, the changelog.

`bench/userspace/udx.h` keeps a rule of its own (the bench prototype, still with the
pre-journal-0116 "others" condition); it is not one of the mirrors the suite compares and was
left unchanged.

## What ran

Mac mini host (Debian 13, KVM), the suite's guest built by `vm/in-container.sh` run on the
host with `-accel kvm` (Debian 6.12.111+deb13-amd64), one VM at a time.

- `make checkpatch`: no finding. `make layer` and `make tools` against the 6.12.111 headers: no
  warning (`-Werror`).
- `MOOB_ONLY="ext4 permmatrix permgaps stall pylib"` with the fix: `MOOBFS_ONLY_PASS`.
  `PERMMATRIX_STEP_PASS`: 12 seeds, partition and per-result rounds, `mismatches=0`; 307 answers
  the rule before 0116 gave wrongly and 102 the rule before 0215. `ISOLATION_PASS: ext4` (the
  kernel against `moobfs-index --uid`), `PERMISSION_GAPS_PASS` on ext4, btrfs and XFS,
  `STALL_PASS`, the `pylib` step.
- After adding the `pm-so` case (tests only, no module change): `MOOB_ONLY=permmatrix` passes.
- `make pylib-test` on the host: OK. `test_search_only_directory` passes, and fails with
  `python/src` of `f7c83cf`.
- Negative rounds, the new tests with the module of `f7c83cf` (`layer/` checked out from it):
  `permmatrix` fails at seed 1 (`MISMATCH ... s1/f23 returned=1 readable=0`, three identities,
  `pm-mismatch-1`); `ext4` fails at `acl-search-only-leak-ext4`.
- Negative round of the per-result half alone, the fix without the call to
  `caller_may_list_above()`: the matrix passes (as explained above), `pm-so-leak` fails, and
  `ext4` fails at `acl-search-only-leak-ext4` (v1 `QUERY` with `domain_depth=0`).

## Limitations

- Not run: the full `make vm-test`, the btrfs and XFS variants of the `ext4` step, KASAN (no lock
  or lifetime change; the batch's full KASAN run covers it), a lower with SELinux labels (the
  other per-result path).
- Condition 2 is still judged against every class that can traverse `D`, not only those that
  can read it and so query it; a `0711` domain with `0711` directories loses their files
  (ADR-0025, alternatives).
- ACLs on intermediate directories keep the old treatment: an extended ACL makes condition 1 the
  only way in, so a directory with one is heterogeneous unless it grants `a+rx`.
- `bench/userspace/udx.h` was not changed (see Changes).
- Public surfaces left stale on purpose until the coordinated disclosure: the security rows of
  `README.md`/`README.pt-BR.md` and `docs/status.md`/`docs/status.pt-BR.md`, `SECURITY.md` and
  `docs/security-roadmap.md`. They are updated when the advisory is published.

## Next step

Review in the private fork, KASAN not required (no lock or lifetime change); merge with the
other fixes of the batch before the coordinated disclosure, then publish the advisory.
