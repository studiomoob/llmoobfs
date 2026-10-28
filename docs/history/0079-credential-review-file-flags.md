# 0079 — Review of the mounter-credential sites; file flags passed through

Date: 2026-10-13. Status: review done, no new defect; file flags implemented and tested
(`make vm-test`, two negative runs, local KASAN, fstests re-run). Local series (after pull
request 58).

## Goal

Two things the maintainer asked for after journals 0074 and 0075:

1. the follow-up of journal 0074: check every place where the layer acts on the lower with the
   mounter's credentials for decisions the lower takes from the caller's capabilities;
2. the fstests gaps of journal 0075, following the recommendation: pass the file-attribute calls
   through; decide what to do about the layer's `atime` mount options.

## 1. Review

Method: every use of the mounter's credentials in `layer/` listed (`override_creds`,
`backing_file_open`, `dentry_open`, the `backing_file` context), and for each one the path in
the kernel source of the exact build (6.12.112) read for checks that depend on the caller:
`capable()`, ownership, the file mode, inode flags. The question for each: is that check made
with the caller's credentials before the layer switches?

| Site | Lower call as the mounter | Caller-dependent decision | Where the caller is checked |
| --- | --- | --- | --- |
| `moobl_open` | `backing_file_open` | open permission, `O_NOATIME` ownership, append-only and immutable | the VFS on the layer's inode before `->open` (`may_open`), through `moobl_permission`, which asks the lower inode with the caller's credentials |
| `moobl_dir_open` | `dentry_open` | read permission on the directory | same |
| read, write, splice, mmap | `backing_file_*` | set-user-ID, set-group-ID and capabilities dropped on write; quota; reserved blocks | `file_remove_privs` on the layer's file before the lower call; `CAP_SYS_RESOURCE` lowered at mount (journal 0069); ext4's reserve for uid 0 stays open (F4) |
| `moobl_fallocate` | `vfs_fallocate` | as a write | `file_remove_privs` first (journal 0074) |
| `moobl_copy_file_range`, clone | `vfs_copy_file_range`, `vfs_clone_file_range` | as a write | `file_remove_privs` first (journal 0074) |
| dedupe | `vfs_dedupe_file_range_one` | who may dedupe into the target: administrator, a descriptor open for writing, the owner, or write permission (`allow_file_dedupe`) | the same function runs on the layer's files, with the caller, before `->remap_file_range`. The lower's own check then passes for the mounter. **New test** |
| `moobl_fsync`, `moobl_sync_fs` | `vfs_fsync_range`, `sync_filesystem` | none | not needed |
| `moobl_fiemap` | the lower's `->fiemap` | none | a descriptor is enough, as on the lower |
| removal of `security.capability` | `vfs_removexattr` | `CAP_SETFCAP` | `cap_inode_removexattr` on the layer's inode before the handler; the kernel's own removal on write is meant to succeed for any writer (journal 0074) |
| index build at mount | directory reads, `getxattr` | none | mounting needs `CAP_SYS_ADMIN` |

*Note, 2026-10-28 (journal 0218, F16 of ADR-0011):* the row "read, write, splice, mmap" was
wrong about set-user-ID and set-group-ID. The write credentials kept the mounter's `CAP_FSETID`,
so the lower's own drop on write kept the bits for every writer, and the layer's
`file_remove_privs` judged a mode its inode may hold from before a chmod made on the lower.
Found by an external reviewer; since journal 0218 the credentials carry `CAP_FSETID` only when
the writer has it.

Everything else (lookup, create, mkdir, unlink, rmdir, rename, link, symlink, `setattr`,
`setxattr`, ACLs, and now the file flags) calls the lower with the caller's credentials, so the
lower's own checks apply unchanged.

**Result: no new defect.** One finding about how the layer keeps up the VFS-side checks:

- `moobl_copy_attr` copied the lower inode's whole `i_flags` word into the layer's inode with a
  plain assignment. That carried the two flags the checks above rely on (`S_APPEND`,
  `S_IMMUTABLE`), and also flags that describe the lower's private state (DAX, encryption,
  verity, casefold, swap file, quota file, `S_NOSEC`), unlocked against the VFS's own updates of
  the same word. No failure was produced from it. It now copies `S_SYNC`, `S_NOATIME`, `S_APPEND`
  and `S_IMMUTABLE` with `inode_set_flags`, as overlayfs does (`OVL_COPY_I_FLAGS_MASK`).

Limits of the review, stated so nobody reads more into it:

- It is a reading of the code by the same author, backed by tests for the delicate cases; it is
  not the independent review of community action 15.
- No Linux security module policy was exercised. The lower's `security_*` hooks for data
  operations see the mounter. Three points raised by a separate inspection session were checked
  against the 6.12.112 source and are the same as overlayfs there: `getattr` on the lower skips
  `security_inode_getattr` (overlayfs's `ovl_do_getattr` takes the `vfs_getattr_nosec` branch
  whenever `AT_GETATTR_NOSEC` is set, and `vfs_getattr_nosec` sets it on every `->getattr` call,
  `fs/stat.c:168`); `->fiemap` and `->flush` of the lower are called directly
  (`fs/overlayfs/inode.c:602`, `fs/overlayfs/file.c:594`). **SELinux was never loaded in a
  test**; the label of a layer inode comes from the lower's `security.selinux`, so this is the
  most relevant untested configuration. AppArmor was loaded in journal 0030 but not exercised.
- Append-only and immutable are enforced from the layer inode's copy of the flags. A flag set
  directly on the lower while the layer is mounted reaches that copy at the next attribute
  refresh; that is adversary A2 of ADR-0011 (changes behind the layer), not a new promise.

## 2. File flags

Decision: `lsattr`, `chattr`, `FS_IOC_FSGETXATTR` and `FS_IOC_FSSETXATTR` are the lower's,
passed through with the caller's credentials (`moobl_fileattr_get`, `moobl_fileattr_set`), for
files and directories. The lower decides who may change what: the owner, and `CAP_LINUX_IMMUTABLE`
for append-only and immutable.

One refusal: changing the casefold flag of a directory through the layer returns `EOPNOTSUPP`.
The layer's name cache is case-sensitive; a case-insensitive lower directory under it would give
two names for one entry.

`statx` now reports the lower's compressed, immutable, append-only and no-dump attributes and
its creation time. fstests `generic/424` showed they were missing once the flags could be set.

## 3. Access-time mount options

Decision: **not implemented; documented as a limitation.** `noatime`, `relatime` and
`strictatime` on the layer's mount have no effect: reads go to the lower file, and the lower's
mount options decide. To control access times, mount the lower with the wanted option.

Why not the overlayfs way, where the overlay's mount decides: it needs a private clone of the
lower mount with its access-time flags cleared, lower files opened with `O_NOATIME`, and an
`update_time` operation. That changes how the layer holds its lower (a design change that needs
its own ADR) for a property an agent-memory store has little use for. `generic/003` and
`generic/120` go to the fstests exclusion list with this reason.

## Tests

`tests/layer.c`, run over ext4, btrfs and XFS:

- an ordinary user holding a read-only descriptor of a file they cannot write asks to dedupe
  into it: the status is `EPERM` and no extent is shared. (Passes with and without this change;
  it fixes the review's conclusion.)
- flags read through the layer equal the lower's; a user who is not the owner, and the owner
  without the capability, cannot toggle append-only; set by root through the layer it shows on
  the lower and in `statx`; then opening for writing without `O_APPEND`, truncating and unlinking
  fail with `EPERM` while appending works; immutable refuses every open for writing; clearing
  restores the file.

## Evidence

- `make checkpatch`: clean.
- `make vm-test`: every marker through `MOOBFS_LAYER_PASS`.
- Negative runs: without `layer/main.c`'s change, `FAIL` at the first `FS_IOC_GETFLAGS`
  (`ENOTTY`); with everything but the two `statx` lines, `FAIL` at the `statx` check. Both end
  in `MOOBFS_VM_FAIL: layer-tests-ext4`.
- Local KASAN (`vm/kasan-local.sh` on the benchmark host, upstream 6.12.112 with KASAN, UBSAN and
  lockdep): `MOOBFS_KASAN_PASS`, no kernel diagnostic.
- fstests in the guest, fixed module, on ext4, XFS and btrfs: `generic/169`, `generic/420` and
  `generic/424` pass, with `generic/277`, `545`, `553` and `555`, which used to be skipped for
  the missing calls. `generic/079` now runs and fails in one sub-test, which creates a device
  node (by design). No kernel diagnostic.

## Limitations

- Those of the review, listed in section 1.
- The full fstests runs were not repeated; only the tests named above.
- Project identifiers and the other XFS-specific fields of `FS_IOC_FSSETXATTR` go to the lower
  unchanged and were not tested beyond what those fstests cover.
- `generic/607` (per-file DAX flag) fails in the fstests port on ext4 and XFS for a reason of
  the port (it remounts with an option string of its own); not investigated.

## Next step

The rest of security roadmap S6; a test under SELinux as a new item of the security roadmap.
