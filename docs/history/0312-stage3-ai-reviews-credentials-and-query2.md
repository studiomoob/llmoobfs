# 0312 — Stage 3: two AI-assisted reviews of the credential paths and the `QUERY2` authorization

Date: 2026-11-19. Work started at about 07:35 Brasília time (the first review) and the
last run recorded here ended at 10:56.
Status: **fixes and checks done; batch 44, branch `claudiofp/b44-stage3-review1`, local; the
advisory and the pull request wait for the maintainer.** The findings are security defects
handled through the private path of `SECURITY.md`: the public surfaces and the advisory wait
until the fixes are ready (maintainer, 2026-11-19). Estimated effort: M (ADR-0042 decision 1.1);
the time recorded is calendar time, most of it machine time. Work done by Claude Code (an AI
assistant) for the maintainer.

## Goal

ADR-0042 decision 1.1: stage 3 needs two independent AI-assisted reviews, made with different
tools, of the credential paths in `layer/` and of the authorization of `QUERY2`, `QUERY2_BATCH`
and the legacy `QUERY` (the scope ADR-0030 gave the human review it replaced), each started
without the other's findings, and every finding reproduced by a regression test that fails
without its fix, or recorded as an exception (ADR-0030 criterion (2)).

## The two reviews

- **Review 1:** Claude Code (Opus), an AI assistant, run as a subagent of the maintainer's
  session; no human reviewer. Static reading of `layer/main.c`, `layer/index.c`, `layer/moobl.h`,
  `layer/moobl_compat.h`, `layer/attr.c` and `include/moobfs_ioctl.h` at `d9ee667`; kernel
  behaviour from memory, not from the sources, and marked where it mattered; nothing executed.
  Findings R1 to R7 and five suspicions, S1 to S5.
- **Review 2:** Codex (model identifier not reported), an AI assistant, run as an Orca worker in
  its own worktree at `d9ee667`, told not to read review 1's report or anything outside the
  worktree; no human reviewer. Static reading of the same files and `common/attr.c`, with the
  Linux v6.12.116 sources of `fs/xattr.c`, `fs/backing-file.c`, `security/selinux/hooks.c`,
  `security/apparmor/lsm.c` and `security/landlock/fs.c` read from kernel.org; nothing executed.
  Findings C1 to C3 and two suspicions.
- The reports are kept outside the repository, with the drafts of the advisory; this journal
  records their technical content.
- Overlap: C3 is R3 and R4 found again; C1 is S2 of review 1, which review 1 left as an
  unsettled suspicion. Both found the credential split, the write credentials and `CAP_FSETID`,
  the xattr paths, the homogeneity rule and the per-result checks as specified.

## Findings and triage

| ID | Finding | Decision | Fix | Regression test (negative round) |
|----|---------|----------|-----|----------------------------------|
| R1 | A homogeneous `QUERY2` answer names a file under a directory the caller's mount namespace hides (a tmpfs over it, `InaccessiblePaths=`); per-result checks leave it out | **recorded exception** (maintainer, 2026-11-19), next to ADR-0011 non-promise 3; see "R1: the cost of the fix" | none | step `mounthide`, opt-in, fails on this code: `mh-answer-named-a-file-under-a-hiding-mount-ext4` |
| R2 | `E2BIG` of the build allowance counts entries the caller cannot list; with write access in the domain, about 15 probes give their number | **recorded exception** (maintainer, 2026-11-19) | none | none (exception) |
| R3 / C3 | `domain_records` and `selfcheck_counts` module parameters readable by every user: counters of every user's activity | fix | both 0400 (`8ee80fc`) | step `paramperm`: `pp-domain_records-readable-by-others (mode 444)` |
| R4 / C3 | `GET_INFO` gives `MOOB_FLAG_PATHS_STALE` to every user: a rename in a domain the caller cannot traverse | fix (maintainer, 2026-11-19) | with domains on, administrators only; unchanged with `domain_depth=0` (`fb47663`) | step `stalebit`: `sb-flag-told-uid-1001-of-a-rename-in-a-private-domain (0x54)` |
| R5 | Build admission is keyed by the real uid: one user who holds subordinate uids takes the 64 slots and the 64 builds a minute of the mount | **recorded exception** (maintainer, 2026-11-19) | none | none (exception) |
| R6 | Append-only, `O_NOATIME` and `protected_hardlinks` checks use the layer inode's copy of the attributes | known: journal 0079 | — | — |
| R7 | Between a file rename's index hook and the VFS's move of the dentries, a homogeneous answer from the new domain gives the file's old path, a name from another domain | fix | a result whose name is not under the queried domain gets no path (`afb0b38`) | step `renamewin` with the test parameter `rename_end_delay_us`: `rw-window-answer-named-a-directory-of-another-domain` |
| C1 | Under SELinux, a policy may allow a file's contents and refuse its attributes (`getattr`); the per-result check asked only `MAY_READ`, so predicates and the cursor gave the values | fix | under a labelling LSM, a checked result also needs `vfs_getxattr()` of each attribute the query reads, terms and order (`6653949`, `5013c7e`) | SELinux run, type `moobtest_noxattr_t`: `QUERY2 names a file whose attributes the client may not read` |
| C2 | A query refused by the build admission (`EDQUOT`) left the domain record it had made, outside the budget and the shrinker, until unmount | fix | a build that fails before it began, with nobody else holding the record, marks it as a removed one and drops it (`3d7f9a3`) | step `refusedrecs`: `rr-refused-queries-left-records (16 before, 56 after 40 refusals)` |

Suspicions not reproduced, kept for later: S1 (a build fails as a whole on one entry a mounter
without `CAP_DAC_OVERRIDE` cannot read), S3 (`ENOMEM` from the budget as a coarser R2), S4
(overlapping layer mounts), S5 (a cursor turning `ESTALE` is a one-bit signal of a chmod);
review 2: an orphan domain record when a `rmdir` falls between the pre-build unlink check and a
failed build, and a timing difference from MAC-hidden matches in a homogeneous partition.

## R1: the cost of the fix

The fix tried was `path_has_submounts()` (exported, `fs/dcache.c`) on the domain directory at
each `QUERY2`, with per-result checks when it finds a mount. It walks every cached dentry of the
domain with the global `mount_lock` held. Measured on the test host (KVM, 6.12.111), a domain of
1 000 files in 100 directories, all cached, one result, median of 3 000 `QUERY2` calls of a
static client (`tests/q2bench.c`, a local experiment branch, not kept): 12.8 and 13.3 µs
without it, 55.8 and 57.2 µs with it; with a tmpfs mounted on one directory, 29.0 µs (the walk
stops at the mount). The 20 000-file round did not run (the lower image was full). The cost
grows with the cached part of the domain, so the maintainer recorded R1 as an exception: mount
namespaces, like path-based LSMs, do not filter query results; to hide a domain from a confined
process, hide the domain directory itself. A cheaper detection would be a later ADR.

## R2 and R5: reasons of the exceptions

- **R2.** A build counts the domain as the mounter sees it, since the domain is the unit the
  homogeneity rule authorizes; hiding the cost of a part the caller cannot list would need a
  build per caller. The count is coarse (one bit a query, about 15 queries with write access
  inside the domain) and the advice stands: a private subtree belongs in its own domain.
- **R5.** ADR-0019 bounds build work per uid and per mount and calls the uid table's exhaustion
  an availability trade-off; fair sharing between users is ADR-0011 non-promise 5. Subordinate
  uids make one person many uids; per-user memory limits (ADR-0029) still bound the memory.

## Changes

- `layer/index.c`: R3 parameter modes; R4 flag gate in `do_get_info()`; R7 domain prefix in
  `struct moobl_qscope`, checked in `add_candidate()`; C1 `caller_may_getxattr()` and
  `caller_may_get_terms()` in `caller_may_read()`, `sc.xattrs` set from `lsm_labels()` in
  `query2_exec()` and `do_query()`; C2 `domain_forget_unbuilt()` in `domain_ensure()`.
- `layer/main.c`: the test parameter `rename_end_delay_us` (0 by default) at the end of a
  successful `moobl_rename()`.
- `vm/suite.sh`: steps `mounthide` (opt-in), `paramperm`, `renamewin`, `stalebit`,
  `refusedrecs`.
- `vm/selinux/moobtest.te`, `vm/selinux-guest.sh`: type `moobtest_noxattr_t` and the C1 checks,
  with controls (the client reads the file and is refused its attribute directly and through the
  layer; it may read the attribute of `ro`).
- Errors of this batch, fixed in it: the first C1 commit did not build (the scope structure is
  defined after `caller_may_read()`), fixed in `5013c7e`; `stalebit` first mounted its
  single-user case without `domain_depth=0`, whose default is 1.

## Checks

On the test host (Mac mini, KVM, Debian 6.12.111), in fresh clones of the branch:

- Negative rounds, each on the test commit before its fix: the five failures quoted in the table.
- Positive rounds: `paramperm`, `renamewin` at `afb0b38` with `checkpatch`, and the neighbouring
  steps `domainrecs selfcheck hookrace buildmoved landlock`; `refusedrecs domainrecs domainreuse
  buildlimit buildrace buildmoved` at `3d7f9a3`, and `refusedrecs domainrecs domainreuse
  buildlimit` under KASAN, UBSAN and lockdep (`MOOBFS_KASAN_ONLY_PASS`), since C2 changes a
  record's lifetime; at `5013c7e`: `vm/selinux-local.sh` (`fsuse differ=0`, the four earlier
  SELinux checks pass, `rw` and `ro` still answered), `make checkpatch`, and `stalebit diag
  permmatrix permgaps permchurn fallbackfault renamewin paramperm refusedrecs`, pass.
- The batch's full runs at `88aa88f`, from 08:51 to 10:43: full `MOOB_VM_KVM=1 make vm-test`,
  `MOOBFS_VM_PASS`; full `vm/kasan-local.sh`, `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN
  and lockdep with no kernel diagnostics`; `vm/smack-local.sh`, `MOOBFS_SMACK_RESULT: fails=0`
  (Smack is the other labelling LSM C1's check runs under). `make lint` failed on three
  `mkdir -p -m` of the new steps (shellcheck SC2174), fixed in `652e388`; there, `make lint`
  passes and `renamewin stalebit` pass; `mounthide` fails as recorded
  (`mh-answer-named-a-file-under-a-hiding-mount-ext4`).
- On this Mac, after the surfaces' commit: `make test`, `scripts/check-retired-claims.sh`,
  `scripts/check-pins.sh`, `scripts/check-matrix-page.sh`, `scripts/check-site.py`,
  `scripts/site-faq.py --check` and `git diff --check`, pass.

## Limitations

- R1, R2 and R5 stay, as recorded exceptions.
- R7's fix gives no path, not the new one, to a result answered inside the rename window.
- C1's check costs one `getxattr` per attribute per checked result, only under a labelling LSM.
- The suspicions above are not reproduced.

## Surfaces

Updated in this branch, which stays local until the advisory (maintainer, 2026-11-19):
`CHANGELOG.md`, `SECURITY.md`, `docs/status.md` and `docs/status.pt-BR.md`,
`docs/review-findings.md`, notes in ADR-0011 (R1, R2, R4, R5, C1) and ADR-0019 (R5),
`docs/spec-query-v2.md` and `docs/compatibility.md` (R4, R7, C1), `docs/distribution-strategy.md`
§8 and the security roadmap S1. Left for the advisory: `SECURITY.md` and the advisory links.

## Next step

A draft advisory with the maintainer; then `SECURITY.md` and the advisory's links, the pull
request with the maintainer's agreement, and the advisory's publication after the merge.
