# 0284 — The release workflow under immutable releases, Part A's remaining gaps, and the release decisions of 2026-11-11

Date: 2026-11-11. Work started 11:42 Brasília time; ended 12:32, for the documents, the workflow
and the local checks (the pending VM runs are not included). Status: **done locally, not
published; VM runs pending** (the test host is unreachable). Estimated effort S–M; the time
recorded is calendar time of one session, close to the effort.

## Goal

Two things a read-only audit of the stage-2 conditions found to be ours (kept outside the
repository): `.github/workflows/release.yml` would break on the next tag, and Part A of
`docs/review-findings.md`, the part inside criterion (2) of ADR-0030, still listed findings with
no regression test, with the register still marked "draft". Also: record the maintainer's release
decisions of 2026-11-11 and correct two stale lines of `docs/security-roadmap.md`. The kernel is
frozen (ADR-0022): nothing under `layer/`, the UAPI or the format changes.

## Decisions (maintainer, 2026-11-11, about 12:30 Brasília time)

1. The tag `v0.6.0-experimental` is cut only when stage 2 is declared.
2. "A third party reproduces everything" (§8, stage 2, "Done when") means Parts A and B of the
   route of issue #195: the core suite with acceptance, and fstests `quick` over the layer. Part
   C, a syzkaller campaign, is welcome and not required.
3. A reproduction at `d96b0db` counts for the commit that is tagged if nothing under `layer/`
   changes between the two.
4. The `.deb` packages (`moobfs-dkms`, `moobfs-tools`) stay unpublished until stage 2 is declared.
5. The version is `0.6.0`, not `0.5.1`. Justification: the rule of journal 0142 ("a patch
   version: the UAPI and the on-disk conventions do not change") says when a patch is enough for
   compatibility; it does not make every compatible change a patch. Since `v0.5.0-experimental`
   the UAPI header changed only in comments and the on-disk conventions did not, but the code
   adds functionality: the layer module builds and is supported on Linux 7.3 (ADR-0034, journals
   0275 and 0277) and on Ubuntu's 7.0 series (ADR-0036, journal 0281), and the `moobfs` CLI gains
   the workspace commands (ADR-0035, journal 0273). Semantic Versioning, whose
   `MAJOR.MINOR.PATCH` scheme `docs/compatibility.md` §5 uses, marks new backwards-compatible functionality with a minor
   version and keeps a patch for fixes. Alternative: `0.5.1`, by the letter of the 0142 rule; not
   taken, since a user reading `0.5.1` would expect fixes only.

Carried into `docs/distribution-strategy.md` §8 (amendment dated 2026-11-11, decisions 1 to 4),
the "Public release" row of `ROADMAP.md` (1 to 4) and `docs/compatibility.md` §5's list of tag
decisions (1 and 5).

## The release workflow under immutable releases

**Defect.** Immutable releases are on for the repository (`gh api
repos/studiomoob/llmoobfs/immutable-releases` → `enabled: true`, since about 2026-10-30;
`v0.5.0-experimental` predates it). The `archive` job created the pre-release already published,
with three assets; the `provenance` job (the SLSA generic generator v2.1.0, `upload-assets:
true`) then attached `llmoobfs-<tag>.intoto.jsonl`. GitHub's documentation, read through Context7:
with immutable releases on, "you cannot add, replace, or delete assets after a release is
published, and you cannot move or delete its tag while the release exists"; a deleted immutable
release's tag name cannot be used again. So the next tag would have ended with a published
release missing its provenance, and a deletion to retry would have burned `v0.6.0-experimental`.

**Design** (GitHub's recommended order: draft, attach every asset, publish):

- `archive` creates the release with `gh release create --draft --verify-tag --prerelease` and
  the archive, its SHA-256 and the Sigstore bundle. On a rerun it uploads to a draft
  (`--clobber`) and refuses a release that is already published.
- `provenance` keeps the generator, referenced by its tag as before, with `upload-assets: false`.
  The generator stores the signed provenance as a workflow artifact named by its
  `provenance-name` output. Its own uploader (`softprops/action-gh-release` v2.2.1) finds the
  release by tag; the REST lookup by tag returns published releases only, and whether that action
  would find the draft or create a second release was not established, so it is not used.
- A new job `publish` (needs both) downloads that artifact with `actions/download-artifact`
  v8.0.2, pinned by commit, checks its name and that it is not empty, checks the release is still
  a draft, uploads the provenance, checks the release has exactly the four expected assets, and
  publishes it with `gh release edit --draft=false --prerelease`. If anything fails, the release
  stays a draft, which can be fixed or deleted without burning the tag name.
- Template values reach the shell through `env:` only; permissions stay per job (`contents:
  write` for the jobs that touch the release; the generator keeps the permissions its reusable
  workflow declares).

**Validation.** actionlint 1.7.7 (the image of journal 0211, pinned by digest; it runs shellcheck
over each `run:` block) on `release.yml`: no finding. Over every workflow it reports only two
`info` findings already in `fuzz.yml` (SC2015, SC2012), not touched here. `scripts/check-pins.sh`:
`PINS_OK`. The workflow was **not exercised**: it runs only on a pushed `v*-experimental` tag,
and none was pushed.

**Proposed fork test, not done.** In a fork with immutable releases turned on: push a tag such as
`v0.0.1-experimental` on a throwaway commit; expect the draft with three assets after `archive`,
the provenance artifact after `provenance`, and a published pre-release with four assets after
`publish`; then download the assets and run the commands of `docs/security-operations.md`
(`sha256sum -c`, `python -m sigstore verify github`, `slsa-verifier verify-artifact` with the
fork's repository). A second tag whose `publish` job is made to fail (for instance by a fifth
asset) must leave a draft, deletable without burning the name. Not done here: it needs the
maintainer's agreement and a fork.

## Part A of the review register

The audit listed Gaps items 1, 2, 3, 9, 10, 11 and 12 of `docs/review-findings.md` as Part A
findings without a regression test. One by one:

| Finding | Test | Marker | Local result | VM | Decision needed |
| --- | --- | --- | --- | --- | --- |
| 0114 item 3, ref-walk revalidation reads a lower dentry `moobl_instantiate()` may free | none | — | — | — | yes: a test that fails without the fix needs a hold point inside ref-walk revalidation (a kernel change); a stress race under KASAN would pass on the fixed code and fail only by chance on the old one |
| 0114 item 6, long walks without `cond_resched()` | none | — | — | — | yes: the only symptom is scheduling latency or a soft lockup, reached only with a domain far beyond the index budget or a scheduler-latency probe; journal 0120 chose no test |
| 0114 item 8, directory reads with the caller's credentials | none | — | — | — | yes: the two differ only in what an LSM decides; testable without a kernel change under the SELinux harness of S12 (`vm/selinux-local.sh`, not in the suite), effort M |
| V8, rename copied the lower's whole `i_flags` | none | — | — | — | yes: no flag outside the copied mask (`S_SYNC`, `S_NOATIME`, `S_APPEND`, `S_IMMUTABLE`) has an effect the VFS shows on the layer inode after a rename that succeeds on ext4, btrfs or XFS; `S_SWAPFILE`, the one with a clear effect, makes the rename fail first (`may_delete()`) |
| V2, `sync_file_range(2)` succeeds and writes nothing | `tests/passthrough.c` limits | `PT_LIMITS_PASS` | compiled (`-static -O2 -Wall -Wextra -Werror`) | pending | no; a pin of the documented limit: no negative round, since nothing was fixed; it fails if the limit is ever lifted |
| V1 (`cachestat(2)` part) | same case | `PT_LIMITS_PASS` | compiled | pending | no; as V2 |
| M7, `capable()` and `ns_capable_noaudit()` | none | — | — | — | yes: no allowed or denied outcome differs, only whether an LSM logs; testable under SELinux audit as item 8 |
| M8.2 `getattr` | none | — | — | — | yes: on 6.12 `vfs_getattr()` adds `AT_GETATTR_NOSEC` itself, so the two paths cannot be told apart (journal 0221) |
| M8.3 `flush` | none | — | — | — | yes: none of ext4, btrfs and XFS has `->flush` |
| M8.4 `fileattr_set` | not applicable | — | — | — | no: not a defect (journal 0221) |
| M8.5 `SB_I_NOEXEC` documentation | `tests/passthrough.c` noexec | `PT_NOEXEC_PASS` | compiled | pending, with a negative round | no |
| M8.6 `scan_delay_us` kept | not applicable | — | — | — | no: a parameter kept with its reason; no behaviour |

`PT_LIMITS_PASS` writes 1 MiB through the layer without `fsync`, calls `sync_file_range` with
`WAIT_BEFORE | WRITE | WAIT_AFTER` on the layer descriptor (must return 0) and reads `cachestat(2)`
on both descriptors: the layer's shows no cached page, the lower's shows cached and dirty pages;
the same call on the lower descriptor then cleans them (the control). `PT_NOEXEC_PASS` checks the
errnos `docs/when-not-to-use.md` §3 states: `access(X_OK)` and `execve` of a 0755 script `EACCES`,
`mmap` with `PROT_EXEC` `EPERM`, and a character device (1, 3) made with `mknod` on the lower
cannot be opened through the layer (`EACCES`). `docs/testing.md` describes both in the
`PASSTHROUGH_PASS` row.

The register is **no longer marked "draft"**: every Part A finding is tested, answered as a
design objection, or listed for the maintainer's decision with its reason (Gaps, the note on
items 1 to 3 and 9 to 12). Its status line says the VM runs of the new tests are pending.

## Stale lines of the security roadmap

- S6: "Open: a build-time refusal of an unlisted kernel (`make install` and the DKMS package
  refuse one, `make layer` does not)". Closed by journal 0277: `make layer` refuses by API series
  (`layer/moobl_compat.h`), as `docs/compatibility.md` "Open points" already says.
- S14 to S21: "S16 done locally". S16 (ADR-0033, journal 0271) is on `main` since pull request
  187, merged on 2026-11-08.
Both old wordings are added to `docs/retired-claims.txt`.

## Evidence

On this Mac, 2026-11-11, 11:42–12:32 BRT: actionlint as above; `tests/passthrough.c` compiled
with the flags of `vm/build-tests.sh` in the `golang:1.24-bookworm` image already present.
shellcheck 0.9.0 over the files `make lint` lists, `scripts/check-spdx.sh`,
`scripts/test-steps-filter.sh` (`STEPS_FILTER_PASS`), `scripts/check-retired-claims.sh`
(`RETIRED_CLAIMS_OK`), `scripts/check-pins.sh` (`PINS_OK`), `scripts/check-matrix-page.sh`
(`MATRIX_PAGE_OK`), `scripts/check-site.py`, `scripts/site-faq.py --check`, `make test` and
`git diff --check origin/main`: pass.

## Pending (test host unreachable)

In addition to the items of journal 0283, "Pending":

1. `MOOB_ONLY=ext4 make vm-test` (the step that runs `tests/passthrough.c`; then btrfs and xfs) →
   `PT_LIMITS_PASS` and `PT_NOEXEC_PASS` beside `PASSTHROUGH_PASS` on each lower. Negative round
   for `PT_NOEXEC_PASS` on a scratch branch: drop `SB_I_NOEXEC | SB_I_NODEV` from
   `sb->s_iflags` in `layer/main.c` → `PT_NOEXEC_FAIL` and `MOOBFS_VM_FAIL: passthrough-ext4`.
   `PT_LIMITS_PASS` has no negative round (nothing was fixed); if a lower writes its dirty pages
   back before `cachestat` reads them, the case fails and must be revisited.
2. The full `make vm-test` and the full KASAN suite, once for the batch.

## Limitations

- `release.yml` is linted, not run; the fork test above is proposed, not done.
- Seven Part A items have no test and need the maintainer's decision; two of them (0114 item 8,
  M7) could be tested without a kernel change under SELinux, an M-sized piece of work.
- The two new passthrough cases are compiled, not run.

## Next step

The maintainer's decisions on the listed Part A items and on the fork test; the pending VM runs;
then the batch's pull request with the maintainer's agreement.
