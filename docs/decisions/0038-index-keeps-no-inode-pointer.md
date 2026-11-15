# ADR-0038 — A fourth narrow exception to the kernel freeze: the index keeps no pointer to layer inodes (issue #117)

Date: 2026-11-14. Status: **accepted** by the maintainer on 2026-11-14, about 21:45 Brasília
time (journal 0295). Amends [ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md) by
one exception, beside those of [ADR-0033](0033-s16-freeze-exception.md),
[ADR-0034](0034-kernel-compat-freeze-exception.md) and
[ADR-0037](0037-open-lower-files-as-the-mounter.md); the freeze otherwise stays. It changes the
record lifetime rules that journal 0223 relied on. Estimated effort: M (one to three days).
Estimate, not labor time.

## Context

Issue #117 (finding M2 of journal 0155): instantiation and eviction of a layer inode took the
mount-wide index lock (`ix->lock`) for writing. Eviction runs in memory reclaim, so a reclaimer
waited behind any index writer: a domain build, a release, a write path. Journal 0223 removed the
lock for inodes without an index record. An inode with a record still took it on both paths,
because a record held a raw pointer to the cached layer inode (`rec->upper`) that eviction had to
clear before the inode was freed. Journal 0283 made that remainder a documented limit of stage 2,
to be removed only with a kernel change. The maintainer authorized that change on 2026-11-14.

`rec->upper` had one reader: `result_path()`, which names a query result from the dentry cache
(`d_find_alias()`) when the inode is in memory, and falls back to the name recorded at the scan.
Journal 0223 listed what it would take to remove the pointer. The layer's inode hash was keyed by
the lower inode's address, which a record does not hold. So the options were a second hash, a
reference to the lower inode kept in each record, or the lower filesystem's own cache.

## Decision

1. **The layer's inode hash is keyed by the lower inode number.** `iget5_locked()` gets
   `lower->i_ino` as its hash value. Its test still compares the lower inode pointer, so which
   inode a lookup by dentry returns is unchanged. The layer inode copies the lower inode's
   `i_generation` when it is made.
2. **A record holds no pointer to a layer inode.** `result_path()` finds the cached layer inode
   with `ilookup5()` by number. Its test compares the layer inode's own `i_ino` and
   `i_generation` with the record's number and generation. `find_inode()` calls the test before
   it checks `I_FREEING`, and an inode being evicted has already dropped its lower inode, so the
   test reads no lower field. The query drops the inode reference after the index lock is
   released, like the dentry it already held. This is how FUSE finds an inode from a node id
   outside the VFS (`fuse_ilookup()`), and how btrfs keys its inode hash by number and subvolume
   root (`btrfs_iget_locked()`, `btrfs_inode_hash()`).
3. **Eviction does not touch the index.** `moobl_index_inode_evicted()` is removed.
4. **Instantiation takes the index lock only to drop a removed file's record.** That is the
   GHSA-756j-cgwq-gvwm case: a record whose generation differs from the lower inode's. It reads
   the record's generation without the lock, under RCU. Records are now freed with
   `kfree_rcu()`, and a record's generation never changes after the record is stored.
5. **No change to the UAPI, the on-disk format, the mount options or the answers of a query.**
   A record grows by 8 bytes (an `rcu_head` instead of a pointer), which the index budget
   charges.
6. **The rules for a kernel correction apply** (`AGENTS.md`). Those are `make checkpatch`, the
   suite steps in a VM, a negative round showing that the regression test fails without the
   change, and the touched steps under KASAN/lockdep, since the change touches object lifetime.
   The test is the step `ixlock`, extended to inodes with a record.

Anything else in the module stays frozen under ADR-0022.

## Alternatives

- **Keep the documented limit** (journal 0283). Not taken: the maintainer chose to remove it
  before the tag.
- **A reference to the lower inode in each record.** It would pin a lower inode, and its pages'
  owner, for every indexed file. Not taken.
- **A second hash keyed by number**, beside the layer's own. It adds state for what the inode
  cache already does once its key is the number. Not taken.
- **A spinlock for `rec->upper`, or freeing records with RCU and clearing the pointer under
  RCU.** Each keeps a pointer that eviction must clear, so eviction still has to find the record.
  Not taken.

## Consequences

- No index lock in the reclaim path; the step `ixlock` measures it for inodes with and without a
  record.
- Naming a result costs one lookup in the inode hash (`inode_hash_lock`) per candidate, where it
  cost one pointer test before.
- An inode whose lower generation changed after it was made (`FS_IOC_SETVERSION` on ext4) is not
  found by the lookup, and its result is named from the recorded name instead. Two files with the
  same number and generation across btrfs subvolumes may be confused, as the index, keyed by
  number, already confuses them.
- Issue #117 can be closed. The surfaces that state its remainder as a limit change in the same
  unit of work.
