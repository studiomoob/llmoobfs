# 0218 — Write credentials carry CAP_FSETID only when the writer has it (GHSA-6xf4-r246-92r6)

Date: 2026-10-27. Status: on the branch `marcelometal/write-creds-fsetid` of the advisory's
temporary private fork; embargoed until the advisory is published. Work started 2026-10-27 about
15:00 America/Sao_Paulo (reproduction, patch committed 15:59, report sent 16:11 UTC); resumed
20:56 for the rebase on `main`, these documents and the re-run; ended 21:27. Wall-clock time,
not an effort measurement. Estimated effort: S.

*Note, 2026-10-28:* published with the merge of security batch 2 (journal 0219); the embargo stated above ended then.

## Goal and decision

Private report GHSA-6xf4-r246-92r6, severity low, written by the author of this branch: a file
given set-user-ID or set-group-ID on the lower, behind the layer (ADR-0011's A2), kept the bit
after an ordinary user wrote to it through the layer, where the same write on the lower clears
it. Two causes act together:

- The write credentials (`moobl_wcred_get()`, F4) are the mounter's with the writer's fsuid,
  fsgid and groups, and they kept the mounter's `CAP_FSETID`. The lower decides whether to clear
  the bits with `setattr_should_drop_suidgid()` (`fs/attr.c:63`, Linux 6.12.111), which keeps
  them while `capable(CAP_FSETID)` holds for the current credentials (`:75`); under those
  credentials it never cleared them.
- The layer's own `file_remove_privs()` call before each delegated write judges the layer's
  inode, whose mode may be the one from before a chmod made on the lower: an RCU-walk
  revalidation copies nothing, and after one write that found no bit the VFS marks the inode
  `S_NOSEC` (`inode_has_no_xattr()`, on the `SB_NOSEC` superblock of `moobl_fill_super()`), after
  which `file_remove_privs_flags()` returns at once (`fs/inode.c:2179`).

Decision: the write credentials carry `CAP_FSETID` only when the writer has it in the initial
user namespace (`ns_capable_noaudit(&init_user_ns, CAP_FSETID)`), the test the lower applies
(`capable()` is `ns_capable(&init_user_ns, ...)`). The lower then decides as it would for the
writer directly, for a write, a splice, `fallocate`, a clone and `copy_file_range`, which all
take their credentials from `moobl_wcred_get()`. It is the lesson recorded with F5, applied to the
credentials themselves: every operation delegated with the mounter's credentials is checked for
decisions the lower takes from the caller's capabilities. `SB_NOSEC` stays; the layer's early
return no longer matters once the lower performs the check under the writer's capability.

Alternatives not taken: dropping `SB_NOSEC`, which costs one `dentry_needs_remove_privs()` per
write and still judges a mode that may be stale; clearing `S_NOSEC` in `moobl_copy_attr()` when
the lower mode carries a bit, which repairs the cache only when a revalidation copies, and the RCU
walk does not; keeping the capability and clearing the bits on the lower from the layer with a
`notify_change()`, a second rule to keep equal to the lower's.

Prior art: overlayfs prepares the credentials of a creation from the mounter's and adjusts them
for what the lower will decide (`ovl_setup_cred_for_create()`, `fs/overlayfs/dir.c:556`), the
model the F4 credentials followed; the layer already lowers `CAP_SYS_RESOURCE` on the mounter's
credentials at mount (`layer/main.c`, `moobl_fill_super()`). References in `docs/references.md`
§18.

## Changes

- `layer/main.c`: `moobl_wcred_fits()` replaces `moobl_same_id()` and also compares the effective
  `CAP_FSETID`; `moobl_wcred_get()` decides whether the writer keeps it and lowers it on a new
  credential set, so a cached set is reused only when it matches on the capability as well.
- `tests/layer.c`: the bit is set on the lower behind the layer, with and without an earlier write
  through the layer (which sets `S_NOSEC`), before a write, `copy_file_range`, a clone (where the
  lower supports it) and `fallocate` as uid 1001; the lower mode must be `0666` after each. It
  runs inside the `ext4`, `btrfs` and `xfs` steps of the suite.
- `docs/testing.md` (the `LAYER_PASSTHROUGH_TESTS_PASS` row), ADR-0011 (finding F16 and a dated
  note on F4), a dated note on R-MOUNT-2 in `docs/research-stacked-fs-kernel-rules.md`,
  `docs/references.md` §18, `CHANGELOG.md` and the history index.

## Evidence

- 2026-10-27 afternoon, on `0f5850b`, recorded in the advisory: the new check failed on the
  unfixed module (`FAIL line 714`, ext4); with the patch the complete `make vm-test` (Debian
  6.12.111 guest, Docker) passed in 8 minutes, ending in `MOOBFS_VM_PASS`,
  `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`, with `LAYER_RESERVE_PASS` for the F4 reserve
  check that uses the same credentials.
- After the rebase on `5fa567b` (`main` with pull requests 161 to 165): `make checkpatch`,
  nothing reported.
- Negative round after the rebase, `layer/main.c` of `5fa567b` with the new test,
  `MOOB_ONLY="ext4 btrfs xfs reserve" make vm-test`: `FAIL line 714: !stat(lo("suidlow"), &lst)
  && (lst.st_mode & 07777) == 0666`, `MOOBFS_VM_FAIL: layer-tests-ext4`.
- The same steps with the fix: `LAYER_PASSTHROUGH_TESTS_PASS` and `LAYER_LOWER_PASS` on ext4,
  btrfs and xfs, `LAYER_RESERVE_PASS`, `MOOBFS_ONLY_PASS`.
- The complete `make vm-test` at the fix commit (Debian 6.12.111 guest, Docker, TCG), 10 minutes:
  `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`. The `pylib` step printed
  `PYLIB_SKIPPED` (no `python3` in that guest).
- Not run: the Debian 6.12.113 guest and the KASAN/lockdep suite; this host has no cached KASAN
  kernel. The change adds no lock and keeps the `get_cred()`/`put_cred()` pairs of
  `moobl_wcred_get()`, so it does not touch locking or object lifetime; the batch's full KASAN
  run covers it before publication.
- *Note, 2026-10-28:* that run passed (`MOOBFS_KASAN_PASS`, no kernel diagnostic), with a
  negative round of the new check on the module before the fix; see journal 0219, run 5.

## Limitations

- A writer with `CAP_FSETID` only in a user namespace that maps a file's owner and group, and
  not in that group, keeps set-group-ID (without group execute) when it writes the file on the
  lower directly (`capable_wrt_inode_uidgid()` in `in_group_or_capable()`, `fs/attr.c:42`), and
  now loses it through the layer, whose credentials belong to the initial namespace. The safe
  direction.
- The test covers a write, `copy_file_range`, a clone (btrfs and XFS) and `fallocate`. A splice
  takes the same credentials and is not exercised on its own.
- Numbering follows the private branch `sec/batch2` of the other advisories (journals 0215 and
  0216, finding F15, references §16 and §17), so this branch and that one touch the same lines of
  the changelog, the history index, ADR-0011 and `docs/references.md`.

## Next step

The maintainer reviews the branch in the advisory's temporary fork and merges it with the batch;
the advisory is published once the fix is in `main` (`SECURITY.md`).
