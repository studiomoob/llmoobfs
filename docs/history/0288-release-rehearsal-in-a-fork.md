# 0288 — The release workflow rehearsed in a fork

Date: 2026-11-12. Work started about 19:15 Brasília time; rehearsal runs 19:24–19:29; documents
until about 20:05. Status: **done; pull request with the maintainer's agreement.** Estimated
effort S; the time recorded is calendar time of one session.

## Goal

Journal 0284 changed `.github/workflows/release.yml` for immutable releases (a draft that takes
all four assets and is published by the last job) and proposed a test in a fork before the next
real tag, because the workflow runs only on a pushed `v*-experimental` tag and none had been
pushed since. The first real use would otherwise have been `v0.6.0-experimental`, whose tag name
an immutable release burns if something goes wrong after publication.

## Decision (maintainer, 2026-11-12)

The maintainer created the fork `claudiofp/llmoobfs` and agreed to the two tags of journal 0284's
plan there. Nothing was pushed to `studiomoob/llmoobfs`.

## Setup

In the fork: GitHub Actions turned on (`PUT /repos/{owner}/{repo}/actions/permissions`) and
immutable releases turned on (`PUT /repos/{owner}/{repo}/immutable-releases`, which then reports
`enabled: true`, as the project's repository does). The fork's other workflows stay in GitHub's
`disabled_fork` state; only `Release` ran.

## Positive run: `v0.0.1-experimental`

An annotated tag on `fc589c8` (the `main` of that afternoon). Run `43319591919` of the fork,
22:24:25–22:25:41 UTC: `Source archive, signature and pre-release`, the three jobs of the SLSA
generator (`upload-assets` skipped, as configured) and `Attach the provenance and publish` all
succeeded. The release is published, a pre-release, immutable (`isImmutable: true`), with exactly
`llmoobfs-v0.0.1-experimental.tar.gz`, `.tar.gz.sha256`, `.tar.gz.sigstore.json` and
`.intoto.jsonl`.

Verification on the test host (Debian 13), with the commands of `docs/security-operations.md` §4
and the fork's repository in place of the project's:

- `sha256sum -c llmoobfs-v0.0.1-experimental.tar.gz.sha256`: `OK`.
- `python -m sigstore verify github ... --repository claudiofp/llmoobfs --ref
  refs/tags/v0.0.1-experimental`, sigstore 4.5.1 from PyPI in a fresh venv: `OK`.
- `slsa-verifier verify-artifact ... --source-uri github.com/claudiofp/llmoobfs --source-tag
  v0.0.1-experimental`, slsa-verifier v2.7.1 (`slsa-verifier-linux-amd64`, SHA-256 checked against
  the project's `SHA256SUM.md`): `PASSED: SLSA verification passed`.

## Negative run: `v0.0.2-experimental`

An annotated tag on a throwaway commit `488fde7` (on `fc589c8`, never pushed to any branch) whose
`release.yml` uploads a fifth asset, `llmoobfs-v0.0.2-experimental.extra`, to the draft. Run
`43320047414`, 22:27:19–22:28:44 UTC: the first two stages succeeded and `Attach the provenance and
publish` failed with `assets [...extra ...], expected [...]; left as a draft`. The release stayed a
draft, not immutable (`isImmutable: false`), with five assets. Deleting it and creating a draft
under the same tag again succeeded, so the name is not burned; that probe draft was deleted too.

## Result

Both expectations of journal 0284 hold: a good tag ends in a published, immutable pre-release
whose assets verify; a bad one stops before publication and can be cleaned up. The fork keeps the
pre-release `v0.0.1-experimental` and the two tags.

## Change

- `release.yml`'s header, `docs/security-operations.md` §4 and `CHANGELOG.md` no longer say the
  workflow was not exercised; old wording in `docs/retired-claims.txt`. Dated notes in journal
  0284. Journal 0287 records the merge of #198.

## Limitations

- One rehearsal in a personal fork. The project's own repository still has had no tag since
  journal 0284; its first one will be `v0.6.0-experimental`.
- The verification commands ran with the fork's repository; on the project's tag they name
  `studiomoob/llmoobfs`.

## Next step

The stage-2 declaration and the tag `v0.6.0-experimental` (journal 0284), with the maintainer.
