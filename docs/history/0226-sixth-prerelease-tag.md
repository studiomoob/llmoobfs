# 0226 — Sixth pre-release tag: v0.5.0-experimental

Date: 2026-10-28. Status: in progress; branch `release/v0.5.0-experimental` on top of `main`
(`4b72d4a`; first pushed on `e5b6591`). Work started: 2026-10-28 19:00 America/Sao_Paulo. Wall-clock time, not an
engineering-effort measurement. Estimated effort: S.

## Decision

Tag `v0.5.0-experimental` and publish a GitHub pre-release, as the maintainer approved on
2026-10-28, after security batch 3 (journal 0225). `v0.4.1-experimental` carries every defect
fixed since 2026-10-20, F13 to F17 and the four published advisories among them, so the
advisories had no tag to name as patched.

A minor version, not `v0.4.2`: the rule of journal 0142 keeps a patch version for changes that
leave the UAPI and the on-disk conventions alone, and this one does not. The reserved byte of a
query term must now be zero (`EINVAL`, ADR-0026), and lookup gives `EREMOTE` and `ESTALE` for
casefolded directories instead of `EOPNOTSUPP` (journal 0221). Sizes, offsets, command numbers,
the ioctl magic and the on-disk conventions do not change.

It is the first tag built by `.github/workflows/release.yml` (journal 0225): a source archive of
the tag, its SHA-256, a Sigstore bundle and SLSA provenance, attached to the pre-release.

## What it carries since v0.4.1

- Security: F13 (journals 0151, 0152), F14 and the other findings of review round 2 (journals
  0155, 0156, 0172 to 0180, issues 110 to 117 and 119), F15, F16 and F17 with advisories
  GHSA-95h9-j7c2-gcw6, GHSA-6xf4-r246-92r6 and GHSA-82w5-ph62-47wf, GHSA-756j-cgwq-gvwm, issue 113
  (journal 0210) and issue 118 (journals 0182 to 0193, 0210).
- User space, after the first push of this branch: the MCP adapter's paths and identity
  (GHSA-4gr9-rvgr-44qj) and the LangGraph store's batched labels (GHSA-3wx4-fh9w-2v66), two
  private reports with the reviewer's fixes (journal 0227). The tag waited for them; the branch
  was rebased on `4b72d4a` and takes the reviewer's references and testing notes.
- The review batch of journal 0144 (R1, R2; journals 0149, 0150, 0153).
- User space: the command-line tool, the Python library with its LangGraph store, the MCP
  adapter and the container recipe (roadmap C1 to C5 and C7, journals 0197 to 0208).
- Testing: Debian 6.12.113 (journals 0203, 0206), the client packages G1, G3, G4 and G5, and a
  full VM run that can use KVM (journal 0225).

## Changes

- `CHANGELOG.md`: a `v0.5.0-experimental` section with an opening summary above the entries
  that were "Unreleased"; an empty "Unreleased" heading stays on top.
- `layer/main.c`: `MODULE_VERSION("0.5.0")`; `CITATION.cff`: version and date.
- `README.md`, `README.pt-BR.md`, `SECURITY.md` (a row for the new tag; the old row says where
  its defects are fixed), `CONTRIBUTING.md`, `docs/status.md`, `docs/status.pt-BR.md`,
  `docs/faq.md`, `docs/when-not-to-use.md`, `docs/compatibility.md`, `site/index.html`,
  `site/faq.html`, `site/guide.html`, `site/plan.html` and the journey milestone in
  `scripts/site-journey.py` (pages regenerated): name the new tag. Sentences saying that
  `v0.4.1-experimental` contains a defect stay, because it still does, and now say that
  `v0.5.0-experimental` has the fix.
- `docs/retired-claims.txt`: the wordings that named `v0.4.1-experimental` as the last tag.
- The Zenodo citation in the READMEs is left as it is: it names an archived snapshot newer than
  `v0.4.1-experimental` (journal 0168), which this tag does not change.

## Evidence

The tag goes on the merge commit of this branch, whose code is that of `e5b6591` but for
`MODULE_VERSION` and the user-space fixes of journal 0227; batch 3 ran the full `make vm-test` and the full KASAN/lockdep suite on it
(journal 0225). The pull request's required checks, KASAN included (the branch touches
`layer/main.c`).

## Limitations

Same as journal 0142: one kernel series (6.12), x86-64 tested in a VM, arm64 only under
emulation; no independent human review of the whole kernel code. Open: fair shares of the index
budget (S9, F2) and G2's latency ceiling, issue 117 for inodes with an index record.
