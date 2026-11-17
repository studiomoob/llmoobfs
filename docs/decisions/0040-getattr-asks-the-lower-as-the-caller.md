# ADR-0040 — A fifth narrow exception to the kernel freeze: `stat` asks the lower's security hook as the caller

Date: 2026-11-16. Status: **accepted** by the maintainer on 2026-11-17 (journal 0299); on
2026-11-16 the maintainer had classified the defect below as a low-severity security defect
(GHSA-w7w8-m49c-vrvr) and agreed to fix it. Amends
[ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md) by one exception, beside those
of [ADR-0033](0033-s16-freeze-exception.md), [ADR-0034](0034-kernel-compat-freeze-exception.md),
[ADR-0037](0037-open-lower-files-as-the-mounter.md) and
[ADR-0038](0038-index-keeps-no-inode-pointer.md); the freeze otherwise stays. It changes M8 of
journal 0221. Estimated effort: S (hours). Estimate, not labor time.

## Context

- The layer reads the lower's security labels under the mounter's credentials (journal 0104).
  When a Smack-labelled mounter without `CAP_MAC_ADMIN` may not read a file's label, the layer
  inode keeps the mount's default label, which is the mounter's (`smack_set_mnt_opts()`).
- `stat` checks the caller against the layer inode (`security_inode_getattr()` in
  `vfs_getattr()`). The layer's `moobl_getattr()` then read the lower with
  `vfs_getattr_nosec()`: since M8 (journal 0221) it followed `ovl_do_getattr()`, which skips the
  lower's hook for a call marked `AT_GETATTR_NOSEC`, and in Linux 6.12 every call from the VFS
  carries that flag (`fs/stat.c`); 7.0 and 7.3 have no such flag and the layer called
  `vfs_getattr_nosec()` directly.
- So the lower's decision for the caller was never asked for `getattr`, and the layer's own
  decision rested on a label the layer could not read. A reader allowed the mounter's label could
  `stat` a lower file the lower refuses it (mode, owner, size, times). Reads, writes and opens
  stayed refused: `moobl_permission()` checks the caller against the lower inode (ADR-0011,
  ADR-0037). Overlayfs mounted the same way refuses the `stat`. Found by the Smack run of
  security roadmap S12 (journal 0298, where the check was held back until this fix).

## Decision

`moobl_lower_getattr()` calls `vfs_getattr()` on the lower path, so `security_inode_getattr()`
runs on the lower inode with the caller's credentials on every supported kernel; on 6.12 the
`AT_GETATTR_NOSEC` flag is cleared first, because `vfs_getattr()` refuses it. A refusal is
returned as is. Linux 6.12.116, 7.0.14 and 7.3.4 have the same `vfs_getattr()`: the hook, then
`vfs_getattr_nosec()`.

## Alternatives

1. **Fail the lookup when the label cannot be read**, as overlayfs ends up doing. It closes the
   `stat` too, but turns every file the mounter may not label into an error for every caller,
   including those the lower allows, and depends on how each LSM reports a refused label read.
2. **Read the label with the caller's credentials.** The inode is shared by all callers; its
   label cannot depend on who looked first.
3. **Accept as a limit** (give the mounter read access to every label in the lower). Not taken:
   the layer would show less isolation than the lower and than overlayfs.

## Consequences

- One more LSM hook per `stat` through the layer.
- `stat` through the layer refuses what the lower refuses the caller, also with root as the
  mounter, where nothing differed before.
- A kernel-internal caller that came through `vfs_getattr_nosec()` now meets the lower's hook
  with its own credentials. None of the suite's steps depends on the old behavior (journal 0299).
- The other paths that give lower metadata to the caller are audited in journal 0299.
