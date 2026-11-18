# 0310 — Finding 1 of issue #195: renames spent the build allowance, and the build said "too large"

Date: 2026-11-18. Work started at 16:30 Brasília time and ended at 19:20. Status: **done**: the
cause is shown, the fix passes the steps it touches, the full suite and the full KASAN/lockdep
suite; ADR-0043 is accepted. Estimated effort M
(journal 0306); the time recorded is calendar time.

## Goal

Explain finding 1 of the first independent reproduction (journal 0306): `tests/buildrace.c` got
`E2BIG` on a 60-file domain on Debian 7.3.3 and 7.3.4 under KVM on the reproducer's host, and fix
it under the kernel-correction rules.

## A test that reproduces it on the test host

The test host does not reproduce it with the default allowance (journal 0306). The `buildrace`
step of `vm/suite.sh` gains, on each lower filesystem, a run under the smallest allowance,
`index_build_work=1024`: every domain is first built with no writer inside it, on its own mount,
which shows it fits; the same domains, on a new mount, are then built under the writers of
`tests/buildrace.c` (`scan_delay_us=40`, as the step's first run). `buildrace` retries only
`EAGAIN`, so an `E2BIG` fails the step (`br-small-budget-refused-a-domain-that-fits-<fs>`). The
step prints the builds the tracepoint `moobfs_domain_build` saw and each refused one.

Negative round, on `main` (`b2b84d9`) plus the test, `MOOB_DEBIAN_KERNEL=7.3.4 MOOB_VM_KVM=1
MOOB_ONLY=buildrace make vm-test` on the test host: four runs, four failures on ext4, each a
single domain refused with `err=-7 tries=1` (r005 twice, r017 twice), after 6 to 18 builds; btrfs
and XFS were not reached. The same step on the default kernel (6.12.111) passed on all three
filesystems (24 builds each, none refused).

## The cause

Two runs of a build with temporary counters (a local branch, not kept) printed, at each `E2BIG`:
the first walk cost 110 and 123 units; then 17 and 14 rounds of the renamed-directory list
rescanned 233 and 227 directories, and that spent the rest of the 1 024. The writers rename
`s`, `t` and `u` back and forth; each rename puts the directory on the list again
(`dom->moved`), and each round rescans what is on it. The work counter is one for the whole
build (ADR-0019 decision 2, `scan_job_init()`), so the rescans ran it out before the 64-round
limit, which would have returned `EAGAIN`, was reached. So the hypothesis of journal 0306 holds
in this form: not the restarts (`tries=1`) nor the rereadings of a file (they are not charged),
but the rescans of renamed directories within the first attempt.

Why 7.3 and not 6.12: not established. On the test host, under the same writers, 6.12 built
every domain within 1 024 units; the reproducer's host runs guests about three times faster,
which on 7.3 is enough to reach 32 768. Recorded as not explained.

## Decision: ADR-0043, accepted by the maintainer on 2026-11-18

ADR-0019 decision 3 made an exhausted allowance `E2BIG` in every case, and
`tests/query-build-moved.c` asserted it for a domain that fits one walk and is rescanned after a
rename. `E2BIG` is documented as "the domain is too large" (raise `index_build_work` or
`domain_depth`), which is wrong advice there. ADR-0043 keeps the allowance as it is and changes
only the errno: `E2BIG` when the allowance runs out in the first walk of the first attempt,
`EAGAIN` when it runs out after a rescan or in a later attempt. Alternatives (one allowance per
walk or per attempt, not counting rescans, a new errno, merging duplicate entries of the list)
are in the ADR.

## Changes

- `layer/index.c`, `domain_ensure_dom()`: a flag set when a rescan of the renamed-directory list
  starts or another attempt begins; an `E2BIG` with the flag set becomes `EAGAIN`, before the
  tracepoint, so the trace and the caller see the same errno.
- `vm/suite.sh`: the small-allowance check in the `buildrace` step; the `buildmoved` comment.
- `tests/query-build-moved.c`: asserts `EAGAIN`. It still shows the one shared allowance: with
  an allowance per walk the query would succeed.
- `docs/decisions/0043-e2big-only-for-a-build-that-met-no-change.md` (new, proposed); a dated
  note in ADR-0019; `docs/spec-query-v2.md`, `docs/compatibility.md`, `docs/operations.md`,
  `docs/tools.md`; `CHANGELOG.md`.
- Finding 1 closed on the surfaces that stated it open: `docs/status.md`, `docs/status.pt-BR.md`,
  `docs/matrix-and-limits.md` (the known limit removed), `docs/distribution-strategy.md` §8,
  `ROADMAP.md`, the site's journey page (both languages, regenerated), a dated note in journal
  0306, and five retired wordings in `docs/retired-claims.txt`.

## Checks

On the test host, in fresh clones of `15512bf`:

- `make checkpatch`: pass.
- `MOOB_DEBIAN_KERNEL=7.3.4 MOOB_VM_KVM=1 MOOB_ONLY=buildrace make vm-test`, three runs: pass;
  on ext4 2 to 5 builds returned `EAGAIN` (`err=-11 tries=1`) where the old code returned
  `E2BIG`, `buildrace` retried them, none returned `E2BIG`, and every domain equals the oracle;
  btrfs and XFS: 24 builds, none refused.
- `MOOB_DEBIAN_KERNEL=7.3.4 MOOB_VM_KVM=1 MOOB_ONLY="buildmoved buildlimit buildwork"`: pass
  (`QUERY_BUILD_MOVED_CHILD_PASS: moved rescan exhausted the allowance: EAGAIN without output`;
  the oversized quiet domains still get `E2BIG`, `BUILD_LIMIT_PASS`, `BUILD_WORK_OPTION_PASS`).
- `MOOB_VM_KVM=1 MOOB_ONLY="buildrace buildmoved"` on 6.12.111: pass.
- From 17:24 to 19:04, in one fresh clone: `make lint` pass; the full `MOOB_VM_KVM=1 make
  vm-test` (6.12.111), `MOOBFS_VM_PASS`, with the new check at `builds=24 e2big=0 eagain=0` on
  ext4, btrfs and XFS; the full `vm/kasan-local.sh`, `MOOBFS_KASAN_PASS: layer suite under KASAN,
  UBSAN and lockdep with no kernel diagnostics`.

These runs were made at `15512bf`, before the branch was rebased on `main` after pull requests
#214 and #215; the rebase changed no kernel code and no test of this batch. After the rebase, on
this Mac: `make test`, `check-retired-claims`, `check-pins`, `check-matrix-page`, `check-site`,
`site-faq --check` and `git diff --check`, all pass.

The change touches no lock and no object lifetime.

## Limitations

- Why 6.12 does not reach the default allowance on the reproducer's host is not explained.
- The test host reproduces the misreport only with the smallest allowance; the default one was
  reached only on the reproducer's host. Confirmation there is up to the reproducer.
- Duplicate entries of the renamed-directory list still cost a rescan each.

## Next step

Publication in one pull request, then a reply on issue #195 with its link (drafted outside the
repository, posted by the maintainer), asking the reproducer to confirm on his host.
