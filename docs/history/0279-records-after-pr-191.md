# 0279 — Records after PR 191; the local KASAN harness reads the 7.3 pin (in progress)

Date: started 2026-11-09, 12:32 Brasília time. Status: **in progress**: the batch's full suites
are running on the test host. Estimated effort S (hours); times below are calendar time.

## Goal

Close the records left open by the merges of 2026-11-09 and remove one limitation of journal 0277,
in the batch of branch `claudiofp/b19-gap-e` with journal 0278.

## Merge of PR 191

With the maintainer's agreement of 2026-11-09, PR 191 (the register of review findings and the
tests of journal 0276, branch `claudiofp/b18-stage2-docs`) was rebased on `main` after PR 190;
its tree was the one its pre-push checks had passed (12 of 12, 10:34–10:39 BRT, `5d879b0`). It
was opened at 11:21 BRT, passed 17 of 17 checks and was merged at 12:32 BRT (`9a6dd20`).
POST_MERGE_191

## Changes

- `vm/kasan-local.sh`: `MOOB_KASAN_KERNEL=7.3` reads the workflow's 7.3 pin (`KERNEL73_VERSION`,
  `KERNEL73_SHA256`) instead of the 6.12 one, and the tarball URL takes `v6.x` or `v7.x` from the
  version; any other value is refused. The default is unchanged. Journal 0275 had run 7.3.3 with
  an edited copy of the script (journal 0277, Limitations, with a dated note).
  `docs/development.md`'s pin table says so.
- Journals 0272, 0275 and 0277 are closed with the merge of PR 190 (journal 0277, "Merge of PR
  190"); the index and the changelog follow.
- `docs/review-findings.md`: finding V-c names step `dirstall` (journal 0278) in its row and in
  Gaps item 4.

## The batch's full suites

SUITES_RESULT

## Limitations

- `MOOB_KASAN_KERNEL=7.3` keeps its kernel in the cache given by `KCACHE`; with the default cache
  both kernels live side by side under `~/.cache/moobfs-kasan` (`linux-6.12.116`,
  `linux-7.3.3`), each rebuilt when `vm/kasan.config` changes.

## Next step

The batch's pull request (journals 0278 and 0279), which the maintainer agreed to on 2026-11-09
if the full suites and the pre-push checks pass; the merge waits for the maintainer.
