# 0281 — The port to Ubuntu 26.04's Linux 7.0 (batch b21)

Date: started 2026-11-10, 17:48 Brasília time; the last run ended 2026-11-11, 00:54 Brasília time. Status: **in progress**. Every gate passed, generic/798 excluded by the maintainer's decision of 2026-11-11, and the series is in `scripts/supported-kernels`. The batch's pull request is next. Estimated effort
M (one to three days, survey of 2026-11-10); the time recorded below is calendar time. Most of it
was spent waiting for VM runs on the test host, not working.

## Goal

ADR-0034's port to Ubuntu 26.04 LTS's kernel, Linux 7.0 (`7.0.0-43-generic` on this date, upstream
base 7.0.14). The goal is to adapt `layer/` to the one interface where 7.0 differs from the 7.3
branch, with no change of behavior, UAPI or on-disk format, and the 6.12 and 7.3 builds as they
were. The work also covers teaching the harness Ubuntu's kernel packaging and gathering the
evidence of `docs/compatibility.md` §1 rules 1 and 3 before the series joins
`scripts/supported-kernels`. The survey of 2026-11-10, kept outside the repository, mapped the
differences. It found that the 7.3 branch, forced on, gives one error on 7.0's headers.

## Merge of PR 193

PR 193 (batch b20, journal 0280) passed 17 of 17 checks and was CLEAN at 14:03 BRT on
2026-11-10. Landlock ran under KASAN in CI with `LANDLOCK_PASS` in both legs, 6.12.116 and
7.3.3. With the maintainer's agreement it was merged at 17:48 BRT as `6a711f4`, which is this
batch's base. Post-merge runs on `main` at `6a711f4`, all started 17:48 BRT:
- CI passed at 18:06;
- CodeQL passed at 17:49;
- Static analysis passed at 17:51;
- OpenSSF Scorecard passed at 17:48;
- Pages also ran and passed at 17:48.

## Decisions (maintainer, 2026-11-10)

1. **Ubuntu 26.04 GA's 7.0 becomes a target series** under ADR-0034 point 3, for the layer module
   only. It also covers Ubuntu 24.04's HWE kernel, which is the same 7.0 build
   (`7.0.0-42.42~24.04.1` in `noble-updates`).
2. **[ADR-0036](../decisions/0036-ubuntu-abi-series-policy.md)** sets the Ubuntu ABI policy:
   - DKMS and `make install` accept the whole series `7.0.0-*-generic`;
   - the documents name the release that was tested;
   - a weekly CI job builds against the newest Ubuntu 26.04 kernel.
   Debian stays exact-release. Alternatives (exact Ubuntu releases refreshed per SRU, any `7.0.*`,
   configure-time probes) are in the ADR.
3. **The stack warning is accepted, with no code change.** On Ubuntu's kernels `query2_exec()`
   (`layer/index.c`) uses 1248 bytes of stack and draws
   `warning: the frame size of 1248 bytes is larger than 1024 bytes [-Wframe-larger-than=]`.
   Ubuntu builds with `CONFIG_FRAME_WARN=1024`, Debian with 2048. Reducing the frame is not an
   interface adaptation (ADR-0022, ADR-0034 point 1), so DKMS and `make layer` on Ubuntu print the
   warning, and the build succeeds. The kernel's stack is 16 KiB on x86-64
   (`THREAD_SIZE`).

## Decision on generic/798 (maintainer, 2026-11-11)

generic/798 is excluded from fstests `quick` as a known limitation of the layer
(`vm/fstests/exclude-quick.txt`, with its reason), in the same way as generic/301 on btrfs
(journal 0277). `cachestat(2)` on a layer file reports the layer inode's page cache, which stays
empty, because the data is cached in the lower's backing file. overlayfs does the same: on
Ubuntu's 7.0.0-43-generic, an overlay file shows `Cached: 0` after a buffered write, and its upper
file shows `Cached: 1`. The test fails the same way on the supported 7.3.3, and on 6.12 it never
ran (Debian's `xfs_io` has no `cachestat`), so it is not a regression of the port. With the
exclusion, fstests `quick` passed on every lower of Ubuntu's own kernel in the second round.
Rule 3 is met, and `7.0.0-*-generic` joins `scripts/supported-kernels`.

## The code (ADR-0034 point 1, interface adaptations only)

Read against Ubuntu's tag `Ubuntu-7.0.0-43.43` (`git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux/+git/resolute`,
`Makefile`: 7.0.14) on 2026-11-10: `fs/namei.c`, `fs/locks.c`, `fs/stat.c`,
`include/linux/fs.h` and `include/linux/backing-file.h`.

- `layer/moobl_compat.h`: the series key `MOOBL_VFS_73` becomes `MOOBL_VFS_70`
  (`>= KERNEL_VERSION(7, 0, 0)`), and the name now says where the branch starts. A second key,
  `MOOBL_CREATE_73` (`>= 7.3`), chooses `.create`. A `static_assert` checks the type of
  `((const struct inode_operations *)0)->create` on each side: without `excl` when the key is
  set, with `bool excl` otherwise. The `#error` window is 6.13 up to before 7.0, and 7.1 and 7.2
  have an `#error` of their own, so only the three series read against their sources build.
- `layer/main.c`: `moobl_create_73` is built and installed only under `MOOBL_CREATE_73`, and 7.0
  installs `moobl_create` as 6.12 does. The other `#if MOOBL_VFS_73` sites are renamed, and their
  comments say "since 7.0" where the form starts there.

What the read showed for each site of journal 0275's table on 7.0:

| Site | 7.0 (Ubuntu-7.0.0-43.43) |
| --- | --- |
| `.create` | `int (*create)(struct mnt_idmap *, struct inode *, struct dentry *, umode_t, bool)` (`include/linux/fs.h:2009`); `vfs_create()` calls `->create(idmap, dir, dentry, mode, true)` (`fs/namei.c:4211`), `lookup_open()` passes `open_flag & O_EXCL` (`fs/namei.c:4518`). **The one difference from 7.3** |
| `vfs_create()` | `(idmap, dentry, mode, struct delegated_inode *)` (`fs/namei.c:4191`), the 7.3 form. The lower's `->create` gets `true` from it on 7.0, and on 7.3 it has no `excl`. ext4, btrfs and XFS do not read it (journal 0275) |
| `vfs_mkdir()`, `vfs_rmdir()`, `vfs_symlink()`, `start_renaming_dentry()`, `lookup_one()` | 7.3 forms (`fs/namei.c:5253`, `5360`, `5643`, `4050`, `3219`); the `static_assert`s on `vfs_mkdir()` and `start_renaming_dentry()` pass |
| `kernel_setlease()` | `-EINVAL` when `->setlease` is NULL (`fs/locks.c:2026–2033`), as on 7.3, so `.setlease = generic_setlease` is needed on 7.0 too |
| `vfs_getattr()` | security hook then `vfs_getattr_nosec()`, no `AT_GETATTR_NOSEC` (`fs/stat.c:254–263`), as on 7.3 |
| `struct backing_file_ctx` | `accessed(file)`, `end_write(kiocb, ssize_t)` (`include/linux/backing-file.h:17–18`), as on 7.3 |
| `i_ino` | `unsigned long` (`include/linux/fs.h:786`), unlike 7.3's `u64`. The self-check's warning already casts it to `unsigned long long` (`layer/index.c`), so the line is unchanged |

The Ubuntu-specific deltas in `fs/namei.c` and `fs/dcache.c` (`unlink_may_reveal()`,
`cant_mount`, the protected-links defaults and the `vfs_tmpfile` refusal) are not on a path the
layer calls; the suite and fstests on Ubuntu's own build below are the evidence.

## The supported-kernels list (ADR-0036)

An entry of `scripts/supported-kernels` is now an exact release or a series with one `*` that
stands for one or more digits. The three readers agree:

- `scripts/install.sh` (`kernel_matches`, `supported`), for `make install`;
- `tools/workspace.h` (`ws_kernel_entry`, `ws_kernel_listed`), for `moobfs mount`. Unit tests in
  `tests/workspace_test.c` cover the near misses: an empty or non-digit ABI, another flavour,
  `7.0.10-…`, a suffix, a prefix, two stars, a commented line;
- `packaging/debian/rules`, which turns `*` into `[0-9]+` in `BUILD_EXCLUSIVE_KERNEL`. The
  generated `7\.0\.0-[0-9]+-generic` was checked with bash's `=~` against the same releases as
  the shell function, with the same answers.

The series entry joins the list only after the gates below, in its own commit.

## Harness

- `vm/run.sh`: `MOOB_UBUNTU_KERNEL=7.0.0-43` takes `ubuntu:26.04` pinned by digest
  (`sha256:6a06111c…f4d3c`, Docker Hub on 2026-11-10) and its own image tag
  (`moobfs-m1-test-ubuntu`), so that an Ubuntu run and a Debian run can share a host.
- `vm/Dockerfile`: the selector pins `linux-image-7.0.0-43-generic`, `linux-modules-7.0.0-43-generic`,
  `linux-headers-7.0.0-43-generic` and `linux-headers-7.0.0-43`, all `=7.0.0-43.43`. btrfs, XFS
  and the dm targets are in `linux-modules`, not `-extra`.
- `vm/stage-modules.py` (new, moved out of `vm/in-container.sh`): reads `.ko.zst` with Python 3.14's
  `compression.zstd` as well as `.ko.xz`. It skips a module named in `modules.builtin` and refuses
  one that is neither a file nor built in. On 7.0.0-43-generic, ext4, virtio_blk, dm-mod and loop
  are built in, and the chain is `libblake2b xor raid6_pq btrfs xfs dm-log-writes dm-flakey`.
  Debian's staging is the same as before. `vm/init` loads `loop.ko` only when it was staged.
- `vm/in-container.sh`: the M1 module is skipped for any `7.*` release, Ubuntu's included.
- `vm/kasan-local.sh`: `MOOB_KASAN_KERNEL=7.0` selects the new pin.
- `vm/fstests-ubuntu.sh` and `vm/fstests-ubuntu/Dockerfile` (new): this is the survey's route
  (i). `vm/fstests-local.sh` runs inside an `ubuntu:26.04` image (same digest) that carries
  7.0.0-43-generic, Ubuntu's `virtme-ng` and fstests v2026.09.22. The container gets `/dev/kvm`,
  and `MOOB_KVER=7.0.0-43-generic` boots Ubuntu's own kernel, with the image's root as the
  guest's root.
- `vm/ubuntu-drift.sh` (new): the body of the weekly drift job.
- `scripts/check-pins.sh`: checks the 7.0 KASAN pin against kernel.org's `v7.x/sha256sums.asc`,
  the Ubuntu base digest in `vm/run.sh`, the three package pins in `vm/Dockerfile` and the release
  in `vm/in-container.sh`.

## CI

- `.github/workflows/ci.yml`: job `native_ubuntu_70`, "Native Linux VM (Ubuntu 7.0.0-43)", like
  `native_733`, with artifact `native-vm-ubuntu70-evidence`.
- `.github/workflows/kasan.yml`: a third leg, "Layer suite under KASAN/lockdep (7.0.14)", on
  upstream 7.0.14 (`KERNEL70_VERSION`, `KERNEL70_SHA256` = `de9999b7…0f98`), artifact
  `kasan-7014-evidence`. This is the survey's option (a), the precedent of 6.12.116 and 7.3.3.
  Option (b), Ubuntu's own source with `vm/kasan.config`, was not taken: the build is heavier and
  the pin would be a git tag rather than a signed list. Ubuntu's own build runs the native suite
  and fstests.
- `.github/workflows/ubuntu-drift.yml` (new): weekly (Wednesday 05:41 UTC) and on demand, it
  builds the layer against the newest `linux-headers-generic` of resolute and checks that the
  list names that release. It runs no suite.

## Evidence

All runs were on the test host (Debian 13, x86-64, 8 CPUs, KVM), pinned to CPUs with `taskset` or
`--cpuset-cpus`, at the commits named. Times are calendar time, BRT. Several runs shared the host
at once, so the durations include waiting for disk I/O (up to 44 % iowait while the three fstests
guests and the KASAN guest ran together).

**Compile, 17:52–17:57, working tree of `8d348f2`.** `make -C <headers> M=layer modules` was run
in throwaway containers of the pinned images:

| Headers | Image | Result |
| --- | --- | --- |
| Ubuntu `7.0.0-43-generic` (`7.0.0-43.43`) | `ubuntu:26.04@sha256:6a06111c…` | `.ko`, 0 errors; one warning, the accepted frame size of `query2_exec()` |
| Debian `7.3.3+deb14-amd64` (`7.3.3-1`) | `debian:sid-slim`, digest of `vm/run.sh` | `.ko`, 0 errors, 0 warnings |
| Debian `6.12.111+deb13-amd64` (`6.12.111-1`) | `debian:trixie-slim`, digest of `vm/run.sh` | `.ko`, 0 errors, 0 warnings |

`make checkpatch`: clean. Its first run flagged `struct inode_operations` in the new
`static_assert` (CONST_STRUCT), and the assert now names a `const` struct.

**Negative round for the `.create` key, 18:00–18:10, `9a5ec95`** (`~/b11night/b21/neg.sh`). The
same tree was built with the key changed:

| Variant | Headers | Result |
| --- | --- | --- |
| as committed | Ubuntu 7.0.0-43 | `.ko` |
| `MOOBL_CREATE_73` 1 on 7.0 (the `.create` key starting at 7.0, with `MOOBL_VFS_70`) | Ubuntu 7.0.0-43 | refused: `static assertion failed: "moobfs_layer needs .create without excl here: see journal 0281"`, then the initializer's incompatible-pointer error; no `.ko` |
| `main` at `6a711f4` | Ubuntu 7.0.0-43 | refused at `#error "moobfs_layer is ported to Linux 6.12 and 7.3 only (ADR-0034)"`, then the 6.12 forms; no `.ko` |
| as committed | Debian 7.3.3 | `.ko` |
| `MOOBL_CREATE_73` 0 on 7.3 | Debian 7.3.3 | refused: `static assertion failed: "moobfs_layer needs .create with excl here: see journal 0281"`; no `.ko` |

A wrong `.create` key therefore never builds, on either series, and the message names this
journal. With `-Wincompatible-pointer-types` an error in gcc 15, the initializer alone would also
stop it, but without a message. This compile check is the regression test. No runtime test can
tell the keys apart: ext4, btrfs and XFS ignore `excl`, and the VFS checks negativity under the
parent's lock on both series.

**Native suite on Ubuntu 7.0.0-43-generic, 18:00–18:46, `9a5ec95`, CPUs 0–1, KVM**
(`MOOB_VM_KVM=1 MOOB_UBUNTU_KERNEL=7.0.0-43 sh vm/run.sh`). Exit 0, `MOOBFS_LAYER_PASS`,
`MOOBFS_ACCEPTANCE_PASS`, 311 pass markers, no kernel diagnostic. `LANDLOCK_PASS` and
`DIRSTALL_PASS` passed, and the memcg steps ran (`MEMCG_ACCOUNT_PASS`; Ubuntu's kernel has the
memory controller). The only skips are `MOOBFS_VM_SKIPPED` (M1, not built on 7.x) and
`PYLIB_SKIPPED`, which every native run prints. Environment: `linux-image-7.0.0-43-generic
7.0.0-43.43`, gcc 15.2.0 (Ubuntu 15.2.0-16ubuntu1), QEMU 10.2.1 (Ubuntu). The staged chain was
`libblake2b xor raid6_pq btrfs xfs dm-log-writes dm-flakey`, with 4 modules built in, and loop
built in.

**KASAN/UBSAN/lockdep on upstream 7.0.14, 18:00–20:31, `9a5ec95`, CPUs 2–7, KVM**
(`MOOB_KASAN_KERNEL=7.0 sh vm/kasan-local.sh`, its own `KCACHE`). The tarball matched the pin;
the kernel (`7.0.14-virtme`, `vm/kasan.config`, gcc 14.2 of Debian) was built 18:00–18:36. The
guest ran 18:37–20:31. Result: `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with
no kernel diagnostics`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_PYLIB_PASS`, 298
pass markers, the same count as 6.12.116 in batch b20. It includes `LANDLOCK_PASS`,
`DIRSTALL_PASS`, `SELFCHECK_PASS`, `FAULT_INJECTION_PASS` and the QUERY2 markers. Lockdep stayed
on to the end. Skipped: only the three memcg markers, as on 6.12. This covers `moobl_create()` on
7.0 (with `excl`) and every other path of the 7.3 branch now taken on 7.0.

**fstests smoke through the container, about 18:50–18:59, `98aebdf`.**
`vm/fstests-ubuntu.sh ~/fstests ext4 generic/001 generic/013`: `MOOBFS_FSTESTS_KERNEL:
7.0.0-43-generic`, passed both. Four earlier attempts failed in the harness, never in the
module:
- the image's clone of fstests got HTTP 502 from git.kernel.org twice, so the script now takes a
  checkout;
- fstests' `configure` lacked `pkgconf` and `autoconf`;
- Ubuntu's `virtme-ng` 1.40 refused `--rwdir` on the bind mount (1.42 from
  `vm/kasan-requirements.txt` is used instead);
- without the `zstd` command, virtme-ng put no virtiofs or 9p module in its initramfs and the
  guest panicked at boot ("Failed to mount real root").

**Drift script, before the series entry, 19:03–19:04, `9a5ec95`.** `vm/ubuntu-drift.sh` in the
pinned `ubuntu:26.04` image installed `linux-headers-generic 7.0.0-43.43` (the newest of
`resolute-updates` on this date). It stopped with `ubuntu-drift: 7.0.0-43-generic is outside
scripts/supported-kernels`, exit 1, as it must while the list has no Ubuntu entry.

**fstests `quick` on Ubuntu's own 7.0.0-43-generic, first round, `98aebdf`.** The run used
`vm/fstests-ubuntu.sh ~/fstests LOWER` with the default rule-3 arguments: `-g quick` with
`exclude-quick.txt`, plus `exclude-quick-btrfs.txt` on btrfs. Each lower had 2 vCPUs and 3 GiB,
with the container pinned (ext4 on 0–1, btrfs on 4–5, XFS on 6–7).

| Lower | Time (BRT) | Result |
| --- | --- | --- |
| ext4 | 19:00–21:48 | failed 6 of 631 |
| btrfs | 19:01–22:14 | failed 6 of 630 |
| XFS | 19:02–22:19 | failed 6 of 631 |

On every lower the same six failed, and no kernel diagnostic appeared (no `BUG`, `WARNING` or
`Oops`). One report, `INFO: task jbd2/vda-8 blocked for more than 122 seconds` on ext4 at
about 19:30 BRT (generic/095), is the guest's own journal thread waiting on the virtual disk.
Its stack has no layer frame. The host was then at 44 % iowait with four guests running.

- generic/193, 597 and 634 failed on the image, not the module. Ubuntu 26.04 ships Rust
  coreutils (`coreutils-from-uutils`) by default: their errors end in `(os error 1)`, `ln` words
  the refusal differently, and `touch` cannot parse `Dec 31 23:59:59 UTC 2147483647`. fstests
  expects GNU's output. The image now installs `coreutils-from-gnu` (commit `575b9a2`).
- generic/131 and 571 failed on the image too: the lock tests' client and server talk over
  `lo`, which virtme-ng could not bring up without `iproute2` (`failed to run: "ip" link set dev
  lo up`). Same commit.
- **generic/798 (cachestat) is a behavior of the layer, and it does not depend on the kernel.**
  It ran here for the first time: Debian's `xfs_io` has no `cachestat`, and every committed
  result of 6.12.113 (`vm/fstests/results-*-6.12.113.txt`, quick and auto, three lowers) records
  `notrun generic/798 xfs_io cachestat support is missing`. Over the layer, `cachestat(2)` on a
  layer file reports `Cached: 0, Dirty: 0` after buffered writes. The data is cached in the
  lower's backing file, not in the layer inode's mapping. It fails the same way on kernel.org's
  7.3.3 built with `vm/fstests.config` (same image, `MOOB_KDIR`, 21:58–22:02 BRT; generic/193
  passed there with GNU coreutils). overlayfs behaves the same on Ubuntu's 7.0.0-43-generic. A
  one-off guest wrote 4 KiB through an overlay mount and ran `cachestat`: `Cached: 0` on the
  overlay file and `Cached: 1` on the upper file (`~/b11night/b21/ovl/t.sh`, about 22:05). No
  fix fits the freeze (ADR-0022, ADR-0034 point 1). Whether it joins the exclusion list, as
  generic/301 did on btrfs (journal 0277), is a decision for the maintainer.

**fstests `quick` on Ubuntu's own 7.0.0-43-generic, second round, `575b9a2`** (the image with
GNU coreutils and `iproute2`). The arguments and the sizes were the same as in the first round.

| Lower | Time (BRT) | Result | Failed |
| --- | --- | --- | --- |
| ext4 | 2026-11-10 22:03 – 2026-11-11 00:20 | 630 of 631 passed | generic/798 |
| btrfs | 2026-11-10 22:21 – 2026-11-11 00:54 | 629 of 630 passed (`generic/301` excluded, journal 0277) | generic/798 |
| XFS | 2026-11-10 22:21 – 2026-11-11 00:48 | 630 of 631 passed | generic/798 |

The five failures of the first round passed. No kernel diagnostic appeared. A `blocked for more
than 122 seconds` report came from btrfs's transaction thread at 1353 s of guest time, in
`barrier_all_devices()`, waiting for the virtual disk's flush. No layer frame was in it, and it
came while the KASAN 6.12 run, the native 6.12 run and the pre-push checks were also using the
host's disk.

**Rule 3 was therefore met except for generic/798.** Until the maintainer's decision of
2026-11-11 (above), the series entry `7.0.0-*-generic` stayed out of `scripts/supported-kernels`.
The change that adds it was prepared outside the repository as one patch: the entry, generic/798
in `vm/fstests/exclude-quick.txt` with its reason, and the public surfaces. It was applied after
the decision.

## The batch's full suites

Test host, KVM, calendar time:

- `sh vm/kasan-local.sh` (upstream 6.12.116, `vm/kasan.config` of `main`, the kernel cached by
  batch b20), `98aebdf`, CPUs 2–3, 2026-11-10 20:31–22:41: exit 0, `MOOBFS_KASAN_PASS` with no
  kernel diagnostic, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_PYLIB_PASS`,
  `LANDLOCK_PASS`, 298 pass markers. Skipped: the three memcg markers.
- `MOOB_VM_KVM=1 sh vm/run.sh` (Debian 6.12.111, the default), `e7e4940`, CPUs 2–3, 22:42–23:34:
  exit 0, `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, 314 pass markers. Only
  `PYLIB_SKIPPED`, which every native run prints.
- The KASAN suite on 7.0.14 above is the batch's new leg.
- Pre-push checks (`~/b11night/prepr/run6.sh`, `run5.sh` with `git diff --check 6a711f4 HEAD`),
  `e7e4940`, CPUs 0–3, 22:42–22:58, 12 of 12 PASS: `make lint`, `make checkpatch`, `make test`,
  the same under clang ASan/UBSan, `make hostile-test`, `make pylib-test`, retired claims, pins
  (online, the three tarballs against kernel.org), `make mutants-check`, site links,
  `git diff --check`, and the test programs built on Ubuntu 24.04.

The commits after `e7e4940` change only records and `vm/fstests-ubuntu`, which no suite above
uses.

## Public surfaces

Committed here: `docs/testing.md` (the selector, the third KASAN leg, the native job and the drift
workflow), `docs/development.md` (the new pins), `CHANGELOG.md`, `docs/references.md` §54, the
manual page of `moobfs` (the series entry) and the history index.

After the decision of 2026-11-11, the surfaces that state the supported kernels say that Ubuntu
26.04's `7.0.0-*-generic` series is supported and name the tested release, in both languages:
- `README.md`, `README.pt-BR.md`, `docs/status*.md` and `docs/faq.md` (with `site/faq.html`);
- `SECURITY.md`, `CONTRIBUTING.md`, `docs/architecture-overview.md`, `docs/operations.md`,
  `docs/distribution-strategy.md`, `docs/security-operations.md`, `docs/compatibility.md` (its
  "Today" paragraph and dated notes under rules 1, 2 and 3) and `docs/when-not-to-use.md`;
- `ROADMAP.md`, `site/index.html`, `site/plan.html` and `scripts/site-journey.py` (with
  `site/status.html` and `site/pt/status.html`);
- `docs/matrix-and-limits.md` (journal 0282), folded into this batch, and the table of
  `docs/compatibility.md` §1.

The old wordings are in `docs/retired-claims.txt`. Until the decision these edits were kept
out of the branch, in a patch, because the series was not yet in the list.

## Time

Calendar time 2026-11-10 17:48 – 2026-11-11 00:54 BRT (about 7 h). The effort was about 3 h of
reading, code, harness and records; the rest was waiting for runs. This is within the M estimate.

## Limitations

- Of Ubuntu's `7.0.0-*-generic` series only `7.0.0-43-generic` ran the suite and fstests. The
  rest are accepted by policy (ADR-0036). The weekly build catches a change of form inside the
  series, not a change of meaning. `7.0.0-44.44` was in `-proposed` on this date.
- KASAN ran on upstream 7.0.14, not on Ubuntu's build with its SAUCE patches. Ubuntu's build ran
  the native suite and fstests, without the memory detectors.
- No Ubuntu install, DKMS or Secure Boot test: `make install-test` and `make package-test` are
  Debian 13 VMs. Ubuntu signs its own modules, and DKMS on Ubuntu signs with its MOK as Ubuntu
  does it, but this was not exercised. `scripts/repro-ko.sh` still covers the Debian 6.12
  releases only.
- The new CI job, the third KASAN leg and the drift workflow had not run on GitHub when this
  journal was written: nothing was pushed.
- When 26.04's HWE stack moves to 7.3 (expected with 26.04.2, an inference from Ubuntu's rolling
  model), Ubuntu's `7.3.0-N-generic` needs its own entry and evidence. Its `LINUX_VERSION_CODE`
  is 7.3.0, and the layer builds on 26.10's `7.3.0-9-generic` as it is (survey).

## Next step

- Done on 2026-11-11: the matrix-page branch (journal 0282) folded in, then the pull request of batch b21, with the
  maintainer's agreement. Its first CI run is the first run of the new job, the 7.0 KASAN leg and
  `check-pins.sh` online on GitHub.
- When a newer Ubuntu ABI reaches `resolute-updates`, the drift job builds against it. Moving
  `MOOB_UBUNTU_KERNEL` to it reruns the native suite and fstests (ADR-0036 point 4).
