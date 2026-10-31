# fstests over the layer

Security roadmap S2 (journals [0075](../../docs/history/0075-s2-fstests-first-run.md) and
[0077](../../docs/history/0077-s2-fstests-btrfs-xfs.md)).
`vm/fstests-local.sh` runs [fstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) over
the layer in a virtme-ng guest of the host's running kernel, so that a crash stays in the guest.

## How

1. On a Debian host with KVM, as in `vm/kasan-local.sh`: the build dependencies of fstests
   (`automake libtool libtool-bin gawk indent libacl1-dev libaio-dev libattr1-dev libcap-dev
   libgdbm-dev liburing-dev uuid-dev uuid-runtime xfslibs-dev attr acl bc dbench dump quota psmisc
   fio`), `busybox-static` for `vng -r`, and the headers of the running kernel.
2. `git clone --depth 1 --branch v2026.09.22
   https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git ~/fstests && make -C ~/fstests -j8`
3. From the repository root: `sh vm/fstests-local.sh ~/fstests ext4 -g quick` (lower filesystem,
   then any arguments of `./check`). Console in `build/fstests/console-ext4.log`, fstests' own
   results in `build/fstests/tree/results/`. `MOOB_KVER=<release>` boots another installed
   kernel in the guest (image in `/boot`, modules and headers under `/lib/modules`) instead of
   the running one, and the layer is built against its headers (journal 0206).

`moobfs_layer.patch` is the whole port: fstests learns the type in the eight places where it
already handles filesystems without a block device (virtiofs, 9p). The "devices" are two names,
`moobtest` and `moobscratch`; each is a layer mounted with `lower=` on a directory of a base
filesystem, with `domain_depth=0`. "mkfs" of the scratch filesystem is fstests' own file removal.
The guest gets the accounts fstests needs written directly into its private `/etc`.

## Result: group `quick`, three lower filesystems

fstests v2026.09.22, Debian kernel 6.12.111, 647 tests per run, no kernel diagnostic in any run.
Per test: [`results-ext4-quick.txt`](results-ext4-quick.txt),
[`results-btrfs-quick.txt`](results-btrfs-quick.txt), [`results-xfs-quick.txt`](results-xfs-quick.txt).

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| module of commit | 81348ee | 7b42ffe | 7b42ffe |
| guest time | 95 min | 118 min | 117 min |
| passed | 178 | 271 | 298 |
| not run | 451 | 360 | 333 |
| failed | 18 | 16 | 16 |

The ext4 run is the first one, before the fixes of journals 0074 and 0076; its two extra failures
(318, 688) pass on btrfs and XFS with the fixed module, and passed on ext4 when run alone after
each fix. The full ext4 run was not repeated.

### The failures

| Tests | What happens | Verdict |
| --- | --- | --- |
| 080, 141, 215, 446 | `xfs_io`'s `mmap` without flags asks for execute permission; the mount refuses it with `EPERM` | By design: the layer is mounted without execution (`SB_I_NOEXEC`). `mmap -rw` works |
| 099, 126, 452 | the test runs a file that lives on the filesystem | By design, same reason |
| 633, 696, 697 | `mknodat` fails with `EPERM` inside a larger test | By design: no device nodes or FIFOs through the layer |
| 503 (ext4, XFS) | mounts with `-o dax`, which the layer refuses as an unknown option, and does not skip | Does not apply. Skipped on btrfs, which has no collapse-range |
| 732 | mounts the same "export" twice and renames between the two mounts | Does not apply: two layers over one lower directory is outside the contract (ADR-0007) |
| 169, 420 | `FS_IOC_FSGETXATTR` returns `ENOTTY` | **Gap, closed** in journal 0079: the file-attribute calls (`lsattr`, `chattr`, `xfs_io lsattr`) are passed to the lower. Pass now |
| 003, 120 | `noatime`, `relatime`, `strictatime` on the layer's mount have no effect | **Limitation, by decision** (journal 0079): access times follow the lower's mount options. To be excluded |
| 674 (btrfs) | after a dedupe and a remount of the layer, free space has not grown | **Defect, fixed** in journal 0078: the dedupe is done, but `syncfs` and the unmount of the layer did not sync the lower, so btrfs had not committed. Passes now |
| 424 | `statx` did not report append-only, immutable, compressed, no-dump | Seen once 169 and 420 could run. **Closed** in journal 0079; passes now |
| 079 | one sub-test creates a device node | By design. It was skipped before journal 0079 for the missing `chattr` |
| 318 | an ACL read back right after it was changed was the earlier one, until a remount | **Defect, fixed** in journal 0076 (the VFS kept a copy the layer never dropped); passes now |
| 688 | `fallocate` by an ordinary user on a file with capabilities failed with `EPERM` | **Defect, fixed** in journal 0074; passes now |

`generic/215` also printed "No space left on device" for a 2 MiB write in the full ext4 run and
not when run alone; the cause was not found, and it did not happen on btrfs or XFS.

The same 15 tests fail on the three lowers (003, 080, 099, 120, 126, 141, 169, 215, 420, 446, 452,
633, 696, 697, 732); 503 and 674 depend on the lower as noted.

### Why tests do not run

On ext4, 451: most are features the lower ext4 or a stacked filesystem does not have, skipped by
fstests itself: reflink and dedupe (159), a block device under the filesystem (103:
device-mapper targets, a sized `mkfs`), encryption (23), quotas (23), the exchange-range call
(18), shutdown (15), swap files (13), fs-verity (10).

On btrfs and XFS no test is skipped for reflink or dedupe: 110 tests that do not run on ext4
pass on btrfs, and 120 on XFS. Everything that passes on ext4 passes on XFS. On btrfs, 16 tests
that pass on ext4 are skipped because btrfs has no collapse-range or insert-range and one
`fiemap` option.

A smaller set names what the layer does not offer and a client may meet, on any lower:
`mknod`/`mkfifo` (12), idmapped mounts (5), `O_TMPFILE` (4), NFS export (4), the `chattr` flags,
freezing (2), a maximum ACL count declared to fstests, and everything that needs a block device
or a sized `mkfs` under the filesystem.

## Not done

A run under the KASAN kernel, `make vm-test` integration, and a repeat of the `auto` run with
its exclusion lists (below).

## Exclusion list

[`exclude-quick.txt`](exclude-quick.txt) lists the 16 tests that fail for a reason that is a
property of the layer (by design, a documented limitation, or not applicable), each with its
reason; one of them, `generic/607`, is listed without having been investigated. Passed to `./check` with `-E`, a `quick` run is then expected to end with no failure on
the three lower filesystems.

That run was made on 2026-10-13 (journal 0091), module of commit `db8f75b`, the same host and
guest:

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| ran | 631 | 631 | 631 |
| passed | 186 | 280 | 307 |
| not run | 445 | 351 | 324 |
| failed | 0 | 0 | 0 |

"Passed all 631 tests" on each, no kernel diagnostic.

## Debian 6.12.113 (issue 142, journal 0206)

The run with the exclusion list, repeated on Debian kernel `6.12.113+deb13-amd64` booted by
`MOOB_KVER` in the guest on the dedicated host (the host itself runs 6.12.111), module built from
`ca035c5` against that kernel's headers, fstests v2026.09.22, 2026-10-26:

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| ran | 631 | 631 | 631 |
| passed | 187 | 281 | 308 |
| not run | 444 | 350 | 323 |
| failed | 0 | 0 | 0 |
| wall time, build included | 101 min | 120 min | 121 min |

"Passed all 631 tests" on each, no kernel diagnostic; fstests' `check.log` records the guest's
kernel. Per test: [`results-ext4-quick-6.12.113.txt`](results-ext4-quick-6.12.113.txt),
[`results-btrfs-quick-6.12.113.txt`](results-btrfs-quick-6.12.113.txt),
[`results-xfs-quick-6.12.113.txt`](results-xfs-quick-6.12.113.txt). Each lower passed one test
more than the 6.12.111 run of journal 0091 and skipped one fewer; that run kept no per-test list,
so the test is not identified.

## Group `auto`, three lower filesystems (journal 0239)

`sh vm/fstests-local.sh ~/fstests <lower> -g auto -E "$PWD/vm/fstests/exclude-quick.txt"` on
Debian kernel `6.12.113+deb13-amd64` (`MOOB_KVER`), module built from `662ba7a`, fstests
v2026.09.22, a guest of 2 vCPUs and 4 GiB (`MOOB_FSTESTS_CPUS=2`), 2026-10-29/30. The 16 tests
of `exclude-quick.txt` are in `auto` and were not run.

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| ran | 779 | 779 | 779 |
| passed | 229 | 339 | 372 |
| not run | 549 | 436 | 406 |
| failed | 1 | 4 | 1 |
| guest time | 5 h 51 min | 11 h 12 min | 7 h 59 min |

No kernel diagnostic in any run. Per test:
[`results-ext4-auto-6.12.113.txt`](results-ext4-auto-6.12.113.txt),
[`results-btrfs-auto-6.12.113.txt`](results-btrfs-auto-6.12.113.txt),
[`results-xfs-auto-6.12.113.txt`](results-xfs-auto-6.12.113.txt). Everything that passes on ext4
or btrfs passes on XFS.

| Test | Lower | What happens | Verdict |
| --- | --- | --- | --- |
| 631 | all | mounts overlayfs with the layer as upperdir; overlayfs refuses it (no tmpfile, no `RENAME_WHITEOUT`) and the test does not skip | Does not apply: no `O_TMPFILE` and no whiteouts (device nodes) through the layer. [`exclude-auto.txt`](exclude-auto.txt) |
| 186, 187 | btrfs | the attempt to fragment free space ends in `ENOSPC`; the expected checksum differs | fstests skips both on btrfs by `FSTYP`, which is `moobfs_layer` over the layer. Skipped on plain btrfs (baseline, same kernel and size); pass over the layer on XFS. [`exclude-auto-btrfs.txt`](exclude-auto-btrfs.txt) |
| 301 | btrfs | "file2 badly fragmented" (1 503 extents, threshold 1 365) | fstests skips that check on btrfs by `FSTYP`; on plain btrfs the test passes (1 133 extents, unchecked); passes over the layer on XFS. `exclude-auto-btrfs.txt` |

None is a layer defect. With both lists, a run is
`-g auto -E .../exclude-quick.txt -E .../exclude-auto.txt` (plus `-E .../exclude-auto-btrfs.txt`
on btrfs); fstests' `-E` adds to the list. That run follows from the per-test results but has not
been made.
