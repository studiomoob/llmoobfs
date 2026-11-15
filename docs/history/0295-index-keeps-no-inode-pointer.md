# 0295 — The index keeps no pointer to layer inodes: issue #117 closed in the kernel (ADR-0038)

Date: 2026-11-14. Work started about 21:45 Brasília time. Status: **in progress; runs on the test
host.** Estimated effort M (ADR-0038). The time recorded is calendar time; most of it is machine
time on the test host.

## Goal

Remove the remainder of issue #117 (finding M2 of journal 0155). Since journal 0223, a layer
inode with an index record still took the mount-wide index lock for writing when it was
instantiated and when it was evicted, and eviction runs in memory reclaim. Journal 0283 made this
a documented limit of stage 2, removable only with a kernel change. The maintainer authorized that
change as an exception to the freeze on 2026-11-14 (ADR-0038).

## Prior art and sources read

Linux 6.12.116 (the KASAN pin), 7.0.14 and 7.3.4, read on the test host:

- `fs/inode.c`:
  - `find_inode()` walks a hash chain under RCU and calls the test before it checks
    `I_FREEING`/`I_WILL_FREE`; it waits for an inode being freed and then walks again.
  - `ilookup5()` also waits for an inode being made (`I_NEW`). Its signature is the same in the
    three trees (the hash value is a `u64` in 7.3.4). `ilookup5_nowait()` has an extra `isnew`
    argument in 7.0.14 and 7.3.4, so the layer does not use it.
  - `evict()` calls `->evict_inode()` before `remove_inode_hash()`, so an inode being evicted is
    still in the hash after it has dropped its lower inode.
  - Inodes are freed after an RCU grace period (`i_callback()`).
- `fs/fuse/inode.c`, `fuse_ilookup()`: a cached inode found by node id with `ilookup5()` from
  outside the VFS (reverse invalidation). The model for finding a layer inode from a record.
- `fs/btrfs/inode.c`, `btrfs_iget_locked()` and `btrfs_find_actor()`: an inode hash keyed by
  number and subvolume root, with a test on those fields of the inode itself.
- `fs/overlayfs/inode.c`, `ovl_lookup_inode()`: the layer's previous model, keyed by the real
  inode's address. A record does not hold that address (journal 0223, alternatives).
- `include/linux/rcupdate.h`, `kfree_rcu()`. `Documentation/core-api/xarray.rst`: `xa_load()`
  takes the RCU read lock; "you can also use RCU to avoid dereferencing freed memory" when an
  object found without the xarray's lock is read.

## Design

ADR-0038 has the decision and the alternatives. In short:

- The layer's inode hash is keyed by the lower inode number. The test that finds an inode for a
  lower inode still compares pointers.
- `result_path()` finds the cached layer inode by number and generation, both fields of the layer
  inode, which copies `i_generation` when it is made. The reference is dropped after the index
  lock is released, as the dentry reference already was.
- `moobl_index_inode_evicted()` is gone; eviction does not touch the index.
- `moobl_index_inode_cached()` reads the record's generation under RCU and takes the lock only
  when it has to drop a removed file's record (GHSA-756j-cgwq-gvwm). Records are freed with
  `kfree_rcu()`.
- The scan no longer looks up the layer inode after storing a record, and `moobl_index_set()` no
  longer stores a pointer. The ordering argument of journal 0223 is no longer needed and is
  removed from the code.

**Why the lookup may sleep under the index lock.** `result_path()` runs with the index lock held
for reading, and `ilookup5()` may wait for an inode being made or freed. Neither waits for the
index lock: `moobl_get_inode()` takes it only after `unlock_new_inode()`, and only for a stale
record. Eviction now calls only `iput()` of the lower inode.

**The NOFS scope.** The index lock's NOFS scope dates from when eviction took the lock. It is
kept, with the lockdep annotation in `moobl_index_init()`. Reclaim still reaches the lock through
the shrinker, which takes it with a trylock. Removing the scope would be a separate change.

## Changes

- `layer/main.c`:
  - `iget5_locked()` hashes by `lower->i_ino`;
  - `moobl_inode_set()` copies `i_generation`;
  - `moobl_find_inode()` is replaced by `moobl_find_cached_inode(sb, ino, gen)`;
  - `moobl_evict_inode()` no longer calls the index.
- `layer/index.c`:
  - `struct moobl_ix_inode` has an `rcu_head` instead of `upper`; records are freed with
    `kfree_rcu()`;
  - `moobl_index_inode_cached()` is rewritten and `moobl_index_inode_evicted()` removed;
  - `result_path()` looks the inode up and returns it in `held_inode`; the query drops it after
    the lock;
  - the scan's lookup and `moobl_index_set()`'s store are removed.
- `layer/moobl.h`: the index keeps its superblock (`ix->sb`); prototypes.
- `vm/suite.sh`, step `ixlock`: a domain `rec` of 8 files with a record, built first, then
  evicted. Its files are instantiated during a third build and evicted during a fourth, each in
  under 1 s. After the four builds the kernel still gives the first answer for `rec`.
  `domain_builds=5`.
- `docs/decisions/0038-index-keeps-no-inode-pointer.md`.

## Evidence

On the test host (Debian 13, KVM), each run on a fresh clone of the branch:

- `make checkpatch` on `2932243`: `checkpatch.pl: OK`, 21:55.

To be completed: the full `make vm-test`, negative round A (the `layer/` of `cdf36b1` with the new
step), negative round B (the lookup always misses), and the touched steps under KASAN.

## Limitations

To be completed with the runs.

## Next step

The runs above; then the surfaces that state the remainder of issue 117 as a limit, and the issue.
