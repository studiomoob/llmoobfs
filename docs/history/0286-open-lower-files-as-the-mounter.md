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

## Not done

- The full `make vm-test` and the full KASAN suite on this branch (once per batch, before
  publication).
- The surfaces that state the limit (`docs/matrix-and-limits.md`, `docs/when-not-to-use.md`, the
  S12 row, `CHANGELOG.md`) change only when the exception is accepted and the fix merged.
- AppArmor and Smack, which also label a file from the allocating task, were not run.

## Next step

The maintainer's decision on ADR-0037; then the batch's full suites and its pull request.
