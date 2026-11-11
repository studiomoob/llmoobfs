# Register of review findings

Date: 2026-11-08. Status: **draft, to be checked by the maintainer.** Compiled from the
journals in `docs/history/` and checked against the tree of branch `claudiofp/b18-stage2-docs`
(based on `main` at `1cac601`). Nothing was run to build it.

*Status, 2026-11-11 (journal 0284):* no longer a draft. Every Part A finding now has a regression
test, is an answered design objection, or is listed under [Gaps](#gaps), item 1, as needing the
maintainer's decision, with the reason a test is not written. *Later the same day:* the
maintainer decided those items: six are recorded exceptions to criterion (2) (ADR-0030,
amendment of 2026-11-11, about 13:00), and 0114 item 8 has a test under SELinux, pending. The tests added in journals 0283
and 0284 are written and compiled; their VM runs are pending (journal 0284, "Pending").

## What this is

Stage 2 of `docs/distribution-strategy.md` §8, as amended on 2026-10-29
([ADR-0030](decisions/0030-stage-2-review-criterion.md), journal 0240), requires "(2) every
finding of an AI-assisted review triaged, reproduced and turned into a regression test". Until
now no single document listed those findings. This register lists them, one table per review in
chronological order, with the triage, the fix and the regression test the journals record. The
section [Gaps](#gaps) at the end lists what does not meet the criterion or could not be
established.

**Scope.** Part A covers the reviews that journal 0240 classifies as AI-assisted: the two
source reviews of `5582e06` (journals 0111 and 0114), the later rounds (journals 0144, 0151 and
0155), and the external reviewer's private reports (F15, F16, GHSA-756j-cgwq-gvwm,
GHSA-4gr9-rvgr-44qj, GHSA-3wx4-fh9w-2v66, and the scan walk behind GHSA-f769-xjf5-54wc). Part B
covers other reviews and inspections recorded in the journals that journal 0240 does not
classify. Some are described as AI-assisted in their own journal (0211 to 0213), others do not
say who or what did the reading. They are listed so that the maintainer can decide whether the
criterion covers them.

*Note, 2026-11-11 (journal 0283):* the maintainer decided (first agreed on 2026-11-09, confirmed
on 2026-11-11) that Part B stays informative, outside criterion (2) of ADR-0030 (amendment of
ADR-0030 of the same date). Its findings without a test whose test needs no kernel change were
given one in journal 0283 (Gaps, items 14, 16 and 17); 0083 A and the remainder of B2 R3 were
not, for the reasons given there. In Part A, the remainder of M2 (issue 117) is a documented
limit of stage 2, and GHSA-756j-cgwq-gvwm is not an open known leak (`docs/distribution-strategy.md`
§8, amendment of 2026-11-11).

## How to read the tables

- **ID**: the finding's identifier in its journal (numbers, `R2-n`, `Fn` of ADR-0011, advisory).
- **Triage**: *fixed*; *fixed, partial*; *accepted limit* (kept and documented); *not
  reproduced*; *false positive*; *duplicate of X*; *design objection* (answered by an ADR, no
  defect claimed); *test gap closed* (the finding was a missing test).
- **Fix**: the journal that made the change, and the commit or pull request where the journal
  records one. "(git)" marks a pull request found in the merge history, not named by the journal.
- **Regression test**: names exactly as they appear in the tree: a suite step (`MOOB_ONLY`
  name), a marker (`..._PASS`), a failure tag of `vm/suite.sh` (`fail "..."`), a test file, a
  Python test, or a mutant of `tests/mutation/homogeneity.mut`. Where the test runs:
  - [suite]: part of a full `make vm-test` run (and of the KASAN harness, which runs the suite);
  - [opt-in]: a step run only when named in `MOOB_ONLY` (`vm/suite.sh` header);
  - [KASAN]: only under `vm/kasan-guest.sh` (fault injection, or `/dev/fuse` needed);
  - [CI-py]: `make pylib-test` in `.github/workflows/ci.yml`;
  - [standalone]: a script no make target or workflow runs.
- "Negative round" means the journal records that the test failed with the fix undone.

---

## Part A — AI-assisted reviews named by ADR-0030

### A1. First source review of `5582e06` (journal 0111, 2026-10-14)

Left at the investigation stage; its candidates were taken up by journal 0114 (dated note of
2026-10-18 in journal 0111). No finding of its own was completed.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| 0111-a | Homogeneity shortcut admits unreadable files | duplicate of 0114 item 1 (F9) | — | see A2 item 1 | |
| 0111-b | Placement reused after a concurrent permission change | not reproduced | — | none | Journal 0114 could not construct the case ("Things checked and found sound") |
| 0111-c | Changes ignore the lower mount's write protection | duplicate of 0114 item 4 (F11) | — | see A2 item 4 | |
| 0111-d | SQL predicate translation | duplicate of 0114 item 9 | — | see A2 item 9 | |

### A2. Second source review of `5582e06` (journal 0114, 2026-10-15)

Made by an assistant from source reading; verified the same day against Linux 6.12.112 (table
at the end of journal 0114). Items 1, 2 and the frozen lower were private advisories
(GHSA-822g-5gf6-fj9q, GHSA-r83m-5gx7-mfxx, GHSA-p942-3xhv-m45h), published after the fixes.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 (F9) | Homogeneity rule answers group/owner members a file only "others" can read | fixed | journal 0116; PR 80 | step `permmatrix`, `tests/permmatrix.c`, `PERMMATRIX_STEP_PASS` [suite]; mutants M06, M07, M08 | The step requires cases the old rule got wrong (journal 0116); since journal 0233 it runs on ext4, btrfs and XFS |
| 2 (F10) | Per-result checks under `query_sem` wait on a FUSE mount, stalling every query | fixed | journal 0116; PR 80 | step `stall`, `tests/fusestall.c`, `STALL_PASS` [KASAN] | Prints `STALL_SKIPPED` where `/dev/fuse` is missing (Debian `make vm-test` guest) |
| 3 | Ref-walk `d_revalidate` reads a lower dentry `moobl_instantiate` may free | fixed | journal 0120 (PR 82, git) | none | No reproducer: needs a preemptible kernel and a full grace period (journal 0120). Recorded exception to criterion (2), maintainer 2026-11-11 (ADR-0030 amendment, journal 0284) |
| 4 (F11) | Name operations ignore a read-only lower mount | fixed | journal 0117; PR 80 | step `lowerro`, `LOWERRO_PASS`, tags `lr-ro-accepted`, `lr-fz-kernel-warning` [suite] | Negative round in journal 0117. Only ext4 is frozen in the step |
| 5 | A build holds a whole directory listing in unaccounted memory | fixed | journal 0120 (PR 82, git) | step `bigdir`, `BIGDIR_PASS`, tag `bd-differs-from-oracle` [suite] | Negative round in journal 0131 (batch boundary broken). Checks answers, not memory |
| 6 | `domain_drop`/`moobl_index_destroy` long walks without `cond_resched` | fixed | journal 0120 | none | Journal 0120: "no test, the cost is a scheduling point". Recorded exception to criterion (2), maintainer 2026-11-11 (ADR-0030 amendment, journal 0284) |
| 7 | Typed attribute cannot be set on a write-only file | fixed | journal 0120 | `tests/query.c` mode `xset` in each lower's step, tag `typed-attr-write-only-refused-$fs` [suite] | Negative round in journal 0131 |
| 8 | Directory reads run with the caller's credentials, not the mounter's | fixed | journal 0120 | `vm/selinux-guest.sh` with `tests/listdir.c`, `SELINUX_DIRREAD_PASS` [SELinux harness, `vm/selinux-local.sh`] (journal 0284) | Journal 0120: the difference shows only under an LSM policy. Test written 2026-11-11; **pending** until its run and negative round pass |
| 9 | Pushed equality ignores the constraint's collation (SQLite table) | fixed | journal 0120 | step `sqlext`, `SQLEXT_PASS`, tag `sx-differs` [suite] | Negative round in journal 0131 |
| V-a | Frozen lower: any user's create or chmod hits `WARN_ON` in ext4 | fixed | journal 0117; PR 80 | step `lowerro`, tag `lr-fz-kernel-warning` [suite] | New finding of the verification, part of F11 |
| V-b (F12) | Idmapped lower mount accepted, mapping ignored | fixed (refused at mount) | journal 0117; PR 80 | step `lowerro`, `LOWERRO_PASS` "idmapped lower: refused" [suite] | The Debian guest prints "idmapped lower: not tested (no idmapped bind mount here)" (journal 0171) |
| V-c | Suspected: a per-result check waiting on a layer directory lock | fixed with item 2 | journal 0116 | step `dirstall` (`DIRSTALL_PASS`) [suite] (journal 0278) | Closed by the same change as item 2 (journal 0114); negative round with `query_sem` held across the checks fails |

### A3. Review of `d110d4e` (journal 0144, 2026-10-20)

Described by its journal as AI-assisted. Correctness defects, no advisory (journal 0153).

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| R1 | Clearing `O_APPEND` with `F_SETFL` does not reach the backing file | fixed | journal 0149; integrated with journal 0153 (PR 107, git) | `tests/fcntl-flags.c` in each lower's step, tag `fcntl-flags-$fs`, `FCNTL_FLAGS_PASS` [suite] | Reproduced in a VM before the fix; negative branch `neg/fcntl-flags-test-only` (`80f9e81`) |
| R2 | Operational `getxattr` errors become a successful incomplete build | fixed | journal 0150; PR 107 (git) | step `xattrread`, `XATTRREAD_PASS`, tag `xr-build-eio` [suite]; mutant M11 | Not reproduced with a real I/O fault: the test injects the error (`scan_getxattr_errno_once`). Negative branch `neg/xattr-read-test-only` (`7c8682b`) |

### A4. Source review of `d110d4e` (journal 0151, 2026-10-20)

AI-assisted (journal 0240, maintainer's confirmation of 2026-10-30).

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| F1 (F13) | A chmod during a domain's first build leaves a stale homogeneous entry | fixed | journal 0152; PR 106 | `tests/chmodrace.c` in step `buildrace`, tag `br-chmod-during-build-still-answered-$fs`, `BUILD_RACE_PASS` [suite]; mutant M17 | Reproduced in a VM (`tests/review-d110d4e-vm.sh`, [standalone]). Negative round on `neg/chmodrace-test-only`. Advisory GHSA-5vmj-wjqc-7gx6 |

### A5. Review of `3627172`, round 2 (journal 0155, 2026-10-20)

One reviewer plus five parallel readings by assistants. Verification and fixes in journal 0156
(PR 109, commits named below), with the follow-ups listed per row. Issues 110 to 119 were opened
for the deferred items.

**Defects R2-1 to R2-12**

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| R2-1 (F14) | A directory 65+ components deep is placed nowhere by the write paths | fixed (deeper names not indexed, documented limit) | journal 0156, `dd4ee3a`; PR 109 | step `deeptree`, `tests/review-deep-tree.c`, `DEEP_TREE_PASS`, tags `dt-control-$fs`, `dt-deep-indexed-$fs`, `dt-value-indexed-$fs` [suite] (journal 0276); the write-path half also: tag `s14-deep-not-heterogeneous-$fs` in `HOMOGENEITY_GAPS_PASS` [suite], mutant M16 | Reproduced. Negative round without the scan's depth guard (journal 0276). Advisory GHSA-qgjj-2f2p-q82m |
| R2-2 | Same-directory rename during a reading keeps the scan's listing name | fixed | journal 0156, `dd4ee3a`; PR 109 | step `samedir`, `tests/review-samedir-rename.c`, `SAMEDIR_RENAME_PASS`, tags `sd-control-$fs`, `sd-race-keeps-listing-name-$fs`, `sd-not-in-the-window-$fs` [suite] (journal 0276) | Reproduced. Negative round without the loader's `name_is()` check (journal 0276) |
| R2-3 | A record left partial by a failed update counts as whole for a later rename/link | fixed | journal 0156, `281a719` | `tests/failnth.c` operation `setxattr-then-move`, `FAULT_INJECTION_PASS` [KASAN] | Negative at `905c4e7` (journal 0170) |
| R2-4 | Failed upper-inode creation leaves the lower object behind a valid negative dentry; `link` skips hooks | fixed | journal 0156, `d7f244d` | `tests/failnth.c` operations `create`, `mkdir`, `link`, `FAULT_INJECTION_PASS` [KASAN] | Negative `c0be08b` detected by create and mkdir; the `link` branch is not isolated (journal 0170) |
| R2-5 | Build root path captured before the `top_gen` snapshot | fixed | journal 0174 (issue 110); PR 126 | step `rootpath`, `tests/rootpath-race.c`, `ROOTPATH_PASS` [suite] | Reproduced by the negative round `1297710` (journal 0174) |
| R2-6 | Failed `record_dentry_path()` in the rename hook keeps the old name | fixed | journal 0156, `281a719`; test in journal 0170 | `tests/renamepath-fault.c`, `RENAMEPATH_FAULT_PASS`, tags `r26-control`, `r26-stale-path` [KASAN] | Negative `c10969f`; ext4 and root only |
| R2-7 | `ix->over_budget` is mount-wide and reset by every build | fixed | journals 0175, 0179 (issue 111); PRs 126, 127 | step `overbudget`, `tests/overbudget-race.c`, `OVERBUDGET_PASS`, tag `overbudget-race-$fs` [suite] | Negative branch `neg/issue111-over-budget-unfixed` (`f46cc15`) |
| R2-8 | `rec_set_partition()` can leave an empty record nothing frees | fixed | journal 0156, `281a719`; test revised in journal 0170 | `tests/failnth.c` operation `chmod-file`, `FAULT_INJECTION_PASS` [KASAN] | First mutation passed unexpectedly; revised fixture detects it (`219840c`, journal 0170) |
| R2-9 | `QUERY2_BATCH`: a runtime failure leaves earlier arrays written, headers not | accepted limit (behaviour stated in the spec, §4.3) | journal 0173 (issue 112); PR 125 | `tests/batch-errors.c`, `BATCH_VALIDATION_ATOMIC_PASS`, `BATCH_LATE_RESULT_FAULT_PASS`, `BATCH_HEADER_FAULT_PASS`, `BATCH_RUNTIME_ERRORS_PASS` [suite] | No code change; the test pins the documented behaviour |
| R2-10 | A build fails with `ENOENT` when a directory dies after its names were read | fixed | journal 0156, `fb602d8`; test in journal 0170 | `tests/deadscan.c`, `DEADSCAN_PASS`, tag `r210-dead-directory` [KASAN] | Negative `7a8bd7f`; ext4 and root only |
| R2-11 | `ix->domains` and `ix->fallback` grow without bound | fixed | journal 0210 (issue 113); PR 160 (git) | step `domainrecs`, `DOMAINRECS_PASS`, tags `dr-records-btrfs`, `dr-dead-query-btrfs` [suite] | Two negative rounds (journal 0210) |
| R2-12 | A file whose attribute list exceeds `XATTR_LIST_MAX` is left out silently | accepted limit (written in spec §9; `moobfs-check` reports it) | journal 0156, `3237804` | step `attrlist` (btrfs and XFS), `ATTRLIST_LIMIT_PASS`, tags `r212-check-status-$fs`, `r212-check-message-$fs`, `r212-answer-$fs` [suite] (journal 0276) | Negative round without the check in `tools/moobfs-check.c` (journal 0276); ext4 cannot hold such a list |

**Test gaps T2 to T11** (T1 was the finding of round 1, A4)

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| T2 | The cursor MAC is never the deciding check | test gap closed | journal 0156, `8c70ac6` | tag `t2-tampered-cursor-accepted-at-...` in `QUERY2_ORDER_PASS` [suite] | Negative round without the MAC (journal 0156) |
| T3 | `ext_acl(dom)` never exercised | test gap closed | journal 0156, `c0aeac3` | `HOMOGENEITY_GAPS_PASS`, tag `t3-acl-on-domain-directory-leaks-$fs` [suite]; mutant M02 | Negative round on `neg/homogeneity-gaps` |
| T4 | Write-path placement under a private directory untested | test gap closed | journal 0156, `c0aeac3` | `HOMOGENEITY_GAPS_PASS`, tag `t4-private-directory-leaks-$fs` [suite]; mutant M14 | Negative round; links not covered (journal 0156) |
| T5 | `query2_target()` `MAY_EXEC` never decisive | test gap closed | journal 0172 (issue 114); PR 124 | step `permgaps`, `tests/permission-gaps.c`, `PERMISSION_GAPS_PASS` [suite] | Negative `e32e2e0` |
| T6 | `truncated` as a count channel never asserted | test gap closed | journal 0172; PR 124 | step `permgaps`, `PERMISSION_GAPS_PASS` [suite] | Negative `5cff699` |
| T7 | `race2`'s child verdict lost; `\| tee` masks exit statuses | test gap closed | journal 0171; PR 123 (git) | `tests/test_harness_verdicts.py` (run by `vm/in-container.sh`) | Negative branch `neg/issue114-verdict` (`d07b8f5`) |
| T8 | Attributed directories never compared with the lower's decision | test gap closed | journal 0172; PR 124 | step `permgaps`, `PERMISSION_GAPS_PASS` [suite] | Negative `0a30f68` |
| T9 | `failnth` and `release` query as root only | test gap closed | journals 0172, 0178; PRs 124, 127 | `tests/permission-gaps.c`: `UNPRIV_RELEASE_PASS` [suite], `UNPRIV_FAULT_PASS`, `UNPRIV_FAULT_CASE_PASS`, tag `fi-unprivileged-permissions` [KASAN] | The T9 run exposed a silent omission under `ENOMEM`, fixed in journal 0178 |
| T10 | Hostile lower trees carry no permission adversity; kernel and oracle share the validator | test gap closed | journal 0172; PR 124 | step `permgaps`, `PERMISSION_GAPS_PASS` (18 fixed fixtures, independent of `common/attr.c`) [suite] | Negative `8f08495` |
| T11 | `docs/testing.md` overstates the harness; silent skips | fixed (documentation) | journal 0171 | not applicable | Inventory corrected 2026-10-21 (note in journal 0155) |

**VFS operations V1 to V9**

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| V1 | `posix_fadvise`, `readahead(2)`, `cachestat(2)` are no-ops | fixed, partial (`cachestat` stays an accepted limit) | journal 0224 (issue 115); PR 170 (git) | `tests/passthrough.c`, `PASSTHROUGH_PASS` (`PT_FADVISE_PASS`, `PT_READAHEAD_PASS`) [suite] | Negative round on `neg/b3d` (journal 0224) |
| V2 | `sync_file_range(2)` succeeds without writing | accepted limit (README) | journal 0156, `cfed44b` | `tests/passthrough.c` limits, `PT_LIMITS_PASS` [suite] (journal 0284) | Not fixable without the lower's mapping. The test pins the documented limit; it has no negative round |
| V3 | `statx` drops lower-provided fields | fixed | journal 0224; PR 170 (git) | `tests/passthrough.c`, `PT_STATX_PASS` [suite] | |
| V4 | Ref-walk `d_revalidate` checks less than RCU path | fixed | journal 0156, `d7f244d` | `tests/layer.c behind`, `LAYER_BEHIND_PASS`, tag `layer-behind-$fs`, in `run_lower` [suite] (journal 0276) | Same as M5. Negative round with the ref-walk checks removed (journal 0276) |
| V5 | Default `drop_inode` pins deleted lower inodes | fixed | journal 0224; PR 170 (git) | `tests/passthrough.c`, `PT_DROP_INODE_PASS` [suite] | |
| V6 | `RENAME_WHITEOUT` forwarded | fixed (refused, `EINVAL`) | journal 0156, `fb602d8` | `tests/query.c` mode `whiteout`, tag `v6-rename-whiteout-not-refused-$fs` [suite] | |
| V7 | `F_SETFL` bits other than `O_APPEND` never reach the lower | fixed | journals 0149, 0224; PR 170 (git) | `tests/passthrough.c`, `PT_NOATIME_PASS` [suite]; `tests/fcntl-flags.c` | |
| V8 | `moobl_rename` copies the whole lower `i_flags` word | fixed (all five sites) | journal 0156, `fb602d8` | none dedicated | Journal 0156: "no dedicated test". Recorded exception to criterion (2), maintainer 2026-11-11 (ADR-0030 amendment, journal 0284) |
| V9 | FIFO/socket comment says opening is refused | fixed (comment) | journal 0156, `cfed44b` | not applicable | |

**Maintainer's reading M1 to M8**

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| M1 | Kernel memory sized by an unprivileged user, not memcg-accounted | fixed | journal 0180 (issue 116); PR 129 | step `memcg`, `tests/memcg-query.c`, `MEMCG_ACCOUNT_PASS` [suite] | Isolated negative rounds for buffer and index (journal 0180) |
| M2 | Mount-wide rwsem taken for writing on every instantiation and eviction | fixed, partial | journal 0223 (issue 117); PR 170 (git) | step `ixlock`, `tests/ixlock.c`, `IXLOCK_PASS` [suite] | Inodes with a record still take the lock; removing `rec->upper` stays a proposal. Accepted on 2026-11-11 as a documented stage-2 limit (journal 0283); issue 117 stays open |
| M3 | Unprivileged `QUERY2` triggers an unbounded subtree walk with the mounter's credentials | accepted limit (bounded by work, deadline and rate; stated in `SECURITY.md`) | journals 0182 to 0193, 0210, 0241; PRs 135, 138 | steps `buildlimit` (`BUILD_LIMIT_PASS`), `buildwork` (`BUILD_WORK_OPTION_PASS`), `buildtime` (`BUILD_TIME_PASS`), `buildmoved` (`BUILD_MOVED_PASS`), `builddenial` [opt-in]; `buildcost` [suite] | Issue 118 closed with the slow-lower evidence of journal 0210 |
| M4 | `->lookup` returns `EXDEV`/`EOPNOTSUPP` for listed names and warns from a user path | fixed | journal 0221 (issue 119); PR 170 (git) | `tests/layer.c` `cfcheck`, `LAYER_CASEFOLD_PASS`, `CASEFOLD_REFUSED_PASS`, `LAYER_SUBVOLUME_PASS` [suite] | Negative round (tag `cf-check`, journal 0221) |
| M5 | Two halves of `d_revalidate` disagree | duplicate of V4 | journal 0156, `d7f244d` | see V4 | |
| M6 | Ioctl magic `'M'` shared in-tree; implicit tail padding | fixed (ADR-0026: magic kept, padding named and checked) | journal 0222; PR 170 (git) | `tests/query.c` refuses `pad[3]` (in `LAYER_LOWER_PASS`) [suite] | Negative round `term_check()` without `pad[3]` (journal 0222) |
| M7 | `capable()` and `ns_capable_noaudit()` gate related things | fixed (one privilege test) | journal 0222; PR 170 (git) | steps `demo` (`DEMO_PASS`) [suite] and `buildlimit` [opt-in] | No negative round: no allowed/denied outcome changes (journal 0222). Recorded exception to criterion (2), maintainer 2026-11-11 (ADR-0030 amendment, journal 0284) |
| M8 | Seven one-line items (build guard, `getattr`, `flush`, `fileattr_set`, `SB_I_NOEXEC` doc, `scan_delay_us`, `pr_warn_ratelimited`) | fixed or answered item by item | journal 0221; PR 170 (git) | M8.1: `static_assert` in `layer/main.c` (build-time); M8.7: tag `lm-build-failure-logged-repeatedly` in `LIMITS_BUDGET_PASS`, step `limits` [suite] (journal 0276); M8.5: `tests/passthrough.c` noexec, `PT_NOEXEC_PASS` [suite] (journal 0284); others none | M8.2 and M8.3: Recorded exception to criterion (2), maintainer 2026-11-11 (ADR-0030 amendment, journal 0284); M8.4 and M8.6 have no behaviour to test. See Gaps |

**Design opinions D1 to D4**

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| D1 | The kernel validates `user.*` values | design objection | ADR-0017 (`aee15b4`, accepted `7a5adda`) | not applicable | |
| D2 | The homogeneity rule is a second permission model | design objection | ADR-0017 | not applicable; commitments became S14 (mutants), S15 (`permchurn`), S16 (`selfcheck`) | |
| D3 | Whether this belongs in the kernel | design objection | ADR-0017 | not applicable | |
| D4 | `index_idle` floor lets a hot mount opt out of reclaim | design objection (decided 2026-11-09, journal 0276) | ADR-0012 (the floor is its answer to thrashing; the budget still bounds a build) | not applicable | Was absent from journal 0156's table and ADR-0017 |

### A6. External reviewer's private reports (2026-10-27 to 2026-10-29)

AI-assisted analysis; the reviewer chose where to look, reproduced each defect and wrote or
checked the fix (journal 0240, ADR-0011 note after F16).

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| F15 (GHSA-95h9-j7c2-gcw6) | `QUERY2` names files under a search-only directory | fixed (ADR-0025) | journal 0215; PR 166 (git) | step `permmatrix`, tags `pm-so-leak-$fs`, `pm-cr-leak-$fs`, `pm-mv-leak-$fs` [suite]; mutant M09; `python/tests/test_linux.py::test_search_only_directory` [CI-py] | Negative rounds per hunk (journal 0215) |
| F16 (GHSA-6xf4-r246-92r6) | A set-user-ID bit given on the lower survives a write through the layer | fixed | journal 0218 (fix written by the reviewer); PR 166 (git) | `tests/layer.c` `suidlow` checks, `LAYER_PASSTHROUGH_TESTS_PASS` [suite]; splice and sendfile added in journal 0233 | Negative round `FAIL line 714` (journals 0218, 0219) |
| GHSA-756j-cgwq-gvwm (a) | Domain record of a directory removed behind the layer answers its inode number's next owner | fixed (records keep `i_generation`) | journal 0216; PR 166 (git) | step `domainreuse`, `DOMAINREUSE_PASS`, tag `du-stale-$fs` [suite] | Two negative rounds (journal 0216) |
| GHSA-756j-cgwq-gvwm (b) | Same cause for file records (`ix->inodes`) | fixed | journal 0216, second round | step `domainreuse` (file case and v1-first case) [suite] | Found by "an independent read-only review of the branch" (journal 0216); see Gaps |
| GHSA-4gr9-rvgr-44qj (a) | MCP adapter: path components checked then reused, symlink swap escapes `--root` | fixed | journal 0227; PR 175 | `python/tests/test_mcp.py::test_component_swapped_for_a_link` | Skipped in CI (no `mcp` package); see Gaps |
| GHSA-4gr9-rvgr-44qj (b) | MCP `read` blocks forever on a FIFO | fixed | journal 0227; PR 175 | `python/tests/test_mcp.py::test_fifo_does_not_hang_read` | Skipped in CI |
| GHSA-4gr9-rvgr-44qj (c) | `check_identity()` misses saved uid 0 and effective capabilities | fixed | journal 0227; PR 175 | `python/tests/test_mcp.py::test_identity_refused` | Skipped in CI |
| GHSA-3wx4-fh9w-2v66 | LangGraph store: `batch()` operations skip the label rule, `..` escapes the root | fixed | journal 0227; PR 175 | `python/tests/test_langgraph.py::test_batched_labels_stay_inside_the_root` | Skipped in CI (no `langgraph`) |
| GHSA-f769-xjf5-54wc | Python scan fallback walks by path (TOCTOU, attribute oracle) | fixed (walk by descriptor) | journal 0231; PR 177 (git) | `python/tests/test_linux.py`: `test_scan_keeps_a_directory_swapped_for_a_link`, `test_scan_reads_the_file_it_judged`, `test_scan_under_a_racing_swap`, `test_scan_never_follows_links` [CI-py] | Old walk failed 3 of 11 as root (journal 0231) |

---

## Part B — Other reviews in the journals, not classified by ADR-0030

### B1. External architecture review, three rounds (journals 0019 and 0020, 2026-10-10)

A "master prompt" produced outside the project; its authorship is not recorded.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| B1-1 | `QUERY` returns every index entry without checking the caller's permissions | fixed (phase 3b) | journal 0020 | `ISOLATION_PASS` [suite] | |
| B1-2 | "ACLs reach the lower through xattr passthrough" unproven | fixed (claim was wrong; `get_acl`/`set_acl` delegated) | journal 0020 | ACL block of each lower's step, tags `acl-set-$fs`, `acl-open-grant-$fs`, `acl-search-only-leak-$fs` [suite] | |
| B1-3 | Identity checks for copy, hard link, namespace | fixed | journal 0020 | tags `id-dup-not-detected-$fs`, `id-dup-reason-$fs` [suite] | |
| B1-4 | Idmapped and user-namespace mounts must be proven refused | fixed (contract proven) | journal 0020 | `tests/idmap.c`, `IDMAP_REFUSED_EINVAL`, `USERNS_MOUNT_REFUSED_EPERM` [suite] | |
| B1-5 | Design and plan proposals (isolation in M3, benchmark method, batch/cursor, persistence, scales, SDK/MCP, baselines) | accepted or rejected with reasons | journal 0019 | not applicable | Grouped: not defects |

### B2. Project review (journal 0023 and `docs/project-review-2026-10-10.md`, 2026-10-10)

The journals do not record whether it was AI-assisted. No journal takes up R1 to R7 by number;
the outcomes below come from later journals that fixed the same defects.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| R1 | Typed value changed to RAW keeps its index entry | fixed | journal 0083 (item D); PR 60 (git) | tag `insp-raw-over-typed-stale-$fs` [suite] | Negative round in journal 0083 |
| R2 | Path truncated before authorization drops readable results | fixed | journal 0083 (item C) | tag `insp-deep-truncated-$fs` [suite] | Negative round in journal 0083 |
| R3 | `caller_may_read()` turns operational errors into absence | fixed, partial | journal 0178 (`ENOMEM` only) | `UNPRIV_FAULT_PASS` [KASAN] | Other lookup errors still omit the result (`layer/index.c`, `caller_may_read()`); see Gaps |
| R4 | Inode identity across btrfs subvolumes | fixed (subvolume refused) | journals 0083 (item F), 0100 | `NESTED_SUBVOLUME_REFUSED`, tag `subvol-entered` [suite]; `LAYER_SUBVOLUME_PASS` | Reproduced in journal 0083 |
| R5 | Idmapped lower accepted | fixed | journal 0117 (F12) | see A2 V-b | Journal 0114 notes R5 was never fixed before F12 |
| R6 | The batch limits results, not the scan work | not established | — | — | See Gaps |
| R7 | Checker ignores `FTW_NS`/`FTW_DNR`; attribute name not escaped in JSON | fixed, partial (escaping) | journal 0213 | `tests/tools-json.sh` (`TOOLS_JSON_PASS`), `tests/hostile-user.sh` (`HOSTILE_USER_PASS`) | The incomplete-inspection half: see Gaps |
| R-other | Eight "other robustness points" and the plans/communication notes | not established | — | — | Not triaged one by one in any journal; see Gaps |

### B3. Stacked-filesystem rules audit (journal 0066, 2026-10-12)

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| R-CRED-3 (F3) | `setattr` forwards `ATTR_MODE` with `ATTR_KILL_S*ID`: `BUG()` on a write to a set-user-ID file | fixed | journal 0069 | `tests/layer.c` (06755 write, truncate, chown as uid 1001), `LAYER_PASSTHROUGH_TESTS_PASS` [suite] | Reproduced (oops) in journal 0066 |
| R-CRED-2 (F4) | Mounter credentials keep `CAP_SYS_RESOURCE` | fixed | journals 0069, 0132 | step `reserve`, `LAYER_RESERVE_PASS` [suite] | Quota case argued from source, not tested (journal 0069) |
| R-INODE-4 | `st_dev`/`st_ino` not unique across btrfs subvolumes | fixed (subvolume refused) | journal 0083 | see B2 R4 | |
| R-doc | Anachronistic kernel-doc citation in the security roadmap | fixed (noted in the research document) | journal 0066 | not applicable | |

### B4. Review of the mounter-credential sites (journal 0079, 2026-10-13)

"A reading of the code by the same author" (journal 0079).

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| B4-1 | `moobl_copy_attr` copies the lower's whole `i_flags` word | fixed (four flags with `inode_set_flags`) | journal 0079 | `tests/layer.c` file-flag checks (`FS_IOC_GETFLAGS`, append-only, immutable) [suite] | Two negative runs (journal 0079) |
| B4-2 | The table's "read, write" row was wrong about set-user-ID | duplicate of F16 | journal 0218 | see A6 F16 | Dated note in journal 0079 |

### B5. Three inspection reports (journal 0081, 2026-10-13)

Sessions run by the maintainer; whether they were AI-assisted is not recorded.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| 1-a | `GFP_ATOMIC` allocations under the index lock; reclaim can re-enter the lock | fixed (NOFS scope) | journal 0082 | lockdep priming in `moobl_index_init()` (`fs_reclaim_acquire`, `might_lock`, `layer/index.c`) under the KASAN/lockdep harness | Negative run (journal 0082). An assertion, not a suite step |
| 1-b | No `__GFP_ACCOUNT` | fixed later | journal 0180 (M1) | step `memcg` [suite] | |
| 1-c | One lock for the whole index | accepted limit | — | — | Later M2 (A5) |
| 1-d | No `lockdep_assert_held` | fixed | journal 0082 | assertions in `layer/index.c` | |
| 1-e | No `MODULE_AUTHOR` in the M1 module | fixed | journal 0082 | not applicable | Cosmetic |
| 2-a | `vfs_getattr_nosec`, direct `->fiemap` and `->flush` | false positive (same as overlayfs) | — | — | Later M8.2, M8.3 |
| 2-b | SELinux never tested | fixed (new roadmap item S12) | journal 0104 | `vm/selinux-local.sh` (not part of `make vm-test`) | Found F6 |
| 3-a | 11 allocations not zeroed | false positive | — | — | Every copy to user space checked (journal 0081) |
| 3-b | No `MODULE_VERSION` | fixed | journal 0082 | not applicable | |
| 3-c | No `pr_debug`, tracepoints or debugfs | accepted as roadmap item | journal 0102 (tracepoints) | not applicable | |
| 3-d | Security roadmap partly in Portuguese | duplicate of issue 38 | — | — | |

### B6. Read-only inspection of the index (journal 0083, 2026-10-13)

A separate session run by the maintainer; whether it was AI-assisted is not recorded.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| A | Dentry reference dropped with the index lock held | fixed | journal 0083 | none | Covered indirectly by lockdep and race tests (journal 0083); no test written in journal 0283, which says why |
| B | Rename over an indexed file leaves the replaced file indexed | fixed | journal 0083 | tag `insp-rename-over-count-$fs` [suite] | Negative round |
| C | `MOOB_RES_PATH_TRUNCATED` never reaches the caller | fixed | journal 0083 | tag `insp-deep-truncated-$fs` [suite] | = B2 R2 |
| D | Typed attribute overwritten by raw keeps its entry | fixed | journal 0083 | tag `insp-raw-over-typed-stale-$fs` [suite] | = B2 R1 |
| E | Unchecked `xa_store` on a directory permission change fails open | fixed | journal 0083 | step `fallbackfault`, `tests/fallbackfault.c`, `FALLBACK_FAULT_PASS` [KASAN] (journal 0283) | Fault injection; written 2026-11-11, VM run pending (journal 0283) |
| F | Two files with one inode number (btrfs subvolume) share a record | fixed (subvolume refused) | journal 0083 | `NESTED_SUBVOLUME_REFUSED` [suite] | Reproduced |
| G | Partition not recomputed when a hard link count returns to one | accepted limit (false negative) | — | — | |
| H | Query loops walk non-matching entries without `cond_resched` | not established | — | — | Recorded, not changed; see Gaps |

### B7. Code review of `layer/` (journal 0105, 2026-10-14)

Requested by the maintainer; the reviewer is not named.

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| F7 | Results copied to user space with `query_sem` held | fixed | journal 0105; PR 71 (git) | `lockdep_assert_not_held()` at the copy (`layer/index.c`) under the KASAN/lockdep harness; tests of journal 0113 (`--bad-results` path) [suite] | Journal 0113: its test guards the new path but "does not prove the fix of finding F7" |
| B7-1 | An invalid v1 `QUERY` built the whole index first | fixed | journal 0105 | `tests/query.c` mode `v1bad`, tag `v1-bad-request-built-$fs` [suite] | Negative round (journal 0113) |
| B7-2 | Stale texts (module description, comments, UAPI header) | fixed | journal 0105 | not applicable | Documentation |
| B7-3 | Recorded items: index lock per inode, rebuild after release, fallback until remount, remount options, term padding, ioctl letter, domains table never shrinks | duplicates of later findings | journals 0106, 0210, 0221 to 0223 | see M2, M6, R2-11 | |

### B8. Contributor's AI-assisted source reviews (journals 0211, 0212, 0213, 2026-10-27)

Described as "made with an AI assistant" in each journal; merged as PRs 161 to 163 (journal
0214).

| ID | Finding | Triage | Fix | Regression test | Notes |
| --- | --- | --- | --- | --- | --- |
| 0211-1 | Manual KCSAN/arm64 KASAN `steps` input reaches the guest's command line | fixed (input filter) | journal 0211; PR 161 | `scripts/test-steps-filter.sh`, `STEPS_FILTER_PASS` (`make lint`, in CI) (journal 0283) | Negative rounds in journal 0283 |
| 0211-2 | `install.sh uninstall` splits a path with a space | fixed | journal 0211; PR 161 | `scripts/test-install.sh`, `INSTALL_UNIT_PASS` (`make install-unit`, in CI) | Negative round |
| 0211-3 | Syzkaller image setup follows absolute links to the host | fixed (`nosymfollow`) | journal 0211; PR 161 | `scripts/test-image-setup.sh`, `IMAGE_SETUP_UNIT_PASS` (`make image-setup-unit`, root) (journal 0283) | Written 2026-11-11; run pending (journal 0283) |
| 0211-4 | Dependabot version updates; dev-container digest pin | not taken (decision) | — | not applicable | |
| 0212-1 | `Term` accepts hand-built values that overflow the request field | fixed | journal 0212; PR 162 | `test_unit.py::test_term_checks_a_value_built_by_hand`, `::test_fill_bounds_a_payload_replaced_after_the_term` [CI-py] | |
| 0212-2 | `struct.error` escapes for out-of-range integers | fixed | journal 0212; PR 162 | `test_unit.py::test_int64_range` [CI-py] | |
| 0212-3 | `find_library("c")` starts helper processes | fixed | journal 0212; PR 162 | `test_linux.py::test_no_helper_process` [CI-py] | |
| 0212-4 | Scan judges a linked domain by `lstat()` | fixed | journal 0212; PR 162 | `test_linux.py::test_scan_through_a_link_to_the_domain` [CI-py] | |
| 0213-1 | `moobfs-check` escapes only `path` in its JSON | fixed | journal 0213; PR 163 | `tests/hostile-user.sh` (`HOSTILE_USER_PASS`), `tests/tools-json.sh` (`TOOLS_JSON_PASS`) | Negative round |
| 0213-2 | `moobfs-index --uid` keeps root's groups; non-numeric uid read as 0 | fixed | journal 0213; PR 163 | `tests/tools-uid.sh`, `TOOLS_UID_PASS` (`make hostile-test`; the group part as root) (journal 0283) | Negative round in journal 0283 |
| 0213-3 | Tools trust `count` and `path_len` beyond the UAPI header | fixed | journal 0213; PR 163 | `tests/tools-uapi.sh` with `tests/fake-query2.c`, `TOOLS_UAPI_PASS` (`make hostile-test`) for the `moobfs` CLI (journal 0283) | Negative rounds in journal 0283; the SQLite table's half has no test |
| 0213-4 | Tools read attributes by path after `nftw()` | fixed | journal 0232; PR 178 (git) | `tests/tools-walk.sh`, `TOOLS_WALK_PASS` (`make hostile-test`) | Negative round (journal 0232) |

---

## Gaps

### 1. Findings with no regression test in the tree

Part A (inside the criterion as worded):

1. **0114 item 3** (ref-walk revalidation reads a freed lower dentry): fixed in journal 0120,
   no reproducer and no test.
2. **0114 item 6** (`cond_resched` in long walks): no test, by the decision of journal 0120.
3. **0114 item 8** (directory reads with the caller's credentials): no test; differs only under
   an LSM policy, which `make vm-test` does not load.
4. **0114 V-c** (suspected wait on a layer directory lock): closed by journal 0278, step
   `dirstall` in the full suite (needs root's userfaultfd; `DIRSTALL_SKIPPED` without it).
5. **R2-1 (F14)**: closed by journal 0276, step `deeptree` in the full suite.
6. **R2-2**: closed by journal 0276, step `samedir` in the full suite.
7. **R2-12**: closed by journal 0276, step `attrlist` (btrfs and XFS).
8. **V4 / M5**: closed by journal 0276, `tests/layer.c behind` in `run_lower`.
9. **V8**: no dedicated test (journal 0156).
10. **V2** and the `cachestat(2)` part of **V1**: accepted limits, no test.
11. **M7**: no negative round; the outcome is checked by `demo` and by `buildlimit`, which is
    opt-in.
12. **M8** items 2 to 6: no test (journal 0221 states for item 2 that no test can tell the two
    paths apart on 6.12). Item 1 is a build-time `static_assert`, not a test. Item 7 (the
    build-failure warning logged once) is checked in step `limits` since journal 0276.

*Note, 2026-11-11 (journal 0284), on items 1 to 3 and 9 to 12.* Written: **V2** and the
`cachestat(2)` part of **V1** (`PT_LIMITS_PASS`, a pin of the documented limits, no negative
round possible since nothing was fixed) and **M8.5** (`PT_NOEXEC_PASS`, the errnos of
`SB_I_NOEXEC` and `SB_I_NODEV`); both in `tests/passthrough.c`, VM run pending. **M8.4** (not a
defect) and **M8.6** (a test parameter kept, with its reason) have no behaviour to test.
**Needs the maintainer's decision**, with the reason a test is not written (journal 0284):
0114 item 3 (a test that fails without the fix needs a hold point inside ref-walk revalidation:
a kernel change); 0114 item 6 (the only symptom is scheduling latency or a soft lockup, which a
test reaches only with a domain far beyond the index budget, or with a scheduler-latency probe);
0114 item 8 and M7 (they differ only in what an LSM sees or logs: testable without a kernel
change under the SELinux harness of S12, `vm/selinux-local.sh`, not in the suite; effort M);
V8 (no flag outside the copied mask has an effect the VFS shows on the layer inode through a
rename that succeeds on ext4, btrfs or XFS: `S_SWAPFILE` makes the rename fail first); M8.2 and
M8.3 (no supported kernel or lower tells the two paths apart: `vfs_getattr()` adds
`AT_GETATTR_NOSEC` on 6.12, and none of ext4, btrfs and XFS has `->flush`).

*Note, 2026-11-11, about 13:00 (journal 0284):* decided. 0114 items 3 and 6, V8, M7, M8.2 and
M8.3 are recorded exceptions to criterion (2) (ADR-0030, amendment of that time). 0114 item 8
has a test, `SELINUX_DIRREAD_PASS` in the SELinux harness (`vm/selinux-local.sh`), written and
**pending**: it stays listed here until its run and its negative round pass.
13. **D4**: closed on 2026-11-09 as a design objection answered by ADR-0012 (journal 0276); no
    test, as for D1 to D3.

Part B (outside the criterion as worded, listed for the maintainer):

14. **0083 A** and **0083 E**: no test (journal 0083).
    *Note, 2026-11-11 (journal 0283):* E has a test (`fallbackfault`, KASAN harness; its VM run
    and negative round are pending). A has none: a test that fails without the fix needs a point
    in the query path that holds the window open, which is a kernel change (journal 0283).
15. **B2 R3** (remainder): `caller_may_read()` still returns 0 (omit) for lookup and permission
    errors other than `-ENOMEM`; no test and no recorded decision for those errors.
    *Note, 2026-11-11 (journal 0283):* still no test: what the test should expect depends on a
    decision about those errors, which is not taken.
16. **0211-1** (workflow `steps` filter) and **0211-3** (`nosymfollow` in
    `vm/syzkaller/image-setup.sh`): checked by hand, no test in the tree.
    *Note, 2026-11-11 (journal 0283):* both have a test now: `scripts/test-steps-filter.sh` and
    `scripts/test-image-setup.sh`.
17. **0213-2** and **0213-3**: no dedicated test.
    *Note, 2026-11-11 (journal 0283):* `tests/tools-uid.sh` and `tests/tools-uapi.sh`; the
    SQLite table's half of 0213-3 still has none.
18. **F7** and **0081 1-a**: guarded by in-code lockdep assertions that fire only under the
    KASAN/lockdep harness; journal 0113 states its own test does not prove F7.

Tests that exist but do not run in the gates:

19. **R2-3, R2-4, R2-6, R2-8, R2-10, T9 (fault part), 0114 item 2 (F10)**: run only in
    `vm/kasan-guest.sh` (fault injection, `/dev/fuse`), not in a Debian `make vm-test`. The
    KASAN job runs them; the plain suite prints `STALL_SKIPPED` for `stall`.
20. **R2-4**: the `link` recovery branch is not isolated by its negative round (journal 0170).
    **R2-6, R2-10**: ext4 and root only (journal 0156 follow-up).
21. **M3**: `buildlimit`, `buildwork`, `buildtime`, `buildmoved` and `builddenial` are opt-in
    steps, absent from a full `make vm-test`.
22. **GHSA-4gr9-rvgr-44qj (a, b, c)** and **GHSA-3wx4-fh9w-2v66**: the tests skip unless the
    `mcp` or `langgraph` packages are installed; CI's `make pylib-test` installs neither
    (journal 0227: "the runs above are the evidence for them"). `python/tests/mcp_mounted.py`
    (`MOOBFS_MCP_PASS`) is run by hand only.
23. **F12**: the Debian guest prints "idmapped lower: not tested"; the refusal is exercised only
    where an idmapped bind mount can be made (journal 0171).
24. **R2** of 0144: the test injects the error; no physical I/O fault was reproduced (journals
    0150, 0153).

### 2. Findings whose triage could not be established

1. **0155 D4** (`index_idle` floor): absent from journal 0156's table, from ADR-0017 (D1 to D3)
   and from issues 110 to 119 as listed in journal 0156.
   *Note, 2026-11-09 (journal 0276):* triaged by the maintainer as a design objection answered by
   ADR-0012: the floor is the mechanism ADR-0012 chose against release-and-rebuild loops; it
   applies to the shrinker only, is bounded (0 to 86 400 s, a mount option), and a build
   over `index_budget` may still release a domain used a moment ago.
2. **B2 R6** (scan work not bounded by the batch): no journal takes it up; whether the later
   `QUERY2` design (journals 0035 to 0038) or the build limits (M3) answer it is not recorded.
3. **B2 R7**, incomplete-inspection half: journal 0232 keeps the earlier behaviour of not
   passing entries `fstatat` refuses or directories that cannot be opened, and does not say
   whether the result is now marked incomplete.
4. **B2 "other robustness points"** (eight items, among them the incomplete-index state, real
   RAM vs `index_used`, `--uid` supplementary groups, revocation barriers, read-only/freeze,
   reflink and xattrs, git and export/import, namespace as a barrier): not triaged one by one.
   Some match later work (F11, 0213-2), but no journal records the mapping.
5. **B6 H** (query loops without `cond_resched`): recorded, not changed; no later decision found.
6. **GHSA-756j-cgwq-gvwm (b)**: found by "an independent read-only review of the branch"
   (journal 0216). Who made it, and whether it was AI-assisted, is not recorded.
7. **Classification**: journal 0240 does not list journals 0211 to 0213, although each says its
   review was "made with an AI assistant". Journals 0019, 0023, 0066, 0079, 0081, 0083 and 0105
   do not say whether their reading was AI-assisted. Whether criterion (2) covers Part B is for
   the maintainer to decide.
   *Note, 2026-11-11 (journal 0283):* decided: Part B stays outside criterion (2), as
   information.

### 3. Tests cited by journals and not found in the tree

1. `FCNTL_FIX_VM_PASS` (journal 0149): not in the tree; it was the marker of the one-off guest.
   The test in the tree is `tests/fcntl-flags.c`, which prints `FCNTL_FLAGS_PASS`.
2. `REVIEW_FCNTL_DONE` (journal 0144): marker of the review's one-off guest, not in the tree.
3. `tests/review-build-permissions.c` (journal 0151): renamed `tests/chmodrace.c` (journal 0152),
   found under the new name.
4. The negative-round branches (`neg/...`) cited by journals 0131, 0149, 0150, 0152, 0156, 0170,
   0172, 0174, 0175 are evidence, not tests in the tree. 24 `neg/` branches exist in this clone
   (17 local, 7 remote); whether every cited one exists was not checked.

Every other test named in the tables above was found by name in `vm/`, `tests/`, `python/tests/`,
`scripts/` or `layer/`.
