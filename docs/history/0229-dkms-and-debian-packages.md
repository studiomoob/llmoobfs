# 0229 — DKMS and Debian packages, tested under Secure Boot (C6, S10)

Started 2026-10-29 09:02 UTC; ended 11:35 UTC (calendar time, with the batch's full runs). Estimated effort: L (ROADMAP item C6);
the actual effort is recorded at the end.

## Goal

Roadmap item C6, the next one in the order set on 2026-10-27 (security, then C6, then G6), started
with the maintainer's go-ahead: a `dkms.conf` that refuses kernels outside the matrix, the
`moobfs-dkms` and `moobfs-tools` packages, and MOK signing for Secure Boot, built and tested in VMs
only. Acceptance: stage 2 acceptance 1, 2 and 4 of distribution strategy §8 in a VM.

## Prior art inspected

On Debian 13 in a container on the dedicated host: `dkms` 3.2.2-1~deb13u1, `dh-dkms`, `debhelper`
13.24.2, `ovmf` 2025.02-8+deb13u1, `shim-signed` 1.51~1+deb13u1 and `python3-virt-firmware`
24.11-2. A throwaway `dkms.conf` with `BUILD_EXCLUSIVE_KERNEL` showed that `dkms build -k
6.12.999+deb13-amd64` exits 77 before it looks for headers. `prepare_mok()` signs with
`/var/lib/dkms/mok.key`, created once per host. `virt-fw-vars --add-mok` writes a certificate into
the `MokList` of an edk2 variable store. Citations: `docs/references.md` §27.

## Decisions

[ADR-0028](../decisions/0028-dkms-and-debian-packages.md), status "implemented; the design awaits
the maintainer's review":

- one native source `moobfs`, built with debhelper and `dh_dkms` from `packaging/debian/` copied
  into a temporary tree by `scripts/build-debs.sh`;
- `BUILD_EXCLUSIVE_KERNEL` generated from `scripts/supported-kernels`, the list `make install`
  reads;
- signing by the host's own DKMS key, with no project key;
- `moobfs-dkms` refuses to install next to a `make install` copy;
- upgrades do not unload the module.

Rejected alternatives: hand-written maintainer scripts, a root `debian/` directory, a project
signing key, and `BUILD_EXCLUSIVE_KERNEL_MIN`/`_MAX`.

## Changes

- `packaging/debian/`: `control`, `rules`, `copyright`, `source/format`,
  `moobfs-dkms.dkms.in` and `moobfs-dkms.preinst`. No `-dbgsym` package.
- `scripts/build-debs.sh` and `make deb`: version from `MODULE_VERSION` by default; existing
  packages in the output directory are not replaced without `FORCE=1`.
- `vm/install-test/in-container.sh`, `MOOB_TEST=package`:
  - builds two versions of each package, `0.5.0~c6test1` and `0.5.0~c6test2`;
  - boots the Debian 13 cloud image under `OVMF_CODE_4M.secboot.fd` with a fresh copy of
    `OVMF_VARS_4M.ms.fd`;
  - between the two phases, powers the guest off and enrols the guest's own `mok.pub` in
    `MokList` with `virt-fw-vars`, owner `SHIM_LOCK_GUID`.
- `vm/package-test/guest.sh`: the two phases, following `docs/operations.md`.
- `make package-test`.
- `vm/install-test/run.sh` passes `/dev/kvm` to the container when the host has it. The
  install and container tests now also run under KVM there.
- `vm/install-test/Dockerfile` adds `build-essential`, `debhelper`, `dh-dkms`, `ovmf`,
  `python3-virt-firmware` and `openssl`.
- Documentation:
  - `docs/operations.md`: section 2 "Or: the experimental packages", the Secure Boot
    enrolment, and two rows in section 8;
  - ADR-0028 and `docs/references.md` §27;
  - `docs/distribution-strategy.md`: a dated note, and the DKMS, package and signing status
    cells now say EXISTS in a VM;
  - ROADMAP C6 row; security roadmap S10 status;
  - `docs/testing.md` row; README in both languages; status in both languages;
  - `docs/faq.md` §9 and the regenerated `site/faq.html`; `docs/when-not-to-use.md`;
  - `site/guide.html` §5, which still said only 6.12.111 was accepted, a fact stale since
    journal 0206;
  - a site-journey milestone, four retired claims, and the CHANGELOG.
- `.github/workflows/release.yml`: its comment said Scorecard accepts the tag reference to the SLSA
  generator. Scorecard 5.6.0 of 2026-10-29 counts it as the one unpinned dependency
  (Pinned-Dependencies 9 of 10), so the comment now says that. The reference stays a tag, because
  the generator requires one.

## Evidence

All runs were on the dedicated host (Debian 13, KVM). Logs are kept outside the repository.

1. **Package build.** `sh scripts/build-debs.sh 0.5.0~c6test1 /tmp/out` in a `debian:trixie-slim`
   container built both packages.
   - `moobfs-tools` depends on `libc6 (>= 2.38)`.
   - The generated `dkms.conf` reads
     `BUILD_EXCLUSIVE_KERNEL="^(6\.12\.111\+deb13-amd64|6\.12\.113\+deb13-amd64)$"`.
2. **First `MOOB_TEST=package sh vm/install-test/run.sh`: `MOOBFS_PACKAGE_FAIL: modinfo finds
   another file`.**
   - The guest booted with Secure Boot enabled and lockdown `[integrity]`.
   - DKMS installs the module as `moobfs_layer.ko.xz`, compressed like the kernel's own modules,
     but the test expected `.ko`.
   - Fixed in the test: it accepts the suffix and compares against the DKMS build of the same
     name.
3. **Second run: `MOOBFS_PACKAGE_PASS`, 469 s under KVM.** Markers, in order:
   - `SECURE_BOOT: SecureBoot enabled; lockdown none [integrity] confidentiality`;
   - `INSTALLED`: `.../updates/dkms/moobfs_layer.ko.xz`, signer "DKMS module signing key";
   - `UNLISTED_KERNEL_REFUSED: dkms exit 77 for 6.12.999+deb13-amd64`;
   - `NOT_ENROLLED_REFUSED: modprobe: ERROR: could not insert 'moobfs_layer': Key was rejected by
     service`;
   - `MOK_ENROLLED`: SHA-256 fingerprint `C2:69:A5:EF:...:DA:8C`, the guest's own key;
   - after the reboot, `ENROLLED: /var/lib/dkms/mok.pub is already enrolled`;
   - `UNSIGNED_REFUSED: insmod: ERROR: could not insert module unsigned/layer/moobfs_layer.ko: Key
     was rejected by service`;
   - `SIGNED_LOADED`;
   - `VERIFIED`, three times (installed, upgraded, rolled back): each time the 20 decisions in
     alice's domain equal the oracle, bob's query on that domain exits 77, bob cannot read alice's
     note, and the `moobfs-check` report is byte-identical to the first one;
   - `PURGED`: module and tools removed, content and `user.moob.kind` readable on the lower;
   - `MAKE_INSTALL_COPY_REFUSED`.
4. **Existing tests after the harness change, now under KVM in the container.**
   - `MOOB_TEST=install`: `MOOBFS_INSTALL_PASS`, 396 s.
   - `MOOB_TEST=container`: `MOOBFS_CONTAINER_PASS`, 707 s.
5. **Batch runs, once, on the batch tree with journals 0229 to 0231.**
   - `make vm-test`, under QEMU TCG, the CI mode: `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS` and
     `MOOBFS_ACCEPTANCE_PASS` in about 42 min. Among the steps: `LANDLOCK_PASS` (journal 0230),
     `FAIRSHARE_PASS` and `MINACL_PASS`. Under KVM it was not run this time, because of the
     `rootpath` timing of issue 168.
   - `sh vm/kasan-local.sh` on upstream 6.12.112: `MOOBFS_KASAN_PASS` with no kernel diagnostics,
     and `MOOBFS_PYLIB_PASS` (kernel = scan through the mount = scan of the lower, journal 0231).
     That kernel has no Landlock (`vm/kasan.config`), so the step printed `LANDLOCK_SKIPPED`.
   - The first KASAN attempt never started. Run in the same tree as `make vm-test`, whose
     container had left `build/` owned by root, it could not create `build/vng`; it was run again
     from a separate copy.
   - Also: `make lint`; `make attr-tools` with clang, ASan and UBSan; `RETIRED_CLAIMS_OK`; and
     `scripts/site-faq.py --check`.

## Limitations

- **Coverage:** one distribution (Debian 13), one architecture (amd64), one kernel (the image's
  6.12.113), and the ext4 lower of the cloud image.
- **MokManager:** its confirmation screen is replaced by writing `MokList` while the guest is off.
  The enrolment path of shim and the kernel's use of the key are exercised; the console dialog is
  not.
- **Not exercised:** real firmware and hardware, a kernel update with DKMS rebuilding the module,
  and a reproducible build of the `.ko` (S10's second item).
- **Packages:** they are not signed as files, not in an apt repository, and not published, by
  design (ADR-0022 decision 4; distribution strategy §8). No third party has reproduced the test.

## Next steps

The batch pull request, then G6. The
maintainer reviews ADR-0028's design choices.

Actual effort: about 2.5 hours of calendar time for C6 alone, against an estimate of L (a week
or more). Most of the estimate assumed building the Secure Boot test without prior art;
`virt-fw-vars --add-mok` and DKMS's own signing removed that work.
