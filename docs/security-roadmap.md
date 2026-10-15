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

### S9. Resource limits and denial of service

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
