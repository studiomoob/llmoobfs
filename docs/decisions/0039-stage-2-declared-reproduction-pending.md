# ADR-0039 — Stage 2 is declared with the third-party reproduction pending; the tag is the reproduction pin

Date: 2026-11-15. Status: **accepted** by the maintainer on 2026-11-15, about 09:50 Brasília
time (journal 0297). Amends the "Done when" line of stage 2 in
[distribution-strategy.md](../distribution-strategy.md) §8 and decisions 2 and 3 of
[journal 0284](../history/0284-release-workflow-and-part-a-gaps.md); the text of both is kept.
Estimated effort: S (hours).
*Note, 2026-11-18 (journal 0306):* the reproduction came: Parts A, B and C of issue #195's route
at `v0.6.1-experimental`, by an outside reproducer on his own KVM host. The maintainer counts it as
the reproduction this decision left pending; its findings are in journal 0306. Estimate, not labor time.

## Context

Stage 2 (technical preview) is done when "a third party reproduces everything, the matrix and the
limitations fit on one page and no leak is known". By 2026-11-15:

- Every deliverable and acceptance step of stage 2 is met on `main`, with the amendments of
  ADR-0030 and journals 0283, 0284, 0286 and 0289. The last open condition the project could
  act on, the remainder of issue #117, was removed in the kernel by
  [ADR-0038](0038-index-keeps-no-inode-pointer.md) (journal 0295, merged as `1f9dd80`).
- The matrix and the limitations are on one page ([matrix-and-limits.md](../matrix-and-limits.md),
  checked in CI). No leak is known (journal 0283).
- "A third party reproduces everything" means Parts A and B of the route of issue #195 (journal
  0284, decision 2). The call has been open since 2026-11-11, and no one has reproduced it yet.
  The project cannot meet this condition by its own work, and nothing says when someone will.

Journal 0284 also tied two things to the declaration: the tag `v0.6.0-experimental` is cut only
when stage 2 is declared, and the `.deb` packages stay unpublished until then. Its decision 3
counts a reproduction at `d96b0db` for the tagged commit if nothing under `layer/` changes in
between. `layer/` changed on 2026-11-12 (ADR-0037) and on 2026-11-14 (ADR-0038). The tag also
changes the module version string in `layer/main.c` (`MODULE_VERSION`), from which the DKMS
package takes its version, as the tag `v0.5.0-experimental` did (journal 0226).

## Decision

1. Stage 2 is declared on 2026-11-15. The third-party reproduction is no longer a condition of
   the declaration. It is a known open item of the technical preview, stated wherever the stage
   is stated, until a third party reports a reproduction.
2. Issue #195 stays open, and its route is pinned to the tag `v0.6.0-experimental` once the tag
   exists. A reproduction at that tag is a reproduction of the declared stage. Decision 3 of
   journal 0284 (a reproduction at `d96b0db`) is replaced.
3. Setting `MODULE_VERSION` to `"0.6.0"` is a release step and not a kernel change under the
   freeze ([ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md)): it changes only
   the version string that `modinfo` and the DKMS package show.
4. The release steps follow journal 0284 and each needs the maintainer's agreement:
   - the tag `v0.6.0-experimental` on the merge commit of the batch that carries this decision;
   - the release workflow, whose draft-first form was rehearsed in a fork (journal 0288);
   - the `.deb` packages, which journal 0284 kept unpublished until the declaration.

## Alternatives

- **Keep waiting for a third party.** The text of §8 is met to the letter, but the stage stays
  open for an unknown time on a condition the project cannot meet. The packages and the tag wait
  with it. Not taken.
- **Keep the pin at `d96b0db` or at `1f9dd80` with an exception for the version string.** Either
  keeps a commit that is not the published one. A reproducer would test code that differs from
  the tag, and the exception would need to be explained. Not taken.

## Consequences

- The technical preview is published without independent reproduction. Every surface that
  states the stage says so, and `docs/retired-claims.txt` keeps the old wording ("stage 2 is not
  reached").
- A failed reproduction reported in #195 is a finding against the tag, handled like any other.
- Stage 3 (public release) is unchanged. It still requires M4 published, the security roadmap
  items and a hand-read human review.
