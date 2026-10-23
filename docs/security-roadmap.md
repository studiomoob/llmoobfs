# Security and robustness roadmap for the layer

Status: **proposal** (2026-10-10). No item below marked "to do" has been implemented.
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

## Current state (verified on 2026-10-10)

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

*Status, 2026-10-18:* findings F1 to F12 recorded; all fixed but part of F2 (fair shares, S9; its silent partial answers fixed in journal 0130); F4 completed in journal 0132. Two source
reviews by AI assistants (journals 0111, 0114) found F9 to F12, fixed in pull request #80 with
private advisories published afterwards; no outside human review yet.

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

### S2. fstests (xfstests) over the layer

*Status, 2026-10-13 (journals 0075, 0077, 0079).* fstests runs over the layer through a small
port (`vm/fstests/`). The `quick` group is recorded over ext4, btrfs and XFS; every failure is
classified; the file-attribute gaps are closed; the exclusion list is written
(`vm/fstests/exclude-quick.txt`). The complete run with that list passes on the three lowers
(journal 0091), which meets "done when" for the `quick` group. Open: the `auto` group.

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
- Done when: the suite passes with the options enabled and the `WARN_ON`/`pr_*` review is
  complete.
- *Progress, 2026-10-15 (journal 0115):* KCSAN over the stress steps `buildrace` and `release`,
  no race in the module; the rest of the suite and the other options are still to run.
- *Progress, 2026-10-18 (journal 0127):* the KCSAN run moves to CI (`.github/workflows/kcsan.yml`,
  on demand and weekly, a runner with KVM), with the report filter of journal 0115; it no longer
  needs the benchmark host.

### S9. Resource limits and denial of service

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
- Done when: limit tests in the suite and the numbers recorded.

### S10. Module signing and distribution

- Module signing and Secure Boot compatibility (lockdown); DKMS packaging.
- Reproducible build of the `.ko` with the hash recorded.
- Done when: a signed module loads in a VM with lockdown active.

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
