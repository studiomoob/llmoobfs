# 0261 — Private cursor output, current-state reconciliation and S20 assessment

Date: 2026-11-03. Status: local cursor correction and documentation reconciliation complete; S20 remains partial.
Started about15:06 Brasília; cursor controls completed15:07:52. Calendar time, not measured effort.
Estimated effort: S. Follows audit0260 and the maintainer's instruction to continue its three local units.

## Cursor alert49: finding and correction

The VM test client used fopen(path,"wb") for --cursor-out: with umask000 it creates
mode0666. It is not a set-user-ID executable or service; --uid drops supplementary groups
and uid/gid before the query. It is a real output-permission weakness in a test tool, not
evidence of a kernel authorization flaw. The operator supplies the path; existing cursor
fixtures deliberately overwrite their files between runs.

Reuse the prior-art shape of bench/common.h:bench_create_out(): open with explicit creation
mode, then fdopen. New cursor files request0600, rather than0666. Existing-file overwrite
semantics remain: this does not tighten an existing file's mode, refuse symlinks, or guarantee
an atomic replacement. These are operator-controlled test outputs, not an untrusted upload API.
Check fclose as well as fwrite: a buffered write error must cause nonzero exit. No change to
layer/, UAPI, attributes, active runtime source, or the running S18 campaign.

## Actual userspace controls

Built old and corrected tests/query.c with common/attr.c on the Mac mini using cc
-Wall -Wextra -Werror. Used a deterministic LD_PRELOAD ioctl shim that accepts QUERY2,
returns no rows and a known128-byte cursor. This exercises the real userspace binary
without loading a module or starting a VM; it is not kernel integration validation.

- umask000: old binary created0666, failing the private-output check; corrected binary0600.
- All128 cursor bytes matched the expected fixture; saved-cursor input/output roundtrip passed.
- Rewriting an existing private cursor passed, preserving suite behavior.
- /dev/full: old binary returned0 despite buffered-write failure; corrected binary returned1.
- Missing output parent: corrected binary returned1.

Isolated output: b11night/cursor-audit-20261103, separate from the active immutable runtime.
The copied archive SHA256 is948a5f08d722f952e0f15930414244bdb8a94c01b32318ed09c31a6fa51cbfb2.
All seven internal manifest entries and the tested source against the local file were checked.
No new permanent test framework is needed for this small libc-output correction; the control
source and binary receipts remain outside Git. Native VM/KASAN suites were not repeated for
this userspace-only change. Future publication still needs its applicable CI/batch checks.
The remote CodeQL alert remains open until an authorized publication/analysis, not dismissed.

## Next unit

Reconcile stale current summaries while preserving historical checkpoints. Verify the S20
receipt and checklist rather than treating a specification-only design as automatic closure.
No push, PR, merge, release, remote setting change or additional outreach is authorized.

## S20 receipt and lifetime checklist assessment

The existing G6 receipt explicitly describes a specification-only design, not an executable
test. It names the two allowed sources (specification and harness) and excludes layer/ and
the correction proposal. The correction author then implemented that design in C. Journal0259
records the real negative control and focused/full positive gates. This proves independent
fixture design plus implemented/tested regression coverage. It does not prove the literal S20
requirement that a second model wrote the executable test without seeing the implementation.
No evidence of different model identity is asserted. Keep S20 partial; do not weaken its criterion.

For CONTRIBUTING's value-lifetime checklist, inspect the seven-line correction in4f17fb4:
place_and_lock obtains the placement/write-lock context; rec is then looked up from the xarray.
The destination liveness check, source-domain invalidation and rec_drop occur under that write
lock. No new registration, retry, sleep or lock drop is introduced between lookup/check/use.
rec_drop may destroy rec: the new branch never dereferences rec afterwards. The else branch
retains its existing rec_set_partition/inode_rec_put_if_empty ownership rule; the final unlock
uses ix/nofs rather than rec. This bounded inspection is not a review of every helper or proof
of global race freedom. Existing VM/KASAN evidence remains the stronger runtime check.

## Current-state reconciliation and public surfaces

Reconciled READMEs (existing user CLI), ROADMAP (fair shares, G2 chronology, S3 and first
static/fuzz runs), security-roadmap (merged baseline versus local correction, S20 limits),
adoption-risks (fstests reruns), client-test-plan (actual bounded results), DX plan (completed
hand-offs), both status documents (tag scope and accepted ADR0031), testing.md and CHANGELOG.
Updated the bilingual site generator and regenerated its status pages. Added retired wording
to retired-claims.txt; old dated journals/proposals are retained, not silently erased.

The public-surfaces inventory was reviewed. Remote issues16/17, discussion69 and the live
site remain at their published checkpoints because remote edits/publication are unapproved;
these are deliberate queued surfaces, not claimed updated. No outreach draft or account data
is stored in Git. New private cursor output is a test-tool change, not a new user-facing API.

Validation results and final local commit are recorded below after the checks actually run.

## Final local verification (2026-11-03, about15:09 Brasília)

site-journey generation completed, retired-claims passed, site-faq --check matched,
check-site passed for12 pages, and git diff --check was clean. No new dependencies or
pins changed. The cursor correction is local commit4611940; its source exactly matches
the archived tested source. This second unit changes only documentation/site text.

The existing campaign still runs on immutable61ee4f7: at15:09:38 the manager reported
150,232 executions, corpus1,176, coverage16,713, pending0/reproducing0. Its configured
workdir has no crash directory yet; no final done marker exists. The single observer71609
is alive. No new VM, fuzzer, monitor or runtime mutation was introduced by these units.

Future publication requires the maintainer's authorization and applicable CI; no remote
alert closure, public document update or S20 completion is claimed here.
