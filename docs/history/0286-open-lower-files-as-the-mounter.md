# 0286 — Open lower files as the mounter: the fix for the confined-mounter limit

Date: 2026-11-11 and 12. Work started about 21:50 Brasília time; test runs until 03:46 on the
12th; decision and documents 06:15–06:40. Status: **done locally; ADR-0037 accepted; pull
request after batch 22's (#196) is merged and this branch is rebased on it.**
Estimated effort S; the time recorded is calendar time of one session.

## Goal

Prepare, while the maintainer is away, the later batch that journal 0284 left for the SELinux
limit found that evening: under SELinux the domain that mounts the layer needs `fd use` on every
caller's domain, because the layer opens lower files as the caller and reads them as the mounter.
Nothing here is merged or published without the maintainer's decision.

## Decision (maintainer, 2026-11-12, about 06:15 Brasília time)

ADR-0037 is accepted: the third narrow exception to the kernel freeze. With the fix the
documented limit of journal 0284 is gone; the surfaces that stated it now state the fix
(`CHANGELOG.md`, `docs/matrix-and-limits.md`, `docs/when-not-to-use.md`, the S12 row of
`docs/security-roadmap.md`, a dated note in `docs/review-findings.md`), ADR-0022 lists the
exception, ADR-0030 is amended (the change to the freeze reopens the exception of 0114 item 8,
which gets a test with a confined mounter or a renewed reason in a later batch), and the old
wording is in `docs/retired-claims.txt`.

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
- Full KASAN/UBSAN/lockdep suite on `aba8860` (`sh vm/kasan-local.sh`, kernel 6.12.116,
  02:29–03:46): `MOOBFS_PYLIB_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`,
  `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with no kernel diagnostics`,
  rc=0.

## Finding 0114 item 8 gets its test (2026-11-12 morning)

ADR-0030 reopened the item 8 exception when the freeze changed. With the fix, the lower
directory file carries the mounter's domain, so the confined-mounter step can tell the old and
the current `moobl_iterate()` apart: the reader domain has no `fd use` on `moobtest_mounter_t`,
and a directory read checked with the reader's credentials is refused that use. The step now
also counts the reader's `fd use` refusals on the mounter and checks that the reader is not
permissive (control), and prints `SELINUX_DIRREAD_PASS` (`e1d93dc`).

- Positive round on `e1d93dc` (test host, 07:27–07:28): `list=0 read=0 fd_use_on_reader=0
  mounter_open_logged=1 reader_fd_use_on_mounter=0 reader_permissive=0`,
  `SELINUX_MOUNTER_PASS`, `SELINUX_DIRREAD_PASS`.
- Negative round, `override_creds()` and `revert_creds()` removed from `moobl_iterate()`
  (07:28–07:29): `list=11 ... reader_fd_use_on_mounter=1 reader_permissive=0`,
  `MOOBFS_SELINUX_FAIL: fsuse: item 8: a directory read through the layer was checked with the
  caller's credentials (list=11, fd use refusals=1)`.
- A first attempt at 07:23 failed at `insmod` in both rounds (`.gnu.linkonce.this_module section
  size must match`): the queue script did not run `make layer`, so the guest loaded the module
  the KASAN run had left in that clone. Not a defect of the harness, whose header says the
  module must be built; the rounds above build it first.

0114 item 8 leaves the recorded exceptions (ADR-0030 amended); six remain. Updated:
`docs/review-findings.md`, ADR-0030, `docs/distribution-strategy.md` §8, `CHANGELOG.md`.

## Public surfaces brought up to date (2026-11-12 morning)

A walk of `docs/public-surfaces.md` found states left behind by journals 0273–0284: the README's
"Facts as of 2026-10-25" list (both languages) still said "one kernel series" and big-endian
"under emulation"; the landing page counted "1 kernel series tested" beside a text listing three,
dated its facts 2026-10-25 and left 7.0.14 out of the KASAN item; the journey pages stopped at
2026-11-01. Updated: the README lists (as of 2026-11-12: the kernels on every pull request, s390x
on IBM LinuxONE Community Cloud as exploratory, KCSAN, the confined-mounter SELinux run, the
review-findings register); `site/index.html` (the count, the date, s390x, 7.0.14);
`scripts/site-journey.py` (six milestones from 2026-11-04 to 2026-11-12, the IBM LinuxONE host in
the architectures answer, both languages, pages regenerated). Checks: retired claims, the FAQ
page and the matrix page pass.

## Batch 22 merged (2026-11-12, about 07:40)

Pull request #196 (batch 22, journals 0283–0285) was merged with the maintainer's agreement after
its 19 checks passed (merge commit `5b5b237`); this branch was rebased on it. The CI run of #196
also confirmed the 7.3.3 image from the snapshot archive (`Native Linux VM (Debian 7.3.3)`, 11 min
29 s), and the full suite on 7.3.3 passed on the test host with the same image
(`MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`).

## Pre-push checks (2026-11-12, 07:32–07:37)

On `f5bc6bc` (rebased on `5b5b237`), test host: lint, checkpatch, test, asan, hostile (clang
ASan/UBSan), pylib, retired, pins, mutants, site, `git diff --check 5b5b237` and ubuntu-tests
all PASS. The full `make vm-test` and KASAN suite ran on `aba8860`, before the rebase; the
commits since change no file of `layer/` (the item 8 check and documents only).

## Not done

- AppArmor and Smack, which also label a file from the allocating task, were not run.

## Next step

The pull request with the maintainer's agreement.
