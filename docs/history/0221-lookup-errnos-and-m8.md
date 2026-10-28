# 0221 — Lookup errnos, once-only logs, getattr, flush and build checks (issue 119, M4 and M8)

Date: 2026-10-28. Status: on the local branch `b3/a` (security and solidity batch 3), not
published. Work started 2026-10-28 09:10 America/Sao_Paulo; ended 2026-10-28 10:00. Wall-clock time, not
an effort measurement. Estimated effort: S.

## Goal and decision

Issue 119 gathers items of the round-2 maintainer's reading (journal 0155, "Priority 4"). This
journal closes M4 and M8 on the branch; M6 and M7 are another unit of the batch, M5 was fixed in
journal 0156.

**M4.** `moobl_lookup()` failed with `EXDEV` for a btrfs subvolume and `EOPNOTSUPP` for a
casefolded directory, names `readdir` lists, and logged `pr_warn_ratelimited()` on each refusal.
`vfs.rst` asks `->lookup` to fail only on a real error, because a negative dentry is what lets a
creation proceed. Read in the 6.12.112 sources and in overlayfs of Linux 7.3:

- A name that exists on the lower and that the layer cannot represent is a real error: overlayfs
  fails the lookup too (`ovl_lookup_single()`, `ovl_dentry_weird()`, `EREMOTE`). Returning a
  negative dentry would let `mkdir` or `open(O_CREAT)` try the name on the lower. The refusal
  stays; what changes is the errno and the log.
- Casefolded directory: `EREMOTE`, overlayfs's errno for a lower object it does not support
  (6.12) and for "child wrong casefold" (7.3). `EOPNOTSUPP` from `stat()` read as "the operation
  is not supported" and is what the user-space tools take for "no extended attributes here".
- Lookup inside a directory that became casefolded on the lower after the layer entered it (the
  layer itself refuses the flag change, so only a change behind the layer reaches this):
  `ESTALE`, overlayfs 7.3's "parent wrong casefold". The VFS retries the walk once with
  `LOOKUP_REVAL` and then reports it.
- btrfs subvolume: `EXDEV` stays. It is the errno the VFS itself returns when a lookup may not
  cross into another filesystem (`LOOKUP_NO_XDEV`, `openat2(RESOLVE_NO_XDEV)`), and the README
  and changelog have promised it since journal 0083.
- The refusal is logged with `pr_info_once()`: a caller can repeat it at will, the errno reports
  every occurrence, and the administrator learns the configuration once.
- Not followed: overlayfs 6.12 refuses at mount any lower whose superblock has an encoding
  (`sb_has_encoding()`). Linux 7.3 relaxed that to one casefold state per layer, and the module
  promises that the ordinary directories of a casefold-capable ext4 work (step `casefold`); the
  casefolded lower root is already refused at mount.

**M8, one line each.**

1. `backing_file_open()`: a `static_assert(__same_type(...))` in `layer/main.c` states the
   signature with the user file that every supported kernel has (6.12.111, .112, .113: the 6.12
   stable backport of the backing-file LSM hooks; the v6.12 tag takes the user path). A kernel
   with the old form now fails with a message naming `docs/compatibility.md` instead of a
   pointer-type error. A type check rather than a `LINUX_VERSION_CODE` guard: the stable sublevel
   that brought the change was not established, and the policy builds only for listed kernels,
   so no second branch for the old form is wanted. Verified by building with the assertion's
   type changed to the old form (`error: static assertion failed: "moobfs_layer needs
   backing_file_open(...)"`).
2. `getattr`: `moobl_lower_getattr()` calls `vfs_getattr()` unless the call carries
   `AT_GETATTR_NOSEC`, as `ovl_do_getattr()` and `ecryptfs_do_getattr()`. On Linux 6.12 this
   changes nothing: `vfs_getattr()` checks the layer path and then calls `vfs_getattr_nosec()`,
   which adds the flag to every `->getattr` call (`fs/stat.c:166`). It keeps the choice with the
   caller if that changes. No test can tell the two apart on this kernel. The two internal
   `vfs_getattr_nosec()` calls (the device of a directory, at mount and in lookup) stay: they are
   the layer's own questions.
3. `flush`: runs under the mounter's credentials, as `ovl_flush()` and the layer's other
   delegated I/O. None of ext4, btrfs and XFS has `->flush`, so no supported lower sees a
   difference; a lower that has one (NFS, FUSE) is unsupported (`docs/compatibility.md` §1.4).
4. `fileattr_set` without the lower `i_rwsem`: not a defect. `vfs_fileattr_set()` on the lower
   dentry takes `inode_lock()` on the lower inode (`fs/ioctl.c:681`) and reads the lower flags
   again under it; ecryptfs and overlayfs call it the same way. The layer's own unlocked read only
   decides whether this request changes `FS_CASEFOLD_FL`; a flag change racing it from behind the
   layer is non-promise 2 of ADR-0011. A comment says so. No lock was added or removed.
5. `SB_I_NOEXEC`: `docs/when-not-to-use.md` §3 now says that nothing runs from the layer and that
   device nodes in it cannot be opened, with the errnos (`path_noexec()`, `may_open_dev()`).
6. `scan_delay_us` (and `scan_getxattr_errno_once`): kept in the shipped module, with the reason
   in the code. Moving them behind a test-only build would make the suite test a binary users do
   not install; only root can write them, they are 0 by default, and while 0 the cost is one
   predicted branch per scanned file. The `pm-mv`, `buildrace`, `buildmoved` and KASAN steps that
   use them are unchanged.
7. `pr_warn_ratelimited()` on user-driven paths: the lookup one became `pr_info_once()` (M4); the
   two in `layer/index.c` (an index update that failed, so its domain is built again, and a build
   that failed) became `pr_warn_once()`. An ordinary user causes both at will by filling the
   budget or exceeding the build limits; the caller gets the errno, and the tracepoints
   `moobfs_index_incomplete` and `moobfs_domain_build` record every occurrence. No suite step
   reads these messages.

## Alternatives

- A negative dentry for an unrepresentable name: rejected, a creation would then reach the lower
  for a name that exists, and `readdir` and `stat` would disagree.
- Refusing a casefold-capable superblock at mount (overlayfs 6.12): see above.
- `EREMOTE` for the subvolume too, for uniformity with overlayfs: rejected, `EXDEV` is the VFS's
  own answer for this boundary and a shipped promise.
- Revalidating away a directory that became casefolded (`d_revalidate` returning 0) instead of
  `ESTALE` in lookup: more code in a function another unit of the batch also changes, for a case
  that only a change behind the layer produces.
- A test-only Kconfig-like define for `scan_delay_us`: see M8.6.

## Changes

- `layer/main.c`: `moobl_casefolded()` is a pure predicate; new `moobl_lookup_refused()` with the
  errnos above, used by `moobl_lookup()`; `moobl_lower_getattr()`; `moobl_flush()` under
  `override_creds()`; the `backing_file_open()` assertion above `moobl_open()`; a comment in
  `moobl_fileattr_set()`.
- `layer/index.c`: the two warnings once; the reason `scan_delay_us` stays.
- `tests/layer.c`: `cfsetup` adds an empty directory `later`; `cfcheck <mount> <lower>` expects
  `EREMOTE` (also for `mkdir`), that `readdir` lists the refused directory, and, after casefolding
  `later` on the lower, `ESTALE` for `stat` and `open(O_CREAT)` under it with nothing created on
  the lower. New `svcheck <mount> <name>`: the subvolume is listed, `stat`, `mkdir` and
  `open(O_CREAT)` fail with `EXDEV`. `vm/suite.sh` passes the lower to `cfcheck` and runs
  `svcheck` in the btrfs step.
- `docs/testing.md` (the `CASEFOLD_REFUSED_PASS` row, new row `LAYER_SUBVOLUME_PASS`),
  `docs/when-not-to-use.md`, dated notes in `docs/compatibility.md` (open points) and
  `docs/security-roadmap.md` (S8), the old casefold errno wording in `docs/retired-claims.txt`,
  `docs/references.md` §20, `CHANGELOG.md`, the history index.

## Evidence

All on the dedicated host, Debian 13, KVM, in `~/b3a` from a bundle of this branch.

- `make checkpatch`: exit 0, nothing reported. `make layer` against the Debian 6.12.111
  headers: no warning. The same build with the assertion's type changed to the v6.12 form:
  `error: static assertion failed: "moobfs_layer needs backing_file_open(const struct file *,
  ...): see docs/compatibility.md"`.
- `MOOB_VM_KVM=1 MOOB_ONLY="casefold ext4 btrfs xfs overbudget permmatrix" make vm-test`
  (Debian 6.12.111 guest), commit `f72bc5a`, 13 minutes: `LAYER_CASEFOLD_PASS`,
  `CASEFOLD_REFUSED_PASS`, `LAYER_SUBVOLUME_PASS`, `NESTED_SUBVOLUME_REFUSED`,
  `LAYER_PASSTHROUGH_TESTS_PASS` (6), `LAYER_LOWER_PASS` on ext4, btrfs and XFS,
  `OVERBUDGET_PASS` on the three, `PERMMATRIX_STEP_PASS`, `QUERY_BUILD_MOVED_PASS`,
  `MOOBFS_ONLY_PASS: steps casefold ext4 btrfs xfs overbudget permmatrix (not a complete run)`.
- Negative round: `layer/` of `main` (`e68da30`) with the new tests, `MOOB_ONLY="casefold
  btrfs"`: `FAIL line 403: stat(up("ci"), &st) == -1 && errno == EREMOTE (errno=95 Operation not
  supported)`, `MOOBFS_VM_FAIL: cf-check`. `LAYER_SUBVOLUME_PASS` printed there too, as expected
  (see Limitations); the run stopped at the first casefold check, so the `ESTALE` checks were not
  reached on the old module.
- `PATH=$HOME/.venv-vng/bin:$PATH MOOB_ONLY="casefold btrfs ext4" sh vm/kasan-local.sh`
  (upstream 6.12.112, `vm/kasan.config`), commit `f72bc5a`: `MOOBFS_KASAN_ONLY_PASS: steps
  casefold btrfs ext4 under KASAN, UBSAN and lockdep with no kernel diagnostics (not a complete
  run)`. No lock changed; run because `flush` now overrides credentials and lookup changed.
- Not run: the full `make vm-test`, the full KASAN suite and the Debian 6.12.113 guest (left to
  the batch).

## Errors found on the way

- A first draft of `docs/when-not-to-use.md` said overlayfs sets `SB_I_NOEXEC`; it does not
  (`grep` of `fs/overlayfs` in 6.12.112). Corrected before the commit: binfmt_misc and pidfs do.

## Limitations

- The `getattr` and `flush` changes have no test that tells them apart on the supported kernels
  and lowers (see M8.2 and M8.3); the steps that exercise `stat` and file close still pass.
- `svcheck` documents behaviour the module already had (it passes on the module before this
  change too); the negative round applies to `cfcheck`.
- f2fs and tmpfs casefolding are still not exercised (the `casefold` step uses ext4).
- The stable sublevel that changed `backing_file_open()` was not pinned down.

## Next step

The maintainer runs the full `make vm-test` and KASAN suite once for batch 3, then reviews the
batch. Issue 119 keeps M6 and M7 for their own unit; nothing was ticked on GitHub.
