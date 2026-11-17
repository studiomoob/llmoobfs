# 0302 — v0.6.2-experimental tagged; its packages published in the signed apt repository (ADR-0041) (in progress)

Date: 2026-11-17. Work started at 17:34 Brasília time, with the merge of #210. Status: **in
progress; the tag and its release exist and are verified; the signed index, the key and the
surfaces are ready in this batch; the repository is live once it is merged and the site deploys.**
Estimated effort: part of ADR-0041's M; the time recorded is calendar time.

## Goal

Tag `v0.6.2-experimental`, the first version for the apt repository (journal 0301), and publish
its packages there with the maintainer's offline key.

## Merge, tag and release

With the maintainer's agreement (2026-11-17, about 17:20, "merge and tag after CI"):

- Pull request #210 (ADR-0041 and the `v0.6.2-experimental` preparation, journals 0300 and 0301):
  all 25 checks passed on `a9f0154`; merged at 17:34 as `9a7384e`.
- The annotated tag `v0.6.2-experimental` on `9a7384e`, pushed at 17:35, after checking that the
  merge commit has `MODULE_VERSION("0.6.2")` and `CITATION.cff` 0.6.2, and that no such tag existed.
- Release workflow run `44170861937`: every job succeeded (`upload-assets` skipped, as
  configured); the release is a published pre-release, immutable, published at 17:36 (20:36
  UTC), with the source archive, its `.sha256`, `.sigstore.json` and `.intoto.jsonl`.
- Verified on the test host with the commands of `docs/security-operations.md` §4:
  `sha256sum -c` `OK` (`56f3d1d43934f271...`), `sigstore verify github` `OK`, `slsa-verifier`
  `PASSED`; the archive's `layer/main.c` has `MODULE_VERSION("0.6.2")`.
- The site's deploy for the merge (Pages run `44170814398`) passed; `scripts/apt-site.sh` printed
  "no site/apt/builds, nothing published", as intended before the first publication.

## The two builds

At snapshot `20261117T000000Z` (`site/apt/builds`):

- on the test host, `sh scripts/apt-debs.sh v0.6.2-experimental 20261117T000000Z`, from a fresh
  clone of `main` at `9a7384e`;
- in CI, the `apt packages build` workflow, run `44171219685`, same tag and snapshot.

Both gave `moobfs-dkms_0.6.2_all.deb` `7ff11fed1bdf1322a850a7c99016c38d41daf0b125e7c9059fbe6adf32b6e537`
and `moobfs-tools_0.6.2_amd64.deb` `3f7d6ec53022cbe93abc7df219c6a962ff5f93e0b378b977b3c2e130493e827f`.
They differ from the `0.6.2` packages of `make repro-test` in journal 0301, which take the date and
commit of the branch's head, not of the tag.

## The index, the key and the signature

- `scripts/apt-index.sh` on the test host, `SOURCE_DATE_EPOCH` the tag's commit time
  (`1794947695`): `Release` dated `Tue, 17 Nov 2026 20:34:55 +0000`, giving `Packages`
  `94dd8240...` (1978 bytes).
- The maintainer made the key on the maintainer's own machine, at about 17:47, with the commands of
  `docs/apt-repository.md`: Ed25519, signing only, user ID "MoobFS apt repository" (no e-mail
  address; the page's command first had one, removed in this batch), expiring 2028-11-16, with an
  offline backup. Fingerprint `35E6 715F 0D27 D8FA CD84  56C2 9491 8AEE 2850 F202`. The secret key
  and its passphrase did not pass through the AI assistant's session; the assistant read only the
  exported public key.
- The maintainer signed with `scripts/apt-sign.sh`, which checked `Release` against `Packages`
  and both package hashes against the two builds before signing.
- On the test host, from a fresh clone of this batch with the tags: `sqv --cleartext` verified
  `InRelease` with the committed key (`35E6715F...`), and `scripts/apt-site.sh` built both packages
  again from the tag and printed `MOOBFS_APT_SITE_PASS: 2 packages built from their tags, equal to
  the signed index`. The first `sqv` call failed with "File exists" because it was given
  `/dev/null` as output; with a temporary file it verified.

## Surfaces

Changed in this batch: `docs/operations.md` (the user's steps: the key into
`/usr/share/keyrings`, its fingerprint, a deb822 source with `Signed-By`, upgrade and roll back;
building the packages yourself stays as the other way), `SECURITY.md` (a section with the key's
fingerprint), `docs/security-operations.md` §1 (a row for the key), `docs/apt-repository.md`
(status), both READMEs (the install row), `docs/status.md` and `docs/status.pt-BR.md`,
`ROADMAP.md` (C6 and the release row), `docs/distribution-strategy.md` (two rows, the scenario
and a dated amendment), `docs/matrix-and-limits.md`, `docs/public-surfaces.md` (a row for the
repository and the fingerprint), `CHANGELOG.md`, the journey milestone, and
`docs/retired-claims.txt` with the "not published" sentences. Dated notes that said the packages
were not published on their dates stay.

## Next step

Merge this batch with the maintainer's agreement; after the site deploys, install the packages
from `https://studiomoob.github.io/llmoobfs/apt/` on a disposable Debian 13 host, following
`docs/operations.md`, and record it here.
