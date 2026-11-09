# 0280 — The layer module on a big-endian kernel: a first s390x run

Date: 2026-11-09, 13:46–14:30 Brasília time; second round 14:49–20:25. Status: **done
locally**, not published; exploratory, two full runs on one host, then the second round below
(T2, cross-machine data, KASAN: the lowers passed, the full KASAN run stopped at `release`). s390x does not join the supported list
(`docs/compatibility.md` §1 rule 5). Estimated effort S (hours); the time recorded here is
calendar time, which for this unit is close to the effort (one session, mostly waiting on runs of
a few minutes).

## Goal

Run the layer module (`layer/`, `moobfs_layer.ko`) for the first time on a big-endian kernel:
Debian 13's s390x build of Linux 6.12 in a KVM guest, with the layer suite (`vm/suite.sh`) on
ext4, btrfs and XFS. Until now the big-endian evidence covered user space only: the attribute
encoding and the image tools under QEMU user mode in CI (journal 0124). The kernel is frozen
(ADR-0022): `layer/`, the UAPI and the on-disk format are unchanged; only the harness moves.

## Host

An IBM LinuxONE Community Cloud instance (IBM z17, 2 vCPUs, 8 GB), Ubuntu 24.04 with its
6.8 kernel, `/dev/kvm` present and the CPU flag `sie`. Docker came from Ubuntu's `docker.io`
29.1.3, installed for this run. The guest ran under KVM (`-accel kvm`): QEMU would have refused
to start otherwise.

## Native user-space checks on the host (before the guest)

At `main` `9a6dd20`, natively on the s390x host, first on 2026-11-09 about 13:50 BRT and repeated
at 14:12 BRT to quote the output: `make test` (`attr encoding tests: 60 checks passed`,
`workspace_test: all checks passed`, `Ran 9 tests ... OK`); the same with
`CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'`
`LDFLAGS='-fsanitize=address,undefined'` (GCC 13.3, Ubuntu; no clang on the host): the same
lines, exit 0, no sanitizer report; `make bench-smoke`: `BENCH_SMOKE_PASS`.

`make bench-smoke` also prints `load: proc 0: 300 failed operations (recorded as errors)`. It is
expected on every architecture: `bench/smoke.sh` runs W2 against a missing tree on purpose ("an
error must be recorded, not hidden"), with the default single process (`nprocs = 1`,
`bench/load.c:61`), so its 300 reads fail and `bench/load.c:528` prints that line; the script
then checks that the report shows `W2 read: n=0 err=300`. Nothing in it is specific to s390x.

## Path chosen

The existing `make vm-test` flow, parameterised by architecture (`MOOB_ARCH=s390x`): Docker on
the s390x host with the same pinned multi-architecture `debian:trixie-slim` image pulled for
`linux/s390x`, Debian's s390x kernel and headers, and QEMU's `s390-ccw-virtio` machine booting
the same busybox initramfs and the same `vm/suite.sh`.

Alternatives: virtme-ng (`vng -r`) booting Debian's s390x `vmlinuz` with the host's root, and a
Debian cloud image (`debian-13-generic-s390x`) booted with a seed ISO and driven over ssh. Both
would test the module, but with a different guest, different tools and a different way of
reaching the markers than the x86 suite that runs on every pull request; the Docker path reuses
the initramfs, the static test programs, the lower images and the verdict checks of
`vm/in-container.sh` as they are, so a difference in result points at the architecture, not at
the harness.

## Kernel and packages

- Guest kernel `6.12.111+deb13-s390x` (`uname -r` in the guest), package
  `linux-image-6.12.111+deb13-s390x` 6.12.111-1, headers `linux-headers-6.12.111+deb13-s390x`
  6.12.111-1 (and `-common` 6.12.111-1); `vmlinuz` SHA-256
  `4b84b6956a97d570b545af30b3d06ea62305acb675fe0f7d62a6d5e26977d256`. The same Debian source
  version, 6.12.111-1, as `6.12.111+deb13-amd64`, the default of `make vm-test` and the first entry
  of `scripts/supported-kernels`; the newer trixie s390x builds (6.12.113, 6.12.115, 6.12.116)
  were left aside so that only the architecture differs.
- Configuration read from `/boot/config-6.12.111+deb13-s390x`: `CONFIG_CPU_BIG_ENDIAN=y`,
  `CONFIG_64BIT=y`, `CONFIG_S390_GUEST=y` (virtio over CCW built in), `CONFIG_VIRTIO_BLK=m`,
  `CONFIG_SCLP_CONSOLE=y`, `CONFIG_MEMCG=y`, ext4, btrfs, XFS, `dm-log-writes` and loop as modules.
- GCC 14.2.0-19 (Debian), QEMU 10.0.13 (`qemu-system-s390x` 1:10.0.13+ds-0+deb13u1),
  `busybox-static` 1:1.37.0-6+b9; e2fsprogs, btrfs-progs, xfsprogs, dmsetup, acl, sqlite3 and git
  from trixie, as for amd64.
- Both modules built against those headers without a warning or error (checked again by a clean
  `make module` and `make layer` in the image at 14:27): the M1 module
  (`kernel/moobfs.ko`, SHA-256 `f16126ff…c593d6`) and the layer (`layer/moobfs_layer.ko`, SHA-256
  `ba28e474…54a8f6`). M1 was not skipped.

## Harness changes (commit `82579d5`)

- `vm/run.sh`: `MOOB_ARCH` (`amd64` by default, or `s390x`; anything else refused) sets the
  Docker platform and passes a build argument; the s390x image is tagged
  `moobfs-m1-test-s390x`, the amd64 one keeps `moobfs-m1-test`.
- `vm/Dockerfile`: `ARG MOOB_ARCH=amd64`; with `s390x` it installs
  `linux-{image,headers}-6.12.111+deb13-s390x=6.12.111-1` and `qemu-system-s390x` instead of
  `qemu-system-x86`. Only 6.12.111 is accepted for s390x.
- `vm/in-container.sh`: the kernel suffix, the QEMU binary and package, the machine
  (`s390-ccw-virtio`) and the console (`ttysclp0`) are chosen by `MOOB_ARCH`.
- Disk order. The first ext4 run failed with `MOOBFS_VM_FAIL: lower-mount-ext4`: the guest named
  the 256 MiB btrfs image `vda` and the 64 MiB ext4 image `vdb` (`virtio_blk virtio1: [vda]`,
  `virtio0: [vdb]`): `vdX` names follow probe completion, and on CCW the devices do not finish in
  the order of the command line. On s390x each disk is now given as
  `-drive if=none,id=N -device virtio-blk,drive=N,serial=moob-N` (QEMU refuses `serial=` on
  `-drive` since 3.0), and `vm/suite.sh`, right after loading `virtio_blk`, sets `DEV_EXT4`,
  `DEV_BTRFS`, `DEV_XFS`, `DEV_SCRATCH` and `DEV_LOG` from `/sys/block/vd*/serial`. The two
  block-counter checks (`bc-`, `bdn-`) read `/sys/block/${DEV_EXT4##*/}/stat` instead of
  `/sys/block/vda/stat`.
- amd64 is unchanged: the same packages, the same image tag, the same QEMU command line (the disk
  options expand to the same five `-drive ...,if=virtio` arguments, checked by evaluating the
  loop with both machines); without serials the loop in `vm/suite.sh` sets nothing, and on amd64
  `DEV_EXT4` is `/dev/vda`, so the stat path is the same file. `scripts/check-pins.sh --offline`
  reports no mismatch, and shellcheck 0.9.0 (`--shell=sh`) is clean on the three changed
  scripts. `make vm-test` itself was not rerun on amd64 in this unit.

## Commands

On the s390x host, in a fresh clone of this branch (shipped as a git bundle):

```
sudo env MOOB_ARCH=s390x MOOB_VM_KVM=1 MOOB_ONLY=ext4 sh vm/run.sh
sudo env MOOB_ARCH=s390x MOOB_VM_KVM=1 MOOB_ONLY="btrfs xfs" sh vm/run.sh
sudo env MOOB_ARCH=s390x MOOB_VM_KVM=1 sh vm/run.sh
```

## Results

Every run also printed `MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly,
remount, 15 malformed images`: the M1 read-only module ran on the big-endian kernel too. No run
printed a `MOOBFS_VM_FAIL`, `BUG:`, `WARNING`, `Oops` or call trace. Counts are lines of the
guest console containing `_PASS`.

| Run (BRT, calendar time) | Steps | Result |
| --- | --- | --- |
| 13:53–13:55 | `ext4` (smoke: build, `insmod`, mount over one lower, queries) | exit 0, `MOOBFS_ONLY_PASS: steps ext4`, 38 pass lines, among them `LAYER_LOWER_PASS: ext4 reflink=no`, `QUERY2_PASS`, `CLI_PASS`, `ISOLATION_PASS`, `BENCH_LAYER_PASS: ext4` |
| 13:55–13:58 (138 s) | `btrfs`, `xfs` | exit 0, `MOOBFS_ONLY_PASS: steps btrfs xfs`, 71 pass lines; `HOSTILE_LOWER_PASS` and `LAYER_LOWER_PASS ... reflink=yes` on both |
| 13:58–14:12 (839 s) | full suite | exit 0, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, 313 pass lines |
| 14:13–14:27 (790 s) | full suite, again | exit 0, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, 313 pass lines |

Two earlier attempts of the `ext4` run failed in the harness: at 13:51 the guest stopped at
`lower-mount-ext4` because of the disk order above, and at 13:52 QEMU refused `serial=` on
`-drive` ("Block format 'raw' does not support the option 'serial'") before booting. No failure
was seen in the module.

In the full run every per-lower marker appears three times (ext4, btrfs, XFS): among others
`QUERY2_PASS`, `QUERY2_ORDER_PASS`, `QUERY2_LEAPFROG_PASS`, `QUERY2_RACE_PASS`, `CLI_PASS`,
`TYPED_LIMITS_PASS`, `ISOLATION_PASS`, `BENCH_LAYER_PASS`, `BUILD_RACE_PASS`, `HOOK_RACE_PASS`,
`DEEP_TREE_PASS`, `SAMEDIR_RENAME_PASS`, `RELEASE_PASS`, `ROOTPATH_PASS`, `OVERBUDGET_PASS`,
`PERMMATRIX_STEP_PASS`, `PERMCHURN_STEP_PASS`, `PERMISSION_GAPS_PASS`, `HOSTILE_LOWER_PASS`,
`LAYER_LOWER_PASS`; `ATTRLIST_LIMIT_PASS` on btrfs and XFS; and once `DEMO_PASS`,
`POWER_LOSS_REPLAY_PASS`, `IO_ERROR_INJECTION_PASS`, `CASEFOLD_REFUSED_PASS`, `LOWERRO_PASS`,
`SQLEXT_PASS: 33 statements equal to the ordinary table`, `SELFCHECK_PASS`, `MEMCG_ACCOUNT_PASS`,
`LIMITS_BUDGET_PASS`, `LIMITS_MEMCG_PASS`, `FAIRSHARE_PASS`, `STALL_PASS`, `BIGDIR_PASS`,
`DOMAINREUSE_PASS`, `MINACL_PASS`, `IXLOCK_PASS`, `LANDLOCK_PASS`. Skipped, as on every native
amd64 run: `PYLIB_SKIPPED: no python3 in this guest` (and the python scan of `MINACL_PASS`,
`not-run`). The 30 `lower tree uses an unsupported moobfs attribute format` lines in the kernel
log are the hostile format markers the suite plants on purpose (10 per lower), each refused as
required.

## Endianness

What the runs show. The attribute encoding is defined byte by byte, little-endian, in
`include/moobfs_attr.h:65` (`moob_attr_get64()`) and `:75` (`moob_attr_put64()`); the layer's
ordering (`layer/index.c:130` `double_key()`, `:146` the int64/time comparison) reads through
those helpers, so it does not depend on the CPU's byte order. The suite agrees on s390x:

- `INDEPENDENT_ENCODING_PASS` (6: v1 and QUERY2 on each lower): 18 fixtures written as fixed byte
  strings by `tests/permission-gaps.c`, not by the shared encoder, read back byte for byte and
  answered by the kernel exactly as the fixtures say.
- Typed attributes and QUERY equal to the oracle on every lower (`MOOBFS_LAYER_PASS` "index and
  QUERY equal to the oracle"), with ordered answers: `QUERY2_ORDER_PASS` (string and time
  keys, paged, ascending and descending, against the oracle), the bench
  `EQUIV_PASS: 9 shapes (kernel, SQLite, oracle)` six times, `SQLEXT_PASS` with int64 and
  double conditions (among them `weight BETWEEN -2.5 AND 3.5 ORDER BY weight`), and the hostile
  trees equal to the oracle (`HOSTILE_LOWER_PASS`).
- The UAPI structs (`include/moobfs_ioctl.h`) are native-endian on both sides of the ioctl, and
  the QUERY2 cursor is opaque to user space and checked by its MAC; paging with cursors passed
  (`CLI_PASS ... paging`, `QUERY2_PASS`).
- The internal attribute buffer of the index build (`layer/index.c:1529` writes a native `u16`
  size, `:1598` and `:1741` read it back) never leaves the kernel.

No mismatch was found in the module. One test assumes little-endian: the T2 cursor check of
`vm/suite.sh:643-651` patches byte 15 of `struct moobl_cursor` (`layer/index.c:4097`, `ino` at
offsets 8–15) as "the high byte of the inode", "zero in this cursor". On s390x byte 15 is the
inode's low byte. The check still holds there (any changed byte must be refused by the MAC, and it
was: `QUERY2_PASS` on each lower), but if that low byte happened to be 0xff the patch would change
nothing and the step would fail with `t2-patch-changed-nothing-15-$fs`; the comment is also wrong
on big-endian. Left as is, recorded: the fix, if s390x runs continue, is to pick the offset of
the most significant byte by the byte order.
*Note, 2026-11-09, second round:* done in commit `1162708` (section "Second round" below).

What the runs do not show: no lower tree or M1 image written on an amd64 machine was read on
s390x (every fixture was made in the s390x container), so cross-machine interchange of a lower
tree rests on the byte-wise encoding, not on a test.
*Note, 2026-11-09, second round:* now tested; see "Cross-machine data" below.

## Findings

None in the module: it builds with no warning and passes the full suite on the big-endian
kernel. Two in the harness: the suite assumed `vda`..`vde` follow the command line (fixed above,
for s390x), and the T2 cursor check assumes a little-endian `ino` (recorded under Endianness,
not changed).

## Public surfaces updated

`README.md` and `README.pt-BR.md` (the paragraph on other architectures), `docs/faq.md` and the
generated `site/faq.html`, `docs/testing.md` (the s390x time, and "not tested" now says "in CI"),
and the status pages in both languages (`scripts/site-journey.py`). The wording "the module ran
only in the exploratory arm64 job" (and its Portuguese) goes to `docs/retired-claims.txt`.
`docs/compatibility.md` is unchanged: rule 5 (x86-64 only until another architecture runs the
same two jobs) still holds. `scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`;
`python3 scripts/site-faq.py --check`: up to date.

## Limitations

- Exploratory: two full runs on one host, one kernel (Debian 6.12.111), under KVM on z17. No
  KASAN/UBSAN/lockdep kernel for s390x, no fstests, no 7.3, no other s390x build.
- `PYLIB_SKIPPED` as on amd64.
- s390x stays outside the supported list and CI; nothing here promises it.
- amd64 `make vm-test` was not rerun after the harness change; the argument check above is static.

## Next step

If the maintainer wants big-endian coverage to continue: a KASAN/UBSAN/lockdep kernel for s390x
(upstream 6.12.116 with `vm/kasan.config`, built on the instance) through `vm/kasan-local.sh`,
then a cross-machine check (a lower tree and an M1 image made on amd64, read on s390x). The
amd64 `make vm-test` at this branch's head runs in the batch's full suite before publication.

## Second round, 2026-11-09 (14:49 BRT onwards): T2, cross-machine data, KASAN on s390x

Asked after the first round: correct the T2 check, read data written on x86-64 with the s390x
kernel, and run the suite under KASAN/UBSAN/lockdep on s390x. Same rules: `layer/` unchanged.

### T2 on either byte order (commit `1162708`)

`vm/suite.sh`, in `run_lower` (so the steps `ext4`, `btrfs` and `xfs`): the offset of the inode's
high byte comes from the guest's byte order, `printf '\001\000' | od -An -tx2`, which prints
`0001` on little-endian (offset 15, as before, so an amd64 run patches the same three bytes 5, 15
and 119) and `0100` on big-endian (offset 8); anything else fails with `t2-byte-order-$fs`. The
guest's BusyBox (`busybox-static` 1:1.37.0-6+b9, s390x) prints ` 0100`, checked in the s390x
image. Run on s390x, `MOOB_ONLY="ext4 btrfs xfs"`, KVM, 15:00–15:03 BRT (191 s): exit 0,
`QUERY2_PASS` and `LAYER_LOWER_PASS` on ext4, btrfs and XFS, `MOOBFS_ONLY_PASS`, 105 pass lines,
no kernel diagnostic. The run does not print which offset it chose; the evidence for 8 is the
`od` output above and the absence of `t2-byte-order`.

### Cross-machine data (commit `5e98579`)

`vm/cross-machine.sh` is the writing side, run on the developer's Mac in Docker with the pinned
`debian:trixie-slim` image for `linux/amd64`, `--privileged` for loop mounts. Exactly: x86-64
user space under emulation (gcc, `moobfs-attr`, `moobfs-index`, `mkfs.moobfs`, `fsck.moobfs`
built and run as x86-64), on Docker Desktop's Linux VM whose kernel is `7.0.14-linuxkit` for
arm64 (little-endian). The attribute bytes are those encoded by x86-64 user space; the ext4,
btrfs and XFS structures are those of trixie's mkfs tools and of that kernel. No native x86-64
kernel took part: what this proves is a little-endian writer (x86-64 user space on an arm64
little-endian kernel) to a big-endian reader (the s390x kernel and user space). It makes:

- three lower images (ext4 64 MiB, btrfs 256 MiB, XFS 300 MiB), each with domain `da` (the
  suite's Q2b shapes: 30 files, time and int64 `created` with ties, `kind`, long `title`s; uid
  1001, 0600, domain 0700) and domain `dt` (10 files with every type: doubles from -inf to inf
  with -0.0 and ±1e300, int64 from INT64_MIN to INT64_MAX, time, bool, raw, UTF-8 strings);
- the x86-64 oracle (`moobfs-index`) for 18 QUERY2 queries as uid 1001 (equality, ranges, prefix,
  ordered ascending and descending by time, int64, double and string, limits) and 4 v1 queries
  as root, and every file's `moobfs-attr list`;
- an M1 image of five files (two identical) by `mkfs.moobfs`, its `fsck.moobfs` report and the
  files' SHA-256.

Hashes of what was transferred (`build/cross/SHA256SUMS`):

```
405a1451683fcc1e36f009b770dfc9c03d103c8b8ead826a0949965c1496205d  lower-ext4.img
3741c57d86d12c79495f4482776007c0178c9f03326374a3c120b286855c0342  lower-btrfs.img
5fd0851fd83712be37dd83809db6ca57e4128d33db6a8c9c93ee01d4a73f58a3  lower-xfs.img
9f1c00b1d5c7d40694cca9367cd83cc546048523096b92db28cd2a671d254267  guest/m1.img
1993adefc4169717b2ba64d5a2ec4f1ffefeeab9116632701263108ae5474531  m1-fsck.json
f47b21c86a76995608ad2b1ff0402bb5abd9559b79d0719768f2e7d8751bc228  guest/queries.txt
f72d0ea7a73afaf2852b70e494491056a59de9659333530f3f97bf5510fa49bb  guest/queries-v1.txt
```

The reading side is `MOOB_CROSS=build/cross MOOB_ONLY=crossmachine` with `MOOB_ARCH=s390x`:
`vm/in-container.sh` checks every hash, runs `fsck.moobfs` built for s390x on the M1 image and
compares its report byte for byte, and attaches the three images as the lower disks; the suite
step `crossmachine` (run only when named, never in the full suite) mounts each under the layer
and compares, byte for byte, the kernel's QUERY2 answers with the x86-64 oracle, the s390x
oracle with the x86-64 oracle, `moobfs-attr list` of every file through the layer with the x86-64
listing, and the v1 answers (layer remounted with `domain_depth=0`) with the x86-64 oracle; then
it loads the M1 module, mounts the M1 image and compares the files' SHA-256.

Run on s390x, KVM, 15:04 BRT (15 s with the image cached): `CROSS_FSCK_PASS: fsck.moobfs on s390x
reports the M1 image as on x86_64` and `CROSS_MACHINE_PASS: written on x86_64, read on s390x: 66
answers on ext4, btrfs and xfs equal to the writer's oracle, 135 attribute lines per lower equal;
M1 image of 5 files read with the writer's hashes`, `MOOBFS_ONLY_PASS`. Negative round, 15:05: a
copy with two lines of the x86-64 answer `wasc` (order by double ascending) swapped, and its
hashes recomputed, fails with `MOOBFS_VM_FAIL: cx-mismatch-wasc-ext4`.

One semantic worth stating, the same on both machines: `weight eq double 0` matches +0.0 only,
not -0.0 (the kernel and both oracles agree; `layer/index.c:130` keys the two zeros apart).

### KASAN, UBSAN and lockdep on s390x (commit `1784ba7`)

Path: `vm/kasan-local.sh` with an architecture switch, not a separate script. It already builds
the pinned upstream kernel with `vm/kasan.config` through virtme-ng and runs
`vm/kasan-guest.sh` (the KASAN harness's own checks, the `faults` step, the final
`MOOBFS_KASAN_PASS`); on s390x only, chosen by `uname -m`, it differs in four ways, and on any
other host it does exactly what it did:

- the image is `arch/s390/boot/bzImage`;
- `vm/kasan-s390x.config` is applied after `vm/kasan.config`, with one line, `CONFIG_KVM=n`. It
  concerns the guest kernel only (the debug kernel the suite runs in, which does not need to host
  VMs); the instance's own kernel and the KVM that runs the guest are untouched. Reason: the first
  build stopped in `arch/s390/kvm/kvm-s390.c` of 6.12.116 (lines 4776, 4786, 4806:
  `__kvm_faultin_pfn()`, `KVM_PFN_ERR_NEEDS_IO`, `kvm_release_faultin_page()` undeclared), a
  defect of that stable release on s390, not of this project;
- `vng --no-virtme-ng-init`: the `virtme-ng-init` in virtme-ng 1.42's wheel is an x86-64 binary,
  and the first boot panicked running it ("Requested init ... failed (error -8)"); the arm64
  workflow uses the same option;
- the disks are given to vng as links named `moob-ext4` etc. in the kernel tree, so their serials
  are the names `vm/suite.sh` maps (vng uses the path as serial, and the CCW disks may probe in
  any order; in this run they happened to come up in command-line order).

Every option of `vm/kasan.config` (and the s390x line) was checked present in the resulting
`.config` with its requested value: no other difference. The kernel: Linux 6.12.116, tarball
SHA-256 `cb3ed3df…6a511b` verified, built on the instance with Ubuntu's GCC 13.3.0 at `nice 10`,
2 jobs, 15:09–15:34 BRT (about 25 min); `CONFIG_MARCH_Z13=y` from virtme-ng's base.
Prerequisites installed on the instance: build tools, `qemu-system-s390x` 1:8.2.2+ds-0ubuntu1.18 (Ubuntu), virtme-ng
1.42 from `vm/kasan-requirements.txt` by hash in a venv. One more harness hiccup, not recorded
in the scripts: the checkout had root-owned files from the Docker runs, and `make layer` as the
user failed until they were given back (`chown`).

**Per-lower steps**, `MOOB_ONLY="ext4 btrfs xfs"`, KVM, 2 CPUs, 3 GiB, 15:37–17:21 BRT (6231 s;
ext4 about 34 min, btrfs 33, XFS 36): exit 0,
`MOOBFS_KASAN_ONLY_PASS: steps ext4 btrfs xfs under KASAN, UBSAN and lockdep with no kernel
diagnostics`, `MOOBFS_ONLY_PASS`, 95 pass lines; `QUERY2_PASS`, `HOSTILE_LOWER_PASS` and
`LAYER_LOWER_PASS` on each lower; no `BUG`, `KASAN:`, `UBSAN:` or `WARNING` line. The boot log
shows lockdep ("Lock dependency validator", "RCU lockdep checking is enabled") and KFENCE; s390's
KASAN prints no "KernelAddressSanitizer initialized" line, so the evidence that KASAN is in is
`CONFIG_KASAN=y`, `CONFIG_KASAN_GENERIC=y`, `CONFIG_KASAN_INLINE=y` in the built `.config`. The
guest's first lines (the harness's `kernel:` summary) were lost in the console interleaving of
the shell init. Taint: `E` from the guest kernel's own unsigned `vfio` module that udev loaded,
and `O` from the layer module; neither is a diagnostic. One line differs from the native runs:
`EQUIV_PASS ... SQLite SCAN plans to justify: 2` (1 natively), because this guest runs the
host's SQLite (Ubuntu's), not Debian's. Once, during `BENCH_LAYER` on ext4 after a
`drop_caches`, the kernel logged `moobfs_layer: building the index of a domain failed (-110)`:
a build hit the CPU-time or wall-time bound of `scan_check()` (`layer/index.c:2066-2070`, journal
0241) under KASAN's slowdown; the step still passed (why the bench tolerates it was not
checked). Not a diagnostic; recorded as the one place where the debug kernel's speed showed.

**Full suite** under KASAN, 17:25–20:13 BRT (10084 s): **failed** at the step `release`,
`MOOBFS_VM_FAIL: rl-measure-ext4` ("domain d0: kernel 0 oracle 500"), after `ATTRLIST_LIMIT_PASS`
on btrfs and XFS. Before it, every step from the start to `attrlist` passed (among them the three
lowers again, `DEMO_PASS`, `ACCEPTANCE_CLIENTS_PASS`, `POWER_LOSS_REPLAY_PASS`,
`IO_ERROR_INJECTION_PASS`, `LAYER_RESERVE_PASS`, `CASEFOLD_REFUSED_PASS`, `BUILD_RACE_PASS`,
`HOOK_RACE_PASS`, `DEEP_TREE_PASS`, `SAMEDIR_RENAME_PASS` on each lower), with no `BUG`, `KASAN:`,
`UBSAN:` or `WARNING` in the console or in the dmesg saved at the failure. The steps after
`release` (among them `faults`, the permission matrix and churn, `selfcheck`, `lowerro`,
`sqlext`, `bigdir`, `limits`) did not run. The vng console capture is lossy when the guest's
outputs interleave: a few per-lower markers that passed are missing from it (for example two
`QUERY2_PASS` lines instead of three), so the counts of this run (113 pass lines) are not
comparable with the others.

Cause, checked with a probe (a script outside the repository booted in the same kernel,
20:16–20:22 BRT): the step builds a domain of 500 files with 16 attributes each (`bench-gen
--seed 7 --inodes 500 --profile 16`) and compares the kernel's `kind exists` answer with the
oracle. In this guest the build ends in `ETIMEDOUT`: `query ... q2errno` prints `q2 110`, twice
in a row, 13 s each, and the tracepoint shows `moobfs_domain_build: ... err=-110 tries=1
released=0 bytes=0 ns=12715158016`; the oracle finds the 500 files. The bound is the build's
CPU-time limit, `MOOBL_BUILD_CPU_NS` = 10 s (`layer/index.c:864`, checked in `scan_check()` at
`:2068`, journal 0241): under KASAN's inline instrumentation on this 2-CPU guest one 500-file
build needs more than 10 s of CPU. The suite's `rl_cmp` hides the errno (`query ... | cut | sort`)
and reports it as an empty answer. Not an endianness defect and not a module defect: the module
refuses the over-long build as specified, and the same step passed natively on s390x (two
`RELEASE_PASS` runs, three lowers each). It is a mismatch between a fixed CPU bound and the speed
of a debug kernel on this machine; the x86-64 KASAN runs stay under it. Not changed: the bound is
in `layer/` (frozen), and shrinking the test's domain only for s390x would weaken it; a choice for
the maintainer (see next steps).

### Second round: limitations and next steps

- One run of each item; the cross-machine writer was x86-64 user space on an arm64 kernel, not a
  native x86-64 machine (little-endian writer to big-endian reader).
- KASAN on s390x: the per-lower steps passed; the full suite stopped at `release` on the 10 s CPU
  bound, so `faults` and the later steps have not run under KASAN on s390x.
- An amd64 `make vm-test` of the first round's head (`394fe05`) was started on the dedicated
  host outside this session; its result is not recorded here. The second round's harness changes
  (T2, the `crossmachine` step, `kasan-local.sh`) were not run on amd64. They are written so that
  amd64 does the same as before (T2 offset 15; `crossmachine` only when named; the x86-64 branch of
  `kasan-local.sh` passes the same arguments).
- Next, for the maintainer to choose: run the steps after `release` under KASAN on s390x by name
  (`MOOB_ONLY`), several hours; or decide whether the build bound should scale for debug kernels
  (a module change, outside the freeze); or leave KASAN on s390x at the per-lower steps.

Second round's time: 14:49–20:25 BRT, calendar time; effort about 2 h, the rest waiting on the
kernel build (25 min) and the KASAN runs (1 h 44 min and 2 h 48 min). Estimate was S; actual S
for the effort, M in calendar time.
