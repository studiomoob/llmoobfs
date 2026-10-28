# 0193 — Reconcile the merged per-mount query-build work policy

Date: 2026-10-25. Status: local documentation reconciliation complete;
publication follows the merge with PR 139.

*Numbering note, 2026-10-25:* this record was drafted as journal 0188.
PR 139 had already published journals 0188–0192 (the G6 protocol through
the same-fsuid budget release, ADR-0021), so this reconciliation is 0193.
The body below was written against `main` at `b7ae1b8`, before that merge.
Work started and ended: 2026-10-25. Labor time was not measured.

## Goal and observed result

After the maintainer authorized merging PR 138, reconcile the project's
current-state documents with the published `index_build_work` mount option.
The exact PR head `6621b21` passed all twelve reported checks, including
Native Linux VM, KASAN/lockdep and CodeQL. PR 138 merged on 2026-10-25 at
11:41:46 UTC as `b7ae1b8`; `git diff --quiet 6621b21 b7ae1b8` exited zero.
The primary checkout fast-forwarded to that `origin/main` commit and was
clean. Issue 118 remained open. These are observations of the prior PR,
not tests newly performed for this documentation update.

## Decision and alternatives

Describe the bounded policy as implemented on `main`: the default remains
32,768 cumulative build-work units, while the mounter may choose
`index_build_work=1024..65536`. The chosen value is fixed during the mount.
A valid 17,000-file attributed domain completed in the dedicated native
VM with 65,536 and was denied at the default. The upper setting still
allows more synchronous work, and neither it nor the 10-second cooperative
deadline strictly caps CPU time or physical I/O. The 16-per-UID and
64-per-mount frequency limits continue unchanged. No disk format or ioctl
UAPI changed. The KASAN suite and the full PR VM suite exercised the
default; the 65,536 path was covered by the separate native opt-in test.

Do not call the option a universal fix or close issue 118: a real slow
lower device and one real client project near 17,000 files in one domain
remain untested. The alternative of raising every mount's default was
already rejected by ADR-0020; the alternative of leaving current-state
documents at their pre-merge checkpoint would misstate the published
mount contract.

## Documentation and validation

Updated both READMEs, the roadmap, query specification, testing and use
guidance, security roadmap, SECURITY, FAQ, landing page, generated status
and journey pages, site guide, ADR-0019 supersession note, ADR-0020 status,
journal 0187 status, history index and changelog. The specification no
longer says, in the present tense, that no mount option exists. The
2026-10-20 review is described as merged on `main`; the tag still contains
its defects. Preserved the dated pre-merge checkpoints in the ADRs,
specification and journal; added dated status notes. Recorded retired
current-state phrases in `docs/retired-claims.txt`.

Walking `docs/public-surfaces.md` also checked the GitHub repository
description, discussions 29 and 69, issue 118 and the Zenodo badge already
labeled as a snapshot in the READMEs. The repository description still
matches the pitch. Discussion 29 points newcomers at the README. Discussion
69 still asks for human review, which remains true, but its opening says
the current snapshot is `v0.4.0-experimental` and that syzkaller had four
runs; a comment records that the last tag is `v0.4.1-experimental` and that
later corrections are on `main`
(https://github.com/studiomoob/llmoobfs/discussions/69#discussioncomment-19177537).
The original posts were not rewritten.
Issue 118's original report does not mention the guard or the mount option;
a comment records the merged policy and the evidence that is still missing
(https://github.com/studiomoob/llmoobfs/issues/118#issuecomment-6397435462).
This unit does not republish the Zenodo record. GitHub Pages serves `site/`
from `main` through `.github/workflows/pages.yml`, so the live site changes
only after this documentation unit is published.

`python3 scripts/site-journey.py .`, `python3 scripts/site-faq.py`,
`python3 scripts/site-faq.py --check`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/check-site.py .` and `git diff --check` exited zero. The
FAQ generator wrote 61 elements and then matched its source. The
retired-claims command printed `RETIRED_CLAIMS_OK`. The site check
validated links and structure on twelve HTML pages. This is a
documentation-only update: no new module or VM test was run for it.
`make checkpatch` was not run; this unit does not change the module.

## Second pass, 2026-10-25

A direct question asked whether the inventory had all been checked. It had
not. This pass read `site/plan.html`, the ADR status lines, `docs/help-wanted.md`,
the open review issues, the `v0.4.1-experimental` release notes and the five
GitHub security advisories.

`site/plan.html` and the test guide still said that nothing had been
released. The plan also said that the M4 report was in progress and that
M4 had not started. The status, the page
description and the two plan notes now match the pilot report in journal
0088 and the source tags through `v0.4.1-experimental`. ai-memory is named
as the client that was exercised; mem0 and Letta are not. `docs/help-wanted.md`
now includes findings F13 and F14 and the later review journals. ADR-0019's
opening status now says PR 135 merged and ADR-0020 superseded the fixed
work number; the old wording is quoted there. The security roadmap's
2026-10-22 note that PR 127's CI was pending now has a dated correction.

Issues 16 and 17 pointed reviewers at `v0.4.0-experimental`. Their commit
pins now name `main` at `b7ae1b8`, and each body records the previous pin.
The `v0.4.1-experimental` release notes were left as the snapshot of that
tag. The F14 advisory GHSA-qgjj-2f2p-q82m is still a private draft; journal
0156 says to publish it after the review batch merged. This pass did not
publish it. LinkedIn, the fediverse, mailing lists and the maintainer's
private working pages were not inspected.
*Note, 2026-10-28:* GHSA-qgjj-2f2p-q82m was published on that date, without a CVE (journal 0225).

## Code comparison, 2026-10-25

Compared the mount parser, the build-work constants and the rename path in
`layer/main.c` and `layer/index.c` with the current-state documents.
`index_build_work` (1,024–65,536, default 32,768), the 16/64 admission
window, the ten-second deadline, `index_budget` (1 MiB–4 GiB, default
128 MiB), `index_idle` (0–86,400, default 60), `domain_depth` (0–8, default
1) and the `QUERY2` term and limit checks match the specification. A
directory rename sets `paths_stale` and `domain_fresh` rejects that domain,
so the next query rebuilds it.

`docs/architecture-overview.md` still said the UAPI was only version 1, omitted
`domain_depth` and `index_build_work`, and said a directory rename left false
negatives until the next mount. `docs/testing.md` omitted `index_idle` from
the mount line. Those now follow the parser. `docs/distribution-strategy.md`
remains the old proposal; a dated note points at the current contract instead
of rewriting it. `docs/adoption-risks.md` and `docs/spec-m3.md` already carry
dated notes and were left as history.

## Limitations and next step

The evidence remains limited to the recorded native and CI kernels,
filesystems and workloads. Issue 118 remains open for slow lower-device
and large single-project client evidence. This documentation
reconciliation does not change the module or the `index_build_work`
range.

*Merge addendum, 2026-10-25:* while the pull request for this unit was
open, PR 139 merged as `e57d8c5`. The conflict kept both this policy text
and the budget-release record (ADR-0021, journal 0192). This unit does
not retest that kernel change. The F14 advisory stays unpublished.
