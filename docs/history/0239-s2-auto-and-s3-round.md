# 0239 — S2: fstests `auto` on three lowers; S3: a sixth syzkaller round with `moobl_xattr_get` covered

Date: 2026-10-29 to 2026-10-30. Status: runs done on the dedicated test host (batch 10, branch
`claudiofp/b10-s2s3`, based on batch 7-8). Work started: 2026-10-29 20:05 UTC; ended 2026-10-30
about 13:40 UTC. Calendar time about 17.5 hours. Machine time: about 25 guest-hours of fstests
(three lowers, partly in parallel), 6 hours of fuzzing plus 46 minutes of kernel build. Attended
work about 3 hours. Estimated effort: M; actual: M in calendar time, S in attended work.

## Goal

Advance security roadmap S2 and S3. S2: run fstests' `auto` group over the layer on ext4, btrfs
and XFS lowers (`quick` with `vm/fstests/exclude-quick.txt` passes since journal 0091), and
classify every failure. S3: one more syzkaller round whose report shows `moobl_xattr_get()`
covered, as journal 0154 asked, with crashes, per-function coverage and the "done when" checked.

## Decisions

- **Both runs at once, pinned to host CPUs.** The host has 8 threads and two had to stay free.
  syzkaller ran with 2 VMs of 2 vCPUs pinned to CPUs 0-3 (`taskset`), fstests in a 2-vCPU guest
  pinned to CPUs 4-5; when syzkaller ended, XFS took CPUs 0-1. For that, three knobs were added
  to the harness, with defaults unchanged: `MOOB_FSTESTS_CPUS` and `MOOB_FSTESTS_MEM`
  (`vm/fstests-local.sh`), and `SYZ_VMS` and `SYZ_CACHE` (`vm/syzkaller-local.sh`). With
  `SYZ_CACHE` the run kept its own kernel tree and image and did not change the shared cache.
- **The syzkaller kernel was rebuilt.** `vm/kasan.config` gained the S8 hardening options in
  journal 0237 (`FORTIFY_SOURCE`, `HARDENED_USERCOPY`, KFENCE, `DEBUG_OBJECTS` and others), so the
  harness's configuration check rebuilt the cached 6.12.112 kernel. The round therefore ran on
  the hardened KASAN kernel, not the one of journal 0143.
- **One more seed, `vm/syzkaller/seeds/xattr-get-security`.** It reads `security.capability` on
  a file of a mounted layer, by descriptor and by path. That reaches the branch of
  `moobl_xattr_get()` that runs with the mounter's credentials (`cap_inode_getsecurity()` calls
  the handler). Journal 0154's three seeds only reached the `user.*` branch.
- **fstests ran on Debian 6.12.113** (`MOOB_KVER`), the kernel of journal 0206, with fstests
  v2026.09.22 and the `quick` exclusion list (`-g auto -E exclude-quick.txt`).
- **Failures are classified against a baseline when the lower can be the cause.** generic/186,
  187 and 301 failed only over btrfs. They were run again on plain btrfs, without the layer, on
  the same kernel and device size (requested by the coordinator).
- **Exclusions in two new files.** `vm/fstests/exclude-auto.txt` is for every lower and
  `vm/fstests/exclude-auto-btrfs.txt` for btrfs only. fstests' `-E` can be repeated and adds to
  the list. One reason per test, in the files and in `vm/fstests/README.md`.

Alternatives not taken: a third syzkaller VM (it would have left no CPU for fstests or for the
coordinator); fstests at 4 vCPUs (one lower at a time, about three times the calendar time).

## S2: fstests `auto` over the layer

Command, from a copy of the tree on the host:
`MOOB_KVER=6.12.113+deb13-amd64 MOOB_FSTESTS_CPUS=2 taskset -c <cpus> sh vm/fstests-local.sh ~/fstests <lower> -g auto -E "$PWD/vm/fstests/exclude-quick.txt"`.
The module was built from `662ba7a`; the layer is unchanged since `e6dca41` (ADR-0027). The 16
tests of `exclude-quick.txt` are all in `auto` and were not run, so each lower ran 779 of the
795 tests.

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| ran | 779 | 779 | 779 |
| passed | 229 | 339 | 372 |
| not run | 549 | 436 | 406 |
| failed | 1 | 4 | 1 |
| kernel diagnostic (BUG, Oops, KASAN, WARNING, lockdep) | none | none | none |
| guest time (UTC) | 2026-10-29 20:21 – 10-30 02:12 (5 h 51 min) | 02:13 – 13:25 (11 h 12 min) | 03:12 – 11:10 (7 h 59 min) |

Per test: `vm/fstests/results-{ext4,btrfs,xfs}-auto-6.12.113.txt`. Every test that passes on
ext4 passes on XFS. Every test that passes on btrfs passes on XFS. Of the 229 that pass on ext4,
19 are skipped on btrfs: collapse-range and insert-range (17), a `fiemap` option, and one free
space requirement.

### The failures

| Test | Lower | What happens | Verdict |
| --- | --- | --- | --- |
| generic/631 | ext4, btrfs, XFS | mounts overlayfs with the layer as upperdir. overlayfs refuses it: "upper fs does not support tmpfile", "does not support RENAME_WHITEOUT", "upper fs missing required features". The test then works on directories that do not exist | **Does not apply.** The layer has no `O_TMPFILE` and refuses `RENAME_WHITEOUT` (no device nodes through the layer, `layer/main.c`). The test requires overlayfs but not a capable upper, so it fails instead of skipping. In `exclude-auto.txt` |
| generic/186 | btrfs | the attempt to fragment free space ends in `ENOSPC` on `pwrite`. `file3` and `file3.chk` then agree with each other but not with the expected checksum | **fstests and the port.** fstests skips it on btrfs (`_notrun "Can't fragment free space on btrfs."`). Over the layer `FSTYP` is `moobfs_layer`, so that skip cannot fire. Skipped on plain btrfs; passes over the layer on XFS. In `exclude-auto-btrfs.txt` |
| generic/187 | btrfs | the same as 186 | **fstests and the port**, as 186 (`_exclude_fs btrfs`) |
| generic/301 | btrfs | "file2 badly fragmented": 1 503 extents after random CoW, threshold 1 365 (2/3 of 2 048) | **fstests and the port.** The threshold is skipped when `FSTYP` is btrfs ("btrfs CoW extent allocation depends on transaction commit frequency"). On plain btrfs file2 had 1 133 extents and the check was not made; over the layer on XFS the test passes |

No failure is a layer defect: no data loss, no wrong permission, no crash.

**Baseline on plain btrfs**, 2026-10-30 11:02-11:04 UTC. A virtme-ng guest of the same kernel
pinned to the then idle CPUs 2-3, two fresh 8 GiB btrfs images, `FSTYP=btrfs`,
`./check generic/186 generic/187 generic/301`. Result: 186 not run ("Can't fragment free space on
btrfs."), 187 not run ("not suitable for this filesystem type: btrfs"), 301 passed. A first
attempt at 10:59 failed before running anything (`mkfs.btrfs` not in the script's `PATH`).

### Why tests do not run

The skips are those of journal 0091, with more of them in `auto`. On ext4, reflink and dedupe
skip 193 tests (none on btrfs or XFS). A block device or a sized or block-sized `mkfs` under the
filesystem skips 127 to 165, depending on the lower. Quotas skip 31 to 35, shutdown 30,
encryption 26, the exchange-range call 16 or 17, swap files 14, fs-verity 11, and
`mknod`/`mkfifo` 10. Then come idmapped mounts, `O_TMPFILE`, NFS export, freezing, casefold and
a few `xfs_io` commands. Four tests are skipped for `renameat2` with `RENAME_WHITEOUT` (the
message reads "kernel doesn't support renameat syscall"), and three for `O_DIRECT`. A test that
is not run proves nothing about the layer.

## S3: the sixth syzkaller round

Command (from a copy of the tree, `SYZ_CACHE`, `SYZ_VMS=2`, pinned):
`SYZ=~/b10/syzkaller GO=~/syz/go/bin SYZ_CACHE=~/b10/syzcache SYZ_VMS=2 taskset -c 0-3 sh vm/syzkaller-local.sh 21600 kernel module image descriptions run`.
syzkaller at `8fb0ae8` (the revision of journal 0109), descriptions and constants as committed,
seeds as above. The kernel build ran 20:22 to 21:08 UTC (`-j4`).

- **First manager start failed in seconds.** The copied image key had mode 0644 and syz-manager
  refuses it ("sshkey ... is unprotected"). After `chmod 600` only the `run` step was started
  again. The first script was stopped by PID, because it would have fetched coverage and written
  a summary from the wrong manager. No fuzzing time was lost.
- **Fuzzing:** 2026-10-29 21:09 to 2026-10-30 03:09 UTC, 21 600 s, exit status 0. 386 844
  executions (about 18 per second with 2 VMs; journal 0143 had about 25 with 3). Corpus 1 095,
  manager coverage 16 260 PCs. **No crash directory: no report from KASAN, UBSAN, lockdep or the
  new hardening options.** The 50 starting candidates include the 14 layer seeds, and the
  machine check disabled none of the layer's calls.
- **Per-function coverage** (`/funccover?force=1`, saved 20 minutes before the end as intended):

| File | Functions reached | PCs |
| --- | --- | --- |
| `layer/attr.c` | 5 of 7 | 52 of 60 (87 %) |
| `layer/index.c` | 66 of 93 | 1 322 of 2 080 (64 %) |
| `layer/main.c` | 70 of 80 | 449 of 790 (57 %) |

- **`moobl_xattr_get()` is covered in the report: 3 of 18 PCs** (0 of 4 in journal 0143). The
  corpus kept 14 programs with `fgetxattr$moob` and 11 with `getxattr$moob`; 5 and 7 of them mount
  the layer. Three keep the new seed's `security.capability` reads.
- **The handler set of the threshold** is the 23 functions listed in journal 0138, with compiler
  suffixes (`.isra.N`, `.constprop.N`) counted as their function and the `.cold` parts left out.
  The same computation on the fifth run's `funccover.csv` gives journal 0143's 659 of 743. This
  run: **684 of 809 PCs, 84.5 %**, 4 PCs short of 85 %. With the `.cold` parts: 688 of 838,
  82.1 %. The denominator grew by 66 PCs in four functions: `query_run` 211 to 233,
  `query2_pin` 15 to 31, `moobl_xattr_set` 50 to 64 and `moobl_xattr_get` 4 to 18. The layer has
  changed since the fifth run (findings F13 to F17, ADR-0027), and so has the kernel
  configuration. Covered PCs rose in `query_run` (186 to 201), `query2_pin` (13 to 23) and
  `moobl_xattr_get` (0 to 3) and fell by one in `moobl_xattr_set` (38 to 37). The other 19
  functions are within two PCs of the fifth run.
- **Replay of the read seeds**, as in journal 0154. A disposable guest of the same kernel and
  image (`snapshot=on`, 2 vCPUs pinned to CPUs 2-3) ran
  `syz-execprog -sandbox=none -procs=1 -repeat=1 -threaded=false -cover` on
  `xattr-get-security` and `xattr-get-fd`. PCs were mapped with `nm -S` of the module and
  `/sys/module/moobfs_layer/sections/.text`. The module's object has four
  `__sanitizer_cov_trace_pc` call sites in `moobl_xattr_get()`, at offsets 0x25, 0x61, 0x14c and
  0x162. Reading `security.capability` reaches 0x25, 0x61 (the `override_creds()` branch) and
  0x14c. Reading `user.moob.kind` reaches 0x25, 0x14c and 0x162. **Together the two seeds reach
  all four.** The manager's total of 18 for the function counts more sites than these four
  coverage callbacks; which ones was not established.

### Against S3's "done when"

- Zero open reports: **met** (six six-hour runs; F8, found by the first, is fixed).
- Every function a program can reach exercised at least once: **met for the handler set.** All
  23 functions have covered PCs in this run's report, `moobl_xattr_get()` included, and the
  replay reaches all four of its coverage sites.
- Handlers at 85 % of their PCs or more: **not met by this run's number.** It is 84.5 %, on a
  denominator that grew with the layer's changes and the hardened kernel. The fifth run's 88.7 %
  was on the earlier code and kernel.
- Reproducers of past crashes kept in `tests/`: as before (F8's regression in the suite).

## Changes

- `vm/fstests-local.sh`: `MOOB_FSTESTS_CPUS`, `MOOB_FSTESTS_MEM`. `vm/syzkaller-local.sh`:
  `SYZ_VMS`, `SYZ_CACHE`. The summary now takes the manager's own counter line
  (`corpus=N coverage=N exec total=N`); the old pattern (`corpus: N`) matched nothing in the
  pinned revision, so the summaries of earlier runs had no counts.
- `vm/syzkaller/seeds/xattr-get-security` (new).
- `vm/fstests/exclude-auto.txt` and `vm/fstests/exclude-auto-btrfs.txt` (new), the three
  `results-*-auto-6.12.113.txt` files, and `vm/fstests/README.md`.
- Surfaces: `docs/security-roadmap.md` (S2 and S3 notes and the current-state table),
  `ROADMAP.md`, `docs/testing.md`, `README.md` and `README.pt-BR.md`, `docs/faq.md` and
  `site/faq.html`, `docs/when-not-to-use.md`, `docs/adoption-risks.md` (current answer),
  `docs/architecture-overview.md`, `site/index.html`, `site/guide.html`,
  `scripts/site-journey.py` (fstests and fuzzing answers, a milestone) with the pages it
  generates, `CHANGELOG.md`, `docs/references.md` §37, and `docs/retired-claims.txt`.
  `docs/testing.md` §9 also dropped "a hostile lower tree" from what is not covered: the suite
  has run it since journal 0073 (S4).

## Checks of this change

`shellcheck --shell=sh` over `vm/*.sh` and `scripts/*.sh` (the `make lint` set, run from the
`koalaman/shellcheck:stable` image because the workstation has no shellcheck):
clean. `sh scripts/check-spdx.sh`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/site-faq.py --check` and `python3 scripts/check-site.py`: pass.

## Evidence kept

Raw logs (consoles, fstests `results/` directories, `manager.log`, `funccover.csv`,
`corpus.db`, the replay's coverage files and the helper scripts) are outside the repository, in
the maintainer's local folder for this review (`revisao/2026-10-29-s2s3-logs/`), as for journals
0091 and 0154. The test host's working directory was removed after the copy.

## Limitations

- One run per lower, on one kernel, without KASAN in the fstests guest; 406 to 549 tests per lower
  were not run.
- The `auto` run with the two new exclusion lists was not repeated. That the lists give a clean
  run follows from the per-test results, since `-E` only removes tests. It is not a measured run.
- fstests ran with 2 vCPUs instead of the 4 of journals 0091 and 0206; timings are not
  comparable.
- The S3 aggregate is 84.5 %, below the 85 % threshold. Which sites the manager counts in the
  18 PCs of `moobl_xattr_get()` was not established.
- The syzkaller round ran 2 VMs instead of 3, with fewer executions than the fifth run.

## Next steps

- S2: a full `auto` run with both exclusion lists on the three lowers, expected clean. Then decide
  whether S2's "done when" (a reproducible result in `make vm-test` or a dedicated target) needs
  a make target for the long run.
- S3: to close the aggregate, either a seventh round with 3 VMs and descriptions or seeds for
  the uncovered paths of `query_run`, `query2_pin` and `moobl_xattr_set`, or a recorded decision
  on how the threshold counts PCs on the hardened kernel (for example only the
  `__sanitizer_cov_trace_pc` sites).
