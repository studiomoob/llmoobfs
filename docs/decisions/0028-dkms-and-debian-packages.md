# ADR-0028 — DKMS and Debian packages, signed with the host's own MOK, built and tested in VMs only

Date: 2026-10-29. Status: **implemented; the design awaits the maintainer's review** (item C6 of
`ROADMAP.md`, started with the maintainer's go-ahead on 2026-10-29, journal 0229). Estimated
effort: L (a week or more). Implements the DKMS, package and signing rows of
`docs/distribution-strategy.md` §2.6 and §6, which were FUTURE. User space and packaging only
under ADR-0022: no change to `layer/`, the UAPI or the format. Nothing is published: publishing
stays behind stage 2 and the "Public release" row of `ROADMAP.md`.

## Context

`make install` (item C2, `scripts/install.sh`) builds the module against the running kernel's
headers and copies it; a kernel update leaves the host without a module, and a host with Secure
Boot refuses the unsigned module (`docs/operations.md` §8). Distribution strategy §6 proposed a
DKMS tree, two packages and MOK signing; stage 2 acceptance 1, 2 and 4 of its §8 ask for an install
on a clean Debian, an upgrade and a roll back without loss, a refusal outside the kernel matrix,
and a signed module that loads under lockdown while an unsigned one is refused.

Prior art inspected in Debian 13 (`dkms` 3.2.2-1~deb13u1, `dh-dkms`, `debhelper` 13.24.2):

- `dkms` reads `BUILD_EXCLUSIVE_KERNEL`, a regular expression over `uname -r`, before it looks for
  headers, and exits 77 with a warning when the kernel does not match (`/usr/sbin/dkms`, line
  815, in `read_conf()`); `/usr/lib/dkms/common.postinst` (line 248) treats 77 as "not built",
  so the package still installs on a host whose kernel is outside the list.
- `dkms` signs every module it builds when the kernel has `CONFIG_MODULE_SIG`, with
  `/var/lib/dkms/mok.key` and `mok.pub`, and generates that pair once if it is missing
  (`prepare_mok`, line 1185). On Ubuntu the same function uses `update-secureboot-policy`'s key;
  Debian has no such tool. The user enrols the certificate with `mokutil --import` and confirms it
  in MokManager at the next boot; from then on shim exposes it to the kernel's machine keyring.
- `dh_dkms` installs `debian/<package>.dkms` as `/usr/src/<name>-<version>/dkms.conf`, replaces
  `#MODULE_VERSION#` with the package version (`-V`), and adds the `postinst` and `prerm` that call
  `/usr/lib/dkms/common.postinst` and `dkms remove`.

## Decision

1. **Two packages from one native source, `moobfs`.** `moobfs-dkms` (all) carries `layer/*.c`,
   `layer/*.h`, `layer/Makefile`, `include/*.h` and `common/attr.c` under
   `/usr/src/moobfs-<version>/`, in the repository's layout, because `layer/attr.c` includes
   `../common/attr.c`. `moobfs-tools` (amd64) carries `moobfs`, `moobfs-attr`, `moobfs-check` and
   `moobfs-index` in `/usr/bin` with their manual pages. `mkfs.moobfs` and `fsck.moobfs` (M1,
   frozen) stay out, as §6 proposed. The packaging lives in `packaging/debian/`;
   `scripts/build-debs.sh` copies the sources to a temporary directory, writes
   `debian/changelog` with the version and runs `dpkg-buildpackage -b`, so the tree gets no build
   output and no `debian/` directory at its root. No `-dbgsym` package.
2. **The kernel matrix is `scripts/supported-kernels`, the same list `make install` reads.**
   `debian/rules` writes it into `BUILD_EXCLUSIVE_KERNEL` as an anchored alternation of exact
   releases, so the two installation paths cannot disagree. DKMS refuses any other release with
   exit 77.
3. **Signing uses the host's own key, never a project key.** DKMS's `/var/lib/dkms/mok.key` is
   generated on the host and never leaves it; the project holds no signing key, so it has no key
   to protect, rotate or leak, and stores no credential (AGENTS.md). A project key for prebuilt
   modules (§6, "release builds") is not needed while nothing prebuilt is published, and is left
   to the "Public release" row.
4. **The package refuses to sit next to a `make install` copy.** A module in
   `/lib/modules/*/updates/moobfs/` and the DKMS one in `updates/dkms/` would both be found and
   depmod would pick one silently; `moobfs-dkms`'s `preinst` fails with a message until
   `make uninstall` has run. The tools of `make install` live in `/usr/local/bin` and those of the
   package in `/usr/bin`, so they do not collide, but the guide says to use one path.
5. **An upgrade replaces the files, not the loaded module.** As with `make install`, the guide
   unmounts and runs `rmmod` before upgrading or rolling back, then `modprobe`; the packages do not
   unload anything themselves, because unloading would break a mounted layer.
6. **Tested only in a disposable VM, under Secure Boot.** `make package-test` builds two versions
   of both packages, boots the Debian 13 cloud image under OVMF with Secure Boot (the firmware's
   Microsoft keys accept the image's signed shim), and follows the guide. MokManager's
   confirmation at the console is the one step it replaces: while the guest is powered off the
   host writes the guest's own `mok.pub` into the firmware's `MokList` with `virt-fw-vars
   --add-mok`, with the owner GUID shim gives enrolled keys.

## Alternatives rejected

- **Hand-written maintainer scripts with `dpkg-deb`.** Fewer build dependencies, but it would
  re-implement `common.postinst`'s handling of exit 77, of kernels without headers and of removal,
  which Debian maintains.
- **A `debian/` directory at the repository root.** The Debian convention for native packages, but
  it would sit next to a build that does not use it and suggests the project is packaged for the
  archive; `packaging/debian/` is copied into place at build time.
- **A project signing key for the DKMS build.** DKMS builds on the user's host; a project key
  would have to be on that host to sign, which defeats it.
- **`BUILD_EXCLUSIVE_KERNEL_MIN`/`_MAX` over 6.12.** It would accept every 6.12.x, and the rule of
  `docs/compatibility.md` supports a release only after the suite and KASAN pass on it.

## Consequences

- A host on a listed kernel installs the module with `apt install ./moobfs-dkms_*.deb
  ./moobfs-tools_*.deb`, and under Secure Boot enrols one key once.
- A kernel update to an unlisted release leaves DKMS saying it was not built, as `make install`
  refuses today; the matrix grows only by `docs/compatibility.md` rule 3.
- Stage 2 acceptance 1, 2 and 4 of distribution strategy §8 have a VM test; acceptance 3 (backup)
  and 5 (containers) are covered by C2/C3, a third party has not reproduced any of it, and stage 2
  also needs S2 results. The packages are not published, signed as files, or in a repository.
- Limitations: one distribution (Debian 13), one architecture (amd64), one kernel (the image's
  6.12.113); the MokManager screen itself is not exercised; real hardware and firmware are not
  tested.
