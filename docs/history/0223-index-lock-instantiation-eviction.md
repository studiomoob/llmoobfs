# 0223 — Instantiation and eviction take the index lock only for inodes with a record (issue #117)

Date: 2026-10-28. Status: implemented on the local branch `b3/c` (security and solidity batch 3);
not published. Work started 2026-10-28 about 09:15 America/Sao_Paulo; ended 2026-10-28 10:25. Wall-clock
time, not an effort measurement. Estimated effort: S.

## Goal and decision

Issue #117 (finding M2 of journal 0155): `moobl_index_inode_cached()` and
`moobl_index_inode_evicted()` took the mount-wide `ix->lock` for writing on every instantiation
and every eviction of a layer inode, even when the inode had no index record. Eviction runs in
memory reclaim, so a reclaimer evicting any layer inode waited behind any index writer (a domain
build storing a file, a release, a write path). Most layer inodes have no record: directories,
files without typed attributes, and every file of a domain nobody has queried.

What the lock protects there: the raw pointer `rec->upper` of a `struct moobl_ix_inode` (the
cached layer inode, or `NULL`), which `result_path()` follows with `d_find_alias()` under the
index lock to name a result from the dentry cache. Instantiation links the new inode into an
existing record (and drops a removed file's record whose generation differs, GHSA-756j); eviction
clears the pointer before the inode is freed. Both need the record; neither has anything to do
when there is none.

Decision: both functions first test `xa_load(&ix->inodes, inode->i_ino)` without the lock and
return when it finds no record. The result is only compared with `NULL`, never dereferenced:
records are freed with `kfree()`, not with RCU, and `xa_load()` itself takes the RCU read lock
(`lib/xarray.c:1466`). With a record the code is unchanged: the write lock, a second
`xa_load()` and the same updates. No UAPI, format or mount-option change (ADR-0022).

Why the early return is correct:

- **Eviction.** `rec->upper = inode` is stored only under the index lock, into a record that is
  in `ix->inodes` at that moment, by a task holding a reference to `inode`: `moobl_get_inode()`
  (the caller's reference, `moobl_index_inode_cached()` runs before it returns),
  `moobl_index_set()` (the writer's dentry) and the scan in `index_load()` (the reference from
  `ilookup5()`, dropped with `iput()` after the unlock). That `iput()` is a value-returning atomic
  on `i_count`, fully ordered (`Documentation/atomic_t.txt:169`), and it precedes the final
  `iput()` that evicts (`fs/inode.c:1889`). The evicting task therefore sees the stored record,
  or its later removal from `ix->inodes`; a later removal frees the record and the pointer with
  it. No record at eviction means no record points at this inode.
- **Instantiation.** A scan may store a record just after the lockless look found none. The scan
  looks the layer inode up with `moobl_find_inode()` (`ilookup5()`) after storing the record,
  which was already the rule ("one of the two always makes the link", `index_load()`). Both that
  lookup (`ilookup5_nowait()`, `fs/inode.c:1559`) and the insertion of the new inode by
  `iget5_locked()` → `inode_insert5()` (`fs/inode.c:1263`) take `inode_hash_lock`, and the
  insertion precedes the lockless look in program order. Either the scan's lookup takes the hash
  lock after the insertion and finds the inode (it waits for `I_NEW`) and makes the link, or it
  took it before, and then the store of the record, which precedes the scan's lock acquisition,
  is visible to the instantiating task after its own acquisition (ACQUIRE/RELEASE,
  `Documentation/memory-barriers.txt:474`). `moobl_index_set()` stores the link itself. When
  `index_set_path()` fails the scan makes no lookup; that was already a missed link before this
  change (the result is then named from the recorded path), not a new one.
- **Unmount.** `kill_anon_super()` evicts every inode before `moobl_index_destroy()` runs; a
  lockless `xa_load()` of an emptied, destroyed xarray returns `NULL`.

Prior art (Linux 6.12.112): `ecryptfs_evict_inode()` (`fs/ecryptfs/super.c:119`) and
`ovl_destroy_inode()` (`fs/overlayfs/super.c:177`) take no filesystem-wide lock on eviction;
overlayfs keeps no pointer from shared state to its inodes and finds them by key with
`ilookup5()` when needed (`ovl_lookup_inode()`, `fs/overlayfs/inode.c:1102`). The xarray is meant
to be read without the user's lock under RCU (`Documentation/core-api/xarray.rst`, "Takes RCU
read lock: xa_load()"). References in `docs/references.md` §22.

## Alternatives

- **Remove `rec->upper` (the issue's suggestion), proposal only.** Keeping the link in the
  inode, or finding the inode by `ilookup5()` when a result needs a path, would take the lock
  out of eviction also for inodes that do have a record. The layer's inode hash is keyed by the
  lower inode's address, which a record does not hold (only number and generation): a lookup
  from the record would need a second hash keyed by number, a reference to the lower inode
  kept in the record (pinning lower inodes for every indexed file), or the lower filesystem's
  own cache, which XFS does not expose through the VFS hash. Each is a change of the record's
  lifetime rules under the freeze, against a cost that is now proportional to indexed inodes
  only. Not done; recorded here as the next step if a measurement on records ever shows the
  remaining cost matters. It would be architectural (an ADR).
- **A spinlock dedicated to `rec->upper`.** Eviction would still have to find the record, which
  `rec_drop()` frees under the index lock: either that spinlock also covers every free of a
  record, or the record is freed with RCU. More rules for the same gain on records; not done.
- **A read lock in eviction.** A query holds the index lock for reading while it follows
  `rec->upper`; an evictor under a read lock could clear the pointer and free the inode under
  it. Not correct.
- **Leave it.** The fix is two lockless tests with no new state and a short ordering argument
  over rules the code already followed; the risk is lower than the reclaim stall it removes.

## Changes

- `layer/index.c`: the early return in `moobl_index_inode_cached()` and
  `moobl_index_inode_evicted()`, with the ordering argument above them; the comment on
  `ix_write_lock()` says the lock is taken on eviction of an inode with a record. A test-only
  parameter `scan_hold_us` (0644, default 0) makes `index_load()` sleep with the index lock held
  for writing once per file, beside the existing `scan_delay_us`.
- `tests/ixlock.c` (built by `vm/build-tests.sh`): `ixlock stat <dir> <n>` times the
  instantiation of `n` files, `ixlock drop` times `drop_caches=2`.
- `vm/suite.sh` step `ixlock` (in the full suite): on ext4, domain depth 1, two domains `s1`, `s2`
  of 3 planted files each and a domain `plain` of 8 files without attributes, all made on the
  lower; `scan_hold_us=2500000`. One second into the build of `s1` the 8 files are instantiated,
  one second into the build of `s2` they are evicted; each must take under 1 s, each build must
  still be running when its measurement ends, and `domain_builds=2` after. Prints `IXLOCK_PASS`.
- `docs/testing.md` (step and marker), `docs/references.md` §22, `CHANGELOG.md`, the history
  index.

## Evidence

On the test host (Debian 13, KVM), branch `b3/c`, tree of commit `2ce11e7`:

- `make checkpatch`: exit 0, nothing reported.
- `MOOB_VM_KVM=1 MOOB_ONLY=ixlock make vm-test`: `ixlock: stat_ms=0`, `ixlock: drop_ms=0`,
  `IXLOCK_PASS: 8 inodes without a record instantiated in 0 ms and evicted (drop_caches) in 0 ms
  while builds held the index lock`, `MOOBFS_ONLY_PASS: steps ixlock (not a complete run)`.
- Negative round, both early returns removed (the old code, with the new parameter and test):
  `ixlock: stat_ms=6726`, `ixlock: drop_ms=6665`, `MOOBFS_VM_FAIL: il-instantiate-waited (6726 ms
  for 8 files without a record)`.
- Negative round, only the eviction's early return removed: `ixlock: stat_ms=0`,
  `ixlock: drop_ms=6675`, `MOOBFS_VM_FAIL: il-evict-waited (6675 ms to evict them)`. The two
  checks fail independently on the code each guards.
- `PATH=$HOME/.venv-vng/bin:$PATH MOOB_ONLY="ixlock buildrace release overbudget domainrecs
  domainreuse ext4 btrfs xfs" sh vm/kasan-local.sh` (6.12.112 with KASAN, UBSAN and lockdep):
  `ixlock: stat_ms=11`, `ixlock: drop_ms=91`, `IXLOCK_PASS`, `BUILD_RACE_PASS` on ext4, btrfs and
  xfs, `RELEASE_PASS`, `OVERBUDGET_PASS` on the three, `DOMAINRECS_PASS`, `DOMAINREUSE_PASS`,
  `LAYER_LOWER_PASS` on the three, ending in `MOOBFS_KASAN_ONLY_PASS: steps ixlock buildrace
  release overbudget domainrecs domainreuse ext4 btrfs xfs under KASAN, UBSAN and lockdep with no
  kernel diagnostics (not a complete run)`. These steps were not repeated in `make vm-test`
  (Debian guest) apart from `ixlock`.

This is the measurement before and after: the old code made instantiation and eviction of
inodes without a record wait for the whole of a build's lock holds (about 6.7 s for 8 inodes with
2.5 s holds); the new code does not wait (0 ms at millisecond resolution).

## Errors found on the way

- The first form of the step measured instantiation and eviction during one build with 500 ms
  holds. On the old code the instantiations used the whole build (`instantiate_ms=7194`) and the
  eviction ran after it; the step then failed on its window check, not on the time. A second
  form with two builds showed the eviction of a batch waiting only about twice (`drop_ms=1038`):
  between two holds the evictor often gets the lock back before the scan. The final form uses
  2.5 s holds and starts each measurement 1 s into its build, so any wait is at least 1.5 s
  against a 1 s limit.

## Limitations

- Instantiation and eviction of an inode that has a record still take the write lock; their
  number is bounded by the indexed files of built domains, not by the mount.
- The lockless test reads `ix->inodes` without the index lock; its correctness rests on the
  invariants stated in the comment (every store of `rec->upper` made by a holder of a reference,
  the scan's lookup after its store). A new writer of `rec->upper` must keep them.
- `scan_hold_us` sleeps with the index lock held, which R-LOCK-3 of
  `docs/research-stacked-fs-kernel-rules.md` forbids in the steady state; it is a test-only
  parameter, 0 by default, like `scan_delay_us`, and a mount where it is set is a test mount.
- The timing step depends on a 1 s limit; a guest so slow that 8 `stat` calls or a
  `drop_caches` take a second would fail it without a lock problem.
- Measured on ext4 only in the new step; the existing `ext4`, `btrfs` and `xfs` steps exercise
  the changed paths on all three lowers.

## Next step

The maintainer's full `make vm-test` and full KASAN/lockdep run for batch 3. The maintainer
decides whether this closes issue #117 (the case the issue names, "even when no record exists",
is fixed) or whether it stays open for inodes that have a record; the removal of `rec->upper`
stays a proposal (above), not a roadmap item.
