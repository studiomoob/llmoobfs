# 0313 — Issue #68: steady-state `QUERY2` is 50 to 68 % slower than module 0.3.0; two merges

Date: 2026-11-19. Work started at about 12:40 Brasília time; the measurements recorded here
ended at 12:51 (calendar time). Status: **in progress; batch 45, branch
`claudiofp/b45-query-cost`, local.** The cause is located; the fixes are not written.
Work done by Claude Code (an AI assistant) for the maintainer.

## Goal

Issue #68 asked for the on-demand benchmark on other hardware. On 2026-11-19 Marcelo Jorge
Vieira, who asked publicly to be credited by name (journal 0306), ran it on a second machine
([comment](https://github.com/studiomoob/llmoobfs/issues/68#issuecomment-7010948901)): on his
host, `main` at `d9ee667` answered steady-state `QUERY2` 15 to 76 % slower than module 0.3.0
(`99302cb`, journal 0098) with the same data, the same `bench/load.c` and the same ioctl
structure sizes. His cold numbers are about his host; this difference is about the module.
This journal reproduces it on the test host and finds where it comes from.

## Method

The method of journal 0139 (`docs/benchmark-plan.md`, revision of 2026-10-19), with its
driver (`bench/results/2026-10-19-macmini-steady/driver-bisect.sh`, kept here as `driver.sh`
with its directory changed and the syzkaller guard removed): the test host (Mac mini, Debian 13,
6.12.111 on bare metal), the data of `2026-10-13-macmini-ondemand` (domain `ns3`, 7 459 files,
54 472 entries), CPU frequency held by `bench/cpu-freq.sh`, a fresh `insmod` and mount per
module and repetition, one discarded warm-up pass, module order rotated each repetition,
user-space tools of `d9ee667` for every module, `bench/compare.py` (median of per-repetition
p50, 95 % bootstrap interval, Mann-Whitney U). Shapes: `v2-dom` Q1 two equalities (253
results), Q2 one equality (253), Q3 one equality (3 030); `v2-recent` Q1 and Q2 the newest 20
of one kind and of one agent, as uid 1001.

## Results

**Confirmation, 12 repetitions** (`confirm/`), p50 medians in µs, against `99302cb`:

| Shape | 0.3.0 `99302cb` | `main` `d9ee667` | batch 44 (`9851dd8`) |
| --- | --- | --- | --- |
| v2-dom Q1 | 181.0 | 267.6 (+47.8 %, +44.4..+53.7) | 270.5 (+49.4 %) |
| v2-dom Q2 | 121.9 | 194.9 (+59.9 %, +54.8..+64.1) | 194.8 (+59.8 %) |
| v2-dom Q3 | 1 382.0 | 2 292.2 (+65.9 %, +60.9..+70.8) | 2 342.4 (+69.5 %) |
| v2-recent Q1 | 28.3 | 37.2 (+31.6 %) | 38.8 (+37.4 %) |
| v2-recent Q2 | 142.5 | 146.6 (+2.9 %) | 149.0 (+4.6 %) |

Every row "differs" (fewer than 30 repetitions, flagged). The reproducer's sizes of the
difference (+38 to +76 %) are of the same order.

**Every merge that touched `layer/` since `99302cb`, 6 repetitions each** (`merges/`), in one
run with the order rotated: the module of each merge is within ±3 % of 0.3.0 up to `0d37836`,
except `6502d46` (#71) to `2808f5c` (#97), the regression of journal 0139 fixed by `a003c0c`
(#101). Two steps remain:

| Merge | v2-dom Q1 / Q2 / Q3 | v2-recent Q1 |
| --- | --- | --- |
| `0d37836` (#127) | +0 / +0 / +1 % | -0 % |
| `40645e2` (#129, issue 116, journal 0180) | +36 / +53 / +52 % | +26 % |
| `fc589c8` (#197) | +37 / +54 / +53 % | +26 % |
| `1f9dd80` (#204, ADR-0038, issue #117) | +51 / +68 / +69 % | +32 % |
| `d9ee667` (#216) | +51 / +67 / +67 % | +34 % |

## Cause, from the diffs

- `40645e2`: the memcg policy of journal 0180 made the module's allocations
  `GFP_KERNEL_ACCOUNT`, including those a query makes for each result and frees before it
  returns (`add_candidate()`: `kstrdup()` of the path, `kmalloc()` of the value). An accounted
  allocation is charged to the caller's memory cgroup and comes from the separate `kmalloc-cg`
  caches. The added cost is about 250 ns a result (+62 µs on 253 results).
- `1f9dd80`: ADR-0038 removed the record's pointer to the layer inode (issue #117), and
  `result_path()` now finds the inode by number in the inode cache (`moobl_find_cached_inode()`)
  for every result, then puts it; about 75 ns a result.
- Batch 44's R7 check (a prefix comparison per result, one `dentry_path_raw()` per query) adds
  1 to 4 %.

These are inferences from the diffs and the per-result arithmetic; no profile was taken.

## Next step

Two corrections, each measured against `99302cb` and `main` with 30 repetitions: (1) the
per-result transient allocations gathered in one accounted buffer per chunk; (2) the recorded
path used when the domain's names are fresh, the inode cache only when they are not, if every
rename path keeps the recorded path current (to be checked; an ADR, since it changes where a
result's name comes from). The reply on issue #68, drafted outside the repository and posted by the maintainer
([comment](https://github.com/studiomoob/llmoobfs/issues/68#issuecomment-7018502478)). README, FAQ and `docs/when-not-to-use.md` state the 1 to 3 % residual of journal
0143 as the current difference: updated in this batch.
