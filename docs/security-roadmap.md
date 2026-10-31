# Security and robustness roadmap for the layer

Status: **proposal** (2026-10-10). No item below marked "to do" has been implemented.
*Note, 2026-10-29 (journal 0234):* that status line is the one of 2026-10-10. Most items have
progressed or closed since, each with dated notes below; "Where each item stands" gives the
current state in one place.
Origin: the survey recorded in [docs/history/0017](history/0017-macos-and-security-posture.md),
which compares MoobFS with the kernel documentation for new filesystems, with the filesystem
fuzzing literature (Janus, Hydra, LFUZZ) and with the CVE history of overlayfs, eCryptfs,
ntfs3, JFS, ext4 and OpenZFS. This document is the list of techniques to implement, with a
suggested order and a done-when criterion. Design decisions go to ADRs; execution evidence
goes to `docs/history/`.

Principle: a module runs in ring 0, so a bug in it is a bug in the whole kernel. No technique
eliminates that. The goal is to reduce probability and impact with input validated at the
edge, detectors in CI, continuous fuzzing, a minimal surface and a written threat model. A
crash triggerable by an ordinary user is a vulnerability (denial of service), even without an
exploit, as the XFS and ext4 maintainers treat it.

## Where each item stands (2026-10-31)

<!-- current-state -->
State on `main` at `7ac52cd`, with the rows of batch 10: S1 (journal 0240), S2 and S3 (journal 0239), S5 and S14 to S21 (journal 0242), S9 (journal 0243); summarised from the dated notes of each item below.

| Item | State |
| --- | --- |
| S1 threat model | Done ([ADR-0011](decisions/0011-threat-and-credential-model.md)). Findings F1 to F17 are fixed on `main` (journal 0228), with the reused-inode records of GHSA-756j-cgwq-gvwm (journal 0216; its limits are in [when-not-to-use.md](when-not-to-use.md) §3). An external reviewer's AI-assisted private reports found kernel defects, all fixed with regression tests; there is no independent human review of the kernel code. The hand-read review of the credential paths and the `QUERY2` authorization is a stage-3 gate, not a stage-2 one ([ADR-0030](decisions/0030-stage-2-review-criterion.md)). |
| S2 fstests | `quick` group done on ext4, btrfs and XFS with a justified exclusion list (journal 0091). The `auto` group ran once on the three lowers on Debian 6.12.113 (journal 0239): 1, 4 and 1 failures of 779, none a layer defect, now in exclusion lists. Open: a repeat of `auto` with those lists, and a make target. |
| S3 syzkaller | **Done** (journal 0245, ADR-0032). Seven six-hour runs, no open report; every handler function exercised, `moobl_xattr_get()` included (journal 0239). The handlers reach 688 of 781 KCOV call sites, 88.1 %, above the 85 % threshold, which counts call sites since ADR-0032; the manager's own count, which includes 28 PCs that are not call sites, was 84.9 % in the seventh run. |
| S4 hostile lower | Done for its scope (journal 0073). |
| S5 static analysis | `checkpatch.pl` clean and required in the `Lint` job. `make static` runs `W=1` (gcc and clang), `sparse`, `smatch`, Coccinelle, the clang analyzer and `kernel-doc` on both modules and the UAPI header, with six findings justified in `tests/static/baseline.txt` and none fixed (journal 0242); `.github/workflows/static.yml` runs it on every push and pull request; its first GitHub runs, on PR 179 on 2026-10-30, passed after two fixes (journal 0242); not yet a required check. |
| S6 disclosure and versions | Done: `SECURITY.md` and [compatibility.md](compatibility.md); the KASAN/lockdep job is a required check of `main`. Open: a build-time refusal of an unlisted kernel (`make install` and the DKMS package refuse one, `make layer` does not). |
| S7 continuous fuzzing | Three libFuzzer targets (attributes, the image parser, the tool parsers) run 30 s each per push, and `.github/workflows/fuzz.yml` runs them 45 min twice a week with a cached corpus (journal 0237); its first scheduled run is pending. Locally: 30–60 min per target, no crash. OSS-Fuzz is not applied for. |
| S8 runtime hardening | KCSAN over `buildrace` and `release` in CI (journal 0127), with no race in the module; the KASAN kernel adds KFENCE, `FORTIFY_SOURCE`, `HARDENED_USERCOPY`, freelist hardening, `BUG_ON_DATA_CORRUPTION` and `DEBUG_OBJECTS`, and the full suite passed under them (journal 0237); a read-only review found no defect. Open: KMSAN. |
| S9 resource limits | Done (ADR-0029, accepted 2026-10-29; journal 0235): memory-cgroup charges (ADR-0018), bounded build work and rebuild frequency (ADR-0019, ADR-0020), fair shares of the index budget (ADR-0027) and the per-call result `limit`, each tested when hit (suite step `limits`); the mount scans nothing (`mountscale`). Residual: without `memory.max` on the users' cgroups a user can pin about 2 MiB of kernel memory per open descriptor; `docs/operations.md` §5 sets `MemoryMax=` with a user-slice drop-in and `moobfs diagnose` warns when no limit applies (journal 0243, checked in a dedicated VM). |
| S10 signing and distribution | The done criterion is met in a VM (journal 0229, ADR-0028), and the unsigned `.ko` and the packages build reproducibly (`make repro-test`, journal 0236). Open: real firmware and hardware, a rebuild by a third party, no published package. |
| S11 M3 and M4 surfaces | Nothing recorded yet. |
| S12 SELinux | One run with SELinux enforcing (journal 0104): F6 and the labels read with the caller's credentials fixed; the rule `fs_use_xattr moobfs_layer` is needed. Open: mmap, attribute writes, rename and link under a policy, MLS/MCS, a distribution booted with SELinux, the per-result check over a lower labelled by SELinux (journal 0215), Smack; AppArmor is outside the item. |
| S13 tracepoints | Done (journal 0102). |
| S14 to S21 | Planned item by item below, with effort and done check (journal 0242). S14: 23 mutants of the homogeneity rule apply, build and change the object code (`make mutants-check`); on 2026-10-30 a recorded VM run of the whole catalogue killed all 23, after tests were added for the five that had survived the full suite; the weekly CI job is still to do. S19: as S5, green on GitHub since PR 179. S20: the checklist item is in `CONTRIBUTING.md`; no change has gone through the procedure. S15 to S18 and S21 not started; journal 0172 added bounded tests that do not complete S14 or S15. A stage-2 gate since ADR-0030. |
| S22 security operations | Documented (journal 0205). Open: the subscriptions, which are the maintainer's action, and the re-read of the access table before each tag. |
<!-- /current-state -->

## Current state (verified on 2026-10-10)

*Note, 2026-10-29 (journal 0234):* this table is kept as verified on 2026-10-10; "Where each
item stands" above is the current state.

| Technique | Where | Status |
| --- | --- | --- |
| ioctl input validation (`QUERY`, `GET_INFO`): limits, flags, reserved fields, fixed-size `copy_to_user` | `layer/index.c` | Implemented |
| Typed-attribute validation before touching the lower layer (type, size, name alphabet, UTF-8, `EPERM` on `user.moob.format`) | `layer/attr.c`, `common/attr.c` | Implemented, tested in the VM |
| Two-layer permission check (`inode_permission` on the lower layer plus the lower layer's own check) and mounter `override_creds` | `layer/main.c` | Implemented, model not written |
| KASAN, UBSAN and lockdep on a custom kernel; failures in `BUG`, `Oops`, `WARNING`, `KASAN`, `UBSAN`, lockdep in `dmesg` | `vm/kasan-guest.sh`, `.github/workflows/kasan.yml` | Implemented, on demand |
| ASan and UBSan on the shared `common/` code | `.github/workflows/ci.yml` | Implemented, on every push |
| libFuzzer over the attribute encoding | `tests/fuzz_attr.c` | Implemented, 30 s per push |
| Index memory budget (`index_budget`, `index=off`) | `layer/index.c` | Implemented |
| Module loaded only inside a disposable QEMU VM | `vm/` | Implemented |
| xfstests `fsx`, `dm-log-writes`, `dm-flakey` | `vm/suite.sh` | Implemented |
| `-Wall -Wextra -Werror -Wconversion -Wshadow` on the tools | `Makefile` | Implemented |

## Techniques to implement

Suggested order by cost-benefit. Each item has a verifiable done-when criterion.

### S1. Threat and credentials model (ADR)

*Status, 2026-10-12 (journal 0060).* Done: [ADR-0011](decisions/0011-threat-and-credential-model.md),
with findings F1 to F5 added since. Outside review was requested on 2026-10-13 (journal 0084).
*Update, 2026-10-20 (journal 0156):* findings F13 (review round 1, journal 0151) and F14 (round 2, journal 0155) recorded and fixed; the other defects of round 2 are fixed or recorded as issues, and S14 to S21 below plan the tests that would have found them.
*Update, 2026-10-28 (journal 0222):* finding F17 found by the maintainer's team and fixed: a minimal access ACL written outside the VFS could refuse a caller the index's rule admitted by the bits ([GHSA-82w5-ph62-47wf](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-82w5-ph62-47wf)).
*Update, 2026-10-28 (journals 0221, 0222, 0223 and 0224):* of the round-2 issues, 119 (M4, M6 to M8: lookup errnos, the reserved byte of a query term, one privilege test, ADR-0026) and 115 (V1, V3, V5, V7: `fadvise`, `readahead`, `statx` fields, `drop_inode`, `O_NOATIME`) are fixed; 117 (M2) is fixed for inodes without an index record and stays open for inodes with one. `cachestat(2)` stays a limitation.
*Update, 2026-10-28 (journal 0228):* F2's fair shares implemented ([ADR-0027](decisions/0027-fair-shares-of-the-index-budget.md)): a uid above its share of the index budget lends its room back to a uid below its own, and a uid within its share keeps its domains. Findings F1 to F17 are all fixed on `main`.
*Update, 2026-10-28 (journal 0227):* outside ADR-0011's kernel scope, two private reports from the same external reviewer, with their fixes, on the user-space clients: the MCP adapter followed a directory swapped for a symbolic link by a member of a shared domain ([GHSA-4gr9-rvgr-44qj](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-4gr9-rvgr-44qj)), and the LangGraph store's `batch()` let `..` labels leave the root ([GHSA-3wx4-fh9w-2v66](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-3wx4-fh9w-2v66)). Both fixed, with tests that fail before the fix.
*Update, 2026-10-29 (journal 0231):* the same class as GHSA-4gr9-rvgr-44qj, pointed out by the external reviewer in the Python library's scan fallback: it walked the domain by path, so a member of a shared domain who swapped a directory or a file for a symbolic link during the walk could make it report a file outside the domain. The walk now goes through directory descriptors with `O_NOFOLLOW`; three tests fail before the fix.
*Update, 2026-10-28 (journals 0215, 0216, 0218 and 0219):* three private reports from an external reviewer recorded and fixed through private advisories: F15 (search-only directories, GHSA-95h9-j7c2-gcw6, ADR-0025), F16 (`CAP_FSETID` in the write credentials, GHSA-6xf4-r246-92r6) and records answered for a reused inode number (GHSA-756j-cgwq-gvwm, no finding number). Findings F1 to F16 recorded; all fixed but part of F2 (fair shares, S9).
*Correction, 2026-10-29 (journal 0240, ADR-0030):* the external reviewer's private reports (F15, F16, GHSA-756j-cgwq-gvwm, and in user space GHSA-4gr9-rvgr-44qj, GHSA-3wx4-fh9w-2v66 and the pointer behind GHSA-f769-xjf5-54wc) were produced with AI assistance: the reviewer chose where to look, reproduced the defects and wrote or checked the fixes, but the analysis was AI-assisted. They are AI-assisted reviews, like those of journals 0111 and 0114, not independent human review, of which there has been none; the defects they found are real and each is fixed with a regression test.

*Update, 2026-10-21 (journals 0174 and 0175):* the round-2 domain-rename
(R2-5, issue 110) and concurrent index-budget refusal (R2-7, issue 111)
defects were reproduced on an unfixed module, corrected, and validated in the
complete native VM suite over ext4, btrfs and XFS. The corrected kernel/layer
code passed KASAN/UBSAN/lockdep on the dedicated host. The combined
[PR 126](https://github.com/studiomoob/llmoobfs/pull/126) was open at that
checkpoint; published-head CI, review and merge were separate gates. Neither correction provides S9 fair
budget sharing or completes the broader S14–S21 work below.

*Status correction, 2026-10-22 (journal 0177):* the preceding update describes
the pre-merge state. All 11 reported checks on PR 126 passed; it was merged
on 2026-10-21, and issues 110 and 111 were closed. S9 fair sharing and the
S14–S21 test plan remain separate work.

*Local follow-up, 2026-10-22 (journal 0179):* a direct native KVM run exposed
a repeatable remaining budget refusal under concurrent builds. The local
correction skips a busy release candidate and tries the next eligible domain.
Its negative control failed on ext4; targeted KVM, complete Docker VM, native
KVM and KASAN/UBSAN/lockdep runs passed on the corrected module. PR CI and
integration remain pending; this does not provide S9 fair sharing.

*Status correction, 2026-10-23 (journals 0179 and 0181):* PR 127 merged
that correction after eleven green checks. The preceding paragraph is the
pre-merge checkpoint. S9 fair sharing remains open.

*Status, 2026-10-18:* findings F1 to F12 recorded; all fixed but part of F2 (fair shares, S9; its silent partial answers fixed in journal 0130); F4 completed in journal 0132. Two source
reviews by AI assistants (journals 0111, 0114) found F9 to F12, fixed in pull request #80 with
private advisories published afterwards; no outside human review yet. *Note, 2026-10-28:* an
external reviewer has since reported three kernel defects privately (update of that date above);
there is still no independent human review of the whole kernel code.
*Correction, 2026-10-29 (journal 0240, ADR-0030):* the external reviewer's private reports (F15, F16, GHSA-756j-cgwq-gvwm, and in user space GHSA-4gr9-rvgr-44qj, GHSA-3wx4-fh9w-2v66 and the pointer behind GHSA-f769-xjf5-54wc) were produced with AI assistance: the reviewer chose where to look, reproduced the defects and wrote or checked the fixes, but the analysis was AI-assisted. They are AI-assisted reviews, like those of journals 0111 and 0114, not independent human review, of which there has been none; the defects they found are real and each is fixed with a regression test.

*Separate stabilization note, 2026-10-20:* independent review 0144 identified
two file/index correctness defects outside the F1–F12 list. Corrections
([0149](history/0149-sync-backing-file-flags.md),
[0150](history/0150-propagate-index-xattr-read-errors.md)) passed the integrated
full VM suite and local KASAN/UBSAN/lockdep gate at `49098aa`.
Pull-request CI is still a separate pre-merge gate. The S9 fair-share
gap and outside-human-review gap remain unchanged. A further review ([0151](history/0151-review-d110d4e.md))
found a permission defect: a chmod during the first build of a domain left the file in `QUERY2`
answers to users who can no longer read it. Fixed in [0152](history/0152-chmod-during-build-fixed.md)
through a private advisory.

> Note from 2026-10-11 (entry 0030), measured on the Azure host: `QUERY` time grows by about
> 1.15 µs per entry from another owner that matches the query (18 µs with none, 28 ms with
> 20 000), with variation below 3%. An agent can estimate how many memories belonging to
> others have a given attribute value and test whether a value exists anywhere. The S1 ADR
> must decide the model; the structural mitigation is not to traverse entries from other
> domains (ADR-0009).

> Note from 2026-10-11 (entry 0028): the per-result authorization of `QUERY` covers DAC, ACLs
> and inode-based security modules, but does not consult those that decide by path at open
> time (AppArmor, Landlock). A process confined by them may see, in the results, a file it
> would not be able to open. The S1 ADR must decide whether this enters the contract.
>
> Note from 2026-10-28 (journal 0220): measured again with `QUERY2` on `main` (Debian 6.12.111,
> Landlock ABI 6): the query returns the file, `getxattr` of it works, opening it fails with
> `EACCES`. Landlock documents that it does not restrict xattr reads yet, and a metadata access
> right is in progress upstream (landlock-lsm/linux issue 11), so the query reveals what
> `getxattr` already does. When that right lands, the layer should honor it. AppArmor remains
> untested.

Precedent: the `override_creds=off` discussion in overlayfs showed that, without a written
model, reviewers cannot tell whether the credentials scheme is exploitable.

- Write an ADR with: who the adversary is (an unprivileged local user with access to the mount
  point; a process writing to the lower layer from outside; the on-disk contents of the lower
  layer), what the layer promises (not to crash the kernel, not to escalate privileges, not to
  expose data the lower layer would not expose) and what it does not promise (confidentiality
  beyond the lower layer, integrity against modification of the lower layer while mounted).
- State explicitly that `FS_USERNS_MOUNT` stays off and that mounting requires
  `CAP_SYS_ADMIN` in the initial namespace. Precedent: CVE-2015-1328, CVE-2016-1576,
  CVE-2020-16120 and CVE-2026-74619 in overlayfs, all through the enabled flag; distributions
  that keep it off avoid the entire class.
- List the unprivileged surfaces: `setxattr`/`removexattr` on `user.moob.*`,
  `QUERY`, `GET_INFO`, `FICLONE*`, `FIDEDUPERANGE`, `FIEMAP`, `open_by_handle_at` with
  handles returned by `QUERY`.
- Done when: ADR published, the module's `fs_flags` reviewed against it, and `moobfs-check`
  reports the model version.
- *Note, 2026-10-29 ([ADR-0030](decisions/0030-stage-2-review-criterion.md), journal 0240):* that
  criterion is met (ADR-0011). The independent human review of the whole kernel code that
  `ROADMAP.md` carried as S1's remaining work is no longer a stage-2 gate: stage 2 rests on
  automated coverage, on every AI-assisted finding reproduced with a regression test and on S14
  to S21; stage 3 needs a hand-read human review of the credential paths and the `QUERY2`
  authorization.

### S2. fstests (xfstests) over the layer

*Status, 2026-10-13 (journals 0075, 0077, 0079).* fstests runs over the layer through a small
port (`vm/fstests/`). The `quick` group is recorded over ext4, btrfs and XFS; every failure is
classified; the file-attribute gaps are closed; the exclusion list is written
(`vm/fstests/exclude-quick.txt`). The complete run with that list passes on the three lowers
(journal 0091), which meets "done when" for the `quick` group. Open: the `auto` group.
*Update, 2026-10-30 (journal 0239):* the `auto` group ran once over ext4, btrfs and XFS on Debian
6.12.113. Of 779 tests per lower, 229, 339 and 372 passed, 549, 436 and 406 were not run, and 1,
4 and 1 failed, with no kernel diagnostic. generic/631 does not apply on any lower: overlayfs
refuses the layer as its upper. generic/186, 187 and 301 fail only over btrfs: fstests skips
them, or the failing check, by `FSTYP`, which the layer hides. A baseline on plain btrfs
confirmed it, and they pass over the layer on XFS. None is a layer defect. They are listed in
`vm/fstests/exclude-auto.txt` and `exclude-auto-btrfs.txt`. Open: a repeat of `auto` with the
lists, and a target for it.

Precedent: the kernel documentation "Adding New Filesystems" calls fstests standard
infrastructure and its use strongly recommended; bcachefs reported fstests under lockdep and
KASAN before the merge.

- Add an fstests configuration for `moobfs_layer` with ext4, btrfs and XFS lower layers, in
  the line of what overlayfs does (`FSTYP=overlay` with `OVL_BASE_*`).
- Run the `quick` group in the VM; record pass, fail and not-run per lower layer.
- Done when: an exclusion list justified per test and a reproducible result in
  `make vm-test` or a dedicated target.

### S3. syzkaller over the ioctls and setxattr

Precedent: Janus found 90 bugs in eight kernel filesystems with 32 CVEs; Hydra found 155 more
by fuzzing image and syscalls together. The current libFuzzer covers only the encoding parser.

- Write syzlang descriptions (`sys/linux/moobfs.txt` in a local syzkaller checkout) for
  `openat$moobfs_root`, `ioctl$MOOB_QUERY`, `ioctl$MOOB_GET_INFO`, `setxattr$moob`,
  `removexattr$moob`, with typed structs and flags; constants via `.const` or `syz-extract`.
- Run `syz-manager` against the VM with the existing KASAN and lockdep kernel; collect
  coverage of `layer/`.
- Done when: coverage of the ioctl and xattr handlers above a defined threshold, zero open
  reports, and reproducers of previous crashes kept in `tests/`.
- **Status, 2026-10-14 (journal 0109):** set up (`vm/syzkaller/`, `vm/syzkaller-local.sh`); the
  descriptions now also cover the mount and its options and `QUERY2`/`QUERY2_BATCH`, and each
  program mounts the layer itself. A 30-minute run: 78 055 executions, the corpus reaching every
  ioctl, no report. Two six-hour runs followed: the first found finding F8 (a failed mount pinned the lower root,
  journal 0112); the second, with the fix, 567 085 executions and no report. Coverage of `layer/`:
  90 % of PCs in `attr.c`, 68 % in `index.c`, 65 % in `main.c`; the release paths are not reached.
  A third six-hour run on 2026-10-18, on `main` at bf1a34b, before F2 (journal 0133): 720 747 executions, no
  report; its per-function coverage was not collected, so the 78 % below is still the last measure.
  A fourth, on 2026-10-19 with F2 and F4 (journal 0138): 625 124 executions, no report; the
  handlers at 78.6 % of their PCs; two harness gaps fixed (attribute calls on a layer file,
  coverage fetched in time).
  A fifth, on 2026-10-19/20 with richer descriptions (journal 0143): 540 919 executions, no
  report; the handlers at **88.7 %** of their PCs, above the threshold below.
- **Clarification, 2026-10-20 (journal 0146):** the fifth run reaches the aggregate PC target,
  but `moobl_xattr_get()` still has 0 of 4 PCs, without an established explanation. The separate
  requirement below to exercise every reachable function is therefore not yet demonstrated.
  Keep this evidence gap distinct from the maintainer's stabilization milestone.
- **Update, 2026-10-20 (journal 0154):** the 0 of 4 is explained by the corpus, not by the
  function. Of the fifth run's 1 082 programs, 40 read an attribute and 14 of those mount the
  layer, and in most of them the read cannot reach it. A replay of the seeds in a disposable
  guest, with per-call KCOV coverage, gives 3 of the 4 PCs to every read that reaches the layer.
  The fourth is the `security.*` branch, which the LSM answers itself for `security.selinux`.
  Three new seeds end in such a read. The per-function criterion is demonstrated when the next
  run's report shows the function covered.
- **Update, 2026-10-30 (journal 0239):** a sixth six-hour run, with 2 VMs on the KASAN kernel that
  now carries the S8 hardening options, and a seed reading `security.capability`. Result:
  386 844 executions and no report. `moobl_xattr_get()` is covered in the report (3 of 18 PCs).
  A replay of the read seeds reaches all four of its KCOV call sites, the mounter-credentials
  branch included. Every function of the handler set is now exercised. The handlers stand at
  684 of 809 PCs, **84.5 %**, below the 85 % threshold, on a denominator that grew with the
  layer's fixes and the hardened kernel (743 in the fifth run). The per-function criterion is
  met; on the current code the aggregate is not.
- **Update, 2026-10-31 (journal 0239):** a seventh six-hour run, with the same threshold and
  counting. It started from the sixth run's corpus plus three seeds written after a per-PC replay
  of that corpus. Result: 301 716 executions, no report, the handlers at 687 of 809 PCs,
  **84.9 %**, one PC below 85 %. 28 of the 809 PCs, in the two xattr handlers, are not coverage
  call sites and cannot be covered.
- **Update, 2026-10-31 (journal 0245, ADR-0032):** the threshold counts the handlers' KCOV call
  sites (`__sanitizer_cov_trace_pc` calls in `.text`, listed with `objdump -dr`), covered as a
  per-call KCOV replay of the run's corpus reports them; the 85 % value and the other conditions
  stay. On that count the handlers reach 688 of 781, **88.1 %**: S3 is done.
- **Threshold, set on 2026-10-15** (journal 0109): the ioctl and attribute handlers (from
  `moobl_root_ioctl()` through `query_run()`, `results_to_user()`, `moobl_xattr_get/set()` and the
  validation in `common/attr.c`) at 85 % of their PCs or more, measured by the manager's
  per-function coverage (78 % after the second run, the rest mostly error paths and the `QUERY2`
  cursor); every function a program can reach exercised at least once; each function left out
  listed with its reason; no open report; reproducers of past crashes kept in `tests/`. Not 100 %:
  some code is out of a program's reach (domain release and the shrinker need memory pressure or a
  budget of at least 1 MiB to overflow, about 1 700 files), and inlined functions show no coverage
  of their own. Those paths are left to the suite's stress steps and to KCSAN (S8).

### S4. The lower layer as hostile input

*Status, 2026-10-13 (journal 0073).* Done for the scope below: `tests/hostile.c` generates the
adversarial trees, `vm/suite.sh` mounts the layer over them on ext4, btrfs and XFS
(`HOSTILE_LOWER_PASS`, also under KASAN), and `make hostile-test` runs them through the tools
under sanitizers in CI. The behaviour is recorded in ADR-0011. The choice made: an invalid
attribute is skipped silently and reported by `moobfs-check`, not logged in `dmesg`, because a
writer on the lower could otherwise fill the log.

Precedent: CVE-2019-19319 (ext4, `setxattr` after a forged image writes out of bounds),
CVE-2009-2406 (eCryptfs, overflow in the metadata parser), the maintainers' position that the
kernel promises no security when mounting a hostile image.

- Treat every xattr read in the mount-time scan and in `getxattr` as untrusted: the layer
  cannot trust that `user.moob.*` on the lower layer went through its own validation, because
  it may have been written from outside.
- A fuzz target that generates adversarial lower directories (names, sizes, types, truncated
  payloads, counts above 64, invalid `user.moob.format`) and mounts the layer over them in
  the VM; a userspace version with `moobfs-index` under ASan as the first line.
- Define the behavior: an invalid attribute on the lower layer is ignored in the index and
  reported in `dmesg` with rate limiting, or the mount fails; record the choice in the S1 ADR.
- Done when: fuzzing without a crash for a defined time and the behavior documented and
  tested.

### S5. Kernel lint and static analysis in CI

*Status, 2026-10-12 (journal 0050).* `checkpatch.pl` reports nothing on `layer/` and `kernel/`,
with no suppressions, and is required in the `Lint` job. `sparse`, `smatch`, `W=1` and
`kernel-doc` are still pending in CI.
*Update, 2026-10-29 (journal 0242):* `make static` runs `W=1` with gcc and with clang, `sparse`,
`smatch`, Coccinelle (the kernel's `coccicheck`), the clang analyzer (`clang-analyzer`) and
`kernel-doc` on `layer/`, `kernel/` and `include/`, in a pinned image (`tests/static/Dockerfile`),
and `.github/workflows/static.yml` runs the same command. `sparse`, `smatch`, `W=1` and
`kernel-doc` report nothing on either module; the clang analyzer reports four findings and
Coccinelle two, all in `layer/index.c`, each read and justified as not a defect in
`tests/static/baseline.txt`. A canary module with one known defect per checker fails the run
when a checker stops reporting. Run on arm64 locally; on GitHub (amd64) since PR 179, 2026-10-30, after two fixes (journal 0242).

- `checkpatch.pl`, `sparse` (`make C=1`) and `smatch` over `layer/`; `W=1` in the module
  build.
- `scripts/kernel-doc` for the UAPI in `include/moobfs_ioctl.h`.
- Done when: zero warnings or a list of justified suppressions.

### S6. Disclosure process and versioning policy

*Status, 2026-10-13 (journals 0080, 0084).* `SECURITY.md` is published. The policy of supported
kernels and of compatibility is in `docs/compatibility.md`, linked from the README and accepted
by the maintainer. `MOOB_ONLY` exists and the KASAN workflow can be a required check. Follow-ups:
turning that rule on, and the build-time check of an unlisted kernel.

- `SECURITY.md` with a contact channel, response deadline and what counts as a vulnerability
  (including DoS by an ordinary user).
- Policy for supported kernels and UAPI compatibility, already foreseen in the M1 hardening.
- Done when: file published and linked from the README.

### S7. Continuous and larger fuzzing

*Status, 2026-10-29 (journal 0237); first run 2026-10-29 (journal 0242).* Workflow written; its first run on GitHub (run 40953229237) passed for the three targets: `fuzz.yml` runs
each libFuzzer target 45 minutes (on demand 1 to 300) twice a week, keeps each corpus in the
Actions cache from run to run and uploads it with any crash as an artifact; the job summary
carries the exec count and coverage for the journal. Two targets were added, `image` (the image
parser `common/format.c` that the M1 module and `fsck.moobfs` share) and `tools` (query terms
and ACL values of the tools); `make fuzz` runs all three. Locally on the test host, with no
crash: `attr` 258 million executions in 30 minutes, `image` 1.6 million in 60 minutes (every
line of `common/format.c` reached), `tools` 66 million in 30 minutes. Janus, Hydra and LFUZZ
were weighed and not adopted (journal 0237): the layer has no image of its own, and syzkaller
(S3) and the hostile trees (S4) cover the system calls and the lower's attributes. Still open:
the first scheduled run and its report in `docs/history/`.

- Raise the libFuzzer `FUZZ_TIME` in a scheduled workflow; keep the corpus in an artifact.
- Evaluate Janus/Hydra or LFUZZ for image-plus-syscall fuzzing over the lower layer mounted
  through the layer; record feasibility.
- Done when: a scheduled workflow exists and a per-run report goes to `docs/history/`.

### S8. Runtime hardening

- Enable and test with `CONFIG_FORTIFY_SOURCE`, `CONFIG_SLAB_FREELIST_HARDENED`,
  `CONFIG_INIT_ON_ALLOC_DEFAULT_ON`, KFENCE and KCSAN in the test kernel; fix whatever shows
  up.
- `WARN_ON_ONCE` instead of `WARN_ON` on user-triggerable paths (precedent: CVE-2026-74619,
  overlayfs, `WARN_ON` in a loop crashes the kernel with `panic_on_warn`).
- Rate limiting on every user-triggerable `dmesg` message (`pr_warn_ratelimited`).
  *Note, 2026-10-28 (journal 0221):* stricter for the three a caller can repeat at will (a
  casefolded directory refused by lookup, a failed index update, a failed domain build): printed
  once per module load (`pr_info_once`, `pr_warn_once`); the errno and the index tracepoints
  report each occurrence.
- Done when: the suite passes with the options enabled and the `WARN_ON`/`pr_*` review is
  complete.
- *Progress, 2026-10-15 (journal 0115):* KCSAN over the stress steps `buildrace` and `release`,
  no race in the module; the rest of the suite and the other options are still to run.
- *Progress, 2026-10-18 (journal 0127):* the KCSAN run moves to CI (`.github/workflows/kcsan.yml`,
  on demand and weekly, a runner with KVM), with the report filter of journal 0115; it no longer
  needs the benchmark host.
- *Progress, 2026-10-29 (journal 0237):* `vm/kasan.config` adds `FORTIFY_SOURCE`,
  `HARDENED_USERCOPY`, `SLAB_FREELIST_HARDENED`, `SLAB_FREELIST_RANDOM`, `BUG_ON_DATA_CORRUPTION`,
  KFENCE (100 ms), `SCHED_STACK_END_CHECK` and `DEBUG_OBJECTS` (`_FREE`, `_RCU_HEAD`), so the
  KASAN suite builds and runs the module under them; the full suite passed locally with them
  (`MOOBFS_KASAN_PASS`, KFENCE initialized, no report).
  `INIT_ON_ALLOC_DEFAULT_ON` stays off in the KASAN kernel (zeroing would hide uninitialized
  state from the detectors); the Debian 6.12.113 job already runs the whole suite on a kernel
  with `init_on_alloc=1`, `FORTIFY_SOURCE`, `HARDENED_USERCOPY`, `SLAB_FREELIST_HARDENED` and
  `BUG_ON_DATA_CORRUPTION` (KFENCE built but off: sample interval 0). The `WARN_ON`/`pr_*`
  review is complete: the module has only `WARN_ON_ONCE`, the messages a caller can repeat are
  `_once` (journal 0221), and the one plain `pr_info` is printed at mount, which needs
  `CAP_SYS_ADMIN` in the initial namespace (no `FS_USERNS_MOUNT`). Every `copy_to_user` is of
  a fixed size or of a count bounded by its buffer; `refcount_t` is the only reference count.
  The `KASAN and lockdep` workflow runs the new fragment on the batch's pull request (it touches
  `vm/`); arm64 has not run it. Still open under S8: KMSAN.

### S9. Resource limits and denial of service

*Remaining limits, 2026-10-29 ([ADR-0029](decisions/0029-s9-limits-met-by-existing-bounds.md),
proposed; journal 0235):* both bullets at the end of this section are met by bounds already in
the code, now tested when hit and measured. The cap on `QUERY` results is the per-call `limit`
(at most 4,096, the cursor beyond it); the per-user cap on query and index memory is the memory
cgroup (ADR-0018) beside `index_budget` and the shares (ADR-0027). The new suite step `limits`
(full suite) hits each cap: a domain larger than the whole budget, retained `QUERY2` buffers in
a 16 MiB cgroup and a build in a 4 MiB cgroup are refused with `ENOMEM`, nothing answered or
left built, another uid answered meanwhile. The mount scans nothing: the optional step
`mountscale` measured the mount at 0.4 to 1.5 ms cold and 76 to 86 µs warm (medians) from 100 to
200,100 files, with and without `noindex`, with no index memory; the first query of a domain
over `index_build_work` was refused with `E2BIG` in about 0.5 s at 50,100 and 200,100 files.
S9 is done: the maintainer accepted ADR-0029 on 2026-10-29. Residual risk: without a `memory.max` on the
users' cgroups, an unprivileged user can pin about 2 MiB of kernel memory per open descriptor.

*Fair shares, 2026-10-28 ([ADR-0027](decisions/0027-fair-shares-of-the-index-budget.md),
journal 0228):* each domain counts its own bytes; a build that does not fit releases the
builder's own domains first and then, while the builder holds less than its share (the budget
divided by the uids with a built domain), the least recently queried domain of a uid holding
more. The suite step `fairshare` checks both halves on ext4 and failed on the module before
the change; `buildcostevict` follows the amended property. The G2 latency ceiling (protocol
section 14) is measured in the same journal. The two bullets left at that date are answered by
the paragraph above (journal 0235).

*Local measurement, 2026-10-23 (journal 0182, issue 118):* bounded native-KVM
probes measured first-build CPU and lower-device reads on 1,000- and
7,500-file ext4 domains, repeated CPU work after seven directory renames,
and budget eviction between private domains queried by two unprivileged
uids. This is evidence of the existing cost, not a new CPU/I/O limit or a
decision to accept the risk. The maintainer chose to design work and rebuild
frequency limits ([ADR-0019](decisions/0019-bound-query-build-work-and-frequency.md)).
An initial fixed policy is merged in `main` by PR 135. The complete native-KVM and
KASAN suites, negative control, cross-filesystem denial and a 30-pair ext4
performance comparison passed or completed as recorded in journal 0182; the
comparison detected a +0.9% cold-query wall-median cost in that shape.
All eleven PR checks passed on head `94dc454`, including Native Linux VM and
KASAN/lockdep. Issue 118 remains open for numerical-policy review and
broader workloads; a valid 17,000-file domain is denied. This is not a
strict CPU or physical-I/O cap and does not solve fair index-memory shares.

*Integration update, 2026-10-25 (ADR-0020; journal 0193):* PR 138 merged
the bounded `index_build_work` option after twelve green checks. The
32,768-unit default still denies the measured 17,000-file domain, while
an administrative 65,536-unit choice completed it in a dedicated VM.
The earlier paragraph records the fixed-policy checkpoint. Issue 118
remains open for real slow lower-device and large single-project evidence;
the option does not strictly cap CPU time or physical I/O or solve fair
index-memory shares.

*Baseline, 2026-10-24 (journal 0191):* on the module of that day, ext4,
`index_budget=1M` and 2 000 files per domain, a second domain's build
released the first. The same ping-pong happened between uid 1001 and uid
1002 and between two domains of uid 1001. Searches still returned every
file, because the released domain was rebuilt from the lower tree. One
domain stayed resident. This records F2 on that module. It does not choose
a share algorithm and it does not pass G2.

*Host regression, 2026-10-25 (journal 0192, ADR-0021):* a budget release
now drops only a domain last built by the same filesystem uid. On the same
shape, uid 1002's search returned `ENOMEM`, uid 1001's domain stayed
built, and uid 1001's next search did not rebuild. Two domains of one uid
still released one another. There is no reserved fraction. The shrinker
can still drop any idle domain. The opt-in native-KVM step then passed
on the dedicated host and failed on the pre-filter module. The steps
`release`, `overbudget` and `buildcostevict` then passed under KASAN,
UBSAN and lockdep. The complete KASAN, UBSAN and lockdep suite of this
change then exited 0 (`MOOBFS_KASAN_PASS`). The complete native-KVM suite
of this change was not run. G2 remained open then; it closed on 2026-10-30 (journal 0243).

- Issue 116 / ADR-0018 accounts user-triggered query and index allocations to
  the allocating task's memory cgroup; journal 0180 records negative controls,
  full native-KVM and integrated KASAN/UBSAN/lockdep passes. The local branch
  was first published as PR 128; its DCO sign-off gate failed. The replacement,
  [PR 129](https://github.com/studiomoob/llmoobfs/pull/129), passed all eleven
  reported checks and merged on 2026-10-23 as `40645e2`; issue 116 closed.
  This addresses missing memcg charges, not the fair sharing below. No
  controlled before/after performance comparison has measured its overhead.

- A per-mount and per-user cap on `QUERY` results and on index memory, already partly covered
  by `index_budget`; test the behavior when the cap is hit.
- Cost of the mount-time scan with a large lower layer; `index=off` and a lazy index option
  as the escape hatch.
  *Note, 2026-10-29 (journal 0234):* the lazy index exists since journal 0098 (ADR-0012):
  nothing is scanned at mount, and the scan cost is paid by the first query on each domain, which
  ADR-0019 and ADR-0020 bound.
- Done when: limit tests in the suite and the numbers recorded.
- *Progress, 2026-10-29 (journal 0235):* both bullets above tested and measured (`limits`,
  `mountscale`); no new mechanism or option; done with ADR-0029, accepted on 2026-10-29.

### S10. Module signing and distribution

- Module signing and Secure Boot compatibility (lockdown); DKMS packaging.
- Reproducible build of the `.ko` with the hash recorded.
- Done when: a signed module loads in a VM with lockdown active.

*Status, 2026-10-29 ([journal 0229](history/0229-dkms-and-debian-packages.md), [ADR-0028](decisions/0028-dkms-and-debian-packages.md)).*
The done criterion is met in a VM: under UEFI Secure Boot with lockdown `integrity`, the module
built and signed by DKMS from `moobfs-dkms` was refused before its key was enrolled and loaded
after; an unsigned build of the same sources was refused (`make package-test`). Signing uses the
host's own DKMS key, not a project key.

*Update, 2026-10-29 ([journal 0236](history/0236-reproducible-module-build.md)).* The second item
is done: `make repro-test` builds the unsigned `.ko` twice for each listed kernel, in two clean
containers that differ in build path, user, host name, time zone, umask and time, and the
results are byte-identical; the hashes for the commit are recorded in journal 0236. The build
path reached the module through `__FILE__` and the debug information, and is now mapped away by
`-ffile-prefix-map` in `make layer` and in the DKMS `MAKE` line, so a DKMS build gives the same
unsigned module. The two `.deb` packages are reproducible too. Open: real firmware and
hardware, MokManager's enrolment screen itself (the test writes the key into the firmware's
`MokList`), and a rebuild by someone outside the project.

### S11. M3 and M4 surfaces

To be recorded when they arrive: `open_by_handle_at` handles returned by `QUERY` (they
require `CAP_DAC_READ_SEARCH` in the caller; verify the layer does not bypass that);
revisions and snapshots (data leakage across revisions; garbage collection as a DoS surface);
LLM clients (content policies live in the client, not in the layer).

### S12. Mandatory access control: one run under SELinux (added 2026-10-13)

Accepted by the maintainer on 2026-10-13 as a bounded investigation, after the btrfs subvolume
test is replaced (decision in journal 0097).

- The module has never been loaded with SELinux enforcing, the default on Fedora, RHEL and
  Android. The layer acts on the lower filesystem with the mounter's credentials, and SELinux
  decides by the label of the process and of the file. Two outcomes are possible and unknown:
  the module does not work there (denials), or a confined process reaches through the layer a
  file that the policy would deny it directly. The second would be a security defect and
  belongs to the threat model (ADR-0011).
  *Note, 2026-10-29 (journal 0234):* that sentence describes 2026-10-13, when the item was added.
  Since journal 0104 (2026-10-14) the module has been loaded and tested with SELinux enforcing,
  in a Debian 6.12.111 guest with the Debian reference policy (status line of this item).
- One run of the suite on a distribution with SELinux enforcing, plus a test with a confined
  process and a file it may not read. Result in a journal.
- Before designing anything: read how overlayfs handles labels and what the SELinux test suite
  and fstests do for stacked filesystems, and cite them. Not verified yet.
- Done when: the result is recorded and one of two things is true: the defects found are
  fixed, or the README and `docs/when-not-to-use.md` say the layer is not supported with
  SELinux enforcing. AppArmor (Debian, Ubuntu) is not covered by this item.
- **Status, 2026-10-14 (journal 0104):** first run done, on a guest of Debian 6.12.111 with the
  Debian reference policy. Two defects found and fixed: F6 (`QUERY2` named files the policy hid)
  and labels read with the caller's credentials. The layer needs the rule
  `fs_use_xattr moobfs_layer` (`vm/selinux/moobfs_layer.cil`). Left open: mmap, attribute
  writes, rename and link under a policy, MLS/MCS, a distribution booted with SELinux (Fedora),
  Smack.

### S13. Observability: tracepoints for the index (added 2026-10-13)

Accepted by the maintainer on 2026-10-13, small, to arrive with the second half of ADR-0012.

- ADR-0012 adds behaviour nobody can see: when a domain is built, how long it took, when it is
  released, whether it is released and rebuilt in a loop. The test of the first half already
  needed two ad hoc module parameters to see whether a race had happened.
- Tracepoints for: build start and end (domain, files, time, result), release of a domain,
  the index marked incomplete. Dynamic debug (`pr_debug`) where a message helps.
- Readable by root only, like the counters of `GET_INFO` (ADR-0011, finding F1): a count
  visible to every user leaks across users. Declared not to be a stable interface.
- Done when: the tracepoints exist, a test turns them on and reads them, and the test-only
  parameters of journal 0098 are replaced by them where possible.

*Note, 2026-10-14 (journal 0102).* Done: four events (build, release, file read again, index
incomplete); the suite enables and reads them; `scan_retries` is replaced, `scan_delay_us` stays
because it changes timing.

Targeted test-gap follow-up (journal 0172): decisive search/truncation checks,
attributed-directory comparisons, 18 independent fixed encoding fixtures and
unprivileged queries across index release. These bounded scenarios do not
complete S14/S15 below. Journal 0178 adds a separate unprivileged
allocation-fault test for T9 and fixes a silent omission when a permission
lookup runs out of memory. Full local KASAN and native Docker VM suites passed;
integration remains pending.

*Integration update, 2026-10-22:* PR 127 passed all eleven GitHub checks,
including KASAN/lockdep, and merged into `main`. T9 is checked and issue 114
is closed. The paragraph above records the earlier local-validation state;
S14/S15 below remain separate coverage work.

### S14 to S21. Making the tests find what reviews found (added 2026-10-20, journal 0156)

Planned, not started. Rounds 1 and 2 of the independent review found defects no test could see.
The tests checked the end state, not the windows in between, and the same assistants wrote the
code and its tests. These items make the test suite itself find that kind of defect. ADR-0017
(accepted on 2026-10-20) refers to S14 and S15; these commitments remain planned.

- **S14. Mutation testing of the homogeneity rule.** A CI job removes, one at a time, each check
  of `reach_ok()`, `target_ok()`, `ext_acl()`, `place_dentry_above()` and the scan's
  `inner_ok`, and requires some suite marker to fail. Done when every check has a failing
  marker, as T2, T3 and T4 now do by hand.
- **S15. Randomized differential test of permissions.** Random trees and sequences of chmod,
  chown, setfacl, rename, link and unlink, from several uids, with and without a build in
  progress (`scan_delay_us`). Every `QUERY2` answer is compared with what `open(2)` and
  `getxattr(2)` say as that uid. Done when it runs in the suite with a fixed seed set and a
  longer run in the nightly job.
- **S16. A test-only consistency mode in the module.** A module parameter that, after each
  update hook and each build, compares the index with the lower filesystem for the files it
  touched, and warns on a difference. It would have caught R2-2, R2-3 and R2-8 when they
  happened. Done when the suite runs with it on and stays silent.
- **S17. Branch coverage of the suite.** gcov of `layer/` under `make vm-test`, published with
  each batch: the branches no test reaches. Done when the report is produced in CI.
- **S18. Nightly fuzzing.** syzkaller every night on the benchmark host with the current
  descriptions and seeds, its report kept. Done when a week of nightly runs is recorded.
- **S19. Static analysis.** `sparse`, `smatch`, Coccinelle and the clang analyzer in CI, the
  tools kernel maintainers run on patches. Done when they run on every pull request and their
  findings are fixed or justified.
- **S20. Writing the tests apart from the code.** For a change to the permission rule or the
  index protocol, a second model writes the test from the specification without seeing the
  implementation. Also a checklist item in `CONTRIBUTING.md`: "a value computed before a
  registration, a lock drop, a retry or a sleep, and used after it". Done when the checklist
  is in place and one change has gone through the procedure.
- **S21. A model of the loader and hook protocol.** A TLA+ model of `index_load()`'s
  registration and retry against the update hooks, checked for every interleaving. Heavier
  than the others; after S14 to S16.

#### Plan per item (2026-10-29, journal 0242)

Effort is engineering effort (S hours, M one to three days, L a week or more), not calendar
time. "Needs" says where the item can run: here (a workstation with Docker), CI (GitHub's
runners) or the test host (KVM).

| Item | Effort | Plan | Done check | Needs | State (2026-10-29) |
| --- | --- | --- | --- | --- | --- |
| S14 | M | A catalogue of mutants (`tests/mutation/homogeneity.mut`), each one check of the rule removed or weakened, with the suite steps expected to fail it; `make mutants-check` proves each applies once, builds and changes the object code (an unchanged object is an equivalent mutant, as in trivial compiler equivalence); `scripts/mutants.py run` runs each mutant's steps in the VM from a copy of `HEAD`; then a weekly CI job over the catalogue. | Every mutant killed by a suite marker in a recorded run; a test added for each survivor; the CI job green. | here (check), test host or CI with KVM (run) | 23 mutants pass `make mutants-check`; `run` killed all 23 on the test host on 2026-10-30 (journal 0242), after tests for five survivors; the CI job remains. |
| S15 | M | Extend `tests/permmatrix.c` (seeded random trees and identities, journal 0172) with seeded sequences of chmod, chown, setfacl, rename, link and unlink between `QUERY2` calls, with and without `scan_delay_us`, comparing each answer with `open(2)` and `getxattr(2)` as that uid. | A suite step with a fixed seed set; a longer run in the nightly job of S18. | VM | Not started; `permmatrix` covers static trees only. |
| S16 | M | A test-only module parameter that, after each update hook and build, re-reads the touched files from the lower and warns on a difference. A kernel change: it waits for a decision on ADR-0022's freeze. | The suite runs with it on and stays silent; a mutant of S14 makes it warn. | VM, a maintainer decision | Not started (frozen kernel). |
| S17 | M | A gcov build of the upstream test kernel (`CONFIG_GCOV_KERNEL`) and of the layer (`GCOV_PROFILE` set only by that job), the suite in virtme-ng, `lcov` branch report of `layer/` as an artifact. | The report is produced in CI and linked from the batch's journal. | CI with KVM | Not started. |
| S18 | S, then a week of calendar time | `vm/syzkaller-local.sh` each night on the test host with the descriptions and seeds of S3, the report kept. | Seven nightly reports recorded. | test host | Not started. |
| S19 | M | `make static` and `.github/workflows/static.yml` (see S5). | Runs on every pull request with no finding outside the justified baseline. | here, CI | Implemented; run locally (arm64) and on GitHub (amd64) since PR 179. |
| S20 | S, then one change | The checklist item in `CONTRIBUTING.md`; for the next change of the rule or the protocol, a second model writes the test from `docs/spec-query-v2.md` without the diff. | The checklist is in place and one change went through the procedure, recorded in its journal. | here | Checklist added; no change has gone through it (kernel frozen). |
| S21 | L | A TLA+ model of `index_load()`'s registration and retry against the update hooks, checked with TLC for every interleaving of a bounded configuration, TLC pinned and run in CI. | TLC finds no violation, and finds one when the retry is removed from the model (its negative control). | here, CI | Not started; after S14 to S16. |

### S22. Security operations from the OSTIF best-practices guide (added 2026-10-26, journal 0205)

Effort: S. *Status, 2026-10-26:* documented in this batch; the subscriptions of item 3 are the
maintainer's action.

The OSTIF guide (chapters 2 to 7) checked against the repository. Already in place: private
reporting, `SECURITY.md` with response targets and disclosure terms, the threat model (S1),
CodeQL, Scorecard, Dependabot, sanitizers and fuzzing. Added:

1. A safe-harbor section in `SECURITY.md` for good-faith research on the code.
2. `security.txt` (RFC 9116) under the project site. RFC 9116 places it at the domain root,
   which belongs to the hosting organization and is not changed by this project, so scanners may
   not find it; `SECURITY.md` stays authoritative. `scripts/check-site.py` fails once it expires.
3. [security-operations.md](security-operations.md): access read from the GitHub API, an
   incident procedure, the advisories that concern the project, the third-party components and
   how to export the SBOM, and when to raise the security effort.

Done when: the three are on `main` and the access table has been re-read before the next tag.

## Out of scope for this list

- LLM content guardrails: the layer is storage; control over what an agent reads or writes
  lives in the POSIX permissions of the lower layer, in `nolinks` and in the memory client.
- Encryption: delegated to the lower layer (fscrypt, dm-crypt), per ADR-0007.
- User namespace support: outside M2; if it enters, it is a new ADR with the overlayfs CVEs
  as a checklist.

## Sources

Listed with links in entry [0017](history/0017-macos-and-security-posture.md): the kernel
documentation "Adding New Filesystems"; Janus (IEEE S&P 2019), Hydra (SOSP 2019), LFUZZ
(2024); linux-fsdevel and LWN discussions on hostile images (2018, 2023, 2025); overlayfs
CVEs (2015-1328, 2016-1576, 2020-16120, 2026-74619), eCryptfs (2009-2406, 2014-9922), ntfs3
(2026-63833), JFS (2010-2946), ext4 (2019-19319), setxattr (2016-7097); OpenZFS `ztest` and
the 2026 userns fix; the syzkaller documentation on descriptions.
