# Tag checklist

Date: 2026-11-16 (journal [0297](history/0297-stage-2-declared-v0-6-0.md)). Status: written
after `v0.6.0-experimental`, from the steps of journals 0226, 0284, 0288 and 0297. One step,
the access table, was missed at that tag and done after it; this list exists so that the next
tag misses none. Each step that publishes something needs the maintainer's agreement.

## Before the tag

1. **Decision.** The decision that the tag marks (a stage, a security batch) is recorded in an
   ADR or a journal, with the maintainer's agreement and the version number (journal 0284 decides
   minor or patch).
2. **Tests on the tree to be tagged.** The full `MOOB_VM_KVM=1 make vm-test` and the full
   `sh vm/kasan-local.sh` passed on the commit of the batch that carries the release, and its
   pull request's required checks passed (AGENTS.md). Exploratory runs (s390x, arm64) are
   recorded as such.
3. **Module version.** `MODULE_VERSION` in `layer/main.c` is set to the new version; the DKMS
   package takes its version from it (`packaging/debian/moobfs-dkms.dkms.in`). This is a release
   step, not a kernel change under the freeze (ADR-0039).
4. **Changelog.** `CHANGELOG.md` gets a section `vX.Y.Z-experimental — <date>` with a summary
   above the entries that were under "Unreleased"; an empty "Unreleased" stays on top. Use the
   date the tag is expected to be cut: the tagged files cannot change later (journal 0297).
5. **Surfaces.** Every place that names the latest tag or the stage names the new one, in both
   languages where there are two (`docs/public-surfaces.md`):
   - `README.md`, `README.pt-BR.md`, `CONTRIBUTING.md`, `CITATION.cff` (version and date),
     `SECURITY.md` (a row for the new tag; the old row says where its defects are fixed);
   - `docs/status.md`, `docs/status.pt-BR.md`, `docs/faq.md` (then `scripts/site-faq.py`),
     `docs/compatibility.md` §5, `docs/matrix-and-limits.md`, `ROADMAP.md`,
     `docs/help-wanted.md` and any pinned call;
   - `site/index.html`, `site/guide.html`, `site/plan.html` and the journey milestone in
     `scripts/site-journey.py` (then `python3 scripts/site-journey.py .`).

   Sentences that say an older tag carries a defect stay, since it still does.
6. **Retired wording.** The old "latest tag" sentences go into `docs/retired-claims.txt`.
7. **Access table.** Re-read `docs/security-operations.md` §1 with the `gh api` calls listed
   there and add a dated note, changed or not.
8. **Local checks.** `sh scripts/check-retired-claims.sh`, `python3 scripts/check-site.py`,
   `sh scripts/check-matrix-page.sh`, `sh scripts/check-pins.sh`, `git diff --check`, and the
   pre-push checks of the test host.

## The tag

9. **Merge, then tag.** After the batch's pull request is merged, the annotated tag goes on the
   merge commit: `git tag -a vX.Y.Z-experimental -m "..." <merge commit>`, then
   `git push origin vX.Y.Z-experimental`.
10. **Pushing the tag publishes.** `.github/workflows/release.yml` creates a draft, attaches the
    source archive, its SHA-256, the Sigstore bundle and the SLSA provenance, and its last job
    publishes the pre-release. Releases are immutable: a published release takes no new asset,
    and a deleted release's tag name cannot be used again (journals 0284, 0288). There is no
    separate publishing step and no way back.

## After the tag

11. **Verify.** Download the four assets and run the commands of `docs/security-operations.md`
    §4 (`sha256sum -c`, `sigstore verify github`, `slsa-verifier verify-artifact`) against the
    project's repository and the new tag.
12. **Calls and pins.** Issues and pages pinned to a commit for the tagged state (issue #195 since
    ADR-0039) are pinned to the tag; the maintainer edits issue bodies.
13. **Record.** The journal of the tag records the run id, the assets, the verification and the
    merge; the next journal of merges records the pull request.
14. **Archive and packages.** Check whether a Zenodo snapshot was made (the READMEs cite one,
    journal 0168) and update the citation only if a new one exists. Packages are published only
    as decided for that tag; the release workflow publishes source only.
