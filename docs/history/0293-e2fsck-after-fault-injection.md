# 0293 — The ext4 lower after the KASAN fault injection: e2fsck

Date: 2026-11-14. Work started about 07:50 Brasília time; ended about 08:30. Status: **done; in
the pull request of batch 28, with the maintainer's agreement.** Estimated effort S; the time
recorded is calendar time of one morning session.

## Goal

The console of the full KASAN runs (journals 0289 and 0291) shows `EXT4-fs (vda): warning:
mounting fs with errors, running e2fsck is recommended` after the `renamepath-fault` step. Find
out where it comes from and whether the steps after it run on a damaged lower.

## Finding

The step `faults` of `vm/kasan-guest.sh` (journal 0096) fails, one by one, the allocations made
with the module on the stack (`require-start`/`require-end` of `failslab` and `fail_page_alloc`).
ext4 below the layer is called from the module, so its allocations get the faults too. In both
consoles:

```
EXT4-fs error (device vda) in __ext4_new_inode:1092: Out of memory
EXT4-fs (vda): ext4_do_writepages: jbd2_start: 9223372036854775807 pages, ino 102; err -12
EXT4-fs error (device vda) in ext4_evict_inode:234: Out of memory
```

`ext4_std_error()` records the error in the superblock. The lower is unmounted at the end of the
step, and the next step, `fallbackfault`, mounted the same device with the record still set:
that is the warning. Nothing checked what the faults left on the disk.

## Change

`vm/kasan-guest.sh`: after the step `faults` unmounts the lower, `e2fsck -fy` runs on it; its
output is kept in `build/vng/e2fsck-faults.txt` and printed, and an exit status above 1 (damage
left uncorrected, or e2fsck failed) fails the run (`fi-lower-ext4-damaged`). Exit 1 is accepted:
ext4's error paths leave work for e2fsck by design (below). It also clears the record, so the
next step mounts a clean lower. `docs/testing.md` (the `FAULT_INJECTION_PASS` row), `CHANGELOG.md`,
`docs/references.md` §64. No file under `layer/` changed.

Alternatives not taken: a fresh image for `fallbackfault` (it hides what the faults left, which
is what was not known); filtering the warning (the same).

## Evidence

Test host, Debian 13, KVM, commit `5eea26b`, `MOOB_ONLY="faults fallbackfault" sh
vm/kasan-local.sh` (upstream 6.12.116, KASAN, UBSAN, lockdep), 08:07–08:10:

- `FAULT_INJECTION_PASS: operations=22 calls=333 faults=311 unexpected=0`; the same three ext4
  error lines as before.
- e2fsck, exit 1: `Deleted inode 56 has zero dtime`, inode bitmap difference `-56`, free-inode
  counts one short, each fixed.
- `FAULTS_E2FSCK: exit 1 after the injected faults`; `fallbackfault` passed; no `mounting fs
  with errors` in the console; `MOOBFS_KASAN_ONLY_PASS`.

Full suite, test host, a fresh clone of `1781572` (this branch before the rebase onto #201; the
trees differ only in journal 0291's list of merges), `sh vm/kasan-local.sh`, 08:41–09:55:
`MOOBFS_KASAN_PASS`, `FAULTS_E2FSCK: exit 1 after the injected faults`, and no kernel
diagnostics nor `mounting fs with errors` in the console.

In 6.12.116's `fs/ext4/inode.c`, `ext4_evict_inode()` handles a failed `ext4_journal_start()`
with `ext4_std_error()` and `goto no_delete`: the inode stays allocated on disk and is left for
e2fsck. That is the inode e2fsck freed. `__ext4_new_inode()` treats a failed journal start the
same way (`fs/ext4/ialloc.c`). So the leak is ext4's designed answer to an allocation failure,
not something the layer did; the layer's own state after the faults is what `failnth` and the
oracle comparison already check.

## Limitations

- One run. Which inode leaks, and whether one does, depends on which allocation fails; other
  runs may need e2fsck for other repairs, all of which exit 1 if they succeed.
- Only the ext4 lower of the KASAN harness is checked; the step does not run on btrfs or XFS.

Pre-push checks on `ac2a265` (base `138f250`), 09:55–10:00: lint, checkpatch, test, asan,
hostile, pylib, retired, pins, mutants, site, diffcheck, ubuntu-tests: all PASS.

## Next step

The pull request of batch 28 after batch 27's; the KASAN check of CI then runs e2fsck too.
