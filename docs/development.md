# Development environment

How to build, test and extend MoobFS reproducibly. The rule that governs
everything here: **the module is loaded only inside a disposable VM, never on
your host.** Compiling on macOS, or on any Linux host, tests nothing about the
kernel code; only `make vm-test` and the KASAN workflow do.

For the meaning of each test and marker see [testing.md](testing.md). For the
contribution rules (DCO, journal, review) see [../CONTRIBUTING.md](../CONTRIBUTING.md).

## 1. Host requirements

| Need | Linux x86-64 | macOS (Intel or Apple Silicon) |
| --- | --- | --- |
| Host tools and tests (`make`, `make test`) | C11 compiler (gcc or clang), GNU Make, Python 3 | Xcode command-line tools, Python 3 |
| `make fuzz` | LLVM clang with libFuzzer | Not available (Apple clang ships no libFuzzer); use CI or a Linux VM |
| `make vm-test` | Docker | Docker Desktop (or another Docker engine) |
| KASAN locally | Linux, `qemu-system-x86`, kernel build toolchain, `virtme-ng` | Not supported on macOS directly; use a Linux VM or the GitHub workflow |
| KVM | Optional. The harness always uses TCG (`-accel tcg`); `/dev/kvm` matters only for the virtme-ng KASAN run, which uses it when present | Not applicable |

Disk: the Docker image pulls a Debian base plus kernel image and headers, and
`build/` holds images, kernels and logs; plan for several GiB. Network: needed
for the first `docker build` and for fetching the KASAN kernel tarball.

### Dev container (experimental)

`.devcontainer/devcontainer.json` describes an Ubuntu 24.04 container (the CI
runners' release) with the docker-in-docker feature and the host tools
(`build-essential`, `clang`, `libclang-rt-18-dev`, `llvm`, `python3`,
`shellcheck`, `libsqlite3-dev`, `sqlite3`, `curl`, `perl`), installed by
`.devcontainer/post-create.sh`. It is meant for GitHub Codespaces or a local Dev
Containers client, so that `make test`, `make lint` and `make vm-test` run
without installing anything on your machine; `make vm-test` still loads the
module only inside its QEMU guest. The time, disk and minimum machine for `make
vm-test` in a Codespace have not been measured yet, so the container is not
advertised in the README and declares no `hostRequirements`; journal 0039 tracks
the measurement.

## 2. Clone and build on the host

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make            # builds build/host/{mkfs.moobfs,fsck.moobfs,attr_test}
make test       # attr_test unit tests + tests/test_images.py (9 tests, 15 malformed images)
make fuzz FUZZ_TIME=30   # libFuzzer, 30 s per target: attributes, image parser, tool parsers (LLVM clang only)
```

Sanitizers, in a separate output directory so that host binaries stay untouched:

```sh
make test BUILD=build/asan CC=clang \
  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LDFLAGS='-fsanitize=address,undefined'
```

`make attr-tools` builds the Linux-only xattr tools (`moobfs-attr`, `moobfs-check`,
`moobfs-index`); on macOS it fails to link, which is expected. `make module` and
`make layer` compile the two kernel modules against `KDIR` (default: the running
kernel's headers); they are meant for the VM container, not for your host.

`make help` lists every target with one line each. `make lint` runs the same
checks as the CI `Lint` job (shellcheck at every severity, info included, on the
tracked `vm/*.sh` and `scripts/*.sh`, with the one repository-wide exception and its reason in
`.shellcheckrc`; and SPDX headers on C sources through
`scripts/check-spdx.sh`); it needs `shellcheck` installed (`apt install
shellcheck` or `brew install shellcheck`). The DCO check runs only in pull
requests. `make checkpatch` runs the kernel's `checkpatch.pl --no-tree` on the
tracked `layer/` and `kernel/` sources; `scripts/checkpatch.sh` downloads the
script and its data files from the stable tag `v6.12.112` into `build/` and
verifies their SHA-256 first (needs `curl` and `perl`). The CI `Lint` job runs
it as an advisory step that does not fail the job.

## 3. `make vm-test`: the native Linux suite

`make vm-test` runs `vm/run.sh`, which:

1. `docker build --platform linux/amd64` from `vm/Dockerfile`: Debian trixie-slim
   pinned by digest, with `linux-image-6.12.111+deb13-amd64` and the matching
   headers pinned to `6.12.111-1`, plus gcc, QEMU, e2fsprogs, btrfs-progs,
   xfsprogs, git, sqlite3, dmsetup and acl.
2. `docker run --platform linux/amd64 -v "$PWD:/work"` executing `vm/in-container.sh`.
   The container is not privileged and does not use `/dev/kvm`.

Inside the container, `vm/in-container.sh`:

- builds the host tools into `build/linux/` and runs `tests/test_images.py`;
- builds `kernel/moobfs.ko` (M1) and `layer/moobfs_layer.ko` against the pinned headers;
- stages a busybox initramfs under `build/vm/stage.XXXXXX` with `vm/init`,
  `vm/suite.sh`, both modules, the lower-filesystem module chain extracted from
  `modules.dep` (`virtio_blk`, `ext4`, `btrfs`, `xfs`, `crc32c_generic`, `dm-mod`,
  `dm-log-writes`, `dm-flakey`), the dynamically linked clients copied with their
  libraries (`git`, `sqlite3`, `dmsetup`, `mke2fs`, `e2fsck`, `setfacl`,
  `getfacl`) and the statically linked test binaries (`mounted`, `layer`,
  `query`, `batch-errors`, `ipc`, `idmap`, `fsx`, `replay-log`, `moobfs-attr`, `moobfs-check`,
  `moobfs-index`);
- creates sparse lower images (`lower-ext4.img` 64 MiB, `lower-btrfs.img`
  256 MiB, `lower-xfs.img` 300 MiB) and two blank disks (`scratch.img`,
  `log.img`, 64 MiB each) for dm-log-writes and dm-flakey;
- boots `qemu-system-x86_64 -machine q35 -accel tcg -cpu max -m 512M -smp 2`
  with the five images as virtio disks, `rdinit=/init`, no network, under
  `timeout 2400`;
- requires QEMU to exit 0 and the console to contain `MOOBFS_VM_PASS:`,
  `MOOBFS_LAYER_PASS:` and `MOOBFS_ACCEPTANCE_PASS:` and no `MOOBFS_VM_FAIL:`.
  QEMU exit 0 alone is insufficient: the guest also powers off on failure.
  Optional skips remain exclusions; see the testing guide.
- runs `tests/test_harness_verdicts.py` in the container before building the
  modules: controlled producer/child failures and success controls, without a module.

Logs and evidence land in `build/vm/`:

| File | Content |
| --- | --- |
| `console.log` | Full guest console: every marker, every failure, `dmesg` tail on failure |
| `environment.txt` | Kernel, gcc, QEMU and package versions; SHA-256 of kernel, modules and image |
| `kernel.config` | The Debian kernel configuration that ran |
| `image-report.json` | `fsck.moobfs` report of the M1 fixture |
| `shared.img`, `moobfs.ko`, `moobfs_layer.ko`, `initramfs.cpio.gz` | The artifacts that were booted |
| `lower-*.img`, `scratch.img`, `log.img` | Lower disks after the run |

CI uploads `console.log`, `image-report.json`, `environment.txt` and
`kernel.config` as the `native-vm-evidence` artifact for 30 days.

Duration: the CI job has a 35-minute timeout and the QEMU phase a 2400-second
timeout. The CI timeout applies to its faster runner; a full local run under
TCG reached the former 1200-second guest guard (journal 0175). On Apple Silicon
everything runs under x86 emulation and takes
noticeably longer than on a Linux x86-64 host; measured times on specific
machines are not recorded yet, so treat any figure you see elsewhere as
approximate until a journal entry records a measurement.

## 4. How the suite is parametrized

`vm/suite.sh` is shared by the Debian initramfs harness and the KASAN harness.
The caller sets:

| Variable | Meaning | `vm/init` (Debian) | `vm/kasan-guest.sh` (virtme-ng) |
| --- | --- | --- | --- |
| `BIN` | Directory with `layer`, `fsx`, `query`, `idmap`, `moobfs-*`, `replay-log`, `moobfs_layer.ko` | `/` (initramfs root) | `build/vng` |
| `TOOLS` | Directory with `git`, `sqlite3`, `dmsetup`, `mke2fs`, `e2fsck`, `setfacl`, `getfacl` | `/tools` | `build/vng/tools` (symlinks to the host's binaries) |
| `MODS` | Optional directory with lower-fs modules and an `order` file | `/mods` | `build/vng/nomods` (filesystems are built in) |
| `LOWER`, `LAYER` | Mount points | defaults `/lower`, `/mnt/layer` | `/tmp/moob-lower`, `/tmp/moob-layer` (the 9p root is read-only) |
| `DEV_EXT4`, `DEV_BTRFS`, `DEV_XFS` | Block devices with the prepared lower filesystems | `/dev/vda`, `/dev/vdb`, `/dev/vdc` | same |
| `DEV_SCRATCH`, `DEV_LOG` | Blank devices for dm-log-writes and dm-flakey | `/dev/vdd`, `/dev/vde` | same |
| `fail()` | Function that prints `MOOBFS_VM_FAIL: <step>` and stops | defined in `vm/init` | defined in `vm/kasan-guest.sh` (also saves `dmesg.txt`) |

The suite runs `run_lower <fs> <device> <reflink yes|no>` for ext4, btrfs and
XFS, then the acceptance block (git, SQLite, power loss, I/O errors) over ext4.
For development, `MOOB_ONLY=<step>` selects named steps and ends with a partial
pass marker; the complete run remains the validation gate (see
[`testing.md`](testing.md), section 5). Correction dated 2026-10-20: the former
claim that this switch was still planned had become stale.

The `permgaps` step (journal 0172) runs decisive directory-search, filtered
truncation, attributed-directory and fixed independent-encoding checks over
all three lowers. The `release` step also checks uid 1001 before a real index
release and after rebuild. Allocation-fault queries remain root-only.

The `rootpath` step (issue 110, journal 0174) renames a domain directory
while a QUERY2 waits for that domain's build mutex, then checks that the
mount-wide QUERY finds every file under the new name on all three lowers.

The `overbudget` step (issue 111, journal 0175) alternates queries between two
domains that fit the index budget individually but not together, while other
domains are rebuilt after renames. It rejects `ENOMEM` on ext4, btrfs and XFS.

## 5. Adding a test

1. **Guest-side logic** goes into `vm/suite.sh`, inside `run_lower` when it must
   run over every lower filesystem, or after it when once over ext4 is enough.
   Every check ends in `|| fail "<unique-step-name>-$fs"`; a success prints a
   marker only when it summarizes a block (`echo "SOMETHING_PASS: $fs"`). The
   step name is what a failing run prints after `MOOBFS_VM_FAIL:`, so make it
   searchable.
2. **A new test binary** lives in `tests/<name>.c` with an SPDX header, builds
   statically and prints its own marker on success. Add it in two places:
   - `vm/build-tests.sh`: one `t <name> [flags] tests/<name>.c ...` line. This is the
     only list of test programs; `vm/in-container.sh`, `vm/kasan-local.sh` and the
     KASAN, KCSAN and arm64 workflows all build from it;
   - `vm/suite.sh`: the invocation as `$BIN/<name> ...`. The suite header comment
     lists what `BIN` must contain; update it.
3. **A new client or tool from the distribution** needs `stage_bin /path` in
   `vm/in-container.sh`, the package in `vm/Dockerfile`, the same package in the
   apt line of `kasan.yml`, and its name in the `for t in ...` loop of
   `vm/kasan-guest.sh`.
4. Run `make vm-test`, keep `build/vm/console.log`, and describe the test and the
   marker in [testing.md](testing.md). Record the run in the journal.

Any kernel-side change (`layer/`, `kernel/`) also needs the KASAN run; see below.

## 6. Running KASAN/lockdep locally

The workflow `.github/workflows/kasan.yml` is the reference; replicate its steps
on a Linux x86-64 host (Ubuntu 24.04 is what CI uses):

```sh
# 1. Dependencies (Debian/Ubuntu names)
sudo apt-get install -y build-essential flex bison libelf-dev libssl-dev bc cpio kmod \
  qemu-system-x86 qemu-utils python3-pip python3-venv udev \
  e2fsprogs btrfs-progs xfsprogs sqlite3 git dmsetup acl
python3 -m pip install --user --require-hashes -r vm/kasan-requirements.txt   # virtme-ng 1.41
export PATH="$HOME/.local/bin:$PATH"

# 2. Kernel source, pinned and verified (values from kasan.yml)
KERNEL_VERSION=6.12.112
KERNEL_SHA256=164dc9d1f6c93c61a15e1f071c48379b467f2b17c469cce7223471968208ed03
mkdir -p build && curl -fsSL -o build/linux.tar.xz \
  "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-${KERNEL_VERSION}.tar.xz"
echo "${KERNEL_SHA256}  build/linux.tar.xz" | sha256sum -c -
mkdir -p build/linux && tar -xJf build/linux.tar.xz -C build/linux --strip-components=1

# 3. Configure with vm/kasan.config and build
(cd build/linux && vng --kconfig --config ../../vm/kasan.config && vng --build --verbose)

# 4. Module, tools, tests and disks (same commands as the workflow step)
make layer KDIR="$PWD/build/linux"
make attr-tools BUILD=build/vng CFLAGS="-O2 -g -static"
gcc -static -O2 -Wall -Wextra -Werror tests/layer.c -o build/vng/layer
gcc -static -O2 -Wall -Wextra -Werror tests/hostile.c -o build/vng/hostile
gcc -static -O2 -Wall -Wextra -Werror -Iinclude tests/query.c common/attr.c -o build/vng/query
gcc -static -O2 -Wall -Wextra -Werror tests/ipc.c -o build/vng/ipc
gcc -static -O2 -Wall -Wextra -Werror tests/idmap.c -o build/vng/idmap
gcc -static -O2 -Itests/third_party/fsx tests/third_party/fsx/fsx.c -o build/vng/fsx
gcc -static -O2 tests/third_party/log-writes/replay-log.c tests/third_party/log-writes/log-writes.c -o build/vng/replay-log
cp layer/moobfs_layer.ko build/vng/
truncate -s 64M build/vng/lower-ext4.img && mke2fs -q -F -t ext4 -b 4096 -L moob-lower build/vng/lower-ext4.img
truncate -s 256M build/vng/lower-btrfs.img && mkfs.btrfs -q -f -L moob-lower build/vng/lower-btrfs.img
truncate -s 300M build/vng/lower-xfs.img && mkfs.xfs -q -f -L moob-lower build/vng/lower-xfs.img
truncate -s 64M build/vng/scratch.img build/vng/log.img

# 5. Boot the KASAN kernel with the checkout as root and run the suite
R="$PWD"
(cd build/linux && vng --user root --cpus 2 --memory 3G --verbose --rwdir "$R/build" \
  --disk "$R/build/vng/lower-ext4.img" --disk "$R/build/vng/lower-btrfs.img" \
  --disk "$R/build/vng/lower-xfs.img" --disk "$R/build/vng/scratch.img" --disk "$R/build/vng/log.img" \
  --exec "sh $R/vm/kasan-guest.sh" 2>&1 | tee "$R/build/vng/console.log")
grep -q '^MOOBFS_KASAN_PASS:' build/vng/console.log && ! grep -q '^MOOBFS_VM_FAIL:' build/vng/console.log
```

Evidence lands in `build/vng/console.log`, `build/vng/dmesg.txt` and
`build/linux/.config`. `vm/kasan-guest.sh` fails on any `BUG: KASAN`, `KASAN:`,
`BUG:`, `Oops:`, `WARNING:`, lockdep (`possible circular/recursive locking`),
`INFO: task/rcu` or `UBSAN:` line in `dmesg`.

Duration: the CI job has a 150-minute timeout, most of it the kernel build; the
instrumented guest is several times slower than the Debian kernel under TCG.
The workflow runs on pull requests that touch `layer/`, `vm/` or the workflow
itself, weekly, and on demand (`gh workflow run kasan.yml --ref <branch>`). When
you cannot run it locally, trigger it on your branch and link the run in the PR;
the `kasan-evidence` artifact holds the console, `dmesg` and `.config`.

## 7. Coding style

- Kernel code (`layer/`, `kernel/`): Linux kernel style, tabs, 80 columns as a
  guide, explicit bounds and endianness, `checkpatch.pl --no-tree` clean where
  practical (`make checkpatch`; advisory in CI, baseline in journal 0039).
- Tools and tests: C11, `-Wall -Wextra -Werror -Wconversion -Wshadow` (the
  `Makefile`'s `WARN`), no warnings silenced to pass the build.
- Every source file starts with `// SPDX-License-Identifier: GPL-2.0-only` (C
  sources) or `# SPDX-License-Identifier: GPL-2.0-only` (Python, shell). CI
  checks the `.c` and `.h` files under `layer/ common/ tools/ tests/ include/`
  (vendored code under `tests/third_party/` keeps its own headers).
- Shell scripts: POSIX `sh`, `set -eu` where the script is not sourced, pass
  `shellcheck` (CI runs it on `vm/*.sh` and `scripts/*.sh` through `make lint`).
- Commit messages and the DCO: see [../CONTRIBUTING.md](../CONTRIBUTING.md).

## 8. What "nothing is tested on Linux by compiling on macOS" means

`make` and `make test` on macOS exercise the portable tools and the shared
attribute encoding. They do not compile the kernel modules, do not run
`moobfs-attr`/`moobfs-check`/`moobfs-index` (which need Linux xattr calls), and
do not touch the VFS. A green `make test` on macOS says nothing about `layer/`.
Likewise, a successful `make layer` on a Linux host only proves the module
compiles against those headers. Only `make vm-test` (Debian 6.12.111 in QEMU)
and the KASAN workflow (upstream 6.12.112) load and exercise the module, and
only on those kernels. Write journal entries and PR descriptions accordingly:
name the command, the kernel and the log file; never report a compile as a test.

## Stale binaries between containers

`build/linux` is shared by `make vm-test` (static binaries built inside the Debian container)
and by any other Linux container used for host-side builds (for example `gcc:13` to run
`bench/smoke.sh`). Binaries built elsewhere are newer than the sources, so `make` inside the VM
container keeps them and the initramfs ends up with dynamically linked tools that cannot run
there; the first symptom is an early `MOOBFS_VM_FAIL` such as `attr-set-ext4`. Run
`rm -rf build/linux` before `make vm-test` after building in another container (journal 0025).

## Pinned versions

The builds and tests depend on pinned versions, each stated in more than one place.
`sh scripts/check-pins.sh` prints them all, fails when two copies of a pin disagree, and checks
the upstream kernel tarball against kernel.org's `sha256sums.asc` (the Lint job runs it; add
`--offline` to skip that last check). To move to another version, change every copy in one commit:

| Pin | Copies |
| --- | --- |
| Upstream kernel for KASAN and KCSAN (`KERNEL_VERSION`, `KERNEL_SHA256`) | `.github/workflows/kasan.yml`, `.github/workflows/kasan-arm64.yml`, `.github/workflows/kcsan.yml`, `.github/workflows/interop.yml`, `tests/static/Dockerfile`, this file's KASAN section; `vm/kasan-local.sh` and `vm/syzkaller-local.sh` read `kasan.yml` |
| checkpatch.pl tag and file hashes | `scripts/checkpatch.sh` (`TAG` follows the KASAN kernel) |
| Debian image digest and kernel package of `make vm-test` | `vm/Dockerfile` (image and headers packages at the same version); `tests/static/Dockerfile` uses the same image and one of its kernels (`DEBIAN_KERNEL`) |
| Static checkers of `make static` (smatch commit, sparse and Coccinelle package versions) | `tests/static/Dockerfile` only (journal 0242); a change of tool version usually changes the findings, so `tests/static/baseline.txt` is reviewed in the same commit |
| virtme-ng and its dependencies | `vm/kasan-requirements.txt` (regenerate the hashes as its header says) |
| Tools of the interoperability test (fselect and cshatag with SHA-256, Recoll and KFileMetaData package versions) | `.github/workflows/interop.yml` only (journal 0140) |

Take the new kernel SHA-256 from kernel.org's `sha256sums.asc` (whose signature can be checked
with the kernel.org signing keys), then run the script.

