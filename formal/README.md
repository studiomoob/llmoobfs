# A TLA+ model of the index loader and the update hooks (security roadmap S21)

`IndexLoad.tla` models the on-demand build of one domain of the index
(`docs/spec-query-v2.md` §9) against the update hooks that run while it is built: the
registration of a loader in `index_load()`, its reading of the lower file with no lock held,
the retry when a hook marked it dirty, and the hooks of `layer/main.c` that change the index.
TLC, the TLA+ model checker, explores every interleaving of a bounded configuration.

Status, 2026-11-04: **proposal, model and tooling implemented; acceptance not met.** TLC finds
two violations in the configuration that models the code as it is (findings F1 and F2 below).
Both are completeness defects of the code, not modeling errors, and neither discloses anything.
The confidentiality property holds in every configuration. The kernel, the UAPI and the format
are frozen (ADR-0022), so the model was not changed to hide the findings. `make tla-check`
fails until they are fixed or a decision changes what the model must show.

## Running

```
make tla-check                        # every configuration against its expectation
sh formal/tlc.sh run IndexLoadSafety  # one configuration; TLC's own exit code
sh formal/tlc.sh fetch                # only download and verify the jar
```

`formal/tlc.sh` downloads `tla2tools.jar` of the tlaplus release `v1.7.4` (2024-08-05, the
latest release not marked prerelease on 2026-11-04; TLC 2.19) into `$TLA_CACHE` (default
`~/.cache/moobfs-tla`), checks its SHA-256
`936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88` and refuses any other
jar. It needs Java 11 or later and `curl`. CI runs the same target (`.github/workflows/tla.yml`,
Temurin 17). `TLA_WORKERS` sets TLC's worker threads (default `auto`).

## Configurations

All have one file, two typed attributes, three names, at most one extra hard link, three lower
operations by the writer, two readings per load (`MOOBL_LOAD_TRIES` is 16) and two build
attempts (`MOOBL_BUILD_TRIES` is 4). Results on 2026-11-04 (TLC 2.19, Java 17, 12 workers on
an Apple M-series laptop):

| Configuration | Expectation | Invariants | Result | Distinct states / depth / time |
| --- | --- | --- | --- | --- |
| `IndexLoad` | pass (S21 acceptance) | all four | **violated: `NoLostUpdate` (F1)** | trace of 13 states, under 1 s |
| `IndexLoadSafety` | pass | `TypeOK`, `NoStaleHomogeneity` | passed, state graph exhausted | 714,649 / 59 / 3 s (1,312,030 generated) |
| `IndexLoadNoLinks` | pass | `TypeOK`, `NoStaleHomogeneity`, `FreshPath` | passed, state graph exhausted | 71,478 / 47 / under 1 s (126,204 generated) |
| `IndexLoadNoRetry` | violation (S21 negative control) | as `IndexLoadSafety` | violated: `NoStaleHomogeneity` | trace of 14 states, under 1 s |
| `IndexLoadPre0152` | violation (journal 0152) | as `IndexLoadSafety` | violated: `NoStaleHomogeneity` | trace of 13 states, under 1 s |
| `IndexLoadPreR22` | violation (R2-2, journal 0155) | `TypeOK`, `FreshPath` | violated: `FreshPath` | trace of 18 states, under 1 s |

The negative controls check an invariant that holds in the corresponding positive
configuration, so the violation they find comes from the switch that was turned off:

- `IndexLoadNoRetry` (`Retry = FALSE`): the build reads the file, a chmod lands and its hook
  marks the loader dirty but finds no record to reclassify; without the retry the reading is
  stored as homogeneous.
- `IndexLoadPre0152` (`ClassifyAfterRegister = FALSE`): the file's homogeneity is computed at
  lookup, before registration; a chmod completes before the loader exists, and the old
  classification is stored. This is the defect fixed in journal 0152.
- `IndexLoadPreR22` (`CheckName = FALSE`): a same-directory rename lands after the scan looked
  the name up; the rename hook adopts the file under its new name, and the scan, which no
  longer checks the name, stores the old one over it. This is R2-2 (journals 0155 and 0156);
  the journal's scenario has the rename during the reading and a retry, which the model also
  allows.

A two-file configuration (`Files = {f1, f2}`, two operations per writer) of the safety
invariants was also run once, outside CI, and passed; see "Bounds" below.

## Invariants

All four are checked in states where the domain is `BUILT` and nothing is in flight (the builder
is outside an attempt and every writer has finished its hook and any adoption). Between a lower
change and its hook the index lags by design.

- `NoLostUpdate`: every file in the domain has exactly its lower typed attributes in the index,
  and an unlinked file has none ("a file is absent from the index or whole in it", spec §9).
- `NoStaleHomogeneity`: a record answered as homogeneous passes `target_ok()` now: no access is
  granted through a classification that a chmod, chown, ACL change or new link has made stale.
  This is the confidentiality property.
- `FreshPath`: a homogeneous record carries a name the file has now. `QUERY2` resolves the
  recorded name when the inode is not cached, and silently omits the file when it no longer
  resolves (R2-2).
- `ExactRecord` (observation, not in any configuration): every record, heterogeneous ones
  too, is classified exactly and carries a live name. It holds without hard links; with them it
  fails as F2 does and also because an unlink that leaves other names calls no hook (see F2).

## Findings

### F1. A partial record is taken as whole when the build ends between a rename and its hook

Code: `layer/index.c` `moobl_index_renamed()` (file branch: `rec && rec_whole(ix, rec)`),
`rec_whole()` (true once the domain is `BUILT`), `index_load()` (the `name_is()` break),
`domain_ensure_dom()` (the end of an attempt), `moobl_index_set()` (`index_put()` in a domain
being built); `layer/main.c` `moobl_rename()` (the hook runs after `vfs_rename()` has moved the
dentry).

Trace (`IndexLoad`, 13 states; file `f` named `a`, attribute `x` set, `y` not):

1. A query starts the build: the domain is `BUILDING`.
2. The scan lists `a`, looks it up (a positive dcache hit) and registers its loader.
3. `setxattr(y)` through the layer: the lower write, then `moobl_index_set()`. The domain is
   live, so `index_put()` creates a record holding only `y`. The loader is marked dirty.
4. `rename(a, b)` through the layer: `vfs_rename()` moves the lower dentry. The rename hook has
   not run yet; the rename holds the lower directory lock.
5. The scan's loader reaches the loop head: the dentry is hashed and has the same parent, but
   `name_is(lower, "a")` is false, so `index_load()` returns 0 without reading. The comment says
   the rename hook adopts the file.
6. The scan has no other name, and no directory was moved, so `domain_ensure_dom()` marks the
   domain `BUILT` under the index lock. No step needs the lower directory lock.
7. `moobl_index_renamed()`: the record exists and `rec_whole()` is now true, so it only records
   the new name and the partition. It does not call `index_adopt()`.

End state: the domain is `BUILT` and fresh. The lower file has `x` and `y`, the index only `y`.
A `QUERY2` on `x` misses the file until the domain is built again or `x` is written. No access is
granted wrongly: the partition is recomputed in step 7.

Why the model's lock matters: a first version of the model let `iterate_dir()` (or a negative
lookup) run between `vfs_rename()` and the hook, which the code forbids: `moobl_rename()` holds
the lower directories' locks until after the hook and `index_adopt()`, and `lookup_slow()` and
`iterate_dir()` take the directory lock shared. The model now has that lock (`dirLock`). The
trace above needs none of the forbidden steps: the dcache lookup in step 2, `name_is()` in step 5
and the end of the build in step 6 take no lower directory lock. The window is between
`d_move()` in `vfs_rename()` and the index lock in the hook, which includes `place_dentry()` and
its `target_ok()` (it may read an ACL). For the scan to finish without another `iterate_dir()`,
the last name batch must not be full. The defect class is the one of S20: a decision made after
a lock drop (`rec_whole()` read in step 7) that rests on an earlier fact (step 5 left the file
to the hook while the domain was still being built).

Not reproduced in a VM; the trace is from the model and was checked by hand against the code
above.

### F2. An unlink that leaves other names calls no hook, so a loader can store a removed name

Code: `layer/main.c` `moobl_unlink()` calls `moobl_index_drop_inode()` only when `i_nlink`
becomes 0, so with other names left nothing calls `index_touched()`. `layer/index.c`
`index_load()` checks `d_unhashed()` at the loop head and stores `path` after the reading.

Trace (`IndexLoad` with `NoLostUpdate` left out, 20 states):

1. Build `BUILDING`; the scan lists `a`, looks it up, but has not registered yet.
2. `link(a, b)`: the lower link, then `moobl_index_linked()` adopts the file (the domain is
   being built), storing it heterogeneous (nlink 2) under `b`; `moobl_index_perm_changed()`.
3. The scan registers; the loop-head checks pass (`a` is hashed and named `a`).
4. `unlink(a)`: `vfs_unlink()` unhashes the dentry the scan holds; nlink becomes 1; no hook.
5. The scan's `target_ok()` sees nlink 1 and the file passes, so it is homogeneous. The scan
   reads the attributes, finds the loader clean and stores the record, homogeneous, with path
   `a`.
6. The domain is `BUILT`.

End state: a homogeneous record whose recorded name no longer exists. While the inode is cached,
`result_path()` takes the name from the dentry alias (`b`). After eviction it uses `a`, the
resolution fails, and `QUERY2` silently omits the file (the R2-2 effect). The same thing happens
without step 2, for a file that already had two names, when the scan loads the name being
removed last. Outside a build, an unlink that leaves one name also leaves a heterogeneous record
heterogeneous (nlink is 1 again) and possibly with the removed name. That is fail-safe and is
recorded under `ExactRecord`.

Not reproduced in a VM.

## Mapping from code to model

One action is one atomic step. A step that takes the index write lock (`ix_write_lock()`) is
one action, because the lock makes it atomic. Each lower reading made with no lock of ours held
is its own action. A lower change made through the layer is one action and its hook a later one.

| Model action | Code |
| --- | --- |
| `BStart` | `domain_ensure_dom()`: under the lock, `state = MOOBL_DOM_BUILDING` |
| `BList` | `scan_run()`: `iterate_dir()` of the domain directory (lower directory lock shared) |
| `BLookup` | `scan_child()`: `lookup_one_len_unlocked()`; without `ClassifyAfterRegister`, the old `scan_file()` placement |
| `BRegister` | `index_load()`: `list_add(&ld.list, &ix->loaders)` under the lock |
| `BCheck` | `index_load()` loop head: `d_unhashed()`, `name_is()` (the `d_parent` check is implied: one directory) |
| `BClassify` | `index_load()`: `cur.hetero = !target_ok(li, dom, &err)` |
| `BRead` | `attrs_read()`: one action per attribute (`vfs_listxattr()`, `vfs_getxattr()`) |
| `BStore` | `index_load()` under the lock: `ld.dirty` then retry, else `list_del()`, `index_replace()`, `index_set_path()` |
| `BUnregister` | `index_load()` after the loop: `list_del()`; returns 0 or `-EAGAIN` |
| `BFail` | `domain_ensure_dom()`: `domain_drop()`, `state = MOOBL_DOM_NONE`, next attempt |
| `BFinish` | `domain_ensure_dom()`: side list empty, `state = MOOBL_DOM_BUILT` under the same lock |
| `WSetLower` / `WSetHook` | `moobl_xattr_set()` `vfs_setxattr()` / `moobl_index_set()` (`place_and_lock()`, `index_touched()`, `domain_live()`, `index_put()`, `record_dentry_path()`) |
| `WRemoveLower` / `WRemoveHook` | `vfs_removexattr()` / `moobl_index_remove()` |
| `WChmodLower` / `WPermHook` | `moobl_setattr()` `notify_change()`, `moobl_set_acl()` / `moobl_index_perm_changed()` (file case) |
| `WRenameLower` / `WRenameHook` | `moobl_rename()` `lock_rename()` + `vfs_rename()` / `moobl_index_renamed()` file branch |
| `WLinkLower` / `WLinkHook` | `moobl_link()` `vfs_link()` / `moobl_index_linked()`, then `moobl_index_perm_changed()` |
| `WUnlinkLower` / `WDropHook` | `moobl_unlink()` `vfs_unlink()` / `moobl_index_drop_inode()` when nlink reaches 0 |
| `WAdoptRegister`, `WAdoptRead`, `WAdoptStore` | `index_adopt()` → `index_load(sb, lower, NULL, pl, NULL, NULL, where)` |

Reductions, each argued from the code:

- **One writer per file.** The VFS takes a file's `i_rwsem` for setxattr, setattr, rename,
  link and unlink, and the hooks run under it. So the operations on one file are sequential,
  and one writer process that picks any operation each time covers all their orders. The scan
  takes no upper inode lock and interleaves freely.
- **A hook's placement in its locked step.** `place_dentry()` and `place_and_lock()` compute
  the placement with no lock held. What they read is this file's lower inode, and only the same
  writer changes it, so computing it inside the locked step gives the same values.
- **The adoption has no loop-head checks and never retries.** The writer holds the file's
  `i_rwsem`, so the dentry can be neither unlinked nor renamed during `index_adopt()`, and no
  other hook can mark its loader dirty. The model keeps the retry branch anyway.
- **The lower directory lock** (`dirLock`) is held from `vfs_rename()` until the end of the
  rename hook and its adoption, as `moobl_rename()` does until `unlock_rename()`. `BList`, and
  a lookup that finds no dentry (`lookup_slow()`), wait for it. A positive lookup is a dcache
  hit (`d_lookup()` under `rename_lock`) and does not wait. `vfs_link()` and `vfs_unlink()`
  take the parent lock only around the lower call; their hooks run after
  `moobl_unlock_parent()`.
- **A missed name.** `iterate_dir()` holds the directory lock for one batch only. A rename
  between batches can move a name into the part already read. With `ListMayMiss`, the listing
  may omit any name whose dentry was renamed or linked since the attempt began. This
  over-approximates: it also allows a miss when the rename came before the listing.

## What is not modeled

These are limitations of the model. A defect that needs one of them can be missed.

- **Directories.** Directory renames, the side list `dom->moved` and `scan_moved()`,
  `paths_stale`, `top_gen` and `rename_gen()`, the per-domain fallback set by a chmod of a
  directory, `inner_ok` and intermediate directories, and depth beyond `MOOBL_MAX_DEPTH`. All
  files are directly inside the domain directory.
- **More than one domain.** No rename or link across domains (refused with `EXDEV` for
  directories; for files the hook moves the record), no "outside" unit, no mount-wide `QUERY`,
  and no release by the budget or the shrinker (`domain_release()`, `domain_takedown()`). A
  domain that is `BUILT` is never taken down again.
- **Failures.** No `ENOMEM`, budget refusal (`charge()`), `domain_lost()`, `restart`, ACL
  read error or lower I/O error. A build attempt fails only by `-EAGAIN` after `MaxTries`
  readings.
- **Rename over an existing file, `RENAME_EXCHANGE`, `O_TMPFILE`, writes directly to the lower
  filesystem behind the layer**, inode number reuse and `i_generation`, `rec->upper`, and the
  instantiation and eviction hooks (`moobl_index_inode_cached()`,
  `moobl_index_inode_evicted()`). Queries are not modeled; the invariants state what a query
  would read.
- **Names are per file.** Each file has its own name pool, so two files never compete for one
  name.
- **Values.** An attribute is present or absent; overwriting a value with another value is not
  modeled separately from setting it. Permissions are one bit (`reach_ok()` passes or not), and
  `target_ok()` is that bit and `nlink <= 1`.
- **Dentry identity** is modeled; a name removed and linked again gets a new dentry slot.
- **Liveness** (that a build ends) is not checked, only safety.

## Bounds

The configurations are small: one file, three operations, one extra link, two readings, two
attempts. A two-file configuration of the safety invariants (`IndexLoadSafety` with
`Files = {f1, f2}` and `MaxOps = 2`, not committed) was run once on 2026-11-04 on the same
machine: no violation, state graph exhausted, 233,070,225 distinct states (606,699,135
generated), depth 82, 16 minutes; TLC estimated the probability of a fingerprint collision
between 0.003 and 0.0047. It is not in CI because it is too slow for a pull request check.
Bugs that need more operations, more files or more retries than these bounds are outside what
TLC checked.
