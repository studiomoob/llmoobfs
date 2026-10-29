# 0236 — Reproducible build of the module and the packages (S10)

Date: 2026-10-29. Started about 12:40 UTC; ended about 14:10 UTC. This is calendar time, with the
test-host runs; it is not an effort measurement. Estimated effort: S.
The actual effort is recorded at the end.

## Goal

The remaining item of security roadmap S10 (ADR-0028, journal 0229): a reproducible build of
`layer/moobfs_layer.ko`, with its hash recorded. Two builds of the same commit against the same
pinned Debian kernel headers (6.12.111+deb13-amd64 and 6.12.113+deb13-amd64), in clean,
independent environments, must give byte-identical modules. A second check covers the two
experimental `.deb` packages of `scripts/build-debs.sh`: same version, two builds.

## Prior art inspected

- `Documentation/kbuild/reproducible-builds.rst` of Linux 6.12.112, the tree the KASAN harness
  builds. It names `KBUILD_BUILD_TIMESTAMP` and `KBUILD_BUILD_USER`/`HOST`, which reach
  `/proc/version`, the initramfs and kheaders; a module has none of these. It also says that
  an out-of-tree build records absolute file names in the debug information, to be overridden
  with a prefix map in `KCFLAGS`.
- In the same tree:
  - `Makefile:1031` passes `-fmacro-prefix-map=$(srctree)/=` for the kernel tree only;
  - `scripts/Makefile.extrawarn:94` adds `-Werror=date-time`, so `__DATE__` and `__TIME__`
    cannot appear;
  - `scripts/Makefile.modfinal:28` compiles `moobfs_layer.mod.c` with `c_flags`, without
    reading the module's own Kbuild file;
  - `include/linux/module_signature.h` gives the layout of the appended signature.
- Debian practice:
  - `dpkg-buildflags` 1.22.22 has `reproducible` features `fixfilepath`, `fixdebugpath` and
    `timeless`, all on by default. They were checked in the pinned image:
    `dpkg-buildflags --get CFLAGS` there includes `-ffile-prefix-map=<cwd>=.`;
  - `dpkg-buildpackage` sets `SOURCE_DATE_EPOCH` from the changelog when it is not set.
- The Reproducible Builds project's pages on the build path and on `SOURCE_DATE_EPOCH`, and
  `diffoscope` 297.

Citations: `docs/references.md` §34.

## Decisions

- **Two containers in sequence.** `scripts/repro-ko.sh` (`make repro-test`) starts two fresh
  `debian:trixie-slim` containers, pinned by the digest of `vm/Dockerfile`, one after the other.
  The second starts when the first has ended, 12 minutes or more later on the loaded host. They differ in:

  | | build a | build b |
  | --- | --- | --- |
  | build path | `/build/moobfs` | `/var/tmp/elsewhere/a-much-longer-build-path/moobfs-src` |
  | uid | 0 | 4242 |
  | host name | `builder-a` | `another-host` |
  | `TZ` | `UTC` | `JST-9` |
  | umask | 022 | 002 |

  Each one:
  - installs the pinned headers of every kernel of `scripts/supported-kernels`;
  - builds with `make layer`, which is how `scripts/install.sh` and the VM harnesses build;
  - builds again with the `MAKE[0]` line of `packaging/debian/moobfs-dkms.dkms.in`, in DKMS's
    own build directory (`/var/lib/dkms/moobfs/0.5.0/build`);
  - builds the two packages with `scripts/build-debs.sh`.

  The script fails if:
  - the two containers installed different package versions, since a moved archive is not a
    reproducibility defect;
  - the modules of `a` and `b` differ;
  - the DKMS command gives another module;
  - a copy signed with a throwaway key, once its signature is removed, is not the unsigned
    module;
  - a package differs.
- **The unsigned module is the reproducible artifact.** A signature depends on the key, and
  DKMS signs with the host's own key (ADR-0028, decision 3). So two hosts can never produce the
  same signed file. What a third party can check is the module once its signature is removed. That
  is the hash recorded here.
- **The prefix map goes in `KCFLAGS`, in `make layer` and in the DKMS `MAKE` line.** The flag
  is `-ffile-prefix-map=<source root>/=`, the parent of `layer/`, mapped to the empty prefix as
  Kbuild does for the kernel tree. The module then records `layer/index.c`. A first attempt
  put the flag in `layer/Makefile` (`ccflags-y`). That fixed the module's own objects but not
  `moobfs_layer.mod.o`, because `Makefile.modfinal` never reads that file; the evidence is
  below. `KCFLAGS` reaches both. `-ffile-prefix-map` covers both `-fdebug-prefix-map` and
  `-fmacro-prefix-map`. GCC supports it since 8 and clang since 10.
- **Timestamps.** The script sets `SOURCE_DATE_EPOCH`, `KBUILD_BUILD_TIMESTAMP`, `KBUILD_BUILD_USER`
  and `KBUILD_BUILD_HOST`, following the kernel document. The module uses none of them. The
  final run used another `SOURCE_DATE_EPOCH` than the previous one and gave the same module hashes
  (evidence 4). `scripts/build-debs.sh` now dates `debian/changelog` from `SOURCE_DATE_EPOCH` when
  it is set. Without git, in a container, it used to take `date -R`, the wall clock. dpkg then
  takes the package's file times from that date.
- **No change to the module's C code** (ADR-0022). Nothing there uses `__DATE__` or `__TIME__`,
  and the kernel's flags would refuse it.
- **No CI job in this unit.** A job would add an independent build machine for free. It could not
  be run from here, though, and an untested workflow is not added (next steps).

Rejected alternatives:

- **The flag in `layer/Makefile` only.** It leaves the `.mod.o` path, as shown below.
- **Building only in DKMS's fixed path.** `/var/lib/dkms/moobfs/<version>/build` is the same on
  every host, so DKMS builds would match without a map. But `make layer` from a checkout would
  not, and nobody could compare a DKMS module with a build from the repository.
- **`snapshot.debian.org` to pin the whole toolchain.** It would make a later rebuild possible
  after the archive moves. It was left for later: the recorded toolchain (below) already says
  which versions a rebuild needs.
- **Signing with a project key.** Rejected in ADR-0028.

## Changes

- `Makefile`:
  - `layer` passes `KCFLAGS="$(KCFLAGS) -ffile-prefix-map=$(CURDIR)/="`;
  - new target `repro-test`, listed in `make help`.
- `packaging/debian/moobfs-dkms.dkms.in`: the `MAKE[0]` line passes the same map for DKMS's
  build directory.
- `scripts/repro-ko.sh` (new) and `scripts/build-debs.sh` (the changelog date from
  `SOURCE_DATE_EPOCH`).
- Documentation:
  - security roadmap S10 status; `docs/security-operations.md` §4;
  - `docs/distribution-strategy.md`: a note and a row in section 6;
  - the ROADMAP row of the remaining security items;
  - a `docs/testing.md` row; `docs/status.md` and `docs/status.pt-BR.md`;
  - `docs/references.md` §34, the CHANGELOG, two retired claims, and the journal index.

## Evidence

All runs were on the dedicated host (Debian 13, x86_64, Docker 26.1.5), in `debian:trixie-slim@sha256:a29215f6...f11f`.
Other agents' VMs were running at the same time (load average about 19 on 8 CPUs), so the
durations below are not representative.

1. **Before any change** (exploration, one container, two paths: `/build/a` and
   `/var/tmp/other/b`):
   - for 6.12.113 the hashes were `af808d2c...2dfd` and `855ff794...7e0b`, so the modules differ;
   - `diffoscope` showed the build path in `.rodata.str1.1` (`/build/a/layer/index.c`, the
     `__FILE__` of a `WARN_ON`), in `.debug_line_str`, in `.debug_info`, in the code's
     relocations and offsets, and in the build ID. `strings` found the path 18 times.
2. **With `ccflags-y` in `layer/Makefile`** (first full `scripts/repro-ko.sh` run, containers
   a and b):
   - both modules still differ (`MOOBFS_REPRO_FAIL` for both kernels);
   - `diffoscope` found only the debug information of `moobfs_layer.mod.c`: its `DW_AT_name` and
     its line-table directory (`/build/moobfs/layer` against
     `/var/tmp/elsewhere/.../layer`), plus the build ID that hashes them. `.text` and `.rodata`
     were equal;
   - the two packages were already byte-identical (`MOOBFS_REPRO_DEB_PASS`).
3. **With the map in `KCFLAGS`** (second full run, before the commit, `SOURCE_DATE_EPOCH=1793276632`):
   the run passed for both kernels, with the module hashes recorded below.
   - Within each container, `make layer` and the DKMS command gave the same module from two
     different paths.
   - `strings` finds no build path; the recorded names are `layer/main.c`,
     `layer/moobfs_layer.mod.c` and similar.
   - `modinfo`: version 0.5.0, srcversion `AFB80BF25F63C029AFA681D`, vermagic
     `6.12.113+deb13-amd64 SMP preempt mod_unload modversions`.
   - Each signed copy carried a 405-byte signature; with it removed, the copy equalled the
     unsigned module.
4. **Final run on commit `ea801e5`** (`SOURCE_DATE_EPOCH=1793281613`, `MOOB_COMMIT=ea801e5`;
   `sh scripts/repro-ko.sh`):

   Build a started at 13:47:59Z and build b at 13:56:28Z. The run exited 0 with these markers:

   ```
   MOOBFS_REPRO_PASS: 82a560146edc07be887a48046b85f8e4428184c0fcc48a2f5923af3776e5450e moobfs_layer.ko 6.12.111+deb13-amd64
   MOOBFS_REPRO_SIGNED_PAYLOAD: signed module 1eeff994...507d, 405-byte signature removed, equals the unsigned module
   MOOBFS_REPRO_PASS: a9cac369fc6b195a2e3429ea1f1a3aa9fef04bd7fc6142bbd5b8d809fd5876b8 moobfs_layer.ko 6.12.113+deb13-amd64
   MOOBFS_REPRO_SIGNED_PAYLOAD: signed module fffb3fdb...1653, 405-byte signature removed, equals the unsigned module
   MOOBFS_REPRO_DEB_PASS: bfcefb7da23a447d130ec44a6a165fbecbec439366d51dc1c0bc3e5bcf6011d1 moobfs-dkms_0.5.0_all.deb
   MOOBFS_REPRO_DEB_PASS: ef4f251e4183a3ac964c05f45fba8bc306a8dcc7721c6b668416c73d00df8a7d moobfs-tools_0.5.0_amd64.deb
   ```

   - The module hashes equal those of item 3, made with another `SOURCE_DATE_EPOCH`
     (1793276632).
   - The signed files differ from run to run, as they should: each run makes a new throwaway
     key.

   Toolchain in both containers: `gcc-14` 14.2.0-19, `binutils` 2.44-3,
   `linux-kbuild-6.12.111+deb13` 6.12.111-1, `linux-kbuild-6.12.113+deb13` 6.12.113-1, `dpkg`
   1.22.22, `debhelper` 13.24.2, `dh-dkms` 3.2.2-1~deb13u1.
5. The commands of `make lint` (shellcheck over `vm/` and `scripts/`, `scripts/check-spdx.sh`)
   passed on the committed tree, and `scripts/check-retired-claims.sh` printed
   `RETIRED_CLAIMS_OK`.

**Recorded hashes (SHA-256 of the unsigned `moobfs_layer.ko`, module sources of commit
`ea801e5`):**

| Kernel | `moobfs_layer.ko` |
| --- | --- |
| 6.12.111+deb13-amd64 | `82a560146edc07be887a48046b85f8e4428184c0fcc48a2f5923af3776e5450e` |
| 6.12.113+deb13-amd64 | `a9cac369fc6b195a2e3429ea1f1a3aa9fef04bd7fc6142bbd5b8d809fd5876b8` |

The hash depends only on `layer/`, `include/`, `common/attr.c`, the headers package and the
toolchain. It holds for every later commit that leaves those unchanged; `make repro-test`
prints the current one.

Checking an installed DKMS module against these hashes takes three steps: decompress
`moobfs_layer.ko.xz`, remove the signature (the way the script does it), and compare.

The package hashes also depend on the commit named in the changelog and on its date, so each
commit has its own. Those of `ea801e5` are in item 4.

## Limitations

- **One builder.** Both builds ran on one host, with one Docker and one CPU model. The
  environments were independent containers, but not independent machines or people. No one
  outside the project has rebuilt the module.
- **The toolchain comes from the live Debian archive**, at the versions recorded above. When the
  archive moves, a rebuild needs those versions (for example from `snapshot.debian.org`), or it
  gives a different hash for reasons that are not a defect. The script detects that case and
  reports it apart.
- **amd64 only**, and only the two Debian kernels of the list. The upstream 6.12.112 KASAN
  kernel is a test build and was not covered.
- **The xz compression of DKMS is not checked.** Compression is not part of the identity
  recorded here, and a user compares the decompressed module.
- **Not loaded in a VM in this unit.** The rebuilt module was not loaded in a VM. The change
  alters only the file names recorded in the module, not its code paths. The next batch's full
  `make vm-test` and KASAN run load it.
- **No CI job yet.**

## Next steps

- A CI job running `make repro-test` (or only the module part, `MOOB_REPRO_DEBS=0`). It would
  give a second, independent builder, and a published hash that anyone can compare with.
- Ask an outside rebuilder to run `make repro-test` and compare hashes.
- The rest of S10: real firmware and hardware, and MokManager's own screen.

Actual effort: about 1.5 hours of calendar time, against an estimate of S (hours). Most of it was
waiting for package installation on the shared host.
