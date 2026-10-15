# 0109 — syzkaller over the layer: setup and first runs (security roadmap S3)

Date: 2026-10-14 to 2026-10-15. Status: two long runs done; one defect found (finding F8,
journal 0112) and fixed; the coverage threshold of the roadmap still to be set.

## Goal

S3 of `docs/security-roadmap.md`: fuzz the layer's mount, ioctls, typed attributes and name
operations with syzkaller, under KASAN, UBSAN and lockdep, and keep reproducers of whatever it
finds. Precedent named by the roadmap: Janus and Hydra, which found hundreds of defects in kernel
filesystems by fuzzing.

## What was built

Read first: syzkaller's `docs/linux/setup_ubuntu-host_qemu-vm_x86-64-kernel.md`,
`docs/linux/kernel_configs.md` and `docs/syscall_descriptions.md`.

- `vm/syzkaller/kernel.config`: the options those documents name (KCOV with comparisons, no
  KASLR, configfs and securityfs for the image, debug information, hang detectors), merged with
  `vm/kasan.config` on top of `defconfig` and `kvm_guest.config`, for the pinned kernel of the
  KASAN job (6.12.112), in its own cache (`~/.cache/moobfs-syz`).
- `vm/syzkaller/moobfs.txt` and its extracted constants `moobfs.txt.const`: syzlang descriptions
  of the layer's mount with its options (`fs_options`, as `mount$tmpfs` does), the four ioctls
  with their structures laid out as the C compiler lays them out (sizes checked against
  `include/moobfs_ioctl.h`), typed attribute writes and reads with valid and broken names and
  encodings of every type.
- `vm/syzkaller/seeds/`: two seed programs that mount the layer, write typed attributes, rename,
  and issue `QUERY2`, `QUERY2_BATCH`, `GET_INFO` and `QUERY`; checked by `syz-db pack`.
- `vm/syzkaller/image-setup.sh`: the module in syzkaller's Debian image, loaded at boot.
- `vm/syzkaller-local.sh`: kernel, module, image, descriptions and run, step by step; saves the
  manager's log, the crashes and a summary in `build/syzkaller/`.

syzkaller itself is a checkout of its main branch at `8fb0ae89890497b4` (2026-10-14), built with
Go 1.26.9 (checksum checked against go.dev).

## Errors on the way

Each a failed attempt first, then the change:

- The module did not link against the fuzzing kernel: only `bzImage` and `modules_prepare` had
  been built, so `Module.symvers` was missing; the kernel's modules are built now.
- syzkaller's generator refused two comments that began with the word `include` ("confusing
  comment faking a directive"); rephrased. `make generate` needs `clang-format` and `flatc` for
  syzkaller's own code; `make descriptions` regenerates only the system call descriptions.
- First design: the layer mounted in the image at a fixed path, with fixed paths in the
  descriptions. syzkaller's executor runs every program in a private root, where that path does
  not exist, and it turned the layer's calls off. Now each program mounts the layer itself, on a
  directory it created, with a relative `lower=`; this also lets the fuzzer vary the mount and its
  options. tmpfs needs `CONFIG_TMPFS_XATTR` for the attributes.
- Enabling `openat` and `setxattr` in the manager enabled every variant of them (devices,
  cgroups, sysfs, overlay attributes): the first run spent its time elsewhere. The list now holds
  the layer's variants and generic calls without variants.
- The per-function coverage page answers only once the fuzzer has triaged its first programs; a
  loop on the host saves it every 30 minutes.

## Results so far

| Run | Executions | Corpus | Programs that mount the layer | ... and query | Crashes |
| --- | --- | --- | --- | --- | --- |
| 15 min, no seeds, every `openat` variant | 15 736 | 382 | 6 | 3 | 0 |
| 30 min, seeds, restricted list | 78 055 | 844 | 326 | 109 | 0 |

| 6 h (stopped at 2 h), old module | 162 222 | 968 | | | 1 (8 reports): F8 |
| 6 h, fixed module, corpus kept, third seed | 567 085 | 1 091 | | | 0 |

In the 30-minute run the corpus reaches every ioctl (`QUERY2` 39 programs, `QUERY2_BATCH` 34,
`QUERY` 56, `GET_INFO` 24), typed attribute writes and removals (246), renames (69), hard links
(101) and unmounts (34). No report from KASAN, UBSAN or lockdep. Per-function coverage of
`layer/` is not measured yet.

## The long runs

**First six-hour run** (from 17:36, old module): after about an hour it reported `BUG: Dentry
... still in use` at the unmount of the executor's tmpfs, eight times in all: a failed mount kept
a reference on the lower root (finding F8). The cause was read in the code and reproduced
without the fuzzer by adding the mount to the fault-injection test (journal 0112); syzkaller's
own reproducer had not completed when the run was stopped at 19:40 to fuzz the fixed module.

**Per-function coverage**, from the manager's page (`/funccover?force=1`: one PC of 14 587 had no
module of its own, and without `force` the page refuses to answer):

| File | At 19:33, old module | After the second run |
| --- | --- | --- |
| `layer/attr.c` | 5 of 7 functions, 90 % of PCs | 5 of 7, 90 % |
| `layer/index.c` | 55 of 81, 62 % | 57 of 81, 68 % |
| `layer/main.c` | 55 of 71, 60 % | 61 of 71, 65 % |

Changes before the second run (from 19:43, 6 h, ended 01:43): the fixed module; the domain
descriptor usable as a directory (`getdents64`, `fstatfs`); `FS_IOC_FIEMAP` and `statfs`
enabled; a third seed with two equalities (the equality-intersection plan), a directory read,
`fallocate` and `sendfile`; the previous corpus kept (`KEEP_CORPUS=1`). Result: 567 085
executions, 1 091 programs, no report from KASAN, UBSAN or lockdep.

Not reached, and why: domain release, the shrinker and the index marked incomplete need memory
pressure or a budget overflow, which programs do not create (the suite covers them, journal
0101); `moobl_show_options()` runs only when `/proc/mounts` is read. Some functions shown as not
reached certainly run (`moobl_copy_attr()`, `moobl_get_tree()`, `moobl_inode_test()`): inlined
into their callers or called through RCU, they have no coverage of their own in the report.

Coverage of the ioctl and attribute handlers named by S3, summed over their functions: 545 of
699 PCs (78 %); the rest are mostly error paths (the compiler's `.cold` sections) and the
`QUERY2` cursor in `query2_exec()` (34 of 75).

## Threshold (2026-10-15)

Set in `docs/security-roadmap.md` (S3): those handlers at 85 % of PCs or more, every reachable
function hit at least once, exceptions listed, no open report, reproducers kept. A description
that lowers `index_budget` to reach the release paths was considered and dropped: the minimum
budget is 1 MiB, which a program cannot overflow (about 1 700 files); the suite's `release` step
and KCSAN cover those paths.

## Next step

Raise the handlers from 78 % to 85 %: a seed that pages through a `QUERY2` with its cursor, and
requests that hit the validation errors; keep the reproducer of F8 if a later run produces one.
