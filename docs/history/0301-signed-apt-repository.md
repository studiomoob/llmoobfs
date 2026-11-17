# 0301 — The `.deb` packages in a signed apt repository (ADR-0041) (in progress)

Date: 2026-11-17. Work started at 11:25 Brasília time. Status: **in progress; ADR-0041 is
accepted; the scripts, the VM step and the site's check are written and tested with throwaway
keys; the project's key does not exist yet and nothing is published.** Estimated effort M
(ADR-0041); the time recorded is calendar time.

## Goal

Publish the `moobfs-dkms` and `moobfs-tools` packages of ADR-0028, the step stage 2 left
separate (journal 0297), so that a test host installs and updates them with `apt`.

## Decisions (maintainer, 2026-11-17)

- About 11:20: a signed apt repository on the project's GitHub Pages site. The alternative,
  `.deb` files attached to releases, was not taken: no updates through apt, and the release of
  `v0.6.1-experimental` is immutable and cannot take them.
- About 11:25: the repository's signing key is an offline key held by the maintainer, not an
  Actions secret. The repository keeps no secret, and a compromised workflow cannot sign a package.
- About 12:00: ADR-0041 accepted as proposed (layout, the two builds that must agree, rebuilding
  at deploy, tests, no `Valid-Until`).

## Prior art read

On the test host (Debian 13, apt 3.0.3, apt-utils 3.0.3, dpkg-dev 1.22.22, sqv 1.3.0):
`/usr/share/apt/default-sequoia.config` (DSA and SHA-1 rejected, RSA 2048 until 2030-02-01) and
`/usr/share/doc/apt/NEWS.Debian.gz` (2.9.24: `Signed-By` expected, deb822 sources,
`trusted.gpg.d` deprecated; 2.9.19: Sequoia verification). Cited in `docs/references.md` §70.

## Changes

- `scripts/apt-debs.sh TAG SNAPSHOT OUT`: the two packages of a tag, built in the pinned
  `debian:trixie-slim` image with apt pointed at `snapshot.debian.org` at a fixed time, from
  `git archive` of the tag, with `SOURCE_DATE_EPOCH` the tag's commit time and `MOOB_COMMIT` its
  12-digit abbreviation. The image alone does not pin the toolchain: it installs `gcc` and
  `debhelper` from the live archive, which moves with each point release, and a deploy builds old
  tags again (ADR-0041, implementation notes). Plain HTTP to the snapshot: the slim image has no
  CA certificates, and apt checks the archive's signature either way.
- `scripts/apt-index.sh DEBS OUT`: `apt-ftparchive` makes `Packages` and `Release` (SHA-256 and
  SHA-512 only). `apt-ftparchive` 3.0.3 dates `Release` with the current time even when
  `SOURCE_DATE_EPOCH` is set; the script replaces the `Date` line, which `Release` does not hash.
- `scripts/apt-sign.sh DIST FINGERPRINT EXPECTED`: the maintainer's step. It refuses unless
  `Release` gives the SHA-256 of `Packages` and every package in `Packages` is one of the expected
  builds, then signs `InRelease` and checks the signature (`VALIDSIG`) before keeping it.
- `scripts/apt-site.sh`, run by `pages.yml` before a deploy (with `fetch-depth: 0` for the tags):
  verifies `InRelease` with the committed key (`sqv --cleartext`, which apt 3 uses, or `gpgv`),
  checks that it signs the committed `Release` and that `Release` hashes `Packages`, builds each
  version of `site/apt/builds` again, and copies the packages into `site/apt/pool/` only if
  `Packages` and the builds agree both ways. A committed `pool/` fails it. Without
  `site/apt/builds` it does nothing, so the site deploys as before until the first publication.
- `.github/workflows/apt-debs.yml`: the CI build of a tag, started by hand, uploading the packages
  and writing their hashes into the run's summary; `contents: read` only.
- `make apt-test` (`MOOB_TEST=apt` in `vm/install-test/`, guest `vm/apt-test/guest.sh`); the
  test image gets `gpg`, `gpg-agent` and `apt-utils`.
- `packaging/debian/control` and the changelog line of `scripts/build-debs.sh` no longer say
  "not published". The packages of `v0.6.1-experimental` keep that text, fixed in the tag.
- `docs/apt-repository.md`: the procedure (the key, publishing a version, rotation and loss,
  limits). `docs/testing.md`: the new step. `docs/references.md` §70.

## Evidence

On the dedicated host (Debian 13, KVM), 2026-11-17:

- Two builds of `v0.6.1-experimental` with `scripts/apt-debs.sh` at snapshot `20261117T000000Z`,
  one with `TZ=JST-9` from another directory, about 2 min 40 s each: the same toolchain
  (`gcc-14` 14.2.0-19, `debhelper` 13.24.2, `dh-dkms` 3.2.2-1~deb13u1, `dpkg-dev` 1.22.22) and
  byte-identical packages: `moobfs-dkms_0.6.1_all.deb` `82a719f1...`,
  `moobfs-tools_0.6.1_amd64.deb` `f3a5aabc...`.
- `scripts/apt-index.sh` twice on the two builds, two seconds apart, with the same
  `SOURCE_DATE_EPOCH`: identical trees (after the `Date` fix; before it, only `Date` differed).
- `scripts/apt-sign.sh` in a container with a throwaway Ed25519 key: signed; refused an
  expected-hash file that lacked a package, and a `Packages` changed after `Release` was made.
- `make apt-test` at `8d2f2d2`, 11:45–11:53 under KVM: `MOOBFS_APT_PASS`. Installed `V1` from a
  deb822 source with `Signed-By` and DKMS built the module for 6.12.113; upgraded to `V2` and rolled
  back to `V1`; refused an index signed by another key (`Missing key 4050...`), a package with one
  byte changed after signing (hash mismatch, `V1` still installed), and the source without
  `Signed-By`; purged.
- `scripts/apt-site.sh` end to end on `v0.6.1-experimental` with a throwaway key:
  `MOOBFS_APT_SITE_PASS: 2 packages built from their tags, equal to the signed index`. Refused:
  an index signed by another key (`Missing key`); a `Release` changed after signing ("InRelease
  signs another Release"); a committed `pool/`; and a signed index of a `moobfs-tools` with one
  byte changed ("built from its tag is not the signed one"). The first run failed every case for
  the wrong reason, because `sqv` needs `--cleartext` for an `InRelease`; and `sqv` drops the
  final newline of the signed text, so the comparison with `Release` ignores trailing newlines.
  A build at snapshot `20260301T000000Z` failed to build at all; it is not counted as a test of a
  differing build.

## The first version to publish

The packages of `v0.6.1-experimental` describe themselves as "Not published" (`moobfs-dkms`'s
description and both changelogs), text fixed in the tag. Publishing them would ship a stated fact
that is false. The choice is the maintainer's: publish `0.6.1` with that text, or publish from the
next tag, whose packaging no longer says it.

## Next step

The full runs of this batch on its last commit (`make apt-test`, `make package-test`,
`make repro-test`, the full `make vm-test` and KASAN suite, the pre-push checks), the pull
request, then the key and the first signed index once the maintainer chooses the first version.
