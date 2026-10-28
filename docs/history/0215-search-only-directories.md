# 0215 — Search-only directories: read and search on every directory on the way (ADR-0025)

Date: 2026-10-27. Status: done on the private branch `sec/ghsa-95h9` (temporary private fork of
the advisory); stays private until the coordinated disclosure. Work started: 2026-10-27 about
17:50 America/Sao_Paulo; ended: 2026-10-27 about 18:15; review round 19:20 to 19:35. Wall-clock time, not an
engineering-effort measurement. Estimated effort: S.

*Note, 2026-10-28:* published with the merge of security batch 2 (journal 0219); the embargo stated above ended then.

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
  ancestors no longer lead there. No lock or format change. The walk adds a `dget_parent()` /
  `dput()` pair per directory per result, an object-lifetime change in the sense of
  `AGENTS.md`, so KASAN/lockdep was run (below). *Corrected 2026-10-27, after the independent
  review: the first version of this journal said "no lock, lifetime or format change" and that
  KASAN was not required.* The lookup and the walk run under the `rename_lock` sequence counter
  and a positive answer is taken only when no rename happened between them (ADR-0025).
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
  (they were lost before; the counts were not). Two cases without any permission change, so
  answered from the partition (`fallback_domains=0` checked): `pm-cr`, a directory created
  search-only through the layer in a built domain (`umask 066`, no `chmod`), which goes through
  `place_dentry_above()`; and `pm-mv`, a search-only directory renamed during the domain's first
  build (`tests/query-build-moved.c`, new argument `any`), which goes through
  `dir_moved_prepare()`. In both, uid 1001 must not get the file and the heterogeneous count
  must rise. The matrix gains directory modes with read but no search (`0754`, `0745`) and
  access ACLs with a named user entry on about a third of the directories (user 1004: `r-x`,
  `--x` or `r--`) and a quarter of the files (user 1002: `r--` or none); its rule leaves every
  inode with such an ACL out, as the kernel's does. Suite step `ext4`: the ACL case
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
  `STALL_PASS`. The `pylib` step printed `PYLIB_SKIPPED` (no `python3` in that guest); the
  library was tested on the host instead (below). *Corrected the same day: the first version
  listed the `pylib` step as run.*
- After adding the `pm-so` case (tests only, no module change): `MOOB_ONLY=permmatrix` passes.
- `make pylib-test` on the host: OK. `test_search_only_directory` passes, and fails with
  `python/src` of `f7c83cf`.
- Negative rounds, the new tests with the module of `f7c83cf` (`layer/` checked out from it):
  `permmatrix` fails at seed 1 (`MISMATCH ... s1/f23 returned=1 readable=0`, three identities,
  `pm-mismatch-1`); `ext4` fails at `acl-search-only-leak-ext4`.
- Review round (independent read-only review of `0c08f42`): with the new cases,
  `MOOB_ONLY=permmatrix` passes (`PERMMATRIX_STEP_PASS`, 281 answers the rule before 0116 gave
  wrongly and 12 the rule before 0215; `QUERY_BUILD_MOVED_PASS` after 5 `index_load` calls).
  Negative rounds, one hunk reverted at a time to the search bit alone: `place_dentry_above()`
  fails at `pm-cr-leak`, `dir_moved_prepare()` at `pm-mv-leak`. `sh vm/kasan-local.sh` with `MOOB_ONLY="ext4 permmatrix"` (Linux 6.12.112, KASAN, UBSAN and
  lockdep, the pinned config): `MOOBFS_KASAN_ONLY_PASS` with no kernel diagnostics, the new
  `pm-so`, `pm-cr` and `pm-mv` cases included. With the `rename_lock` retry in place,
  `MOOB_ONLY="permgaps stall pylib"` passes again (`PERMISSION_GAPS_PASS` on three filesystems,
  `STALL_PASS`; `pylib` skipped in the guest) and `make pylib-test` passes on the host.
- Negative round of the per-result half alone, the fix without the call to
  `caller_may_list_above()`: the matrix passes (as explained above), `pm-so-leak` fails, and
  `ext4` fails at `acl-search-only-leak-ext4` (v1 `QUERY` with `domain_depth=0`).

## Limitations

- Not run: the full `make vm-test`, the btrfs and XFS variants of the `ext4` step, a lower with
  SELinux labels (the other per-result path). The `rename_lock` retry has no test of its own: a
  rename exactly between the lookup and the walk cannot be timed from user space without a
  hook in that window.
- Under a constant stream of renames anywhere in the system, a per-result check can give up
  after four tries and leave a readable result out (never put an unreadable one in).
- Condition 2 is still judged against every class that can traverse `D`, not only those that
  can read it and so query it; a `0711` domain with `0711` directories loses their files
  (ADR-0025, alternatives).
- ACLs on intermediate directories keep the old treatment: an extended ACL makes condition 1 the
  only way in, so a directory with one is heterogeneous unless it grants `a+rx`.
- `bench/userspace/udx.h` was not changed (see Changes).
- Public surfaces left stale on purpose until the coordinated disclosure: the security rows of
  `README.md`/`README.pt-BR.md` and `docs/status.md`/`docs/status.pt-BR.md`, `SECURITY.md` and
  `docs/security-roadmap.md`. They are updated when the advisory is published.
  *Note, 2026-10-28:* they were updated in security batch 2, before its merge, with the other
  surfaces that state findings, advisories and review status.

## Next step

Merge with the other fixes of the batch before the coordinated disclosure, then publish the
advisory. The maintainer decides on the deferred alternative of ADR-0025 (condition 2 judged
against the classes that can query `D`).
