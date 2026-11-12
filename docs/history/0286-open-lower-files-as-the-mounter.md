# 0286 — Open lower files as the mounter: the fix for the confined-mounter limit (proposed)

Date: 2026-11-11. Work started about 21:50 Brasília time. Status: **in progress, local, not
published; the freeze exception (ADR-0037) is proposed and needs the maintainer's acceptance.**
Estimated effort S; the time recorded is calendar time of one session.

## Goal

Prepare, while the maintainer is away, the later batch that journal 0284 left for the SELinux
limit found that evening: under SELinux the domain that mounts the layer needs `fd use` on every
caller's domain, because the layer opens lower files as the caller and reads them as the mounter.
Nothing here is merged or published without the maintainer's decision.

## Change

- `layer/main.c`: `moobl_open()` and `moobl_dir_open()` open the lower file under
  `override_creds()` with the mounter's credentials, as overlayfs does in `ovl_open_realfile()`
  and `ovl_dir_open_realfile()` (Linux 6.12.116, read on the test host). The credentials passed
  to `backing_file_open()` and `dentry_open()` are the same as before; what changes is the task
  identity the LSM reads when it allocates the file.
- `vm/selinux/moobtest.te`: a permissive domain `moobtest_mounter_t` and a reader domain
  `moobtest_reader_t` (lists `moobtest_dir_t`, reads `moobtest_ro_t`, `fd use` on `kernel_t`
  only, which it needs to map its own executable).
- `vm/selinux-guest.sh`, fsuse run: a second mount of the lower made in `moobtest_mounter_t`; the
  reader lists a directory (`tests/listdir.c`) and reads a file through it with the audit printk
  limit lifted; the step fails on any `fd use` from the mounter to the reader, and as proving
  nothing if the mounter's own `open` of the lower directory is not logged. Marker
  `SELINUX_MOUNTER_PASS`.
- `docs/decisions/0037-open-lower-files-as-the-mounter.md`: the exception, proposed.

## Evidence

- `make layer` on the test host (Debian 13, 6.12 headers): no error or warning. `make checkpatch`:
  clean.
- VM runs queued on the test host after the batch-22 suites: the SELinux run (expected
  `SELINUX_MOUNTER_PASS`) and its negative round with the old `layer/main.c` (expected
  `MOOBFS_SELINUX_FAIL: fsuse: confined mounter: the mounter needed fd use on the reader`), then
  `MOOB_ONLY="ext4 btrfs xfs" make vm-test`. Results are recorded below when they finish.
- `sh vm/selinux-local.sh` on `3c6f8eb` (test host, ended 23:33): `fsuse confined mounter:
  list=0 read=0 fd_use_on_reader=0 mounter_open_logged=1`, `SELINUX_MOUNTER_PASS` in the guest
  console, `MOOBFS_SELINUX_RESULT: fsuse differ=0 denied_directly=7`. The host script did not
  print the marker: its filter kept only `^MOOBFS_SELINUX` lines; it now keeps `^SELINUX_` too
  (`18a7c96`).
- Negative round, the same tree with `layer/main.c` of `HEAD~1` (ended 23:36):
  `fd_use_on_reader=1`, `MOOBFS_SELINUX_FAIL: fsuse: confined mounter: the mounter needed fd use
  on the reader (1)`. The test fails without the fix and passes with it.
- `MOOB_ONLY="ext4 btrfs xfs" make vm-test` on `3c6f8eb` (ended 23:48): `MOOBFS_ONLY_PASS:
  steps ext4 btrfs xfs`, with `LAYER_LOWER_PASS`, `HOSTILE_LOWER_PASS`, `ISOLATION_PASS`,
  `QUERY2_PASS` and `EQUIV_PASS` on each lower, rc=0.
- Full `make vm-test` on `aba8860` (2026-11-12, 01:27–02:27): `MOOBFS_ACCEPTANCE_PASS`,
  `MOOBFS_LAYER_PASS`, rc=0. A first start of the KASAN suite right after it stopped at once
  ("vng (virtme-ng) not in PATH": the queue script lacked the test host's virtme-ng path); it
  was started again at 02:29 with that path.

## Not done

- The full KASAN suite on this branch (running).
- The surfaces that state the limit (`docs/matrix-and-limits.md`, `docs/when-not-to-use.md`, the
  S12 row, `CHANGELOG.md`) change only when the exception is accepted and the fix merged.
- AppArmor and Smack, which also label a file from the allocating task, were not run.

## Next step

The maintainer's decision on ADR-0037; then the batch's full suites and its pull request.
