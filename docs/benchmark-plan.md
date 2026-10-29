# M4 benchmark plan

Status: proposal (2026-10-10). No number here was measured. M4 exists only to produce
numbers; until then every performance claim of the project is a hypothesis.

## What the benchmark must answer

1. Does the layer's `QUERY` answer attribute queries faster, or more cheaply, than an
   equivalent index maintained by the application over an ordinary filesystem?
2. What does the layer cost: additional latency per POSIX operation, mount time with the
   index scan, index memory, and from how many inodes does the default budget
   (128 MiB) stop being enough?
3. Does delegated physical sharing (reflink and dedup on btrfs and XFS) reduce space in real
   agent memory loads, and by how much?
4. Does a real agent memory client run unchanged over the layer at an acceptable cost?

Semantic search is not part of these questions: embeddings, ranking and synthesis stay in the client
(ADR-0005). Comparing `QUERY` with vector search would be comparing different operations.

## Systems compared

Storage baselines, functionally equivalent to what the layer does:

| Label | Configuration | Role |
| --- | --- | --- |
| A | ext4, files with xattrs, attribute index in SQLite maintained by the application | baseline without the layer, without reflink |
| B | btrfs, same | baseline without the layer, with the FS's own reflink and dedup |
| C | MoobFS over ext4, btrfs and XFS, native `QUERY` | the object of the experiment |

Real clients, used as load generators and as a compatibility test, not as
competitors of `QUERY`:

| Client | Why it is included | How it is included |
| --- | --- | --- |
| [ai-memory](https://github.com/akitaonrails/ai-memory) | Source of truth in files with front matter; already has a benchmark harness (LongMemEval) and a measured write ceiling (~700 observations/s); the closest to our model | `data_dir` over A, B and C; same captured session; compare page write latency, `reindex`, space |
| [mem0](https://github.com/mem0ai/mem0) | Pluggable storage backend | file backend over A, B and C |
| [Letta](https://github.com/letta-ai/letta) and [letta-code](https://github.com/letta-ai/letta-code) (memory in files, git integration) | Realistic form of agent memory as versioned files | memory directory over A, B and C; git intact |
| [Graphiti](https://github.com/getzep/graphiti), [cognee](https://github.com/topoteretes/cognee) | Facts with temporal validity and graphs; reference for the M3 attribute model | data model reference; load only if there is a file backend |
| [Agent VFS](https://github.com/johannesmichalke/agent-vfs) | User-space virtual filesystem with database persistence | interface reference; not a competitor of a kernel module |
| [Microsoft GraphRAG](https://github.com/microsoft/graphrag) | Knowledge pipeline; produces entities and relations | source of an attribute dataset (entities, relations) for `QUERY` loads; not storage |

PostgreSQL with pgvector is out of the `QUERY` comparison. It comes in, if at all, only in the
"whole semantic client" scenario over A, B and C, where the database is part of the client.

## Loads

Each load is a deterministic program with a fixed seed, versioned in the repository, that
writes the same bytes and the same attributes to A, B and C, and whose query results are
validated against the oracle (`tools/moobfs-index`) before any time measurement. A
truncated query does not count as complete.

| ID | Operation | What it measures |
| --- | --- | --- |
| W1 | creation: tmp + attributes + fsync + rename + fsync of the directory | creation latency, write amplification |
| W2 | read by known path | passthrough cost |
| W3 | structured query by 1 to 4 attributes (`QUERY` in C; SQL with equivalent indexes in A and B) | question 1; basis for deciding `QUERY_BATCH` |
| W4 | update and removal of attributes | validation and index |
| W5 | concurrent rename and removal with queries | locks and index consistency |
| W6 | mount with a cold index (page caches dropped) | scan time |
| W7 | remount and index rebuild with a warm cache | same, separating I/O from CPU |
| W8 | concurrent mixed loads (1, 10, 100 processes) | contention |
| W9 | fsync, durability and recovery (`dm-log-writes`, marks, replay) | integrity; no time metric |

Real client: the same ai-memory session captured and replayed over A, B and C.

## Scales and profiles

S1 1,000, S2 5,000, S3 10,000, S4 25,000, S5 40,000, S6 50 000 to 65 000 inodes, according to the
observed capacity; profiles of 3, 8 and 16 typed attributes per inode, with cardinality,
size and distribution of the values recorded. These numbers follow the current index budget
(128 MiB) and are not definitive limits.

Index saturation: using `GET_INFO` (`index_budget`, `index_used`, `index_entries`,
`index_inodes`, flags), measure `U = index_used / index_budget` at 50%, 70%, 85%, 95% and 100%,
recording real inodes, attributes, RAM, mount and rebuild time, p99 of W3,
and the behavior when the mount is refused for budget or the index becomes incomplete.
No index persistence is implemented before these numbers exist.

## Metrics

Per operation: p50, p95 and p99 latency and throughput. Per system: CPU, resident memory (including
`index_used` from `GET_INFO`), disk space before and after dedup, write amplification
(bytes written to the device per useful byte, via `dm-log-writes` or `/proc/diskstats`), mount
time, time until the index answers, integrity after a crash.

## Method

- Hardware, kernel, tool versions and images recorded in `environment.txt`.
- Deterministic dataset; 60 s warm-up; 180 s measurement; at least 7 repetitions;
  order of the baselines alternated between repetitions; raw results preserved; median and
  dispersion (p5–p95 across repetitions) reported; dispersion above 20% marks the result
  as inconclusive. Durations and repetitions are adjusted to the observed stability.
- Latencies separated by operation type; never a single distribution mixing
  read, write and query.
- Write amplification: `WA_logical = bytes gravados no dispositivo / bytes úteis
  alterados` and `WA_relative = bytes gravados com a camada / bytes gravados no baseline
  equivalente`, with journal, writeback, cache and flushes controlled and no other processes
  writing; any difference between C and its baseline is investigated at the source, since
  persistence is delegated to the lower filesystem.
- Mount and rebuild measured with a cold and a warm cache, with the conditions made explicit.
- Dedicated VM with KVM; emulation (TCG) is for correctness, not for numbers.
- No estimated number is published as measured; each table cites the commit, the command and the
  evidence file.

*Revision, 2026-10-19 (journal 0134), after a methodology review by a researcher in file-system
trace capture and replay.* The items above stay; these are added or replace them where they
conflict:

- A difference between two systems or two builds is reported only with a statistical test and
  a confidence interval: `bench/compare.py` gives, per workload, operation and percentile, the
  relative difference of the medians across repetitions, its 95 % bootstrap interval and the
  Mann-Whitney U p-value. The 20 % spread rule stays as a stability flag; it is not evidence of
  a difference, and a percentage difference of medians alone is not a result.
- At least 30 repetitions for a comparison that will be reported. The minimum of 7 stays for
  exploratory runs, which are labelled as such and claim no difference.
- Latency is shown as an empirical distribution (ECDF, `ecdf.csv` of `bench/compare.py`) per
  operation, not only as p50 and p99: latency has a tail that a summary hides.
- CPU frequency held still: `sudo sh bench/cpu-freq.sh fix` (governor `performance`, turbo off)
  before the run and `restore` after; `environment.txt` records the state, and a run where it
  could not be fixed says so.
- Warm-up discarded and caches dropped between repetitions where the scenario is cold: already
  in the method; every report states which applies.
- SQLite baselines: every SQLite number is stated together with the fact that SQLite performs
  no POSIX permission check, while the layer queries as an ordinary user with one. SQLite's
  `cache_size` and `mmap_size` are declared (the defaults so far) and varied: default, a cache
  that holds the whole database, and the database mapped with `mmap_size`.
- A number is generalized only after more than one machine and kernel.
- The strongest evidence is an end-to-end test of a real client with that client's own
  metrics. Micro and macro benchmarks, and trace replay, serve to catch regressions between
  versions. MoobFS has no production deployment; the staged test of the first client
  (`bench/client/`) is the closest.

*Addition, 2026-10-29 (journal 0238).* A weekly trend monitor: `.github/workflows/bench.yml`
runs `bench/ondemand.sh` (issue #68's steps, 60 000 memories of profile 8) in a disposable
Debian 13 VM under KVM on GitHub's `ubuntu-24.04` amd64 runner, once each on an ext4, a btrfs
and an XFS lower, and shows the run beside the Mac mini reference
(`bench/results/2026-10-13-macmini-ondemand/new/`). Reproduce it locally with
`MOOB_BENCH_FS=ext4 make bench-test` (Docker and `/dev/kvm`). Its numbers come from one run, on
a shared virtual machine whose hardware may change between runs, with the CPU frequency out of
reach: they show a trend or a large regression, they are not M4 results and they do not count
as the "more than one machine" above. A run on other physical hardware (issue #68) is still
wanted. No arm64 variant: arm64 runners have no KVM (journal 0124) and emulated timings mean
nothing (the item "Dedicated VM with KVM" above).

## Tools

Detailed design of the programs, the dataset, the SQLite schema of the baselines and the tests of the
harness itself: [benchmark-harness.md](benchmark-harness.md).

- Our own harness in C for the loads (deterministic, no dependencies), reusing
  `tests/query.c` and `tools/moobfs-index`.
- `fio` only to calibrate the VM device before the loads.
- `bench/compare.py` for the comparison of two systems (test, interval, ECDF) and
  `bench/cpu-freq.sh` to hold the CPU frequency (revision of 2026-10-19).
- `dm-log-writes` for write amplification and recovery; `duperemove` for dedup.
- `perf` and `/proc/lock_stat` (kernel with `LOCK_STAT`) to locate bottlenecks.
- The ai-memory benchmark harness (`evals/`) for the real-client scenario.

## M4 exit criterion

A report in `docs/history/` with the four questions answered by numbers, including the
cases in which the layer loses. If the layer shows no measurable advantage in any of the
four, that is a valid result and changes the course of the project, as per ADR-0007.
