# 0295 — The index keeps no pointer to layer inodes: issue #117 closed in the kernel (ADR-0038)

Date: 2026-11-14. Work started about 21:45 Brasília time and ended at 00:02 on 2026-11-15.
Status: **done.** Estimated effort M (ADR-0038); about 2 h 15 min of calendar time, most of it
machine time on the test host.

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
- `vm/suite.sh`, new step `dcname` (in the full suite; `docs/testing.md`): on ext4, on a mount
  without domains, a file with two hard links is built once. The layer's caches are then dropped
  and only the name the build did not record is looked up through the layer. The mount-wide
  `QUERY` must name the file by that name, from the dentry cache.
- `docs/decisions/0038-index-keeps-no-inode-pointer.md`.
- `tests/static/baseline.txt`: the justified Coccinelle entry quotes the new line.

## Evidence

On the test host (Debian 13, KVM), each run on a fresh clone of the branch:

| Run | Tree | Times | Result |
| --- | --- | --- | --- |
| `make checkpatch` | `2932243` | 21:55 | `checkpatch.pl: OK` |
| `MOOB_VM_KVM=1 make vm-test` (full) | `2932243` | 21:55–22:17 | `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`; `IXLOCK_PASS: ... 8 inodes without a record instantiated in 1 ms and evicted (drop_caches) in 1 ms, 8 with a record in 1 ms and 1 ms` |
| negative round A: `MOOB_ONLY=ixlock`, with the `layer/` of `cdf36b1` | `2932243` | 22:17–22:19 | `stat_ms=0`, `drop_ms=0` without a record; `stat_ms=6670`, `drop_ms=6664` with one; `MOOBFS_VM_FAIL: il-instantiate-record-waited (6670 ms for 8 files with a record)` |
| negative round B: full `make vm-test`, `moobl_find_cached_inode()` always `NULL` | `2932243` | 22:19–22:40 | all three markers **pass**: no step covered naming from the dentry cache (see below) |
| `MOOB_ONLY=dcname` | `6f1a1a7` | 22:47–22:48 | `DCNAME_PASS: recorded name d/one; with only d/two in the dentry cache, the answer named d/two` |
| same, with the `layer/` of `cdf36b1` | `6f1a1a7` | 22:48–22:49 | `DCNAME_PASS` (the old code named it through `rec->upper`) |
| same, lookup always `NULL` | `6f1a1a7` | 22:49–22:50 | `MOOBFS_VM_FAIL: dn-not-from-dentry-cache (recorded d/two, in memory d/one)` |
| full KASAN/UBSAN/lockdep suite (Linux 6.12.116) | `2932243` | 22:40–23:54 | `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with no kernel diagnostics`, with `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_PYLIB_PASS` |
| pre-push checks (lint, checkpatch, test, asan, hostile, pylib, retired, pins, matrix, mutants, site, diffcheck, ubuntu-tests) | `27ba1f4` | 23:54–00:00 | all `PASS` |
| `MOOB_ONLY="dcname ixlock"` under KASAN/UBSAN/lockdep | `27ba1f4` | 00:00–00:01 | `MOOBFS_KASAN_ONLY_PASS: steps dcname ixlock ... with no kernel diagnostics` |

Negative round A shows that the extended step fails on the old code, on the case it adds, and
still passes there on the case of journal 0223.

Negative round B found a gap that predates this change. No step checked that a result whose
inode is in memory is named from the dentry cache, so a lookup that never finds the inode passed
the whole suite. Before this change the same path went through `rec->upper`, also without a
test. The step `dcname` closes the gap: it passes on the new and on the old code, and fails when
the lookup never finds.

The full KASAN run predates the step `dcname`; that step and the extended `ixlock` ran under
KASAN on the tip of the branch.

## Errors found on the way

- The first form of `dcname` put the linked file in a domain. A linked regular file is left out
  of a domain (`target_ok()`: another name may live in another domain), so the first query
  answered nothing, and the step failed with `dn-first-answer` in all three rounds (22:41–22:45).
  The step now uses a mount without domains, where the mount-wide `QUERY` answers the file.
- A rerun in the same clone directories stopped at once: the earlier runs had left root-owned
  build files there. It was repeated in new directories.
- The static-analysis job of pull request #204 failed on its first run. The justified
  Coccinelle false positive in `tests/static/baseline.txt` (journal 0242: the alias reference
  is dropped by the query after the lock) quoted the old line `d_find_alias(rec->upper)`. It
  is now quoted as `d_find_alias(upper)`, with the same justification. The pre-push checks do
  not run `make static`, so this was found only in CI.

## Limitations

- Measured on ext4 only, as in journal 0223. The `ext4`, `btrfs` and `xfs` steps exercise the
  changed paths on all three lowers.
- The `ixlock` step depends on a 1 s limit; a guest so slow that 8 `stat` calls take a second
  fails it without a lock problem (journal 0280 on s390x under KASAN).
- Naming a result now takes `inode_hash_lock` once per candidate. No measurement of that cost
  was made.
- An inode whose lower generation changed after it was made is named from the recorded name
  (ADR-0038, consequences). No test makes that case.
- The reproduction call of issue #195 is pinned to `d96b0db`, and rule 3 of journal 0284 counts
  a reproduction there only if `layer/` does not change before the tag. `layer/` already changed
  on 2026-11-12 (`3179b63`, ADR-0037), and changes again here. The pin was not updated after
  ADR-0037; that is for the maintainer to decide (next step).

## Surfaces brought up to date

`docs/status.md` and `docs/status.pt-BR.md`, `docs/when-not-to-use.md` (§2 sentence; the §3
bullet removed), `docs/matrix-and-limits.md` (the issue-117 limit removed),
`docs/review-findings.md` (M2 fixed), `docs/security-roadmap.md` (dated update), `ROADMAP.md`,
dated notes on `docs/distribution-strategy.md` §8, ADR-0022 and ADR-0030, `docs/testing.md`,
`CHANGELOG.md`, `docs/references.md` §66, `scripts/site-journey.py` (a milestone of 2026-11-14,
in both languages, which also names journal 0294) and the pages it writes, and
`docs/retired-claims.txt`. The issue #195 body and `docs/help-wanted.md` keep the pin
`d96b0db` until the maintainer decides (limitations).

## Next step

The batch's pull request. After the merge,
close issue #117 and, if the maintainer agrees, move the reproduction pin of issue #195 to the
merge commit.
