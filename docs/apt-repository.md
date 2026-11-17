# The apt repository

Date: 2026-11-17 ([ADR-0041](decisions/0041-signed-apt-repository-on-pages.md), journal
[0301](history/0301-signed-apt-repository.md)). Status: **published since 2026-11-17 (journal
[0302](history/0302-apt-repository-published.md)), with `0.6.2` the first version; key fingerprint
`35E6 715F 0D27 D8FA CD84  56C2 9491 8AEE 2850 F202`.** This page is the procedure; the user's side is in
`docs/operations.md`.

## What it is

A Debian repository at `https://studiomoob.github.io/llmoobfs/apt/`, suite `experimental`,
component `main`, architecture `amd64` (the architecture-independent `moobfs-dkms` is listed
under it). Its index, `dists/experimental/InRelease`, is signed by one OpenPGP key that the
maintainer holds offline. The repository commits only text: the key, the index and
`site/apt/builds`. The `.deb` files are built again from their tags at every deploy of the site.

| File | Made by | Where |
| --- | --- | --- |
| `site/apt/moobfs-archive-keyring.pgp` | the maintainer, once (below) | committed |
| `site/apt/builds` | the publisher: `<version> <tag> <snapshot>` per version | committed |
| `dists/experimental/main/binary-amd64/Packages`, `Release` | `scripts/apt-index.sh` | committed |
| `dists/experimental/InRelease` | the maintainer, `scripts/apt-sign.sh` | committed |
| `pool/main/m/moobfs/*.deb` | `scripts/apt-site.sh` at deploy, from the tags | never committed |

## The key (once, on the maintainer's machine)

```sh
gpg --quick-gen-key 'MoobFS apt repository' ed25519 sign 2y
gpg --list-keys --with-colons 'MoobFS apt repository' | awk -F: '/^fpr/ { print $10; exit }'
gpg --export <FINGERPRINT> >site/apt/moobfs-archive-keyring.pgp
gpg --export-secret-keys --armor <FINGERPRINT> >moobfs-apt-key-backup.asc   # offline media, not this disk
```

The passphrase is the maintainer's; neither it nor the secret key goes to CI, a test host, this
repository or an assistant's session. The fingerprint goes into `SECURITY.md`,
`docs/security-operations.md` §1, `docs/operations.md` and the README.

## Publishing a version

1. **Two builds.** On the test host, `sh scripts/apt-debs.sh <tag> <snapshot> <dir>`; in CI, the
   `apt packages build` workflow with the same tag and snapshot (a recent
   `snapshot.debian.org` time, for example the day of the tag at `T000000Z`). Their
   `MOOBFS_APT_DEB` lines must be equal. Put them in one file, `expected`.
2. **The index.** On a Linux host with apt-utils, with the packages of every version that stays
   published (each built as in step 1): `SOURCE_DATE_EPOCH=<tag time> sh scripts/apt-index.sh
   <debs> <out>`. Copy `<out>/dists/` over `site/apt/dists/` and add the line to
   `site/apt/builds`.
3. **The signature**, on the maintainer's machine: `sh scripts/apt-sign.sh
   site/apt/dists/experimental <FINGERPRINT> expected`. It refuses unless `Release` hashes
   `Packages` and every package in `Packages` is in `expected`.
4. **The pull request.** The required checks run; after the merge, `pages.yml` runs
   `scripts/apt-site.sh`, which verifies `InRelease` with the committed key, builds every version
   again from its tag and deploys only if each package equals its signed hash.
5. **Check from outside**: `make apt-live-test` follows the user's steps of `docs/operations.md` in
   a disposable Debian 13 VM against the published repository (journal 0303).

## Rotation and loss

- **Expiry (two years).** Extend it with `gpg --quick-set-expire`, export the public key again,
  and commit it; users fetch the key again (the guide says so).
- **Lost key.** Make a new one, replace the public key and the fingerprint on every surface,
  sign the index again, and say in the changelog and the status that users must install the new
  key by hand.
- **Stolen key.** The same, plus a security advisory naming the old fingerprint, and the old key
  revoked (`gpg --gen-revoke`) with the revocation committed next to the new key.

## Limits

- No `Valid-Until`: an old signed index can be served again by someone who controls the network
  path; HTTPS to GitHub Pages narrows it (ADR-0041).
- Tested as an apt client on Debian 13 amd64 only (`make apt-test`).
- A deploy builds every published version, so it gets slower as versions are added; old versions
  can be dropped from `site/apt/builds` and the index when they are no longer needed.
