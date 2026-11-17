# 0301 — The `.deb` packages in a signed apt repository (ADR-0041) (in progress)

Date: 2026-11-17. Work started at 11:25 Brasília time. Status: **in progress; ADR-0041 is
proposed and waits for acceptance; nothing is built or published yet.** Estimated effort M
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

The rest (layout, the two builds that must agree, rebuilding at deploy, tests, no `Valid-Until`)
is proposed in ADR-0041.

## Prior art read

On the test host (Debian 13, apt 3.0.3, apt-utils 3.0.3, dpkg-dev 1.22.22, sqv 1.3.0):
`/usr/share/apt/default-sequoia.config` (DSA and SHA-1 rejected, RSA 2048 until 2030-02-01) and
`/usr/share/doc/apt/NEWS.Debian.gz` (2.9.24: `Signed-By` expected, deb822 sources,
`trusted.gpg.d` deprecated; 2.9.19: Sequoia verification). Cited in `docs/references.md` §70.

## Next step

The maintainer accepts or changes ADR-0041. Then: `scripts/apt-index.sh` and the CI build, the
`make apt-test` VM step with its negative rounds, the change to `pages.yml`, the key made by the
maintainer, and the first signed index for `0.6.1`.
