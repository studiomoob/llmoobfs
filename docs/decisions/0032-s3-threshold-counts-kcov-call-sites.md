# ADR-0032 — S3's coverage threshold counts the handlers' KCOV call sites

Date: 2026-10-31. Status: **accepted** by the maintainer on 2026-10-31 (journal 0245). Amends the
S3 threshold set on 2026-10-15 (journal 0109, [security roadmap](../security-roadmap.md) S3) only
in what its denominator counts; the 85 % value, the handler set and every other condition of S3's
"done when" stay. Estimated effort: S (hours). Estimate, not labor time.

## Context

S3 asks that the ioctl and attribute handlers reach 85 % of their PCs, as counted by the
syzkaller manager's per-function coverage. On the hardened KASAN kernel of S8 (journal 0237) the
manager counts 809 PCs for the 23 handler functions. Journal 0239 listed the module's
`__sanitizer_cov_trace_pc` call sites in `.text` with `objdump -dr` and found 781: for 21 of the
23 functions both counts agree, while for `moobl_xattr_get` and `moobl_xattr_set` the manager
counts 14 PCs more than each has call sites (18 against 4, 64 against 50). A PC that is not a
KCOV call site is never reported by KCOV, so no program can cover it: 28 of the 809 PCs are out
of reach by construction. Why the manager counts them was not established (journal 0239).

With them in the denominator the seventh run stood at 687 of 809 (84.9 %), one PC short. On the
call sites, a per-call KCOV replay of the corpus the seventh run started from (the sixth run's
corpus plus its three seeds) reached 688 of 781 (88.1 %).

On 2026-10-30 the maintainer kept the manager's count for the seventh run, so that the metric
would not be changed to pass after a near miss. After the seventh run showed that the gap comes
from PCs that cannot be covered, the maintainer accepted this correction on 2026-10-31.

## Decision

1. S3's handler coverage is measured on the KCOV call sites (`__sanitizer_cov_trace_pc` calls) of
   the handler functions' `.text`, as listed by `objdump -dr` of the module built for the run.
   The `.cold` parts stay out, as before.
2. The covered call sites are those that a per-call KCOV replay of the run's corpus reports, in
   a disposable guest of the run's kernel (journal 0239's method). The manager's own percentage
   is still recorded beside it.
3. The value stays 85 %, and the other conditions of S3 stay: no open report, every reachable
   handler function exercised, each function left out listed with its reason, reproducers of
   past crashes kept in `tests/`.

## Consequences

- S3's numeric condition is met on the measurement of journal 0239 (688 of 781, 88.1 %). With
  no open report and every handler function exercised, S3 is done.
- The replay script used on 2026-10-30 ran on the dedicated host only. Committing it, so that the
  next run's figure is reproducible from the repository, is follow-up work (S) before that run.
- A later change of the module or of the kernel configuration that changes the number of call
  sites changes the denominator; each run records both counts.

## Alternatives considered

- **Keep the manager's count and run an eighth round.** Rejected: 28 PCs that cannot be covered
  stay in the denominator, so the threshold measures them, not the fuzzer.
- **Lower the threshold.** Rejected: the value is not the problem.
- **Leave S3 open.** Rejected: the open condition would be an artefact of the counting.
