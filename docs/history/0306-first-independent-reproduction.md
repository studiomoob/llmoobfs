# 0306 — The first independent reproduction of stage 2 (issue #195), and its findings

Date: 2026-11-18. Work started at 12:50 Brasília time and ended at 14:10. Status: **done** for the
record and findings 2 to 6; **finding 1 is under investigation** in a later batch. Estimated
effort S (hours) for this batch; finding 1 is estimated M; the time recorded is calendar time.

## Goal

Record the first reproduction of the stage-2 evidence by someone outside the project, decide what
it means for stage 2, and fix what it found.

## The reproduction

On 2026-11-18 at 11:52 Brasília time, Marcelo Jorge Vieira posted his reproduction on issue #195
([comment](https://github.com/studiomoob/llmoobfs/issues/195#issuecomment-6982119499), with a 7 MB
archive of logs). He ran Parts A, B and C of the route at `v0.6.1-experimental` (`ead5475`, a
fresh clone) on his own host: a ThinkPad X1 Carbon 4th gen (i7-6600U, 2 cores, 15 GiB), Debian 13
with the host kernel `6.12.117+deb13-amd64`, KVM, Docker 26.1.5, QEMU 10.0.13, virtme-ng 1.42. He
had first run Part A at the earlier pin `d96b0db` on 2026-11-17. His guests ran about three times
faster than the project's reference times (A2 in 7 min).

- **Part A:** A1, A2, A3 optional (6.12.115), A6 to A10 passed. A3 (Debian 7.3.3 under KVM)
  failed in `buildrace` (finding 1). A4 and A5 failed when run in order, on root-owned build
  products (finding 2); after a `chown` of the checkout, A4 (KASAN, UBSAN, lockdep) passed and A5
  (KCSAN) passed three times, with `KCSAN_OUTSIDE` counts of 45 to 50 reports.
- **Part B:** fstests `quick` (xfstests-dev `v2026.09.22`) over ext4, btrfs and XFS: 16, 15 and 16
  failures, every one in the exclusion lists, so 631, 630 and 631 passed with them as in
  `vm/fstests/README.md`. Each console ended in `MOOBFS_FSTESTS_KERNEL_DIAGNOSTIC`, from the host's
  own kernel (finding 6).
- **Part C:** syzkaller `8fb0ae8`, six hours, two guests: `crash_dirs=0`, corpus 1 216, coverage
  15 539; the replay of 1 233 programs reached `CALLSITES 698/784 89.0%`, above ADR-0032's 85 %
  and the project's seven campaigns (87.6 % to 88.7 %), with no kernel diagnostic.

The logs were downloaded into the session's scratch space and read as data; the capture of
finding 4 became a test (below).

## Decisions

The maintainer decided on 2026-11-18, about 13:00:

- **This is the third-party reproduction that ADR-0039 left pending.** Stage 2's open item is
  closed, with finding 1 as an open limit (`docs/matrix-and-limits.md`) until it is explained.
- **Credit by name when the person asks for it.** He asked to be credited by name in the journal
  and on the status page. AGENTS.md describes outside people by role; it now makes an exception
  when the person asked publicly to be credited by name, with a link to the request. The rule
  exists to keep other people's data out of the repository; here the person asks for it.
- Findings 2 to 6 are fixed in this batch; finding 1 gets its own batch, since its fix touches the
  module.

## Findings and what was done

1. **`buildrace` gets `E2BIG` on Debian 7.3.3 and 7.3.4 under KVM** on his host, in 13 of 13
   runs at both commits; it passes under TCG (5 of 5) and on 6.12 under KVM. `tests/buildrace.c`
   retries only `EAGAIN`; `E2BIG` comes from the build's work budget (`index_build_work`, 32 768
   units by default) in `layer/index.c`. The domain has 60 files, so a single scan of it is far
   under the budget. Reading of the code: the work counter is set once per build
   (`scan_job_init()`) and spans its up to `MOOBL_BUILD_TRIES` (4) restarts, the up to
   `MOOBL_MOVED_ROUNDS` (64) rescans of directories renamed meanwhile, and the up to
   `MOOBL_LOAD_TRIES` (16) rereadings of a file an update touched while it was read. Under fast
   writers that sum can pass the budget, and the caller gets `E2BIG`, which says the domain is too
   large, where the cause is contention, which `EAGAIN` reports. This is a hypothesis, not yet
   shown; why 6.12 under KVM, with more writes, does not reach it is not explained. On the test
   host, `MOOB_DEBIAN_KERNEL=7.3.4 MOOB_VM_KVM=1 MOOB_ONLY=buildrace make vm-test` at `b2b84d9`
   passed (`writes=` 150 430, 18 131 and 49 022, `eagain=0`): this slower host does not reproduce
   it, as journal 0275's 7.3.3 run did not.
2. **Root-owned build products.** The Docker-based steps run as root on the bind-mounted
   checkout and left 95 root-owned entries, so a later step as the user (`vm/kasan-local.sh`,
   `vm/fstests-local.sh`, both `make layer`) failed. Fix: `vm/run.sh` and `vm/install-test/run.sh`
   give every root-owned path under the checkout back to the caller's uid and gid after the
   container ends, pass or fail, and keep the container's exit status.
3. **The fstests build needs `pkgconf`** (`PKG_CHECK_MODULES` in `m4/package_liburing.m4`). Added
   to the package list of `vm/fstests/README.md`.
4. **The `lowerro freeze` check failed once on a KCSAN report of the kernel itself** (1 of 4 KCSAN
   runs, at `d96b0db`): it grepped raw dmesg for `BUG:`, and caught a data race in
   `virtqueue_kick_prepare` from the check's own `sed` reading the 9p root, with no frame of the
   module. Fix: under the KASAN harness the check passes its dmesg slice through `kdiag`, which
   counts such reports on `KCSAN_OUTSIDE`, as the suite's final check does; the initramfs harness
   keeps the plain grep. On failure it now prints what it matched, `BUG:` lines included (the old
   message printed only `WARNING:` lines, so this failure printed nothing). Test: his capture is
   `tests/kdiag/kcsan-lowerro-outside.txt`, run by `tests/kdiag/run.sh` (`make test`), which checks
   both the `KCSAN_OUTSIDE` count and that the check's filter leaves nothing; with the old grep the
   same capture fails the check.
5. **`vm/syzkaller/moobfs.txt.const` was stale**: it listed `__NR_chdir` and lacked
   `__NR_getxattr` (`getxattr$moob` was added in `2c51e33`). Replaced by the file `syz-extract`
   generated on the test host from the same `moobfs.txt`; runs regenerated it anyway.
6. **A kernel diagnostic from the host kernel, Debian 6.12.117.** `vm/fstests-local.sh` boots the
   host's running kernel. From 6.12.117, which takes upstream `ebaf7c9bbb1e` (the W^X check of
   `verify_rwx()` now sees writable mappings), an Intel KVM guest warns once per module load
   (`CPA detected W^X violation`, `set_memory.c:665`) while ITS thunks are written; fstests
   re-arms `WARN_ONCE` before each test, and `generic/067` and `generic/081` load modules. He
   reproduced it in a guest without the layer. `vm/fstests/README.md` now says to boot a kernel of
   the matrix with `MOOB_KVER` when the host runs a newer one, and names the warning.

He also noted that the tag is annotated but not signed with GPG; the release's source archive is
signed with Sigstore and has SLSA provenance (journal 0284), which is what the project signs.

## Changes

`vm/run.sh`, `vm/install-test/run.sh`, `vm/suite.sh` (the `lowerro` check),
`vm/syzkaller/moobfs.txt.const`, `vm/fstests/README.md`, `tests/kdiag/run.sh` and
`tests/kdiag/kcsan-lowerro-outside.txt`; AGENTS.md (credit by name); ADR-0039 (dated note); README
in both languages, `docs/status.md`, `docs/status.pt-BR.md`, `docs/distribution-strategy.md` §8,
`docs/matrix-and-limits.md` (finding 1 as a known limit), `docs/help-wanted.md`, `ROADMAP.md`,
`CHANGELOG.md`, the site's journey page (both languages, regenerated).

## Checks

On this Mac: `make test` (with the new kdiag case, `KDIAG_TEST_PASS`), `check-retired-claims`,
`check-pins`, `check-matrix-page` (the page kept to 70 lines), `check-site`, `site-faq --check`,
`git diff --check`. On the test host, in a fresh clone of `64c95ae`, in the order the reproducer
ran them: `make lint` PASS; `MOOB_VM_KVM=1 MOOB_ONLY=lowerro make vm-test`, `LOWERRO_PASS` and
`MOOBFS_ONLY_PASS`, then 0 root-owned paths in the checkout; `MOOB_ONLY=lowerro sh
vm/kasan-local.sh` as the user, `MOOBFS_KASAN_ONLY_PASS ... under KASAN, UBSAN and lockdep`;
`MOOB_TEST=workspace sh vm/install-test/run.sh` rc 0, then 0 root-owned paths. Negative round, the
same order on `main` (`b2b84d9`): after `vm-test`, 73 root-owned paths, and `vm/kasan-local.sh`
stopped with `cannot create .../layer/moobfs_layer.mod: Permission denied`, finding 2 as reported.
The `lowerro` change was checked against the reproducer's capture (above); no KCSAN run was made.

## Limitations

- Finding 1 is not explained, and the fix is not written. The test host does not reproduce it.
- Part B was not rerun by him on a matrix kernel; finding 6's explanation rests on his control
  guest.

## Next step

Finding 1: a test that reproduces the misreported `E2BIG` on any host (for example a small
`index_build_work` with writers on a small domain), then the fix in `layer/index.c` under the
kernel-correction rules. Reply on issue #195 (drafted outside the repository, posted by the
maintainer).

*Dated note, 2026-11-18:* finding 1 is explained and fixed in journal 0310 (ADR-0043): the rescans
of directories renamed during the first build spent its allowance; such a build now returns
`EAGAIN`. The known limit of `docs/matrix-and-limits.md` is removed there.
