# 0272 — Widening the kernel matrix (in progress)

Date: started 2026-11-07, 18:40 Brasília time. Status: **in progress**. Estimated effort L.

## Goal

The second phase agreed on 2026-11-07 (journal 0270): run the module on more than one kernel
series. The maintainer confirmed the matrix (6.12 plus newer series) and, once the probe below
showed the module must change for them, a second narrow exception to the freeze (ADR-0034).

## Batch 14 merged

PR 187 (S15 and S16 done, ADR-0033, the weekly S18 schedule, `actions/setup-java` 6.2.0) passed
its 19 checks, the mutation shards and KASAN/lockdep included, and was merged by the
maintainer's approval on 2026-11-08 at 09:39 Brasília time with a merge commit (`1cac601`); the
post-merge runs on `main` (CI, static analysis, TLA+ model, CodeQL, OpenSSF Scorecard) all
passed. Dependabot's PR 185 was closed with a pointer to it. This branch was rebased on it.

## Compile-only probe (2026-11-07, 18:40–18:50, test host)

The `layer/` of `claudiofp/b14-stage2` (with `common/` and `include/`), unchanged, built with
`make -C linux-<v> M=... modules` against `defconfig` plus POSIX ACLs on ext4, btrfs and XFS and
tracepoints, after `modules_prepare`; tarballs from cdn.kernel.org checked against
`sha256sums.asc`. Nothing was loaded.

| Release | Result |
| --- | --- |
| 6.12.116 | builds, no error, no warning |
| 6.18.62 | 55 errors, all in `main.c` |
| 7.3.3 | 66 errors, all in `main.c` |
| 6.6.162, 6.1.192 | stop at the missing `linux/backing-file.h` |

A first run of the probe failed on 6.12.116 because the copy lacked `common/`; it was a fault of
the probe, corrected by copying the tree as the build expects.

## The upstream test kernel moves to 6.12.116 (2026-11-07)

ADR-0034, point 5. The full KASAN/UBSAN/lockdep suite (`vm/kasan-local.sh` with `kasan.yml`'s
pin set to 6.12.116 in a copy of the tree, its own kernel cache) ran on the test host, CPUs 0–3,
18:55–20:30 Brasília time (kernel build until 19:24): `MOOBFS_ACCEPTANCE_PASS`,
`MOOBFS_LAYER_PASS`, `MOOBFS_KASAN_PASS`, no `_FAIL`, no kernel diagnostic in the console;
`SELFCHECK_PASS` and `HOOK_RACE_RENAMEOVER_PASS` on the three lowers. The tarball's SHA-256
(`cb3ed3dfdd47e6ade49dc933db334fd86e24cadee920da612c262b0e9b6a511b`) is kernel.org's.

Every copy of the pin moves together, as `scripts/check-pins.sh` requires: `kasan.yml`,
`kasan-arm64.yml`, `kcsan.yml`, `interop.yml`, `coverage.yml`, `docs/development.md` and
`tests/static/Dockerfile`, and the checkpatch tag in `scripts/checkpatch.sh` (`v6.12.116`; the
three files it fetches have the same SHA-256 as at `v6.12.112`). `sh scripts/check-pins.sh`:
`PINS_OK`, the tarball checked against kernel.org; `sh scripts/checkpatch.sh`: clean. Only the x86
KASAN suite ran locally on 6.12.116; the arm64 KASAN, KCSAN, coverage, interop and static-analysis
jobs first run on it in CI. Current-state documents name 6.12.116 for the harness; records of
earlier runs keep their version.

## Debian 13 moved to 6.12.115 (2026-11-08)

The package test of journal 0273 booted `6.12.115+deb13.1-amd64`: Debian published it in
`trixie-security` (package version `6.12.115-2`), and the cloud image installs it by itself. A
Debian 13 host that updates is then outside the matrix (6.12.111, 6.12.113), and DKMS does not
build the module for it, as ADR-0028 states for any unlisted kernel. Adding it follows rule 3 of
`docs/compatibility.md`:

- `vm/Dockerfile` and `vm/in-container.sh` accept `MOOB_DEBIAN_KERNEL=6.12.115` (`fa5391e`).
- The full native suite on it (`vm/run.sh`, KVM, CPUs 4–7), 11:41–12:01: `MOOBFS_LAYER_PASS`,
  `MOOBFS_ACCEPTANCE_PASS`, no `_FAIL`.
- fstests `quick` with `vm/fstests/exclude-quick.txt` on ext4, btrfs and XFS, booted by
  `vm/fstests-local.sh` with `MOOB_KVER=6.12.115+deb13.1-amd64` (journal 0206's method), in
  parallel on CPU pairs, 12:06–14:33: ext4 and XFS passed all 631 tests; btrfs failed one,
  `generic/301` ("file2 badly fragmented": 1 378 extents after random CoW, threshold 1 365).
  Run alone three more times: 1 425 (fail), 1 072 (pass), 1 466 (fail). It is the failure of
  journal 0239: fstests skips that threshold when `FSTYP` is btrfs, because btrfs's CoW extent
  allocation depends on transaction commits, but over the layer `FSTYP` is `moobfs_layer` and the
  check runs; on 6.12.113 the count happened to stay under it. `generic/301` is in
  `vm/fstests/exclude-auto-btrfs.txt` for the `auto` group and not in the `quick` list.
- **Not added to `scripts/supported-kernels`**: rule 3 asks for the `quick` runs with the
  exclusion list and no failure. Whether to exclude `generic/301` on btrfs for `quick` too, as for
  `auto`, is the maintainer's decision.
- Test host: to install that kernel and its headers without changing what the host boots, its
  `GRUB_DEFAULT` changed from the position `"1>2"` (which a new kernel shifts) to the entry's
  name for 6.12.111, the kernel it was booting; the old `/etc/default/grub` is kept on the host.

## Port map to 7.3.3 (read from the sources, nothing changed yet)

The 66 errors come from 21 interfaces in `layer/main.c`, read against the 7.3.3 tree and its
`Documentation/filesystems/porting.rst`, with overlayfs (`fs/overlayfs/`) and
`fs/backing-file.c` of 7.3.3 as the model. Most are renames or signature changes:
`inode_state_read_once()`, `inode_generic_drop()`, `lookup_one*()` with a qstr,
`delegated_inode` arguments of `vfs_create`, `vfs_rmdir` and `vfs_symlink`, `struct file_kattr`,
`.create` without `excl`, `.mkdir` returning a dentry, `struct backing_file_ctx` without
`user_file` and with `end_write(kiocb, ssize_t)`, the backing-file splice helpers taking a
kiocb, `set_default_d_op()`. Five change semantics or locking and are ported one by one:

1. `vfs_mkdir()` returns a dentry and, on error, has already unlocked the parent and dropped the
   dentry (porting.rst); the module's own unlock would be a second one.
2. `lock_rename()`/`unlock_rename()` are gone; `start_renaming_dentry()`/`end_renaming()` lock,
   look up the target and check the trap. Two rare races change errno: `RENAME_NOREPLACE` onto a
   lower that became positive gives `EEXIST`, `RENAME_EXCHANGE` onto one that became negative
   `ENOENT`, where the module returns `ESTALE` on 6.12.
3. `AT_GETATTR_NOSEC` is gone: the module keeps calling `vfs_getattr_nosec()` on the lower, as
   overlayfs does, which is what every call did on 6.12.
4. The `lookup_noperm*()` forms skip the parent's search check; `lookup_one*()` keeps it.
5. The lower's `d_revalidate()` needs a stable parent and name (overlayfs takes a name snapshot).

Also: without `->setlease`, 7.3 refuses leases with `EINVAL`; the port sets `generic_setlease`,
as overlayfs does, so that leases behave as on 6.12.

## Next steps

1. The port to 7.3 within ADR-0034, read against the 7.3.3 sources, then the suite and KASAN on
   a 7.3 kernel.
