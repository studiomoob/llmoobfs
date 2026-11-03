# 0261 — Private cursor output, current-state reconciliation and S20 assessment

Date: 2026-11-03. Status: cursor correction locally validated; documentation and S20 review in progress.
Started about15:06 Brasília; cursor controls completed15:07:52. Calendar time, not measured effort.
Estimated effort:S. Follows audit0260 and the maintainer's instruction to continue its three local units.

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
