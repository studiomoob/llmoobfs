# 0290 — AppArmor: a first run of the layer under Debian's default LSM

Date: 2026-11-12 and 13. Work started about 20:30 Brasília time on the 12th; guest runs from the
13th, after the release-candidate queue and the soft-lockup runs of the test host. Status: **in
progress**. Estimated effort S; the time recorded is calendar time of one session, most of it
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

Pending: the runs start after 09:15 on the 13th.
