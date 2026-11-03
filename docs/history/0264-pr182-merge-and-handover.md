# 0264 — PR 182 merged; CI rerun of a probabilistic library test; coordination handover

Date: 2026-11-03. Status: merged; all 13 post-merge checks passed.
Started about 18:00 and ended 20:20 Brasilia (calendar time, not measured effort).
Estimated effort: S. Continues journals 0259 and 0263.

## Coordination handover

The agent session that coordinated batch 11 since 2026-10-31 stopped being able to process
turns on 2026-11-03 at 15:37 Brasilia: each request returned a provider content filter for
cybersecurity before any action. Its rollout shows no command after 15:20, so no step was
left half done. A new agent session took over at about 18:00 from the recorded state (local
journals, coordination files and the remote hosts), repointed the local notifier and kept
its send receipts, so no notification was repeated and no test was duplicated.

## Pull request 182

The maintainer authorized pushing claudiofp/b11-nightly-followup and opening one pull
request after the full suites of journal 0263 passed; https://github.com/studiomoob/llmoobfs/pull/182
was opened with head 2ad7412. The first CI run had 18 of 19 checks passing. "Tools and
sanitizers" failed in `make pylib-test`: python/tests/test_linux.py
`test_scan_under_a_racing_swap` raised "no walk went through". The pull request does not
change python/. In that test the security assertions (no rename error, more than 100
swaps, no answer naming the private file) passed; only the last assertion failed. It
requires at least one of 2,000 descriptor walks, run while another thread exchanges the
directory and a symbolic link with renameat2(RENAME_EXCHANGE) without pause, to find the
file. The walk refuses links by design (journal 0231), so a run in which the swapper wins
every race is valid behavior, and the assertion is probabilistic. In the 40 previous CI
runs of the workflow this test never failed. Only the failed job was rerun, unchanged; it
passed and all 19 checks were green on 2ad7412.

The maintainer then authorized the merge. It was made with a merge commit, a1f313b, at
20:19:41 Brasilia; parents e59a244 (main) and 2ad7412 (validated head). All 13 checks of the
merge commit (both native VMs, tools and sanitizers including the library test above, static
analysis, compiler warnings, lint, CodeQL, Scorecard, site build and deploy) passed by 20:36.

## Limits and next steps

- Done the same evening: test_scan_under_a_racing_swap keeps its assertions during the race
  (no rename error, more than 100 swaps, no answer naming the private file) and checks
  liveness after the race instead. With an odd number of exchanges it swaps back once; then a
  walk must find notes/x and still no private file. On the Mac mini (Debian 13) `make
  pylib-test` passed 5 of 5 runs and the test alone 30 of 30 (not skipped). Not yet run in CI.
- S18 stays at 0 of 7 accepted rounds; round r01 with the correction is running.
- S20 remains partial (journal 0261).
