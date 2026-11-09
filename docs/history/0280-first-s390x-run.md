# 0280 — The layer module on a big-endian kernel: a first s390x run

Date: 2026-11-09, 13:46–14:35 Brasília time. Status: **done locally**, not published;
exploratory, two full runs on one host. s390x does not join the supported list
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

What the runs do not show: no lower tree or M1 image written on an amd64 machine was read on
s390x (every fixture was made in the s390x container), so cross-machine interchange of a lower
tree rests on the byte-wise encoding, not on a test.

## Findings

None in the module: it builds with no warning and passes the full suite on the big-endian
kernel. Two in the harness: the suite assumed `vda`..`vde` follow the command line (fixed above,
for s390x), and the T2 cursor check assumes a little-endian `ino` (recorded under Endianness,
not changed).

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
