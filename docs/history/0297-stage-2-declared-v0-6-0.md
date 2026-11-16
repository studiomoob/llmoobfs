# 0297 — Stage 2 declared with the third-party reproduction pending; v0.6.0-experimental tagged (ADR-0039)

Date: 2026-11-15 and 16. Work started about 09:50 Brasília time on the 15th and ended at 07:10
on the 16th, when the published release was verified. Status: **done; the tag and its release
exist and issue #195 is pinned to the tag; the `.deb` packages remain (Next step).** Estimated effort S
(ADR-0039); the time recorded is calendar time, most of it machine time of the test runs.

## Goal

Declare stage 2 (technical preview) of `docs/distribution-strategy.md` §8, and prepare the tag
`v0.6.0-experimental` that journal 0284 tied to the declaration.

## Decisions (maintainer, 2026-11-15)

- About 09:50: declare stage 2 now, with the third-party reproduction as a known open item
  instead of a condition. The alternative, waiting for a reproducer, was not taken. ADR-0039
  records both.
- About 10:30: the module version string must change for the tag (below), so issue #195's route
  is pinned to the tag itself, not to `1f9dd80` (the merge of #204) with an exception for the
  version string. Earlier the same morning the maintainer had agreed to `1f9dd80`; this replaces
  it.

## Audit before the declaration

The stage-2 conditions were read against `main` `1f9dd80`:

- The deliverables and acceptance steps 1 to 5 are met (journals 0229, 0273, 0277, 0283):
  - DKMS refusal outside the matrix and `make layer` refusal;
  - the packages, tested in VMs;
  - the CLI with its workspace commands;
  - upgrade and roll-back, backup and restore with `rsync -aHAX`, signing under lockdown;
  - the container bind (step 5 as amended).
- The review criterion of ADR-0030 is met with four recorded exceptions (journal 0289).
- The one page of matrix and limits is checked in CI (journal 0282). No leak is known (journal
  0283).
- The remainder of issue #117, the last condition the project could act on, was fixed by
  ADR-0038 (journal 0295).
- The third-party reproduction (issue #195) has not happened.

## The module version is in `layer/`

`layer/main.c` carries `MODULE_VERSION("0.5.0")`. The DKMS package takes its version from it
(`packaging/debian/moobfs-dkms.dkms.in`, `#MODULE_VERSION#`), and journal 0226 changed it for the
last tag. Changing it to `"0.6.0"` changes a file under `layer/`, which decision 3 of journal 0284
used to tell whether a reproduction at `d96b0db` counts for the tag. ADR-0039 treats the version
string as a release step, not a kernel change under the freeze, and pins the reproduction to the
tag, so no exception to that rule is needed.

## Changes

- `docs/decisions/0039-stage-2-declared-reproduction-pending.md`.
- `layer/main.c`: `MODULE_VERSION("0.6.0")`.
- `CHANGELOG.md`: the section `v0.6.0-experimental — 2026-11-15`, with a summary of the changes
  since `v0.5.0-experimental`; the entries keep their detail.
- `docs/distribution-strategy.md` §8: an amendment dated 2026-11-15.
- The latest tag, and the stage where it is stated, in:
  - `README.md` and `README.pt-BR.md`;
  - `docs/status.md` and `docs/status.pt-BR.md` (header and a dated update);
  - `docs/faq.md`;
  - `SECURITY.md` (a row for the new tag);
  - `CONTRIBUTING.md`, `CITATION.cff`;
  - `docs/compatibility.md` §5;
  - `docs/matrix-and-limits.md`;
  - `ROADMAP.md` (the public-release row);
  - `docs/help-wanted.md` (the #195 pin);
  - the site: `site/index.html`, `site/guide.html`, `site/plan.html`, and `site/faq.html`
    regenerated;
  - `scripts/site-journey.py`, a milestone of 2026-11-15, and its pages.
- `docs/retired-claims.txt`: the old wording of the latest tag and of the pin.

Left as they are on purpose:

- the body of issue #195, which the maintainer edits after the tag exists;
- the dated texts that state the pin `d96b0db` or say stage 2 was not reached; they are history
  and keep their dates.

## Evidence

The checks of this batch are listed with the pull request (journal 0291 records the merge).
`scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.

On the dedicated host (Debian 13, KVM), at `0a4ec84`, the tip of the batch when the run started;
the later commits change only journals:

- the pre-push checks, 10:16 to 10:26: lint, checkpatch, unit tests, ASan build, hostile
  images, Python library, retired claims, pins, matrix page, mutants, site, `git diff --check`
  against `1f9dd80`, `make static` and the Ubuntu tests, all `PASS`;
- the full `MOOB_VM_KVM=1 make vm-test`, 10:26 to 10:47: `MOOBFS_VM_PASS`,
  `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, exit code 0;
- the full local KASAN/UBSAN/lockdep suite (`vm/kasan-local.sh`), 10:47 to 12:00:
  `MOOBFS_PYLIB_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_KASAN_PASS`, exit
  code 0.

## The tag and the release

With the maintainer's agreement, after the merge of #205 (`b40e3b6`, 2026-11-16 06:50):

- the annotated tag `v0.6.0-experimental` on `b40e3b6`, pushed at about 06:55;
- the release workflow, run `43844134071`, 06:55–06:56: `Source archive, signature and
  pre-release`, the SLSA generator (`upload-assets` skipped, as configured) and `Attach the
  provenance and publish` succeeded. The release is a published pre-release, immutable
  (`isImmutable: true`), with `llmoobfs-v0.6.0-experimental.tar.gz`, its `.sha256`, its
  `.sigstore.json` and `llmoobfs-v0.6.0-experimental.intoto.jsonl`:
  <https://github.com/studiomoob/llmoobfs/releases/tag/v0.6.0-experimental>.

Pushing the tag publishes the release: the workflow's last job turns the draft into a published
pre-release, so there is no separate publishing step, as the rehearsal of journal 0288 showed.
The list in this journal's first version counted one.

Verification on the test host (Debian 13), with the commands of `docs/security-operations.md`
§4 and the assets downloaded from the release:

- `sha256sum -c llmoobfs-v0.6.0-experimental.tar.gz.sha256`: `OK` (`60cfc13f71059d33...`);
- `python -m sigstore verify github ... --repository studiomoob/llmoobfs --ref
  refs/tags/v0.6.0-experimental`, sigstore 4.5.1: `OK`;
- `slsa-verifier verify-artifact ... --source-uri github.com/studiomoob/llmoobfs --source-tag
  v0.6.0-experimental`, slsa-verifier 2.7.1 (the binary checked in journal 0288): `PASSED: SLSA
  verification passed`.

The changelog section, `CITATION.cff` and `SECURITY.md` date the release 2026-11-15, the day of
the declaration; the tag was cut on 2026-11-16. The tagged files cannot change, so the date is
left as it is.

On 2026-11-16 the maintainer replaced the body of issue #195. It now pins the route to the
tag (commit `b40e3b6`) instead of `d96b0db`, says that stage 2 is declared with the
reproduction pending (ADR-0039), offers the release's signed source archive, adds Debian 7.3.4
and the 7.3.4 KASAN build, and says issue #117 was fixed before the tag (ADR-0038). The old
text is in the issue's edit history.

### The access table, read after the tag

`docs/security-operations.md` §1 asks for the access table to be re-read "in any case before a
tag". It was not read before `v0.6.0-experimental`; it was read on 2026-11-16, after the release,
with the `gh api` calls that section lists, and nothing had changed (the dated note in §1). No
written checklist lists the steps of a tag, and the release workflow does not check this one;
a checklist is a next step.

## Next step

- The `.deb` packages: how and where to publish them is still to be decided, with the maintainer.
  The release workflow publishes source only.
- A written checklist of the steps of a tag (the access table among them), so that none is
  missed at the next one.
