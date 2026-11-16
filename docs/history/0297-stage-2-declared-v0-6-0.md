# 0297 — Stage 2 declared with the third-party reproduction pending; v0.6.0-experimental prepared (ADR-0039)

Date: 2026-11-15. Work started about 09:50 Brasília time. Status: **in progress; the release
steps wait for the maintainer.** Estimated effort S (ADR-0039). The time recorded is calendar
time.

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

## Next step

After the batch is merged, each with the maintainer's agreement:

1. the annotated tag `v0.6.0-experimental` on the merge commit, which runs the release workflow
   (draft first, rehearsed in journal 0288);
2. publishing the release;
3. the body of issue #195 pinned to the tag;
4. the `.deb` packages: how and where to publish them is still to be decided. The release
   workflow publishes source only.
