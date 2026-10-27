# 0214 — First outside contributor: review and merge of pull requests 161 to 163

Date: 2026-10-27. Status: done. Work started about 15:40 America/Sao_Paulo, when the pull
requests arrived, and ended about 20:00; calendar time, not effort. Estimated
effort: S.

## Goal and decision

A first outside contributor opened three pull requests from a fork, each with its own journal
(0211, 0212, 0213) and a source review made with an AI assistant: harness input and path
hygiene (161), input checks in the Python library (162), and JSON escaping and `--uid`
privileges in the tools (163). The maintainer read each diff, ran what could run on macOS,
approved them with non-blocking suggestions and merged them in that order, resolving the
`CHANGELOG.md` and history index conflicts at merge (the three were based on `0f5850b`, before
pull request 160).

Follow-ups applied at merge, each with a dated note in the contributor's journal:

- 161: the `steps` filter of the manual KCSAN and arm64 KASAN runs lists the letters
  (`abcdefghijklmnopqrstuvwxyz`) instead of the range `a-z`, which depends on the locale's
  collation (macOS bash let `Ext4` through); a class such as `[:lower:]` was discarded because
  it admits `é` in a UTF-8 locale.
- 162: `_fill()` bounds the term payload right before the copy into the request's 1024-byte
  field, so the check no longer relies on `STRING_MAX == VALUE_MAX` nor on nobody replacing
  `term.value.payload` after the `Term` is built.
- 161 to 163: index lines and titles use the repository's `NNNN —` form.

The review status of the public surfaces changes: there is still no outside human review of the
kernel code, but a first outside contributor reviewed the scripts, tools and Python library.

## Changes

`README.md`, `README.pt-BR.md`, `docs/status.md`, `docs/status.pt-BR.md`, `CONTRIBUTING.md`,
`docs/help-wanted.md`, `docs/faq.md`, `docs/architecture-overview.md`, `docs/operations.md`,
`docs/when-not-to-use.md`, `site/index.html`, `site/guide.html`, `site/faq.html`: "no outside
human review" becomes "no outside human review of the kernel code", with the contributor's
review named where the surface describes the review state. `docs/retired-claims.txt` gains the
unqualified wordings. The changelog and the history index.

## What ran

- 161: the filter under bash 5 on macOS with `LC_ALL=C`, `en_US.UTF-8` and `pt_BR.UTF-8`:
  `ext4` and `buildrace release` accepted; `Ext4`, `ext4é`, `a-b`, `ext4' ;id #` and `ext4$(id)`
  refused. `scripts/test-install.sh` cannot run on macOS (it fails the same way on `main`). CI on
  `642101a`: every job passed, KASAN/lockdep included; merged as `a4ba19b`.
- 162: `python -m unittest` on macOS, Python 3.14: 32 tests OK, 17 skipped (Linux only); the new
  test fails with `ValueError not raised` against the `query.py` before the follow-up. CI on
  `e1f5ea0`: every job passed, the Linux tests and KASAN/lockdep included; merged as `b814b35`.
- 163: not built here (no Linux headers). CI on `2d959e2` (after the merge with `main`): every
  job passed, CodeQL and KASAN/lockdep included; merged as `546122d`.
- `scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.

## Recorded exception: one forced push

The follow-up commit of 161 was pushed without a `Signed-off-by` line and failed the DCO check.
With the maintainer's explicit authorization, that one commit, never merged and only on the
contributor's pull request branch, was amended with the sign-off and pushed with
`--force-with-lease` against its known hash (`6bdf605` replaced by `642101a`); the contributor's
commits and the merge with `main` were unchanged. The rule against forced pushes in `AGENTS.md`
stands.

## Limitations

The merges were reviewed by reading and by the CI; the contributor's Linux-only tests ran in CI,
not on a local VM. The KASAN job runs only for kernel-side paths (journal 0209).

## Next step

Update the discussion "Call for reviewers", which is not in the repository, with the same
review state.
