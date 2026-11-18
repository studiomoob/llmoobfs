# ADR-0043 — `E2BIG` only for a build that met no change: an allowance spent on rescans or another attempt is `EAGAIN`

Date: 2026-11-18. Status: **accepted** by the maintainer on 2026-11-18 (journal 0310). Amends
decision 3 of [ADR-0019](0019-bound-query-build-work-and-frequency.md) on the errno of an
exhausted work allowance; the allowance itself, its size, what it counts and its cumulative
accounting (decision 2) are unchanged. It is a correction under
[ADR-0042](0042-no-human-review-gate-and-kernel-features-one-at-a-time.md)'s rules, not a
feature: it changes no structure, no command and no success meaning. Estimated effort: S
(hours). Estimate, not labor time.

## Context

- ADR-0019 gives each domain build one allowance of `index_build_work` units (32 768 by default)
  across its first walk, the rescans of directories renamed while it ran (up to 64 rounds) and
  its further attempts (up to four), and returns `E2BIG` when it runs out. `E2BIG` is documented
  as "the domain is too large": `docs/operations.md` says to raise `index_build_work` or the
  `domain_depth`, and the `moobfs` tool says the same.
- Finding 1 of the first independent reproduction (issue #195, journal 0306): `tests/buildrace.c`
  got `E2BIG` on a 60-file domain on Debian 7.3.3 and 7.3.4 under KVM, in 13 of 13 runs on the
  reproducer's host.
- Journal 0310 reproduced it on the test host with the smallest allowance (1 024): every domain
  first built with no writer inside it, then the same domains, on a new mount, under the writers
  of `tests/buildrace.c`. On 7.3.4 one domain was refused with `E2BIG` in each of six runs, in
  its first attempt (`tries=1`). Temporary counters showed the first walk cost 110 to 123 units;
  14 to 17 rounds of rescans of 227 to 233 renamed directories spent the rest. The domain fitted;
  the writers' renames were what the allowance could not pay for.
- `EAGAIN` already reports a build overtaken by changes (a file read 16 times, renames for 64
  rounds, eight attempts to start), and clients retry it. The test of ADR-0019's cumulative
  accounting, `tests/query-build-moved.c`, asserted `E2BIG` for exactly this case: 9 000 names
  that fit one walk, then a rename that makes the build scan them again.

## Decision

1. A build returns `E2BIG` only when its allowance runs out in the first walk of its first
   attempt, with no rescan before it: the domain, as it was, does not fit `index_build_work`.
2. When the allowance runs out after the build rescanned a renamed directory, or in a later
   attempt (after a restart or a release over `index_budget`), the build returns `EAGAIN`.
3. The allowance stays one per build, cumulative, as ADR-0019 decision 2 set. The work a single
   build can do is bounded exactly as before; a caller that retries pays the admission of
   ADR-0019 decision 1 (`EDQUOT`) as before.
4. `tests/query-build-moved.c` asserts `EAGAIN`; it still shows the sharing, since with a
   separate allowance per walk the query would succeed. The `buildrace` step gains the
   small-allowance check of journal 0310.

## Alternatives

- **One allowance per walk, or per attempt.** Rejected: it multiplies the bound by up to
  4 × 65 walks, which ADR-0019 refused on purpose.
- **Not counting rescans.** Rejected for the same reason: rescans are work a writer makes the
  build do, and the bound must cover them.
- **A new errno for "allowance spent on changes".** Rejected: `EAGAIN` already means "the domain
  kept changing, retry", and every client handles it.
- **Merging duplicate entries of the renamed-directory list** (the same directory renamed back
  and forth is rescanned once per rename) would lower the cost, but not change what the errno
  says. Left as a possible later improvement.

## Consequences

- A domain that fits is no longer reported as too large because it was written during its
  first query; such a query gets `EAGAIN`, which the documentation already tells callers to
  retry.
- A domain that does not fit and is being renamed meanwhile may get `EAGAIN` first, then
  `E2BIG` on a quiet attempt. The `E2BIG` advice (raise `index_build_work` or `domain_depth`)
  stays correct whenever it is given.
- Compatibility: a valid request that returned `E2BIG` in this case now returns `EAGAIN`. No
  structure, command or success meaning changes. `docs/spec-query-v2.md`,
  `docs/compatibility.md`, `docs/operations.md` and `docs/tools.md` say so.
