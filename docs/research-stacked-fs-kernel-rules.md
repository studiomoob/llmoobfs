# Stacked-filesystem rules from kernel primary sources, checked against MoobFS

Date: 2026-10-12. Author: review for the security roadmap (S1 context, before S2/S3/S4).
Scope: the `moobfs_layer` module (`layer/main.c`, `layer/index.c`, `common/attr.c`,
`include/moobfs_ioctl.h`, `include/moobfs_attr.h`) as it stands on `main` at commit `d39bbb8`.
This document only records rules and status; it changes no code.

## Method and sources

Each rule below cites a primary source by file and version. The kernel sources were read at the
tag the project pins for its build and its KASAN harness:

- **Linux `v6.12`** (torvalds tag) — the upstream reference used by `docs/kernel-infrastructure.md`.
- **Linux `v6.12.112`** (stable) — the series the KASAN harness builds (`Makefile` `SUBLEVEL = 112`);
  MoobFS runs the Debian `6.12.111+deb13-amd64` module in the QEMU suite and `6.12.112` under KASAN
  (`README.md`). For every file cited here the two tags agree on the cited lines; `fs/attr.c` and
  both `fs/ecryptfs/*` files are byte-identical between the tags, and the overlayfs/backing-file
  differences between them do not touch any cited line. Line numbers are from `v6.12` unless noted.

Primary sources are the kernel's own `Documentation/`, the in-tree code of the two reference
stacked filesystems (**overlayfs** `fs/overlayfs/`, ~12.5 kLOC; **ecryptfs** `fs/ecryptfs/`,
~8.2 kLOC, named as the models in ADR-0007 item 7) and of **FUSE passthrough**
(`fs/fuse/passthrough.c`), plus the VFS files they rest on (`fs/attr.c`, `fs/namei.c`,
`fs/xattr.c`, `fs/backing-file.c`, `fs/stack.c`, `fs/dcache.c`, `fs/inode.c`,
`include/linux/fs.h`). The hardening guidance of the **KSPP** (Kernel Self-Protection Project) is
cited too, as the session asked; it is labelled as such, because it lives at `kspp.github.io`
and the `kernsec.org` wiki, **not** in kernel.org `Documentation/` — it is an upstream-aligned
community project, not a primary kernel source.

Status values: **Complies** (code matches the rule, verified by reading and, where marked, by a
VM run), **Does not comply** (a concrete divergence), **Not verified** (the rule applies but this
review did not confirm it on a running kernel).

Three items were reproduced in a disposable QEMU VM (kernel `6.12.111+deb13-amd64`, the module
built from this tree) to turn a code reading into evidence; those carry "**VM:**" notes. The run
script and console log are kept out of the repository (throwaway scratch), but the commands are
described well enough to repeat under `make vm-test`'s harness.

---

## Findings summary (most severe first)

- **R-CRED-3 — user-triggerable kernel `BUG()` (denial of service).** Writing to a set-user-ID or
  set-group-ID file through the layer, as an unprivileged user, oopses the kernel at
  `fs/attr.c:444`. `moobl_setattr` forwards `ATTR_KILL_SUID|ATTR_KILL_SGID` together with
  `ATTR_MODE`, the one combination `notify_change` forbids with `BUG()`. overlayfs and ecryptfs
  both strip `ATTR_MODE` at this exact point; the layer does not. Contradicts ADR-0011 promise 1
  and `SECURITY.md`. **Reproduced in the VM.** Fix is one branch in `moobl_setattr`; this document
  does not apply it.
- **R-CRED-2 — reserved-space / quota bypass.** The mounter credentials keep `CAP_SYS_RESOURCE`,
  so an unprivileged caller writing through the layer draws on the lower filesystem's
  root-reserved blocks that a direct write would deny. overlayfs clears this capability on purpose
  (`fs/overlayfs/super.c:1442`). **Reproduced in the VM.** Low severity (a resource edge, not a
  crash or a cross-user read), but it is a stated-model gap.
- **R-UAPI-6 / R-INODE-4 — `st_dev`/`st_ino` not unique across lower objects.** Two files in
  different btrfs subvolumes appear through the layer with the same `st_dev:st_ino`. This is the
  documented overlayfs hazard; overlayfs mitigates it with `xino`, the layer has no equivalent.
  Not a crash (internal inode keying is by lower-inode pointer, so there is no aliasing inside the
  module), but it breaks `find`/`du`/backup assumptions. **Observed in the VM.**
- Everything else reviewed **complies** or is **not verified** (fault injection, syzkaller, the
  full lockdep matrix — these are the open S-items, not regressions).

Separately, a documentation defect found while sourcing this review: `docs/security-roadmap.md:69`
cites the kernel doc *"Adding New Filesystems"* for S2, but
`Documentation/filesystems/adding-new-filesystems.rst` returns 404 at `v6.12`, `v6.12.112`, `v6.13`
and `v6.14`; it exists only on `master`. The cited claim (fstests is standard infrastructure) is
still traceable to that master document, but the citation is anachronistic for the pinned kernel.

---

## 1. Credentials (`override_creds`, the mounter/caller split)

The model the layer follows is overlayfs's: name operations with the caller's credentials, data
operations on an open file with the mounter's. ADR-0011 "Credentials (as implemented)" states it;
these rules check it against the sources.

*Note, 2026-10-14 (journal 0099).* The first sentence is wrong: that split is MoobFS's, not
overlayfs's. Overlayfs checks every access twice, the caller against the overlay inode and the
mounter against the real inode, and runs the real operation, name operations included, with the
mounter's credentials (`Documentation/filesystems/overlayfs.rst`, "Permission model"). R-CRED-4
below already said so; the rules themselves are unchanged.

**R-CRED-1 — mounter credentials captured by copy-and-replace, reverted after use.**
Source: `Documentation/security/credentials.rst:273-278` ("copy-and-replace principle", a task may
alter only its own creds) and `:555-563` ("Overriding the VFS's Use of Credentials"). Reference:
overlayfs captures `ofs->creator_cred = prepare_creds()` (`fs/overlayfs/super.c:1303`) and wraps
delegated work in `override_creds`/`revert_creds` (`fs/overlayfs/util.c:64-69`,
`fs/overlayfs/file.c:42,216,408,448,473,516`).
Status: **Complies.** `moobl_fill_super` takes `sbi->creds = prepare_creds()`
(`layer/main.c:1198`), frees it in `moobl_kill_sb` (`layer/main.c:1286`), and every delegated path
brackets the override: `moobl_fiemap` (`:603-606`), `moobl_fsync` (`:957-960`),
`moobl_fallocate` (`:993-996`), `moobl_remap_file_range` (`:1011-1018`),
`moobl_copy_file_range` (`:1031-1035`), and the index scan (`layer/index.c:760,810`). Read/write/
mmap/splice go through `backing_file_*`, which overrides and reverts internally
(`fs/backing-file.c:179-200,236-267,284-317,341-343`), fed `MOOBL_SB(...)->creds`
(`layer/main.c:866-871`). The `creds` pointer is written once at mount and only read afterwards.

**R-CRED-2 — a stacked FS must not let the caller spend the mounter's privileged resource budget.**
Source: `fs/overlayfs/super.c:1441-1442`, which lowers `CAP_SYS_RESOURCE` from the creator cred
with the comment *"Never override disk quota limits or use reserved space"*.
Status: **Does not comply.** `moobl_fill_super` uses `prepare_creds()` unmodified
(`layer/main.c:1198`); it never clears `CAP_SYS_RESOURCE`. When the mounter is root (the only
allowed case today, see R-MOUNT-1), delegated writes run with `CAP_SYS_RESOURCE`, so an
unprivileged caller reaches the lower filesystem's root-reserved blocks and ignores quota limits
the lower would enforce on a direct write.
**VM:** on a 16 MiB ext4 lower with the default 5% reserve, uid 1001 filling a file *directly*
stopped at `avail=0` with free blocks still held back; the same uid filling through the layer drove
the lower's free count well below that floor. Not a crash; a quota/reserve bypass that the written
model (ADR-0011 promise 2, "never lets the caller do on the lower what the lower would refuse")
does not currently hold for the reserved-space case.

*Correction, 2026-10-12 (journal 0069).* Lowering `CAP_SYS_RESOURCE` does not close the case
reproduced above. ext4 grants its reserve when the caller's fsuid equals the reserve owner **or**
the caller has the capability (`fs/ext4/balloc.c:650-652`); the mounter credentials have fsuid 0
and the default reserve owner is 0, so delegated writes still reach the reserve. overlayfs with a
root mounter behaves the same. What the capability decides is the quota hard limit
(`fs/quota/dquot.c:1285`, `ignore_hardlimit`) and the reserve of a lower mounted with another
`resuid`. The layer now lowers the capability as overlayfs does, tested on ext4 with
`resuid=4242`; the default-`resuid` case stays open and promise 2 is **not** met for it.

*Note, 2026-10-19 (journal 0132).* The default-`resuid` case is closed: data writes, fallocate,
clone and copy_file_range run with a copy of the mounter credentials carrying the writer's fsuid,
fsgid and groups (as `ovl_create_or_link()` builds creation credentials), so ext4 decides the
reserve as for a direct write. Tested on ext4 with `resuid=4242` and with the default reserve
owner; promise 2 is met for both.

**R-CRED-3 — `setattr` must not pass `ATTR_MODE` together with `ATTR_KILL_SUID|ATTR_KILL_SGID`.**
Source: `fs/attr.c:435-444` — `notify_change` comments *"no function will ever call notify_change
with both ATTR_MODE and ATTR_KILL_S*ID set"* and enforces it with `BUG()` at `:444`. References:
overlayfs clears `ATTR_MODE` in that case (`fs/overlayfs/inode.c:55-56`), and ecryptfs does the
same (`fs/ecryptfs/inode.c:974-975`).
Status: **Does not comply.** `moobl_setattr` (`layer/main.c:774-797`) copies the caller's
`iattr` into `lower_ia` (`:780`) and calls `notify_change(..., &lower_ia, NULL)` (`:789`) without
removing `ATTR_MODE`. The VFS sets `ATTR_KILL_SUID|ATTR_KILL_SGID` on a write to a file that has
those mode bits (`file_remove_privs` → `dentry_needs_remove_privs`), so an ordinary write by an
unprivileged user reaches the forbidden combination.
**VM:** as uid 1001, `echo x > s; chmod 4755 s; echo y >> s` oopsed the kernel:
`kernel BUG at fs/attr.c:444!`, `Oops: invalid opcode`, `RIP: notify_change+0x4d4`, call trace
`moobl_setattr+0xe2 [moobfs_layer] ← notify_change ← moobl_write_iter+0x9e [moobfs_layer]`. This is
a crash reachable by the main adversary (A1), which ADR-0011 promise 1 and `SECURITY.md` classify
as a vulnerability. (The fix pattern is the one-line `lower_ia.ia_valid &= ~ATTR_MODE` guard the
two reference filesystems use; applying it is a code task, out of scope here.)

*Note, 2026-10-12 (journal 0069).* Fixed with that guard and a regression test. The same pair of
flags also arrives through `truncate`/`ftruncate` and through `chown` of such a file, the latter
for every caller including root (`chown_common` always asks to clear set-user-ID); the test covers
the three.

**R-CRED-4 — name operations run with the caller's credentials, not the mounter's.**
Source: ADR-0011's own decision, and the overlayfs alternative it rejects. Reference: overlayfs
performs the two-check model (`fs/overlayfs/inode.c:300-319`, local DAC then `inode_permission`
under the creator cred); MoobFS deliberately keeps name ops on the caller.
Status: **Complies.** `moobl_create/mkdir/unlink/rmdir/rename/link/symlink/setxattr` call the
`vfs_*` helpers with no `override_creds` around them (`layer/main.c:362-583,712-741`), so the
lower's own `permission()` runs against the caller. `moobl_permission` delegates to
`inode_permission` on the lower inode (`layer/main.c:799-802`). This is the ADR-0011 choice and it
avoids re-implementing the lower's permission logic (the overlayfs credential-bug class).

**R-CRED-5 — `f_cred` of the backing file carries the mounter identity for delegated I/O.**
Source: `Documentation/security/credentials.rst:536-545` (open file credentials in `f_cred`).
Reference: overlayfs opens the real file with the creator cred.
Status: **Complies.** `moobl_open` calls `backing_file_open(file, ..., lower, MOOBL_SB(...)->creds)`
(`layer/main.c:880-881`) and `moobl_dir_open` opens the lower directory with `sbi->creds`
(`layer/main.c:1068-1070`). `backing_file_open` requires and marks `FMODE_BACKING`
(`fs/backing-file.c`, checked at `:169,216,281,303,332`).

## 2. Locking and lock ordering

**R-LOCK-1 — lock the lower parent and re-lookup the child under that lock for name operations.**
Source: `fs/ecryptfs/inode.c:27-39` (`lock_parent` takes `inode_lock_nested(I_MUTEX_PARENT)` and
verifies the child's parent). Locking contract: `Documentation/filesystems/locking.rst` inode_ops
section.
Status: **Complies.** `moobl_lock_parent` (`layer/main.c:307-321`) takes
`inode_lock_nested(*lower_dir, I_MUTEX_PARENT)` and `lookup_one_len` under it; `unlink`/`rmdir`
re-check `d_unhashed`/negativity under the lock (`layer/main.c:409-412,435-438`), which ecryptfs
added for the same reason (`fs/ecryptfs/inode.c:147-148`).

**R-LOCK-2 — cross-directory rename uses `lock_rename`, handles the trap and `-EXDEV`.**
Source: `Documentation/filesystems/directory-locking.rst:53-58` (ancestors-first, common ancestor
or fail) and `Documentation/filesystems/porting.rst:1087-1091` (**mandatory**: `lock_rename` may
return `ERR_PTR(-EXDEV)` with no locks taken; out-of-tree users must handle it).
Status: **Complies.** `moobl_rename` calls `lock_rename(lower_old_dir, lower_new_dir)` and checks
`IS_ERR(trap)` before use (`layer/main.c:483-487`), handles `trap == lower_old` (`-EINVAL`) and
`trap == lower_new` (`-ENOTEMPTY`) (`:512-516`), and re-looks-up the target under the locks
(`:494`), mirroring ecryptfs (`fs/ecryptfs/inode.c:609-616`) plus the project's own fix for
rename-over-a-deleted-name (journal 0057).

**R-LOCK-3 — do not hold the index lock across a sleeping lower operation; partition placement
sleeps (ACL reads), so compute it outside the lock.**
Source: `Documentation/core-api/memory-allocation.rst:39-56` (GFP_KERNEL may sleep) and the
locking contract that an rwsem held write-side must not wrap blocking lower I/O.
Status: **Complies.** `place_dentry` is documented "may sleep: outside the index lock" and is
called before `down_write(&ix->lock)` in `moobl_index_set` (`layer/index.c:883-884`),
`moobl_index_renamed` (`:958-959`) and `moobl_index_perm_changed` (`:988-989`). The scan holds the
write lock across `vfs_listxattr`/`vfs_getxattr` (`layer/index.c:761,775`) — those run under the
mounter creds during `fill_super` with no concurrent openers yet, which is the mount-time
exception, not the steady-state path.

**R-LOCK-4 — `d_revalidate` in RCU-walk must not lock, reference or sleep; return `-ECHILD` to
drop to ref-walk.**
Source: `Documentation/filesystems/locking.rst:39` (d_revalidate: rcu-walk "maybe", ref-walk
"yes") and `Documentation/filesystems/path-lookup.rst:890,1060` (`-ECHILD` on inability to
proceed). Reference: ecryptfs refuses RCU outright (`fs/ecryptfs/dentry.c:36-37`).
Status: **Complies.** `moobl_d_revalidate_rcu` (`layer/main.c:208-228`) uses only
`READ_ONCE`/`d_inode_rcu`, takes no locks or references, and returns `-ECHILD` for anything beyond
the plain case. The published-pointer discipline (`WRITE_ONCE` in `moobl_instantiate`,
`layer/main.c:353`) matches the comment's claim that RCU readers see a consistent lower dentry.

**R-LOCK-5 — avoid a kernel stall in long index scans/queries (user-triggerable loops).**
Source: `Documentation/RCU/stallwarn.rst:20-29` (a CPU looping in-kernel without scheduling).
Status: **Not verified.** The query chunking releases the index lock between chunks of
`MOOBL_QUERY_CHUNK` (256) candidates (`layer/index.c:1196,1600,1672`), which bounds the critical
section, but there is no explicit `cond_resched()` in the mount-time scan
(`moobl_index_build`, `layer/index.c:767-809`) or in a single chunk's rbtree walk. On a very large
lower tree the scan holds the CPU under the write lock between implicit sleeps. A dedicated test
(very large tree, `CONFIG_PREEMPT=n`) was not run; the S9 resource-limit work should cover it.

## 3. Dentry and inode lifetime

**R-INODE-1 — one upper inode per lower inode, keyed on the lower inode, referenced correctly.**
Source: ecryptfs model (`fs/ecryptfs/inode.c:41`, `ecryptfs_inode_test`); `fs/inode.c` iget5.
Status: **Complies.** `moobl_get_inode` uses `iget5_locked` with `moobl_inode_test`/`_set` keyed on
the lower inode pointer (`layer/main.c:86-137`), `ihold`s the lower and releases it on the
not-new and error paths (`:123,127,131`). `moobl_evict_inode` `iput`s the lower
(`layer/main.c:154-162`). This keying means two distinct lower inodes never collapse into one upper
inode even when their `i_ino` collide (see R-INODE-4).

**R-INODE-2 — `i_nlink` changes only under VFS serialization; do not `set_nlink` on unlocked
copy-attr paths.**
Source: the SMP hazard the code comments cite; `fs/inode.c` set_nlink/drop_nlink are not
concurrency-safe against racing callers.
Status: **Complies.** `moobl_copy_attr` deliberately leaves `i_nlink` alone, with a comment
explaining the superblock remove-count corruption it avoids (`layer/main.c:41-58`). Link counts
are set only inside the parent-locked name operations (`layer/main.c:393,415,441-442,559`).

**R-INODE-3 — dentry private data freed RCU-delayed; `d_release` must not sleep.**
Source: `Documentation/filesystems/locking.rst:45` (d_release: may block "yes", rcu-walk "no" — it
runs in ref context but the freed object must survive RCU readers) and
`Documentation/filesystems/porting.rst:816-833` (`->free_inode` is the `call_rcu` path).
Status: **Complies.** `moobl_d_release` `path_put`s the lower and frees `moobl_dentry` via
`call_rcu(&info->rcu, moobl_dentry_free_rcu)` (`layer/main.c:255-263`), so an RCU-walk reader
holding the old pointer is safe for a grace period. The inode cache is freed in `moobl_free_inode`
(`layer/main.c:149-152`).

**R-INODE-4 — a stacked FS cannot promise unique `st_dev`/`st_ino` for non-directories across
lower objects; applications are warned, or `xino` makes it unique.**
Source: `Documentation/filesystems/overlayfs.rst:20-34` (st_dev/st_ino uniqueness only with
st_dev, both can change) and `:41-50` (the `xino` feature composes a unique identifier). Reference:
`fs/overlayfs/inode.c:100-182` (`ovl_map_dev_ino`, `samefs` handling).
Status: **Does not comply with the uniqueness property (by design, no `xino` equivalent), and it
is undocumented for MoobFS.** `moobl_get_inode` sets `inode->i_ino = lower->i_ino`
(`layer/main.c:98`) and `moobl_getattr` fills from the layer inode (`layer/main.c:756-772`), so the
reported `st_ino` is the lower inode number and `st_dev` is the single layer anon-dev.
**VM:** two distinct files in two btrfs subvolumes both reported `st_dev:st_ino = 29:257` through
the layer, because each subvolume restarts inode numbering at 257. overlayfs documents this and
offers `xino`; MoobFS neither documents it nor mitigates it. Recommendation: document the limit
(a note like overlayfs's), since a lower spanning btrfs subvolumes is a realistic memory layout.

*Note, 2026-10-12 (journal 0069).* Documented in the README's limitations; not mitigated.

**R-INODE-5 — refuse to open device/fifo nodes while still indexing their attributes.**
Source: general VFS safety; overlayfs/ecryptfs treat special files by `init_special_inode`.
Status: **Complies.** `moobl_inode_set` routes special inodes to `moobl_special_iops`
(no open/read/write) and `init_special_inode` (`layer/main.c:108-112`); the comment states
"opening them is refused."

## 4. ioctl / UAPI (layout, compat, reserved-field validation)

**R-UAPI-1 — fixed-width types, explicit padding, `__u64` for user pointers, struct padded to a
64-bit multiple.**
Source: `Documentation/process/botching-up-ioctls.rst:30-49` and `Documentation/driver-api/ioctl.rst:152-187`.
Status: **Complies.** `include/moobfs_ioctl.h` uses `__u8/__u16/__u32/__u64` throughout, explicit
`pad[]`/`reserved[]` in every struct (`moob_query_term.pad[3]` `:46`, `moob_query.reserved[2]`
`:70`, `moob_query2.pad[5]`+`reserved[4]` `:139-142`, `moob_info.pad`+`reserved[2]` `:107-108`),
and `__u64 results`/`queries` for user pointers (`:67,133,157`).
**Verified by compilation:** the five structs compile to identical sizes on
`x86_64`, `i386`, `aarch64` and `armv7` targets (clang `-ffreestanding` cross `-fsyntax-only`
with `_Static_assert` on `sizeof`/`offsetof`), so there is no 32/64 layout skew and no implicit
tail padding — the compat path can be the plain `compat_ptr_ioctl`.

**R-UAPI-2 — a feature/version field so userspace can detect support.**
Source: `botching-up-ioctls.rst:60-70`; `Documentation/driver-api/ioctl.rst`.
Status: **Complies.** `MOOB_UAPI_VERSION`/`MOOB_QUERY2_VERSION` are checked on entry
(`layer/index.c:1723,1910,2028`), and `moob_info.uapi_version`/`format_version` advertise support
(`layer/index.c:2059-2060`). The README states v1/v2 may change with no compatibility promise,
consistent with `Documentation/process/stable-api-nonsense.rst` (howto.rst:127-138).

**R-UAPI-3 — reject non-zero reserved fields, unknown flags and padding.**
Source: `botching-up-ioctls.rst:72-80` ("check all unused fields and flags and all the padding for
whether it's 0, and reject … otherwise … bakes in the ABI that those fields can never be used").
Status: **Complies.** `do_query` rejects `flags`/`reserved[0,1]` non-zero (`layer/index.c:1723-1725`);
`query2_validate` rejects `flags`, non-zero `pad` and `reserved` via `all_zero`
(`layer/index.c:1910-1913`); `term_check` rejects unknown term flags (`t->flags & ~allowed`) and
non-zero `t->pad[]` (`layer/index.c:1052-1054`); the batch rejects non-zero `reserved`
(`:2028-2029`). This is exactly the rule the guide stresses, and it is applied per struct.

**R-UAPI-4 — full input validation and bounds/overflow checks; no array overflow.**
Source: `botching-up-ioctls.rst:88-97`; `Documentation/security/self-protection.rst:187` (size
calculation overflow).
Status: **Complies.** `nterms`/`limit` are range-checked (`layer/index.c:1723-1724,1910-1911`);
`term_check` validates op, type, length and UTF-8 per type (`layer/index.c:1048-1084`);
`b.nqueries` is capped at `MOOB_QUERY2_BATCH_MAX` (`:2028`); array sizing uses
`kvmalloc_array`/`kcalloc`/`kmalloc_array` (`:2032,1573,536`), which carry the overflow check the
`deprecated.rst:54-62` rule asks for.

**R-UAPI-5 — no uninitialized bytes copied to userspace (KASLR/info leak); `memset` before fill.**
Source: `Documentation/driver-api/ioctl.rst:195-206` and
`Documentation/security/self-protection.rst:294-299`.
Status: **Complies.** `do_get_info` `memset(&info, 0, …)` before filling (`layer/index.c:2058`);
`put_result` `memset(one, 0, …)` per result (`layer/index.c:1527`); `fill_result` sets every field
including the path tail (`layer/index.c:1148-1181`); the cursor is zeroed then filled
(`cursor_encode` `memset`, `layer/index.c:1810`). The `moob_query2`/`moob_query` structs are
round-tripped from a `memdup_user` copy, and the hidden-counter case zeroes the counters rather
than leaving stack data (`layer/index.c:2058,2087-2093`, ADR-0011 F1).

**R-UAPI-6 — unknown command returns `-ENOTTY`; the ioctl is issued on the right object.**
Source: `Documentation/driver-api/ioctl.rst:74-79` (`-ENOTTY` for unknown).
Status: **Complies.** `moobl_root_ioctl` returns `-ENOTTY` for unknown commands and for v1
commands not on the mount root (`layer/index.c:2113-2122`); `query2_target` returns `-ENOTTY`
unless the fd is a directory at exactly `domain_depth` (`layer/index.c:1896-1899`). Note (not a
defect): the ioctl magic is `'M'` (`include/moobfs_ioctl.h:19`), which
`Documentation/userspace-api/ioctl/ioctl-number.rst:173-177` lists as already shared ("conflict!")
by soundcard/mtd/megaraid; collisions are harmless because the handler validates the struct, but
if MoobFS ever upstreams, `ioctl-number.rst` must be updated per `submit-checklist.rst:59-60`.

**R-UAPI-7 — cursor/token integrity against forgery (the QUERY2 resume token crosses the user
boundary).**
Source: `Documentation/security/siphash.rst:7-27` (keyed PRF; key from `get_random_bytes`).
Status: **Complies.** The per-mount `cursor_key` is seeded with `get_random_bytes`
(`layer/index.c:829`); the cursor carries a siphash MAC over its bytes and a query hash, both keyed
with it, and a wrong/foreign/corrupt cursor is refused with `-ESTALE`
(`layer/index.c:1804-1858`). This is the documented correct use of siphash as a short-input PRF.

## 5. Extended attributes

**R-XATTR-1 — one xattr handler per prefix; `get` no lock, `set` exclusive `i_rwsem`; let the VFS
do the permission and namespace work.**
Source: `Documentation/filesystems/locking.rst:133-156` (`xattr_handler` ops and their locking) and
`docs/kernel-infrastructure.md` (VFS xattr reuse). Reference: ecryptfs delegates with
`__vfs_setxattr_locked`/`__vfs_getxattr` (`fs/ecryptfs/inode.c:1055,1074`).
Status: **Complies.** A single catch-all handler (`prefix = ""`) routes to
`moobl_xattr_get`/`_set` (`layer/main.c:743-752`); the VFS holds `i_rwsem` for the set path and
checks the namespace permission before calling. Get/list delegate via `vfs_getxattr`/`vfs_listxattr`
(`layer/main.c:638,648`).

**R-XATTR-2 — validate typed attributes before writing the lower; never read past the buffer.**
Source: `botching-up-ioctls.rst` input-validation rule applied to the xattr path; the layer's own
spec (`docs/spec-m2.md` §3.5).
Status: **Complies.** `moobl_typed_check` rejects reserved/ill-formed names, oversize values and
invalid encodings before the lower write (`layer/main.c:684-710`), using the shared
`moob_attr_validate` that is bounded ("never reads past value + size", `include/moobfs_attr.h`,
`common/attr.c:78-120`). The same validator runs in the tools, satisfying ADR-0011 promise 4.

**R-XATTR-3 — a listxattr from the lower may be larger than the buffer / malformed; handle `-ERANGE`
and a broken list without faulting.**
Source: `fs/overlayfs/xattrs.c:132-137` (overlayfs `WARN_ON` on a broken lower xattr list) and the
`XATTR_LIST_MAX`/`XATTR_SIZE_MAX` caps (`include/uapi/linux/limits.h:15-17`). Reference: FUSE clamps
with `min_t(size_t, outarg.size, XATTR_LIST_MAX)` (`fs/fuse/xattr.c:84,146`).
Status: **Complies.** The scan allocates `XATTR_LIST_MAX` and treats `-ERANGE` as "leave this inode
out of the index, report via moobfs-check" rather than failing the mount
(`layer/index.c:632-633`); it walks the name list with `strlen(p)+1` bounded by the returned length
(`:636`), and `moobl_count_typed` does the same under the per-file cap (`layer/main.c:663-680`).

## 6. Hostile lower tree (adversaries A2/A3)

**R-LOWER-1 — attributes read from the lower are untrusted: skip invalid names/encodings, never
fail the mount, never index them.**
Source: ADR-0011 promise 5; `Documentation/filesystems/overlayfs.rst:613-626` (offline changes to
the lower must not crash or deadlock); the FUSE validation model (`fuse_invalid_attr`,
`fs/fuse/dir.c:358-360`, make-bad on invalid incoming attrs).
Status: **Complies.** The scan skips non-`user.moob.` names, reserved/ill-formed names and values
that fail `moob_attr_validate` (`layer/index.c:640-646`), and the mount proceeds; invalid data is
reported by `moobfs-check`, not indexed.

**R-LOWER-2 — detect the lower changing under the mount and refuse to act on the wrong object,
rather than corrupting state.**
Source: `Documentation/filesystems/overlayfs.rst:616-619` ("Changes to the underlying filesystems
while part of a mounted overlay … behavior … is undefined, though it will not result in a crash or
deadlock").
Status: **Complies.** `moobl_rename` re-looks-up the target under the parents' locks and returns
`-ESTALE` when positivity or the lower inode disagrees with the cached dentry
(`layer/main.c:501-506`); `unlink`/`rmdir` re-check `d_unhashed`/negativity (`layer/main.c:409,435`).
ADR-0011 non-promise 2 states the staleness contract (the index catches up at remount), which
matches the overlayfs "undefined but safe" rule.

**R-LOWER-3 — refuse stacking on another instance of the same FS and cap stacking depth.**
Source: `include/linux/fs.h:275` (`FILESYSTEM_MAX_STACK_DEPTH 2`); overlayfs
(`fs/overlayfs/super.c:1132-1133`) and ecryptfs (`fs/ecryptfs/main.c:518-524,561-564`) both refuse
a same-type lower and enforce the depth cap.
Status: **Complies.** `moobl_fill_super` refuses a lower whose magic is `MOOBL_SUPER_MAGIC`
(`layer/main.c:1193-1194`) and enforces `s_stack_depth > FILESYSTEM_MAX_STACK_DEPTH → fail`
(`layer/main.c:1195-1197`), the same check and constant as the two references.
**VM:** `mount -t moobfs_layer -o lower=<the layer itself>` is refused (suite line
`layer-stack-on-itself`).

**R-LOWER-4 — a read-only lower stays read-only through the layer.**
Source: ecryptfs inherits `SB_RDONLY` from the lower (`fs/ecryptfs/main.c:555-556`); `mnt_want_write`
is the gate (`fs/namespace.c:503-511`).
Status: **Complies (observed).** **VM:** with the lower bind-mounted read-only, a write through the
layer failed and the lower was unchanged. The layer does not copy `SB_RDONLY` from the lower
superblock at mount (unlike ecryptfs), but the lower's own `mnt_want_write`/`notify_change` rejects
the write, so the property holds; copying `SB_RDONLY` would make it fail earlier and clearer, which
is a possible hardening, not a defect.

## 7. Memory limits and allocation discipline

**R-MEM-1 — bounded index memory; a user cannot grow kernel memory without limit.**
Source: `Documentation/core-api/memory-allocation.rst:53-56` (untrusted allocations from userspace
should be accounted) and the project's `index_budget` design.
Status: **Partially complies; one known gap (ADR-0011 F2).** Every index allocation goes through
`charge()`/`uncharge()` against `ix->budget` (default 128 MiB), returning `-ENOMEM` past the cap
(`layer/index.c:124-135,200,231,384,415`). The gap is that the budget is **one pool for the whole
mount**: an unprivileged writer can fill it and mark the index incomplete for everyone until
remount. This is ADR-0011 finding F2, tracked under S9, surfaced by `GET_INFO` and `moobfs-check`;
it is a cross-user degradation, not a crash. The allocations are **not** `__GFP_ACCOUNT`/
`GFP_KERNEL_ACCOUNT`, so they are not charged to a memcg either; the `memory-allocation.rst:53-56`
recommendation for userspace-triggered allocations is **not met**. (Per-uid accounting or
`__GFP_ACCOUNT` are the S9 candidates.)

**R-MEM-2 — `GFP_ATOMIC` only where sleeping is disallowed; prefer `GFP_KERNEL`.**
Source: `Documentation/core-api/memory-allocation.rst:39-52,107`.
Status: **Complies.** `GFP_ATOMIC` appears only in `add_candidate` (`layer/index.c:1391,1399`),
which runs under `down_read(&ix->lock)` where sleeping is not allowed; everywhere else is
`GFP_KERNEL`/`kvmalloc` (`layer/index.c:202,233,389,536,1572-1575`, `layer/main.c:63,666,1183`).

**R-MEM-3 — no `BUG()`/`BUG_ON()` on user-reachable paths; handle the impossible gracefully.**
Source: `Documentation/process/deprecated.rst:32-40` ("Use WARN() … instead"); KSPP sets
`CONFIG_BUG_ON_DATA_CORRUPTION=y` and `panic_on_oops` (kspp.github.io Recommended Settings,
`:109,288`), i.e. a reachable `BUG()` becomes a panic on a hardened kernel.
Status: **Complies in the layer's own code** (no `BUG`/`BUG_ON`; one `BUILD_BUG_ON` for the compile
-time cursor size, `layer/index.c:1809`, which is allowed). **But** the layer reaches a VFS `BUG()`
indirectly via R-CRED-3: on a KSPP kernel with `panic_on_oops`/`oops_limit=1` that crash is an
immediate panic. This raises the severity of R-CRED-3, it is not a second defect.

**R-MEM-4 — `rcu_barrier()` before destroying a cache the module frees via `call_rcu`.**
Source: `Documentation/RCU/rcubarrier.rst:29-64` (modules with pending `call_rcu` callbacks must
`rcu_barrier()` at exit).
Status: **Complies.** `moobl_exit` calls `unregister_filesystem`, then `rcu_barrier()`, then
`kmem_cache_destroy` (`layer/main.c:1321-1326`), which is required because `moobl_d_release` queues
`moobl_dentry_free_rcu` via `call_rcu`.

## 8. Mount surface and submission hygiene

**R-MOUNT-1 — `FS_USERNS_MOUNT` off; mounting needs `CAP_SYS_ADMIN` in the init namespace; idmapped
mounts refused.**
Source: overlayfs sets `FS_USERNS_MOUNT` (`fs/overlayfs/super.c:1481`) and carries the CVE class
that goes with it; ecryptfs refuses idmapped mounts (`fs/ecryptfs/main.c:526-528`). ADR-0011 makes
the opposite choice explicit.
Status: **Complies.** `moobl_fs_type` sets no `fs_flags` (`layer/main.c:1292-1298`), so no
`FS_USERNS_MOUNT`/`FS_ALLOW_IDMAP`; user-ns mounts fail `EPERM`, idmapped `EINVAL` (journal 0020).
This removes the overlayfs unprivileged-mount CVE class (ADR-0011, CVE-2015-1328 et al.).

**R-MOUNT-2 — superblock flags: inherit `SB_POSIXACL` from the lower, mark NOEXEC/NODEV where the
layer should not grant them, set `SB_NOSEC` only when the lower clears suid.**
Source: ecryptfs (`fs/ecryptfs/main.c:547-548` POSIXACL inherit); overlayfs
(`fs/overlayfs/super.c:518-519` NOSEC inherit).
Status: **Complies.** `moobl_fill_super` inherits `SB_POSIXACL` from the lower (`layer/main.c:1228-1229`),
sets `SB_I_NOEXEC | SB_I_NODEV` (`:1230`) and copies `s_maxbytes`/`s_time_gran`/`s_blocksize` from
the lower (`:1223-1226`). It sets `SB_NOSEC` unconditionally (`:1227`) with the comment "lower
enforces suid/sgid clearing" — defensible, but it is the suid-clearing path that R-CRED-3 crashes
on, so the `SB_NOSEC` assumption and that bug should be reconsidered together.

**R-SUBMIT-1 — out-of-tree module build conventions (`M=`, `KDIR`, `MODULE_LICENSE` matching the
`_GPL` exports it uses).**
Source: `Documentation/kbuild/modules.rst:46-69` (`make -C $KDIR M=$PWD`); the module uses
`_GPL`-only exports (`backing_file_*`, iomap) so its license must be GPL-compatible.
Status: **Complies.** `layer/Makefile` is `obj-m += moobfs_layer.o` with the kbuild `M=` pattern
(`Makefile:76` `layer:` target), and `MODULE_LICENSE("GPL")` is set (`layer/main.c:1330`), which is
what the `_GPL` exports it calls require.

**R-SUBMIT-2 — the kernel has no stable internal API; pin to the exact kernel and re-verify on
bumps.**
Source: `Documentation/process/howto.rst:127-138` (stable-api-nonsense) and
`Documentation/filesystems/porting.rst` (per-release API changes, e.g. the `lock_rename` `-EXDEV`
mandatory note at `:1087`).
Status: **Complies with the practice.** The project pins `6.12.111`/`6.12.112` and rebuilds under
KASAN. This review confirms the cited APIs are stable between `v6.12` and `v6.12.112`; the
`fs/backing-file.c` signature differs in a cosmetic way between the two tags
(`backing_file_open(const struct path *…)` in `v6.12` vs `(const struct file *…)` in `v6.12.112`),
which the module tracks through the kernel headers it builds against, not a cited-line change.

**R-SUBMIT-3 — static analysis and lockdep/KASAN before claiming correctness.**
Source: `Documentation/process/submit-checklist.rst:66,70,117-128` (checkpatch, sparse, the lockdep
matrix, fault injection).
Status: **Partially verified.** checkpatch, sparse and W=1 are in CI (journals 0050, 0056); KASAN +
lockdep + `DEBUG_ATOMIC_SLEEP` + `PROVE_RCU` run in `vm/kasan.config`. **Fault injection**
(`submit-checklist.rst:124-128`, slab/page-allocation failure injection) is **not verified** — the
charge-path error returns are exercised only by the budget cap, not by injected allocator failures.
The full `CONFIG_PREEMPT`/`!SMP` matrix of `submit-checklist.rst:110-124` is not run either.

## 9. KSPP hardening (community source, not kernel.org)

The KSPP "Recommended Settings" (`kspp.github.io/Recommended_Settings`, mirror
`kernsec.org/wiki/.../Recommended_Settings`) is a hardening checklist, not primary kernel
documentation. Checked against `build/vm/kernel.config` (the Debian `6.12.111` kernel the suite
runs) and `vm/kasan.config`:

- `CONFIG_FORTIFY_SOURCE=y`, `CONFIG_HARDENED_USERCOPY=y`, `CONFIG_SLAB_FREELIST_HARDENED=y`,
  `CONFIG_BUG_ON_DATA_CORRUPTION=y`, `CONFIG_DEBUG_LIST=y`, `CONFIG_INIT_ON_ALLOC_DEFAULT_ON=y`:
  **present in the Debian build config.** Relevant because the module's usercopy
  (`copy_to_user`/`memdup_user`) and list use (`list_add`/`list_del` in the index) run under them.
- `CONFIG_MODULE_SIG_FORCE=y`, `kernel.modules_disabled=1`, `user.max_user_namespaces=0`,
  `panic_on_oops`/`oops_limit=1`: **KSPP recommends; the project's throwaway VM does not set them**
  (by design — it loads an unsigned test module in a disposable guest, which the harness restricts
  to QEMU, `vm/run.sh`). This is an acceptable deviation for a test harness, but it means the
  R-CRED-3 crash would be a hard panic on a KSPP-hardened production kernel
  (`panic_on_oops`), reinforcing its severity.
- `CONFIG_UBSAN_BOUNDS=y`, KASAN: **present in `vm/kasan.config`** for the instrumented runs.

KSPP self-protection rationale aligns with `Documentation/security/self-protection.rst:13-20` (worst
-case: unprivileged local attacker) — the same adversary A1 of ADR-0011.

## 10. Items the sources require that this review did not verify

- **R-LOCK-5** kernel-stall / `cond_resched` behaviour on a very large lower tree.
- **R-MEM-1** memcg accounting (`__GFP_ACCOUNT`) — confirmed *absent*; the per-mount budget is the
  only bound (F2).
- **R-SUBMIT-3** allocator fault injection and the full `CONFIG_PREEMPT`/`!SMP` lockdep matrix.
- **syzkaller (S3) and adversarial-lower fuzzing (S4/S7)** — not part of this review; these are the
  open roadmap items this document is meant to feed.

## References (primary, by file and version)

Kernel `Documentation/` (torvalds `v6.12`): `filesystems/overlayfs.rst`, `filesystems/ecryptfs.rst`,
`filesystems/fuse.rst`, `filesystems/locking.rst`, `filesystems/directory-locking.rst`,
`filesystems/path-lookup.rst`, `filesystems/porting.rst`, `filesystems/vfs.rst`,
`filesystems/idmappings.rst`, `filesystems/mount_api.rst`, `filesystems/index.rst`;
`process/botching-up-ioctls.rst`, `process/adding-syscalls.rst`, `process/deprecated.rst`,
`process/submit-checklist.rst`, `process/submitting-patches.rst`, `process/coding-style.rst`,
`process/howto.rst`, `process/stable-api-nonsense.rst` (referenced via howto);
`driver-api/ioctl.rst`, `userspace-api/ioctl/ioctl-number.rst`, `core-api/memory-allocation.rst`,
`kbuild/modules.rst`, `security/credentials.rst`, `security/self-protection.rst`,
`security/siphash.rst`, `RCU/rcubarrier.rst`, `RCU/stallwarn.rst`.

Kernel source (`v6.12`, lines re-checked against `v6.12.112`): `fs/attr.c`, `fs/backing-file.c`,
`fs/stack.c`, `fs/namei.c`, `fs/namespace.c`, `fs/dcache.c`, `fs/inode.c`, `fs/xattr.c`,
`include/linux/fs.h`, `include/uapi/linux/limits.h`; `fs/overlayfs/{super,inode,file,dir,util,xattrs,
namei,readdir}.c` and `overlayfs.h`; `fs/ecryptfs/{main,inode,dentry}.c`;
`fs/fuse/{passthrough,dir,xattr,inode}.c`.

Not a primary kernel source, cited as requested: KSPP Recommended Settings
(`kspp.github.io`, `kernsec.org` wiki).

Note on availability: `Documentation/filesystems/adding-new-filesystems.rst`, cited by
`docs/security-roadmap.md:69`, does not exist at `v6.12`..`v6.14`; it is present only on `master`.
