# ADR-0041 — The `.deb` packages in a signed apt repository on the project's site, signed offline by the maintainer

Date: 2026-11-17. Status: **accepted** by the maintainer on 2026-11-17 (journal 0301), who had
chosen the signed apt repository on GitHub Pages and an offline key held by the maintainer the
same day. Publishes the packages of
[ADR-0028](0028-dkms-and-debian-packages.md), which left publication to stage 2; stage 2 was
declared by [ADR-0039](0039-stage-2-declared-reproduction-pending.md). User space and packaging
only under [ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md): nothing under
`layer/`, the UAPI or the format changes. Estimated effort: M (one to three days). Estimate,
not labor time.

## Context

- ADR-0028 builds `moobfs-dkms` and `moobfs-tools` with `scripts/build-debs.sh` and tests them in
  a VM under Secure Boot (`make package-test`); they are not published. Journal 0236 made the build
  reproducible (`SOURCE_DATE_EPOCH`).
- The source releases are signed with Sigstore and carry SLSA provenance, but apt does not verify
  either: it verifies an OpenPGP signature on the repository's index (`InRelease`), and the index
  gives the SHA-256 of every package.
- apt 3.0 (Debian 13) verifies with Sequoia (`sqv`). Its default policy
  (`/usr/share/apt/default-sequoia.config`) rejects DSA and SHA-1 and accepts RSA 2048 only until
  2030-02-01. Since apt 2.9.24, `sources.list` entries without `Signed-By` are deprecated, and so
  is `/etc/apt/trusted.gpg.d`; a source in deb822 format names its key with `Signed-By`, so the
  key is trusted for this repository only (apt `NEWS.Debian`, read on the test host).
- The repository has no Actions secret, and `docs/security-operations.md` §1 states it. The site is
  deployed from `main` by `.github/workflows/pages.yml`.
- Releases are immutable: `v0.6.1-experimental` cannot take `.deb` assets now.

## Decision

1. **Where.** A Debian repository with suite `experimental`, component `main`, architectures
   `amd64` and `all`, under `https://studiomoob.github.io/llmoobfs/apt/`, in the standard layout:
   `dists/experimental/InRelease`, `Release`, `main/binary-amd64/Packages` (and `.gz`),
   `pool/main/m/moobfs/`. The index is made by `apt-ftparchive` (apt-utils, maintained with apt),
   with no other repository tool.
2. **The key.** One OpenPGP key, Ed25519, made by the maintainer on the maintainer's own machine,
   with a two-year expiry. It never reaches CI, the test hosts or the repository; it has a backup
   the maintainer keeps. Its public part is `site/apt/moobfs-archive-keyring.pgp`, and its
   fingerprint is in `SECURITY.md`, `docs/security-operations.md`, the guide and the README, so it
   can be checked against more than one place. A user installs it in `/usr/share/keyrings/` and
   names it in a deb822 source with `Signed-By`.
3. **Who signs what.** The maintainer signs only the `Release` file, as `InRelease`
   (`gpg --clearsign`), on the maintainer's machine, after checking that its hashes match
   two independent builds (item 4). The signed `InRelease`, `Release` and `Packages` files are
   text and are committed under `site/apt/dists/` through a pull request.
4. **Where the packages come from.** The packages of a version are built from its tag, with
   `SOURCE_DATE_EPOCH` set to the tag's commit time, in the pinned Debian 13 image, twice: on the
   test host and in a CI job. The two must be identical, byte for byte, before the index is
   signed. The `.deb` files are not committed: `pages.yml` builds them again from the tag at each
   deploy and fails unless their SHA-256 values are those of the signed `Packages`. So what the
   site serves is bound to the signed index, and the index to the tag.
5. **Tests.** A new VM step (`make apt-test`) serves the repository over HTTP to a clean Debian 13
   guest, which installs both packages with `apt` from the deb822 source and its `Signed-By` key.
   Negative rounds: a changed `.deb`, an `InRelease` signed by another key, and a source without
   `Signed-By` against an empty keyring; apt must refuse each one. `make package-test` (ADR-0028)
   keeps testing the packages themselves, under Secure Boot.
6. **What is published first.** The packages of `v0.6.1-experimental`, version `0.6.1`. A later
   tag adds its version to the same suite; old versions stay in the pool, so a roll back with
   `apt install moobfs-dkms=<version>` keeps working.

## Alternatives

1. **Key as an Actions secret, CI signs.** No manual step, but it would be the repository's first
   secret, and a compromised workflow could sign a package every installed host would accept. Not
   taken (maintainer, 2026-11-17).
2. **Packages attached to releases only** (`apt install ./file.deb`). Simpler, with Sigstore and
   SLSA on each file, but no updates through apt, and a user has to verify by hand. Not taken
   (maintainer, 2026-11-17).
3. **`.deb` files committed under `site/`.** Simpler deploy, but binaries in the Git history that
   no reader can check against the tag. Rebuilding at deploy keeps them out.
4. **`Valid-Until` in `Release`.** It limits how long an old index can be replayed (a freeze
   attack), but with an offline key it makes the repository fail for every user whenever the
   maintainer has not re-signed in time. Not taken for the experimental suite; stated as a limit.
5. **reprepro or aptly.** They manage many suites and incoming queues; one suite with a few
   packages does not need them, and `apt-ftparchive` ships with apt.

## Consequences

- `apt update` gives the user new versions, verified against a key they installed once.
- Each publication needs the maintainer: a local signature and a pull request. Nothing is
  published without it.
- A lost key means a new key, a new fingerprint on every surface, and every user installing the
  new key by hand; a stolen key means the same plus an advisory. `docs/security-operations.md`
  gets the steps.
- An old signed index can be replayed by someone who controls the network path to the site
  (no `Valid-Until`); HTTPS to GitHub Pages narrows it.
- Tested on Debian 13 amd64 only; Ubuntu 26.04 has the kernel in the matrix but is not tested as
  an apt client until a VM step covers it.
- The access table does not change: still no Actions secret.

## Implementation notes, 2026-11-17 (journal 0301)

- Item 1: the `Release` file names one architecture, `amd64`; the architecture-independent
  `moobfs-dkms` is listed under `binary-amd64`, as `apt-ftparchive` does by default, so there is
  no separate `binary-all` index.
- Item 4: "the pinned Debian 13 image" alone does not fix the toolchain, since the image installs
  its build packages from the live archive, which moves with each point release. The build
  (`scripts/apt-debs.sh`) points apt at `snapshot.debian.org` at a fixed time, and
  `site/apt/builds` records that time per version, so a deploy months later rebuilds the same
  bytes. The CI build is the `apt packages build` workflow, started by hand.
- Item 6: the packages of `v0.6.1-experimental` say "Not published" in their description and
  changelog, text fixed in the tag. The packaging now says neither; whether the first published
  version is `0.6.1` with that text or the next tag is the maintainer's choice (journal 0301).
