# 0267 — PR 183 merged; post-merge checks; S21 done

Date: 2026-11-04. Status: merged; all post-merge checks passed; S21 done.
Started about 13:30 (pre-push checks) and ended 15:25 Brasília (calendar time, not measured
effort; most of it waited on CI). Estimated effort: S. Continues journals 0266 and 0248.

## Pull request 183

After the full native and KASAN/UBSAN/lockdep suites of the corrections passed and the
pre-push checks of journal 0266 passed on `24a04ce`, the maintainer authorized pushing
`claudiofp/b12-records` and opening one pull request:
https://github.com/studiomoob/llmoobfs/pull/183, head `8a1946f`, opened at about 13:41.
`main` had not moved since `a1f313b`, so no rebase was needed. GitHub reported a partial
outage of Actions at the time; the checks started at 13:44 without a new push.

All 20 checks passed by 14:20, among them the native Linux VM jobs, the layer suite under
KASAN/lockdep, the four mutant shards with the unmutated baseline, the kernel static analysis
(sparse, smatch, Coccinelle, clang analyzer, W=1, kernel-doc), CodeQL and the four compiler
jobs. The first GitHub run of `tla.yml` (run 41870141114) checked every configuration with
the pinned TLC: `IndexLoad`, `IndexLoadSafety` and `IndexLoadNoLinks` found no violation, and
the five negative controls (`IndexLoadNoRetry`, `IndexLoadPre0152`, `IndexLoadPreR22`,
`IndexLoadPreF1`, `IndexLoadPreF2`) each found one, as required.

With the maintainer's approval, the pull request was merged with a merge commit at 15:06:
`d6ce35e`.

## Post-merge checks

On `d6ce35e`, CI (41883814192), TLA+ model (41883814365), Static analysis (41883814145),
CodeQL (41883814313) and OpenSSF Scorecard (41883814151) all passed by 15:17.

## S21

The acceptance of S21 is met: TLC, pinned and run in CI, finds no violation in the
configuration of the code as it is, and finds one when the retry is removed. The two defects
it found were corrected in the same batch (journal 0266). S21 is done.

## Limitations

- The model's bounds are those of `formal/README.md` (one file in CI; one two-file run
  outside CI).
- S18 stands at 0 of 7 on the corrected code. Campaign c01 runs on `9dce137`, which has the
  same tree as the merged code except for later records (journal 0248).

## Next steps

Audit r05 (code before the correction, extra evidence) when it ends at about 21:30, then
c01 and c02, the first campaigns that count.
