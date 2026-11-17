# 0298 — SELinux and Smack: mappings, changes, exec and mounters (S12)

Date: 2026-11-16. Work started at 07:14 Brasília time; ended at 22:11, when the final runs ended (calendar time, including the test host's power loss). Status: **done on the
local branch `claudiofp/b33-s12-mac-tests`, batch 33; not pushed; final runs passed; one check held back and
recorded privately.** Estimated effort S (hours); the time recorded is calendar time of one session,
including waits for the test host. Work done by Claude Code (an AI assistant) for the maintainer.

## Goal

Close the open test items of security roadmap S12, tests only (ADR-0022: nothing under `layer/`
or `include/` changes):

1. SELinux, for the client domain on files it may read and write, only read, or not touch:
   shared mappings (read and write), attribute writes (`moobfs-attr set`, a `user.*` xattr),
   rename within a directory, hard link (then unlink of the link), each compared directly on the
   lower and through the layer.
2. Smack: shared mappings for the labelled unprivileged subject; exec through the layer with
   `SMACK64EXEC` and `SMACK64MMAP`; a mount by a labelled, unprivileged mounter if the kernel
   allows it.
3. A static C helper for the mappings.

## Decisions

- **A change is made once per file.** The SELinux guest makes two more labelled sets before the
  layer is mounted: `md`, reached only directly, and `my`, reached only through the layer. The
  Smack guest does the same for the mapped writes. So the lower never changes behind the layer
  during the test, and each operation still has a direct reference.
- **A type the client may open but not map** (`moobtest_nomap_t`, `rw_file_perms` without `map`)
  is the only policy addition. Without it every refused mapping was refused at `open`, and the
  `map` check itself was never compared. The direct path needed no other rule:
  `manage_file_perms` already holds `rename`, `link`, `unlink` and `setattr`, and the client's
  `rw` and `ro` types already had `map`.
- **What a change left on the lower is checked**, not only the exit status: after the
  operations through the layer, `my/rw` is renamed to `rw.r`, starts with the mapped byte `M`,
  carries the attribute, and no link is left (`SELINUX_EFFECT_PASS`).
- **Exec through the layer cannot reach Smack.** The layer marks its superblock `SB_I_NOEXEC`
  (journal 0221, `docs/when-not-to-use.md` §3), so `execve` fails before
  `smack_bprm_creds_for_exec()` runs. The step checks that directly Smack allows `x/cat` (label
  `Ex`) with `SMACK64EXEC=Run` (the program runs as `Run`), allows `xs/cat` without it (runs as
  `Sub`) and refuses `xn/cat` (label `Rd`, no `x` rule); that through the layer all three are
  refused; and that `SMACK64EXEC` reads `Run` through the layer as on the lower. `SMACK64MMAP` is
  tested with a read mapping, not an exec one.
- **`SMACK64MMAP`:** `smack_mmap_file()` refuses a mapping when any rule of the subject with some
  access is not matched by the mapping label's rules. `mok` (label `Rd`, `SMACK64MMAP=MmOk`, with
  `rwxat` on every object the subject has a rule for) maps; `mno` (`MmNo`, no rules) does not,
  while `read(2)` of it is allowed, so the refusal is `SMACK64MMAP`'s.
- **An unprivileged mounter is impossible.** `moobfs_layer` is not `FS_USERNS_MOUNT`, so
  `mount_capable()` asks `CAP_SYS_ADMIN` in the initial user namespace. The step records the
  refusal for the subject, plainly and as root of its own user namespace (`unshare -Urm`), with a
  tmpfs mount in that namespace as control (`SMACK_UNPRIV_MOUNT_PASS`), and drops the
  unprivileged mounter.
- **A labelled mounter instead:** uid 0 with `CAP_SYS_ADMIN` only (no `CAP_MAC_OVERRIDE` or
  `CAP_MAC_ADMIN`), label `Mnt`, rules on `Rd`, `Rw` and `Dir` and none on `Hid` or `Only`. A
  mounter without `CAP_MAC_ADMIN` gives its own label to the root of the filesystem it mounts
  (`smack_set_mnt_opts()`, `smack_d_instantiate()`), so the subject needs `Sub Mnt rx` to reach
  the layer at all.
- **Held back:** one check of the labelled-mounter step is held back from the published harness, recorded privately under `SECURITY.md`, and returns with its fix.

## Changes

- `tests/mmapfile.c` (new): `mmapfile r|w <file>`, a `MAP_SHARED` mapping, read of every byte,
  or `PROT_WRITE`, `'M'` into the first byte and `msync(MS_SYNC)`; exit 10 open, 11 fstat or
  empty, 12 mmap, 13 msync. After `tests/mmap/mmap_file_shared.c` of the SELinux test suite.
  Builds `-static -Wall -Wextra -Werror` with gcc (test host) and `-Wall -Wextra -Werror` with
  clang; `checkpatch.pl --no-tree --file` (pinned v6.12.112) reports only the SPDX comment style,
  as for `tests/listdir.c`, whose style the project keeps (`scripts/check-spdx.sh`).
- `vm/selinux-local.sh`: builds `mmapfile`. `vm/selinux-guest.sh`: the sets `md` and `my`, the
  step above (`SELINUX_MAPCHANGE_PASS`, `SELINUX_EFFECT_PASS`, enforced in the `fsuse` run;
  reported in the `stock` run). `vm/selinux/moobtest.te`: `moobtest_nomap_t`.
- `vm/smack-local.sh`: builds `mmapfile`. `vm/smack-guest.sh`: rules for `Ex`, `Only`, `MmOk`,
  `Mnt`; files made before the layer mount; steps 6 (mappings, `SMACK_MMAP_PASS`,
  `SMACK_MMAPLABEL_PASS`), 7 (exec, `SMACK_EXEC_PASS`) and 8 (mounters,
  `SMACK_UNPRIV_MOUNT_PASS`, `SMACK_MOUNTER_PASS`).
- Documents: below.

## Rounds

Test host (Debian 13, KVM), one fresh clone of a git bundle per run under `~/b11night/b33/`.
SELinux: a guest of the host's 6.12.111 kernel. Smack: upstream 6.12.116 with KASAN, UBSAN,
lockdep and Smack, reused from `~/.cache/moobfs-smack`. Each run took about one minute.

- **Round 1** (07:24–07:26, `d6a9d72`): SELinux `fsuse` passed the new step at once;
  no policy rule was needed. Smack `fails=3`, all the harness's:
  - `SMACK64MMAP`: `mok` was refused too. The rule `Sub Mnt rx`, added for step 8, had no match
    among `MmOk`'s rules. Fix: `MmOk Mnt rwxat`.
  - exec through the layer failed for every program (126): `SB_I_NOEXEC`, a documented limit,
    not a Smack decision. The step was rewritten as above.
  - the labelled-mounter step showed the check now held back.
- **Round 2** (07:27–07:31, `cd164dd`): SELinux with `nomap`: 20 operations, all agree. Smack:
  directly, `xn/cat` (label `Rd`, no `x` rule) ran. A probe showed that busybox, as the same
  subject, is refused the same exec. util-linux `setpriv` sets `PR_SET_KEEPCAPS` and re-applies
  root's effective capabilities after the uid change, so the program it `execvp()`s is opened
  with `CAP_MAC_OVERRIDE`. The existing steps are not affected: setpriv always starts busybox or
  a static helper from the unlabelled 9p root, and those run with no capabilities. Fix
  (`e088821`): the exec step starts busybox, which execs the program.
- **Round 3** (07:35–07:40, `e088821`), positive and negative:

| Run | Result |
| --- | --- |
| Smack positive | `fails=0`; every marker |
| Smack negative (harness broken six ways, below) | `fails=5`: mmap, `SMACK64MMAP`, exec, unprivileged mount, labelled mounter |
| SELinux positive | `fsuse`: `SELINUX_MAPCHANGE_PASS` (20 operations, 6 allowed, 14 denied directly), `SELINUX_EFFECT_PASS`, mounter steps, `differ=0` |
| SELinux negative a: `my/ro` labelled `moobtest_rw_t`, `my/nomap` labelled `moobtest_ro_t` | `MOOBFS_SELINUX_FAIL: fsuse: mapping and changes: 5 of 20 operations differ` (mmap write, attribute, link, rename on `ro`; mmap read on `nomap`) |
| SELinux negative b: the mapped write replaced by a mapped read on both paths | `SELINUX_MAPCHANGE_PASS`, then `MOOBFS_SELINUX_FAIL: ... effects ... (byte=r ...)` |

  The Smack negative round changed the guest script only: the layer's mapping paths pointed at
  `rw` (rows `rd` and `hid` differ), the layer's `mno` mapping pointed at `mok`, no
  `SMACK64EXEC` on `x/cat` (`x` runs as `Sub`; the attribute reads empty through both views), the
  user-namespace mount made with `tmpfs` (it succeeds), and no `Mnt Rd r` (the subject's read of
  `rd` through the mounter's layer is refused, and `rd` shows `Mnt` there).
- **Rounds 4–6** (07:41–07:55, on states after `e088821`, later folded into `99f5ddf`): work on the check now
  held back, recorded privately with it.

## Held-back check

*Note, 2026-11-17:* the held-back check is [GHSA-w7w8-m49c-vrvr](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-w7w8-m49c-vrvr), fixed in journal 0299 (ADR-0040), where the check returned to the harness.

On 2026-11-16, before publication, one check of the labelled-mounter step is held back from the published harness, recorded privately under `SECURITY.md`, and returns with its fix. The surfaces of this batch name the Smack steps without
it; this is the stale-on-purpose case of `docs/public-surfaces.md`.

## Also recorded here

- #206 (batch 32, journal 0297: the tag recorded, the access table read after it, and
  `docs/tag-checklist.md`) entered `main` on 2026-11-16 at 09:09 as `2c4a519`, after its 20
  checks passed on `24a172b`.
- The tag `v0.6.0-experimental` on the s390x host (exploratory; Ubuntu 24.04, KVM), each run on a
  fresh clone of the tag from GitHub, 2026-11-16: the full `MOOB_VM_KVM=1` suite twice (07:07 to
  07:22 and 07:22 to 07:37), `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, exit
  0 each; `MOOB_ONLY="ext4 btrfs xfs" vm/kasan-local.sh` (07:37 to 09:15),
  `MOOBFS_KASAN_ONLY_PASS`, no kernel diagnostics (not a complete run), exit 0.
- The test host lost power at about 08:27 and came back in the evening; the final runs below were
  started at about 20:20.

## Final runs on the tip

On the dedicated host (Debian 13, KVM), at `43aa8d5`, each run on a fresh clone of a bundle of the
branch, 2026-11-16:

| Run (BRT) | Result |
| --- | --- |
| 20:22–20:23 `vm/smack-local.sh` | every `SMACK_*_PASS`, `MOOBFS_SMACK_RESULT: fails=0`, no kernel diagnostic |
| 20:23–20:24 `vm/selinux-local.sh` | `SELINUX_MAPCHANGE_PASS` (20 operations), `SELINUX_EFFECT_PASS`, `SELINUX_MOUNTER_PASS`, `SELINUX_DIRREAD_PASS`, `fsuse differ=0`, exit 0 |
| 20:24–20:47 full `MOOB_VM_KVM=1 make vm-test` | `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, exit 0 |
| 20:47–22:00 full `vm/kasan-local.sh` | `MOOBFS_PYLIB_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_KASAN_PASS`, exit 0 |
| 22:00–22:11 pre-push checks | lint, checkpatch, unit tests, ASan, hostile images, Python library, retired claims, pins, matrix page, mutants, site, `git diff --check` against `b40e3b6`, `make static`, Ubuntu tests: all `PASS` |

## Limitations

- One kernel per LSM (Debian 6.12.111 for SELinux, upstream 6.12.116 for Smack), an ext4 lower,
  rules and types written for the test.
- SELinux: no MLS/MCS, no distribution booted with SELinux; the confined mounter of journal 0286
  is permissive.
- Smack: no distribution booted with Smack; exec, `SMACK64EXEC` and executable mappings cannot
  be compared through the layer (`SB_I_NOEXEC`); the unprivileged mounter is impossible by design.
- The negative rounds break the harness (labels, paths, rules), not the kernel.

## Next step

The batch's pull request, rebased on `main`, with the maintainer's agreement; the fix of the
held-back check follows in a later batch, by the private path of `SECURITY.md`.

## Surfaces brought up to date

Security roadmap S12 (current state and a dated status), `docs/testing.md` (SELinux and Smack),
`docs/matrix-and-limits.md`, `docs/help-wanted.md`, both READMEs,
`CHANGELOG.md`, `docs/references.md` §68; the old wording is in `docs/retired-claims.txt`.
`scripts/site-journey.py` milestones are left as they are (not every journal is a milestone).
