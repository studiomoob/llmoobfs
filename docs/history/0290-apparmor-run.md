# 0290 — AppArmor: a first run of the layer under Debian's default LSM

Date: 2026-11-12 and 13. Work started about 20:30 Brasília time on the 12th; guest runs on the
13th from 09:15 (after the release-candidate queue and the soft-lockup runs of the test host);
ended about 09:30 on the 13th. Status: **done; pull request with the maintainer's agreement** (rebased on batch 26, journal
0289). Estimated effort S; the time recorded is calendar time of one session, most of it
waiting for the test host.

## Goal

The documents said that AppArmor and Smack never ran (`docs/matrix-and-limits.md`,
`docs/when-not-to-use.md`, journal 0286). AppArmor is the major LSM Debian and Ubuntu boot with.
Build a harness like the SELinux one (journal 0104) and answer, each with evidence:

1. With AppArmor enforcing, does the layer mount, read, write, set attributes and answer
   `QUERY2` for unconfined root, and what does a confined process see: which path does AppArmor
   check through the layer, the layer's or the lower's?
2. With the mounter and the reader in different profiles (ADR-0037), whose profile needs rules on
   the lower path? With the fix and, as a negative round, without `override_creds()` in
   `moobl_open()` and `moobl_dir_open()`.
3. Can a confined process get past a deny rule on one path of a file through the other path?

The kernel stays frozen (ADR-0022): harness, tests and documents only.

## Prior art and sources read

Kernel sources of the test host's cache, Linux 6.12.116 and 6.12.112 (`security/apparmor/file.c`,
`lsm.c`, `path.c`, `mount.c`, `include/file.h`, `include/cred.h`, `fs/backing-file.c` are byte
for byte the same in both); the guest boots Debian's 6.12.111, whose headers carry the same
backing-file hooks (`include/linux/lsm_hook_defs.h`: `backing_file_alloc`, `mmap_backing_file`).

- **What AppArmor mediates.** By path, at open and on later use of an open file, plus the
  `security_path_*` hooks the VFS calls with the path the process named (`path_mknod`,
  `path_mkdir`, `path_unlink`, `path_rename`, `path_link`, `path_truncate`, `path_chmod`,
  `path_chown`), `inode_getattr`, `sb_mount`, `mmap_file`, `file_lock`. It has no hook for
  extended attributes, for `inode_permission` or for the `backing_file_*` hooks (`lsm.c`,
  `apparmor_hooks[]`).
- **Which path it sees through the layer.** `apparmor_file_open()` checks the label of
  `file->f_cred` against `file->f_path`. For the layer's own file that is the caller and the
  layer path. The lower file comes from `backing_file_open()`, whose `f_path` is the lower path
  and whose `f_cred` is the mounter's credentials (passed in, before and after ADR-0037). So the
  caller is checked by the layer path and the mounter by the lower path, as with overlayfs, whose
  real files are opened the same way (`ovl_open_realfile()`). The layer calls `vfs_create()`,
  `vfs_unlink()`, `vfs_rename()` and `notify_change()` on the lower, which call no path hook: a
  creation, removal or rename through the layer is checked once, on the layer path.
- **What the file's label changes.** `apparmor_file_alloc_security()` stores the label of
  `current` in the file (`ctx->label`). On a later use, `aa_file_perm()` returns at once when the
  current task or the file's label is unconfined, or when nothing beyond the permissions granted
  at open is asked and the file's label is a subset of the current one. Otherwise
  `__file_path_perm()` checks the current label's profiles that are not in the file's label, and,
  when more than the open granted is asked (`denied`), also the file's label's profiles that are
  not in the current one, both on `file->f_path`, the lower path. Reads, writes and directory
  reads of the lower file run as the mounter (ADR-0011). Before ADR-0037 the lower file carried
  the caller's label: with both confined and a request beyond the open's grant, the caller's
  profile was checked on the lower path. One such request is a write on a file opened with
  `O_APPEND`: the open grants `MAY_APPEND` (`aa_map_file_to_perms()`), the write asks
  `MAY_WRITE` (`rw_verify_area()`). Prediction: without the fix, a confined reader appending
  through a confined mounter's layer is refused unless its own profile grants write on the lower
  path; with the fix it is not. Reads and directory reads ask only what the open granted, so they
  do not tell the two apart.
- **AppArmor documentation.** `apparmor.d(5)` and the parser's rule grammar
  (`parser/mount.cc`, `parser/parser_yacc.y`, AppArmor 4.1) for the profile syntax; the kernel's
  `Documentation/admin-guide/LSM/apparmor.rst`. Rules name paths, not files: a file reachable by
  two paths (bind mount, hard link, overlayfs) is governed by the rule for the path used.

## Harness

- `vm/apparmor-local.sh` (host): builds `query`, `moobfs-attr` and `listdir` static, makes an ext4
  image and boots a virtme-ng guest of the running kernel (no extra command line: AppArmor is in
  Debian's default `CONFIG_LSM`); `vm/apparmor-guest.sh` loads `vm/apparmor/moobtest.profiles`
  with `apparmor_parser -r -K` in the guest only. The host's policy is not touched.
- Profiles: `moobaa_both`, `moobaa_layer`, `moobaa_lower` (rules on both paths, the layer's only,
  the lower's only); `moobaa_mounter` and `moobaa_mounter_noro` (a confined mounter with rules on
  the lower only, the second denied the read-only file) and `moobaa_reader` (rules on the layer
  paths only); `moobaa_denylower`, `moobaa_denylayer`, `moobaa_denyalias` and `moobaa_self` for
  the bypass questions. Commands run under a profile with `aa-exec -p`.
- Markers: `APPARMOR_UNCONFINED_PASS`, `APPARMOR_PARITY_PASS`, `APPARMOR_LAYERPATH_PASS`,
  `APPARMOR_MOUNTER_PASS`, `APPARMOR_MOUNTER_RULES_PASS`, `APPARMOR_ALIAS_SAME_AS_OVERLAYFS`,
  `APPARMOR_LINK_PASS`, `APPARMOR_SELFMOUNT_PASS`, `MOOBFS_APPARMOR_RESULT: fails=<n>`; any
  failed check prints `MOOBFS_APPARMOR_FAIL` and the run goes on.

## Evidence

All on the test host (Debian 13), guest kernel `6.12.111+deb13-amd64`,
`lsm=lockdown,capability,landlock,yama,apparmor,tomoyo,bpf,ipe,ima,evm`, ten test profiles in
enforce mode, `make layer` run in each clone first.

**First attempt** (`41d5464`, 09:15–09:16): questions 1 and 2 ran, the step 3 stopped at the
overlayfs mount (`overlayfs: at least 2 lowerdir are needed while upperdir nonexistent`), and two
checks of step 2 failed for the harness, not the layer: a plain `deny` rule is not logged, so the
mounter's refusal left no denial to count; and the reader's control read of the lower file was
counted among the denials the reader must not cause. Fixed in `6efe513` (`audit deny`, a second
empty lower layer for overlayfs, the control after the log is taken).

**Positive round** (`6efe513`, `sh vm/apparmor-local.sh`, 09:19:51–09:20:04), rc=0:

- `APPARMOR_UNCONFINED_PASS`: root mounts, reads, writes, sets an attribute on four files and
  `QUERY2` names all four.
- 18 operations (read, stat, append, list, setxattr, create on `rw`, `ro`, `none`):
  `q1: differ=0 denied_directly=3 layer_rules_mismatch=0 lower_rules_refused=9
  lower_rules_allowed=0`, `APPARMOR_PARITY_PASS`, `APPARMOR_LAYERPATH_PASS`; the profile with
  rules on the lower path only is refused through the layer with
  `operation="open" profile="moobaa_lower" name="/run/aa/layer/t/rw"` (12 such denials).
- `confined mounts: layerm=0 layerx=0`; `q2 reader through layerm: read=0 list=0 append=0 stat=0
  create=0; through layerx: read=1 append=0; lower directly: read=1`; `q2 denials: reader on a
  lower path=0 mounter_noro on lower/t/ro=1` with `operation="open"
  profile="moobaa_mounter_noro" name="/run/aa/lower/t/ro"`; `APPARMOR_MOUNTER_PASS`,
  `APPARMOR_MOUNTER_RULES_PASS`.
- `deny on the lower path: lower=1 layer=0 overlayfs=0 bind=0`; `deny on the layer path: layer=1
  lower=0`; `second name of a denied file: link direct=1 layer=1; rename direct=1 layer=1`
  (`operation="link"` and `operation="rename_src"` denials naming the layer path through the
  layer); `self-mount (secret through own layer, own overlayfs, ro through own layer): 1 1 0`;
  `APPARMOR_ALIAS_SAME_AS_OVERLAYFS`, `APPARMOR_LINK_PASS`, `APPARMOR_SELFMOUNT_PASS`,
  `MOOBFS_APPARMOR_RESULT: fails=0`.

**Negative round** (same tree, `git show 3179b63 -- layer/main.c | git apply -R`: no
`override_creds()` in `moobl_open()` and `moobl_dir_open()`, 09:20:12–09:20:25), rc=1: every
step as above except `q2 reader through layerm: read=0 list=0 append=1 ...; through layerx:
read=1 append=1`, with `operation="file_perm" profile="moobaa_reader"
name="/run/aa/lower/t/rw" requested_mask="w"`, and `MOOBFS_APPARMOR_FAIL: q2: the reader through
a confined mounter's layer was refused or checked on the lower path (... append=1 ...
lower-path denials=2)`, `MOOBFS_APPARMOR_RESULT: fails=1`. The prediction from the sources held:
read and list do not tell the two apart; an append does.

## Answers

1. **Unconfined root**: mount, read, write, setxattr and `QUERY2` work with AppArmor enforcing.
   **A confined client is checked by the layer's path**: rules on the layer path give through
   the layer exactly what rules on the lower path give directly (`differ=0`), and rules on the
   lower path alone neither grant anything through the layer nor are needed. AppArmor did not
   refuse `stat` or `setxattr` on a file without a rule in these profiles, directly or through
   the layer alike (it has no xattr hook in 6.12).
2. **The mounter's profile needs the lower path, the reader's only the layer path**, with
   ADR-0037. Without the fix, a confined reader's profile also needed write on the lower path to
   append through a confined mounter's layer: the lower file carried the reader's label, and
   `__file_path_perm()` checked it on the lower path whenever a use asked more than the open
   granted. ADR-0037 therefore also holds for AppArmor, and the step is its regression test.
   Unconfined mounter or unconfined reader: no difference (`aa_file_perm()` returns at once).
3. **No bypass specific to the layer.** A deny rule names a path: a deny on the lower path does
   not cover the layer path, as it does not cover the same file through overlayfs or a bind mount
   (all three read it); a deny on the layer path does not cover the lower path, which the profile
   grants. Neither link nor rename gives a denied file a second name, through the layer or
   directly. A process denied a lower file cannot read it through a layer it mounts itself: the
   lower file is opened with its own credentials and checked on the lower path, as with
   overlayfs. This is AppArmor's path model, the same for overlayfs and bind mounts, not a defect;
   a profile that must protect lower files from a confined process has to deny the layer paths
   too (or not grant them). `QUERY2` names files the profile does not grant, as ADR-0011 item 3
   already states for path-based LSMs (seen: `none` and `secret` in a confined client's answer).
   Nothing here is a new security issue; the maintainer may still want the profile advice above
   in the user documentation.

One difference from overlayfs, seen in the self-mount step: overlayfs opens its real files on a
private clone of the lower mount, so AppArmor logs them as a disconnected path (`info="Failed name
lookup - disconnected path" name="t/secret"`) and a confined overlayfs mounter needs
`attach_disconnected`; the layer opens them on the lower's own mount, so they carry the full path
(`name="/run/aa/lower/t/secret"`) and the mounter's rules are written on that path.

## Surfaces brought up to date

`docs/matrix-and-limits.md`, `docs/when-not-to-use.md` (a new entry on AppArmor deny rules),
`docs/testing.md` (the harness and its markers; §9), `docs/faq.md` and `site/faq.html`
(regenerated), `docs/help-wanted.md`, the README facts in both languages, a dated note in
ADR-0037, `CHANGELOG.md`; the old wording is in `docs/retired-claims.txt`. The S12 row of
`docs/security-roadmap.md` is about SELinux and already says AppArmor is outside it: unchanged.
Checks: `scripts/check-matrix-page.sh`, `scripts/check-retired-claims.sh`,
`scripts/site-faq.py --check` pass.

## Pre-push checks

On `67828a2`, test host, 09:22–09:28 (`run12.sh`, the pre-push script against `8a8d09b`): lint,
checkpatch, test, asan, hostile, pylib, retired, pins, mutants, site, `git diff --check 8a8d09b`
and ubuntu-tests all PASS. No file of `layer/` changed, so neither `make vm-test` nor KASAN ran.

## Decisions (maintainer, 2026-11-13, morning)

- The advice in `docs/when-not-to-use.md` stays: a deny rule meant to cover files under the
  layer must also name the layer paths, as for overlayfs and bind mounts.
- The AppArmor harness stays a manual run, like the SELinux one; it does not enter CI now.
- References: this journal's sources are `docs/references.md` §62 (renumbered from §61 when the
  branch was rebased on batch 26, whose journal 0289 took §61).

## Limitations

- One kernel (Debian 6.12.111), one host, ext4 lower, two runs; profiles of the test, not of a
  real service; processes confined with `aa-exec`, not by an attached profile. Not run on 7.x,
  not under KASAN (no kernel code changed), not in CI.
- Not exercised: mmap, `file_lock`, exec through the layer, chmod/chown/truncate path hooks, user
  namespaces, AppArmor's policy namespaces and stacking with SELinux. Smack still never ran.

## Full suites of the batch (2026-11-13)

`MOOB_VM_KVM=1 make vm-test` (16:42–17:02) and `sh vm/kasan-local.sh` on upstream 6.12.116
(17:02–18:18) passed on this batch's tip before its rebase, `06e015b`, with no kernel
diagnostics; the table is in journal 0289, which covers batches 26 and 27 together. The rebase
on batch 26's next commit (a journal entry only) changed no file this batch touches. The
AppArmor harness is manual and is not part of either run.

Pre-push checks on `3446465` (the tip before the rebase on #200, same tree), 2026-11-13,
19:01–19:06: all twelve steps PASS.

## Next step

The pull request of this batch with the maintainer's agreement, after batch 26's; whether to add
the profile advice of answer 3 to `docs/when-not-to-use.md` beyond the line added there.
