# 0262 — Existing scheduled fuzzing and benchmark artifacts verified

Date: 2026-11-03. Status: audit complete; no new runtime test or publication.
Started about 15:11 Brasília; verification completed 15:12:43, recorded afterwards.
Calendar time, not accumulated effort. Estimated effort: S. The maintainer asked to continue
useful local work while the existing corrected S18 campaign runs.

## Scope and identity

Audit actual logs, raw data and artifacts of the existing first scheduled libFuzzer run
and the latest weekly benchmark, previously known here only through their green API status.
No workflows were dispatched, no module loaded and no machine reservation changed.

- Fuzz run [41193336695](https://github.com/studiomoob/llmoobfs/actions/runs/41193336695):
  schedule, head fac2d4b083f6d814485e80994fb12e52126c63cb,2026-10-31
  07:34:54–08:20:11 Brasília. All three jobs and their execution steps completed successfully.
- Benchmark run [41454936109](https://github.com/studiomoob/llmoobfs/actions/runs/41454936109):
  schedule, head e59a244f1282994ed1a8cd21d53fb34dbe235f3c,2026-11-02
  10:39:57–10:49:23 Brasília. All three lower-filesystem jobs completed successfully.

Six ZIP artifacts were downloaded and their SHA256 digests matched the GitHub metadata.
All 376 extracted files matched the corresponding original ZIP entries. Hashes below identify
transported evidence, not a cryptographic proof that the build environment was trustworthy.

| Artifact | ZIP SHA256 |
|---|---|
| fuzz-attr | 70db1044ddfc3da929b5055b9e07ecd1c50e52fc511dc8f0188d326030ff0acd |
| fuzz-image | 8887212b1d6c60aad6c0e0fb0d92c08ae54945e42d32c65826c53e01d1bbbbaa |
| fuzz-tools | b096b043abd70d5376159a45a5c35a42864a729bd4b89e24348c8203643047a1 |
| bench-ondemand-ext4 | cb8894d01ecf12842db92a948af080247520b2c026ab7280b54b3ae8eeaa50e7 |
| bench-ondemand-btrfs | de2ef7ad9e92bd296a072b4ce8cd2f7b14fe14e7fac34b0ac457fb036d6f95bf |
| bench-ondemand-xfs | 4b60f429135a86d1097c79dae4258c8c28cea5e955eb0a1018d66824db60f9c9 |

## Scheduled libFuzzer evidence

Each final log contains DONE, execution statistics and 2701 seconds, meeting the 45-minute
budget. No sanitizer error in the logs or crash artifact was found. Retained post-merge corpus
files were counted independently, and all 316 SHA1 filenames match their contents. The final
fuzzer in-memory corpus count need not equal the subsequently merged retained corpus.

| Target | Executions | Seconds | Retained corpus files | Final edge coverage count |
|---|---:|---:|---:|---:|
| attr | 2,847,216,418 | 2701 | 128 | 113 |
| image | 16,305,980 | 2701 | 98 | 486 |
| tools | 518,560,179 | 2701 | 90 | 157 |

These are userspace targets, not syzkaller handler/KCOV percentages and not an additional
S18 campaign. This closes the unverified first-scheduled-run evidence gap of S7; it does
not prove absence of defects, sustained future operation or OSS-Fuzz enrollment.

## Weekly benchmark evidence

Each artifact records Debian 6.12.113 under KVM, the exact main commit, PHASE_DONE: 0 and
MOOBFS_BENCH_PASS. Raw console captures contain no matching BUG/WARNING/Oops/panic/KASAN/UBSAN
diagnostic. No assertion of CPU equivalence among hosted runners is made: the three jobs
report different physical CPU models. They are trend runs, not a controlled cross-lower
performance comparison, a new M4 reference, or the other-physical-hardware reproduction of issue 68.

For each lower, use the exact e59a244 bench/report.py on its six raw .rec files across three
repetitions. All five output rows match the archived JSON numerically; errors are zero.
Recreate ondemand-summary.py from the exact commit's reference times, actual times and
system data: exit0 and the generated table/body matches the archived Markdown.
Deterministic counts match: 3030 query results, 7459 indexed inodes for one domain and 60000
after the mount-wide query. The report flags 4/5 rows inconclusive for ext4, 2/5 for btrfs,
3/5 for XFS under its spread rule. Preserve those flags; do not claim statistically supported
speed differences from these three-repetition trend runs. Cold rows drop guest caches only;
the sparse disk image remains in the host's page cache.

## Documentation and limits

Update security-roadmap S7 and benchmark-plan to point to this verification. The site generator
and its bilingual status pages identify the first scheduled fuzz run as artifact-verified;
no benchmark marketing claim or release-readiness change is introduced. Historical journals
0237/0238 receive dated pointers. Remote GitHub issues, discussions, live Pages and external
posts remain at their published checkpoints because publication is not authorized. Detailed
raw receipts stay outside Git. This audit does not approve the ongoing corrected S18 round.

No new test, VM, monitor, workflow, PR, merge, tag, setting change or Azure action.

## Local document checks
Site generation, retired-claims, site links/structure and git diff whitespace checks passed
after these documentation updates. No kernel or runtime source changed.
