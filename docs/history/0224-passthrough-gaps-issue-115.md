# 0224 — Passthrough gaps: fadvise, statx fields, drop_inode, F_SETFL O_NOATIME (issue #115)

Date: 2026-10-28. Status: implemented on branch `b3/d` (security and solidity batch 3);
targeted VM steps, negative round and targeted KASAN/lockdep recorded below; the full suite and
the full KASAN run are the batch's. Work started 2026-10-28 about 09:00 America/Sao_Paulo and
ended about 10:05 (VM runs at 09:26–10:01). Wall-clock time, not an effort measurement. Estimated effort: S.

## Goal and decision

Issue #115 holds four gaps that round 2 (journal 0155, priority 3) found by comparing the layer
with overlayfs and ecryptfs at 6.12. Under ADR-0022 the kernel takes only fixes, so each is
treated as "the layer behaves as the lower would"; none adds an interface, a mount option or a
format change.

- **V1, fadvise.** `moobl_file_fops` had no `.fadvise`, so `generic_fadvise()` acted on the
  layer's mapping, which never holds a page. `moobl_fadvise()` forwards `vfs_fadvise()` to the
  backing file under the mounter's credentials, as `ovl_fadvise()` does. `readahead(2)` goes
  through the same hook (`ksys_readahead()` calls `vfs_fadvise(..., POSIX_FADV_WILLNEED)`), so it
  is fixed too. `cachestat(2)` reads the descriptor's own `f_mapping` and has no hook: it stays a
  limitation, as in overlayfs.
- **V3, statx.** `moobl_getattr()` builds the answer from the layer inode and copied only
  `blocks`, `blksize`, `btime` and four attribute bits from the lower. It now also copies
  `STATX_DIOALIGN` (`dio_mem_align`, `dio_offset_align`), `STATX_WRITE_ATOMIC` (the three
  `atomic_write_*` fields), `STATX_SUBVOL`, and the `ENCRYPTED`, `VERITY` and `DAX` attributes
  (`MOOBL_STATX_ATTRS`), each only when the lower reported it. Data and `O_DIRECT` reach the
  lower file, so its alignment and atomic-write limits are the ones that hold. overlayfs starts
  from the lower's kstat instead; rebuilding `moobl_getattr()` that way would rewrite the lines
  another branch of this batch changes, for the same result on these fields.
- **V5, drop_inode.** With the default `drop_inode`, a layer inode without references stays in
  the inode cache and keeps its reference on the lower inode, so a file removed behind the layer
  keeps its blocks until memory reclaim evicts the layer inode. The layer's `i_nlink` is not
  refreshed by revalidation (it changes only under the parent's lock, see `moobl_copy_attr()`),
  so `generic_drop_inode()` cannot see the removal. `moobl_drop_inode()` returns
  `generic_drop_inode(inode)` or "the lower inode has no link left". The issue recorded this as a
  design decision because overlayfs's `generic_delete_inode` would change when the index learns
  of evictions. The narrower rule avoids that: a linked inode keeps the default caching, and the
  index's link to a cached inode (`rec->upper`, used by `result_path()` only through
  `d_find_alias()`) is lost only for inodes that have no dentry, which `d_find_alias()` could not
  use anyway. The new eviction point is the `dput()` of a layer dentry whose revalidation failed,
  in ref-walk without locks or in `lookup_slow()` under the parent's shared `i_rwsem`. Eviction
  takes the index write lock (`moobl_index_inode_evicted()`); the order `i_rwsem` then index lock
  already exists in every name operation (`moobl_unlink()` calls `moobl_index_drop_inode()` under
  the VFS's parent lock), and eviction already ran from reclaim and from the last `iput` of a
  file unlinked through the layer. The index's own `dput()` calls of layer dentries run with its
  lock released (`place_dentry_above()`, `caller_may_list_above()`, the `held` dentries of
  `result_path()`). lockdep ran over the steps (Evidence).
- **V7, F_SETFL.** Journal 0149 already synchronized `O_APPEND`, `O_NONBLOCK`, `O_NDELAY` and
  `O_DIRECT` (overlayfs's `OVL_SETFL_MASK`) before every delegated read, write, splice and mmap;
  the issue's mention of `O_DIRECT` and `O_NONBLOCK` predates that integration. The remaining bit
  was `O_NOATIME`: set after open it stopped only the layer's own `touch_atime`, while the lower
  file, whose mount decides access times through the layer (journal 0079, section 3), kept
  updating them. `MOOBL_SETFL_MASK` now includes `O_NOATIME`. The permission is the one open
  already relies on: `setfl()` checks ownership on the layer inode (whose owner is the lower's)
  before setting the bit, and `moobl_open()` passes an opened-with `O_NOATIME` to the backing file
  after the same VFS check. overlayfs does not synchronize `O_NOATIME` (its overlay mount decides
  access times itself); the layer differs because its lower decides them.

No item was judged a new feature.

## Alternatives

- V3: start from the lower's kstat and overwrite `dev`, `ino`, `nlink`, `mode` and owners
  (overlayfs). Equivalent for the fields at issue, larger, and in conflict with the
  `AT_GETATTR_NOSEC` change of another branch of this batch.
- V5: `generic_delete_inode` (overlayfs, FUSE). Frees as well, but evicts every inode that loses
  its last dentry, and with it the index's link to it and the attribute copy; not needed for the
  defect.
- V7: refuse `O_NOATIME` changes in `.check_flags`. Rejected: `check_flags` receives only the
  flags, so it cannot tell a change from the open-time state, and refusing a call the lower
  accepts is a behavior difference of its own.

## Changes

- `layer/main.c`: `MOOBL_SETFL_MASK` adds `O_NOATIME`; `moobl_drop_inode()` in `moobl_sops`;
  `MOOBL_STATX_ATTRS` adds three attributes and `moobl_statx_lower()` copies the three field
  groups, called at the end of `moobl_getattr()`; `moobl_fadvise()` in `moobl_file_fops`.
- `tests/passthrough.c` (new), built by `vm/build-tests.sh`, run in `run_lower` of `vm/suite.sh`
  after `fcntl-flags` for ext4, btrfs and XFS. Each case runs and prints its marker; the program
  prints `PASSTHROUGH_PASS` only when all passed, and removes its files.
- `docs/testing.md` (the `PASSTHROUGH_PASS` row), `docs/references.md` §23, `CHANGELOG.md`, the
  history index.

## Evidence

All runs on the Linux test host (Debian 13, KVM), from a bundle of this branch.

- `make checkpatch`: nothing reported (`checkpatch.pl: OK`).
- Fixed code (`8c2cbfa` with the test at `a321115`), `MOOB_VM_KVM=1 MOOB_ONLY="ext4 btrfs xfs"
  make vm-test` (Debian 6.12.111 guest): every case passed on the three lowers, then
  `LAYER_LOWER_PASS` for each and `MOOBFS_ONLY_PASS: steps ext4 btrfs xfs (not a complete run)`.
  Measured values:

  ```text
  ext4   PT_FADVISE pages=4096 resident_before=4096 after_dontneed=0
         PT_READAHEAD pages=4096 resident_before=0 after_readahead=320
         PT_STATX layer mask=0x2000 dio=512/512 subvol=0 awu=0/0/0 attrs=0/0x300800  (= lower)
         PT_DROP_INODE freed=33554432 of 33554432 bytes
         PT_NOATIME lower_atime set=1000 cleared=1793190599 (planted 1000)
  btrfs  PT_STATX layer mask=0x8000 dio=0/0 subvol=5 awu=0/0/0 attrs=0/0x200000  (= lower)
         PT_READAHEAD ... after_readahead=1024; PT_DROP_INODE freed=33652736
  xfs    PT_STATX layer mask=0x2000 dio=512/512 subvol=0 awu=0/0/0 attrs=0/0x200000  (= lower)
  PASSTHROUGH_PASS (three times)
  ```

- Negative round: the same test with `layer/main.c` of `e68da30` (branch `neg/b3d` on the host
  only), the same command. Every case failed on ext4 and the step stopped there:

  ```text
  PT_FADVISE pages=4096 resident_before=4096 after_dontneed=4096
  PT_FADVISE_FAIL
  PT_READAHEAD pages=4096 resident_before=0 after_readahead=0
  PT_READAHEAD_FAIL
  PT_STATX lower mask=0x2000 dio=512/512 subvol=0 awu=0/0/0 attrs=0/0x300800
  PT_STATX layer mask=0 dio=0/0 subvol=0 awu=0/0/0 attrs=0/0x200000
  PT_STATX_FAIL
  PT_DROP_INODE freed=0 of 33554432 bytes
  PT_DROP_INODE_FAIL
  PT_NOATIME lower_atime set=1793191396 cleared=1793191396 (planted 1000)
  PT_NOATIME_FAIL
  MOOBFS_VM_FAIL: passthrough-ext4
  ```

- KASAN, UBSAN and lockdep on the fixed code (drop_inode changes when layer inodes are evicted):
  `MOOB_ONLY="ext4 btrfs xfs" sh vm/kasan-local.sh` (Linux 6.12.112, `vm/kasan.config`):
  `LAYER_LOWER_PASS` for ext4, btrfs and xfs, `MOOBFS_ONLY_PASS`, `MOOBFS_KASAN_ONLY_PASS: steps
  ext4 btrfs xfs under KASAN, UBSAN and lockdep with no kernel diagnostics (not a complete run)`.
  `PASSTHROUGH_PASS` appears in the captured console for btrfs and XFS; the ext4 lines are cut by
  interleaved output after `PT_STATX`, and ext4's `LAYER_LOWER_PASS`, which the suite prints only
  after `passthrough` succeeded, is present.
- Not run here: the full `make vm-test` and the full KASAN suite (the batch's).

## Errors found on the way

- The first VM round measured readahead in percent of a 16 MiB file and asked for 20 %:
  `readahead(2)` reads at most the device's readahead window, and the guest showed 7 %
  (`PT_READAHEAD resident_before=0% after_readahead=7%`, `PT_READAHEAD_FAIL`, with the fix in
  place). The test now counts pages and asks for 16 pages; the other three cases passed in that
  round.

## Limitations

- `cachestat(2)` on a layer descriptor still reports the layer's empty mapping (no hook in 6.12,
  same in overlayfs).
- `STATX_WRITE_ATOMIC`, encryption, verity and DAX are copied but none of the suite's lowers
  reports them on 6.12, so the test compares them as absent on both sides; ext4's and XFS's
  `STATX_DIOALIGN` and btrfs's `STATX_SUBVOL` are the fields actually exercised.
- V5 frees the space when the layer drops its name for the removed file: a lookup or walk
  through the layer that fails revalidation, or dentry reclaim. A name nobody looks up again
  keeps the dentry, and through it the lower inode, until reclaim (overlayfs has the same).
- `O_NOATIME` by `F_SETFL` is checked against the layer inode's owner, which is copied from the
  lower and may lag a `chown` made behind the layer until the next attribute refresh; the same
  holds for `O_NOATIME` at open.

## Next step

The batch's full `make vm-test` and KASAN/lockdep suite; then close issue #115 with this
journal once the batch is merged (`cachestat` stays a documented limitation).
