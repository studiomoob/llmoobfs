# 0313 — Issue #68: steady-state `QUERY2` is 50 to 68 % slower than module 0.3.0; two merges

Date: 2026-11-19. Work started at about 12:40 Brasília time and ended at 17:30 (calendar time,
most of it machine time). Status: **done; batch 45, branch `claudiofp/b45-query-cost`.** Estimated
effort S (a correction, ADR-0042); actual: one afternoon.
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

## Fixes

1. `9535fb0`, `add_candidate()`: the names and keys of a chunk's candidates come from one
   accounted buffer per query (`struct moobl_arena`, 128 bytes a candidate of the first chunk),
   reset after each chunk; what does not fit is `kmalloc`'ed as before. The memcg policy of
   journal 0180 holds: the buffer is accounted to the caller.
2. `a8695c9`, `result_path()`: a result in a domain is named by its recorded path when the
   domain's names are fresh (`paths_fresh()`), without the inode-cache lookup. A file in a domain
   has one name (`target_ok()` leaves linked files out), and every write path that names or
   renames it updates the record under the index lock, `moobl_index_renamed()` before the VFS
   moves the dentry. Outside the domains (a mount with `domain_depth=0`, or files above the
   domain depth) the lookup stays first, so of two hard links the name in memory is still
   given (step `dcname`). A dated note in ADR-0038 records it; no new ADR, since what a
   result is named does not change where a file has one name.
3. `8cea6af`: an error of fix 2, found by the full suite (`query-after-remount-ext4`): it had
   dropped the fall-back to the recorded name when the inode is not in memory, so outside the
   domains, right after a remount, every result of the mount-wide `QUERY` lost its name and its
   per-result check. Restored.

## Measurements of the fixes

Final, 30 repetitions (`final/`), the method above, p50 medians in µs against `99302cb`
(95 % interval):

| Shape | 0.3.0 `99302cb` | `main` `d9ee667` | batch 45 (`ae16b2c`, = `8cea6af`) |
| --- | --- | --- | --- |
| v2-dom Q1 | 179.1 | 269.7 (+50.6 %, +49.4..+51.2) | 153.8 (-14.2 %, -14.7..-13.5) |
| v2-dom Q2 | 117.8 | 196.2 (+66.5 %, +61.4..+69.4) | 95.0 (-19.4 %, -20.4..-18.0) |
| v2-dom Q3 | 1 363.0 | 2 303.7 (+69.0 %, +63.2..+71.1) | 1 020.7 (-25.1 %, -27.7..-24.2) |
| v2-recent Q1 | 28.2 | 37.4 (+32.4 %) | 28.5 (+0.9 %, +0.2..+1.7) |
| v2-recent Q2 | 144.2 | 146.8 (+1.8 %) | 135.8 (-5.8 %, -6.7..-5.2) |

Every row "differs" (p < 0.05, interval excludes 0). Per-repetition p99, batch 45 against
0.3.0: v2-dom Q1 +38.4 % (+37.7..+40.9), Q2 -19.3 %, Q3 -17.6 %, v2-recent Q1 +7.9 %, Q2
+11.2 %: the tail of the two-equality shape and of the ordered shapes stays above 0.3.0, as
journal 0143 found against module 0102.

Steps, 12 repetitions: fix 1 alone brought the `v2-dom` shapes to +0.5, -5.3 and -6.8 % of
0.3.0; fix 2 then took 13.8, 14.2 and 18.7 % more off fix 1. So fix 1 recovered more than the
first step's share and fix 2 more than the second's: the estimates per result in "Cause" were
inferences, and these are the measurements. Batch 44 (`9851dd8`) measured 1 to 5 % above `main`
before the fixes; its per-result prefix check stays.

## Checks

On the test host, at `ae16b2c` (the tree of `8cea6af` before the rebase on `ef63b26`):
`make checkpatch`; full `MOOB_VM_KVM=1 make vm-test`, `MOOBFS_VM_PASS`; full
`vm/kasan-local.sh`, `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with no
kernel diagnostics` (fix 1 changes the lifetime of the candidates' copies);
`vm/selinux-local.sh`, `fsuse differ=0`. `make lint` passed at `d26dd24` (fix 2 before its
correction, which changed only `layer/index.c`). The first full run, at `d26dd24`, failed with
`query-after-remount-ext4` (fix 3).

## Limitations

- One host and one data set, as journal 0139. The reproducer's host is not remeasured.
- The p99 of three shapes stays above 0.3.0.
- `docs/benchmark-plan.md` asks for 30 repetitions; the merge survey used 6 and the
  intermediate rounds 12, flagged.

## Next step

The pull request of batch 45 with the maintainer's agreement, and the numbers posted on issue
#68. The reply on issue #68, drafted outside the repository and posted by the maintainer
([comment](https://github.com/studiomoob/llmoobfs/issues/68#issuecomment-7018502478)). README (both languages), `docs/faq.md`, the site's FAQ and `docs/when-not-to-use.md`
stated the 1 to 3 % residual of journal 0143 as the current difference: updated in this batch.
