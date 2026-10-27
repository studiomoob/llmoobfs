# 0216 — A domain record keeps its directory's generation (GHSA-756j-cgwq-gvwm)

Date: 2026-10-27. Status: on the private branch `sec/ghsa-756j` of the advisory's temporary
fork; embargoed until the advisory is published. Work started: 2026-10-27 17:45
America/Sao_Paulo; ended: see "Checks of this commit". Wall-clock time, not an
engineering-effort measurement. Estimated effort: S.

## Goal and decision

A private report from an external reviewer (GHSA-756j-cgwq-gvwm, severity low): `ix->domains`
is keyed by the lower inode number of the domain directory. When that directory is removed on
the lower, behind the layer (ADR-0011's A2, for example an administrator removing a departed
user's tree), its record stays built and fresh with its entries. ext4 and XFS give the freed
number to the next directory, and `query2_pin()` then answers the new directory, and its new
owner, from the removed directory's entries. Journal 0210 (issue #113) drops the record on a
removal through the layer and takes it back on reuse; a removal behind the layer never reaches
`moobl_rmdir()`.

Decision: the record also keeps the directory's `i_generation`, sampled from the lower inode
when its build begins, and a record whose generation differs from the directory's is not fresh.
It is then taken down and built again, the same path as a domain made stale by a rename
(`domain_ensure_dom()`). The check (`domain_fresh_for()`) runs where a query decides whether to
build: the fast path of `query2_pin()`, `query_repin()`, and both checks of `domain_ensure_dom()`,
which the mount-wide v1 `QUERY` reaches through `domain_ensure_all()`.

Prior art: an inode number alone does not name one inode for long; NFS file handles pair it with
the generation (`generic_encode_ino32_fh()`, `fs/libfs.c`), as do overlayfs origin and index
records (`ovl_encode_real_fh()` via `exportfs_encode_inode_fh()`, `fs/overlayfs/copy_up.c`) and
fanotify FIDs (`exportfs_encode_fid()`, `fs/notify/fanotify/fanotify.c`). In the build's kernel (6.12.112 sources): ext4 gives a new inode a random
generation (`fs/ext4/ialloc.c:1290`); XFS a random one on v5 filesystems without `ikeep`
(`fs/xfs/xfs_icache.c:675`) and otherwise bumps it when the inode is freed
(`fs/xfs/libxfs/xfs_inode_util.c:746`); btrfs the transaction that created it
(`fs/btrfs/inode.c:6297`), and it does not reuse numbers within a mount.

No ADR: no promise, format or interface changes. ADR-0011's non-promise 2 already says results
from changes behind the layer may be stale but never come from a domain the caller cannot
traverse; this restores that.

Alternatives not taken: the change time of the directory, which changes with every entry added
or removed and would rebuild domains for nothing; an identity read from an extended attribute,
which would write to the lower (ADR-0007); and dropping the record when the scan does not find
its directory, which covers only mount-wide queries.

## Changes

- `layer/moobl.h`: `struct moobl_domain` gains `lower_gen`.
- `layer/index.c`: `domain_fresh_for()`; `domain_ensure_dom()` samples the generation when a
  build begins and checks it before building; `query_repin()` and `query2_pin()` check it.
- `vm/suite.sh`: step `domainreuse` on ext4, XFS and btrfs. A domain built as uid 1001 is
  removed on the lower; directories are made on the lower until one gets the removed number
  (ext4 and XFS must reuse it within 64; btrfs need not); `QUERY2` as uid 1002 on it must
  return nothing, then only its own file, and the mount-wide v1 `QUERY` must not list the
  removed file.
- `docs/references.md`, `CHANGELOG.md`, the history index.

## Evidence

All on the dedicated Debian host, with `vm/kasan-local.sh` and the cached upstream 6.12.112
KASAN/UBSAN/lockdep kernel of `vm/kasan.config`.

- `make layer W=1` against 6.12.111: no warning. `make checkpatch`: nothing reported.
- `MOOB_ONLY=domainreuse sh vm/kasan-local.sh`: `DOMAINREUSE_ext4: inode 14 reused by bob0`,
  `DOMAINREUSE_xfs: inode 262272 reused by bob3`, `DOMAINREUSE_btrfs: no inode number reused`,
  each answering only its own file through `QUERY2` and the mount-wide `QUERY`;
  `MOOBFS_KASAN_ONLY_PASS`, no kernel diagnostic.
- Negative round, `layer/` of `f7c83cf` with the new step: `MOOBFS_VM_FAIL: du-stale-ext4: bob0
  (inode 14) answered 16 plan.md`; with the step limited to XFS, `du-stale-xfs: bob3 (inode
  262272) answered 262274 plan.md`. The advisory reproduces on both filesystems without the fix.
- The steps the change touches, under KASAN/lockdep: see "Checks of this commit".

## Errors found on the way

The first versions of the step did not reuse the number, for two reasons that are not defects of
the layer:

- ext4 kept the removed directory's inode allocated after one `drop_caches=2` when the step ran
  alone in a fresh guest (`debugfs testi`: in use), and freed it after `drop_caches=3`.
  The likely cause, not traced: `drop_slab()` (`mm/vmscan.c`) stops once a pass frees almost
  nothing, and the layer dentry of `alice` becomes unused only when its child `plan.md` is
  pruned, so it waits for a later pass. The step now writes `2` four times. The
  same run after the `domainrecs` step had reused the number at once.
- XFS frees an inode chunk once all its inodes are free and gave the new directories new chunks;
  the number was not reused even after the layer was unmounted. The step now makes a file next to
  `alice` and moves it out, which keeps the chunk.

## Limitations

- A filesystem that leaves `i_generation` at 0 and reuses inode numbers is not protected by this
  check. The suite's lowers (ext4, XFS, btrfs) all set it.
- ext4's generation is 32 random bits: one reuse in 2^32 keeps the same generation, as for NFS.
- The owner of a directory can set its generation (`EXT4_IOC_SETVERSION`, `fs/ext4/ioctl.c`); that only makes
  their own domain rebuild.
- The record of the removed directory and its entries stay until the number is reused and
  queried, or until unmount, as before (journal 0210, limitations).
- btrfs subvolumes repeat inode numbers across subvolumes under one lower; not covered here.

## Next step

Review on the advisory's private fork, then merge with the security batch and publish the
advisory with the reporter.
