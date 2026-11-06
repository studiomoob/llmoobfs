# 0268 — hookrace: a test of the rename path of finding F2

Date: 2026-11-05. Status: **done locally** (test only; not published). Calendar time: about
08:20–11:13 Brasília time, most of it waiting for a gap between S18 campaigns on the test
host; effort under an hour. Estimated effort: S.

## Goal

Close the limitation of journal 0266: rename over a file that keeps other names calls the
new hook (`moobl_index_unlinked()` in `moobl_rename()`), but had no test of its own. The
kernel is frozen to fixes and tests (ADR-0022); this is a test, and `layer/` is unchanged.

## Change

- `tests/hookrace.c`, new mode `renameover`: the `unlink` mode with the second name removed
  by `rename(DIR-src/f, n2)` instead of `unlink(n2)`, at the same moment (the middle of the
  reading through n2, timed on a twin domain). `DIR-src` is a domain no query builds, and its
  file has no attribute, so it does not change the timing. After the rename the test checks
  that n1 keeps the inode and n2 names another one, then asks the mount-wide `QUERY` while
  cached and after the dentry cache is dropped.
- `vm/suite.sh`, step `hookrace`: runs the mode on ext4, btrfs and XFS after `unlink`, with
  its own window check (the `moobfs_scan_reread` tracepoint must count a repeated reading)
  and the marker `HOOK_RACE_RENAMEOVER_PASS`.
- Why the negative holds, from the code (kernel 6.12.112 `fs/dcache.c`): the lower rename
  unhashes the dentry of n2 (`__d_move()` drops the target), so the scan's next pass sees the
  name removed, as after an unlink; but only `index_touched()` on the target's inode makes it
  read again, and in the rename path only the new call does that. Without it the record keeps
  `n2`, which after the rename resolves to the other file; `caller_may_read()` finds another
  inode number and leaves the file out.

The test was written by a separate agent from the code and the specification; the
coordinator reviewed the diff and ran it.

## Evidence

- Offline: `gcc -static -O2 -Wall -Wextra -Werror` of the test (gcc 14.4) and `shellcheck` of
  `vm/suite.sh`, clean; retired-claims and SPDX checks pass.
- Native VM, Mac mini, Debian 6.12.113 KVM guest, `MOOB_ONLY=hookrace`, alone on the host
  between S18 campaigns c02 and c03 (source `ae0dd60`):
  - **With the corrections** (11:10–11:11): `HOOK_RACE_PASS` and `HOOK_RACE_RENAMEOVER_PASS`
    on ext4, btrfs and XFS, each `readings_repeated=1`, `MOOBFS_ONLY_PASS`, exit 0, source
    hashes unchanged.
  - **Without the rename call only** (the `else if` branch of `moobl_rename()` that calls
    `moobl_index_unlinked()` removed; the unlink call kept), 11:11–11:12: `rename` and
    `unlink` passed, then `HOOKRACE_FAIL: [renameover] QUERY on x does not return the file:
    its record names the replaced n2` and `MOOBFS_VM_FAIL: hr-renameover-ext4`, exit 1.
- No KASAN run: the change is a test, with no locking or lifetime change in the module. The
  full suites of the next batch run it.

## Batch suites before publication (2026-11-06)

Run on the test host on `44ac807` (two separate copies of the tree, source hashes checked
after each run), during S18 campaign c04's fuzzing on the other CPUs. Since then the code
changed only in a comment of `include/moobfs_ioctl.h` (`8f9ce8c`).

- Full native suite (`make vm-test`, Debian 6.12.113 KVM guest, CPUs 0–1), 01:36–01:59
  Brasília time: exit 0, no `_FAIL`; `HOOK_RACE_PASS` and `HOOK_RACE_RENAMEOVER_PASS` on ext4,
  btrfs and XFS; source hashes unchanged.
- Full KASAN/UBSAN/lockdep suite (`vm/kasan-local.sh`, CPUs 2–3), 01:38–03:41: exit 0,
  `MOOBFS_KASAN_PASS`, the same `hookrace` markers on all three lowers, no kernel diagnostic
  in the console; source hashes unchanged.
- Pre-push checks, 03:41–03:46: `make lint`, `make checkpatch`, `make test`, the same under
  clang ASan/UBSan, `make hostile-test`, `make pylib-test`, `scripts/check-retired-claims.sh`
  and the build of the suite's test programs on Ubuntu 24.04: all pass.

By the maintainer's decision (2026-11-06), these runs are the batch's full suites for its
pull request: they are not repeated after the last S18 campaign, and only the pre-push checks
run again after the rebase.

## Limitations

- As with `unlink`, the homogeneous case (the replace between the loop-head check and the
  classification) is not reached; the model covers it (`IndexLoadPreF2`).
- Rename over a directory and `RENAME_EXCHANGE` keep their earlier tests.

## Next step

Publish with the batch's records, after the maintainer's approval.
