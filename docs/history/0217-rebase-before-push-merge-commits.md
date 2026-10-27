# 0217 — Rebase before pushing; pull requests enter with a merge commit

Date: 2026-10-27. Status: done. Work started and ended on 2026-10-27, in the evening
(America/Sao_Paulo); calendar time, not effort. Estimated effort: S.

## Goal and decision

The merges of pull requests 161 to 163 (journal 0214) merged `main` into each contributor's
branch to satisfy the protection of `main`, which requires a branch to be up to date before it
merges. A friend of the maintainer suggested turning merge commits off on GitHub for a linear
history. The maintainer chose instead the practice the kernel documents for maintainers:

- Pull requests keep entering `main` with a merge commit: it records that a batch went in
  together, and one revert undoes it while its small commits stay intact.
- A branch is rebased on `main` before it is pushed, and `main` is not merged into a topic
  branch.
- When `main` moves while one of our own pull requests is open and unmerged, the branch is
  rebased again and pushed with `--force-with-lease`. This is the only forced push allowed; the
  ban on forcing `main`, merged history and a contributor's branch stays.
- For a contributor's pull request, the contributor is asked to rebase. If they cannot, merging
  `main` into their branch is the exception and is recorded in the journal of the merge.

Alternatives not taken: rebasing only before the first push and merging `main` afterwards (keeps
the merges back that the change removes); turning off the up-to-date requirement of `main`
(the combination of a pull request with the new `main` would only be tested after the merge);
turning merge commits off (loses the batch boundary and the single revert).

## Changes

`AGENTS.md` (the force-push rule narrowed to what it protects, and the new rule),
`CONTRIBUTING.md` (rebase rather than merging `main` into a branch; merges use a merge commit),
`docs/references.md` §15, the changelog and the history index.

## What ran

Documentation only; `scripts/check-retired-claims.sh`.

## Next step

Apply it from the next pull request on. The security fixes in progress are already on branches
based on `main` and will be rebased before they are pushed for review.
