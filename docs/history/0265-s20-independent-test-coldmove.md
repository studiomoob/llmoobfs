# 0265 — S20: a regression test written by a second model from the specification

Date: 2026-11-04. Status: **done** (S20 acceptance met). Work started about 07:15 and the last run ended
07:34:48 Brasília time; engineering effort under one hour, against the S estimate. Calendar time and effort are both recorded here.

## Goal

Close security roadmap S20: "for a change to the permission rule or the index protocol, a
second model writes the test from the specification without seeing the implementation" and
one change goes through the procedure. Journal 0261 kept S20 partial because the existing
fixture for correction `4f17fb4` (journal 0259) was designed independently but implemented by
the correction author.

## Procedure

- Change under test: `4f17fb4`, `moobl_index_perm_changed()` invalidates a record whose new
  partition lies in a domain that was never built (journal 0259), already in `main`.
  The procedure ran after the change was merged; the change itself is unchanged.
- Isolated package outside the repository: `docs/spec-query-v2.md`, the three UAPI headers,
  `tests/query.c` and `tests/asuid.c` as unrelated examples, harness notes and a task that
  states the behavior to guarantee in specification terms. No `layer/` source, no diff, no
  `tests/permission-gaps.c`, no journal. Receipt and file hashes: `docs/s20/coldmove/`.
- Writer: a separate subagent running a different model (Claude Sonnet 5.5) from the
  coordinator and correction session (Claude Opus 5.5), instructed to read only the package.
  It reported reading only package files. The isolation was by instruction and by what the
  package contained; it was not enforced by a sandbox, which is the remaining limit.
- Integration: `tests/coldmove.c` is the writer's file byte for byte (SHA-256 of revision 1:
  `d18833bd…`); the coordinator only added its build line and its call in the `permgaps`
  suite step, on a fresh `domain_depth=1` mount per lower.

## Runs (Mac mini, native Debian 6.12.113 KVM guest, CPUs 0–3, `MOOB_ONLY=permgaps`)

1. Attempt 1 at 07:28 (both variants): did not test anything; the source archive was made
   before the new file was added to Git, so compilation stopped (`tests/coldmove.c: No such
   file`). Coordinator packaging error, kept as `attempt1-*`.
2. Revision 0 with the correction: `COLDMOVE_FAIL: [s0 …] query after chmod 0600:
   domain_builds 2, expected 1`. Without the correction: functional checks passed and the
   step failed on kernel warnings, with the author's fixture also present in the step.
3. Only that failure line was sent back. The writer judged from spec §9 (the counter "grows
   when a domain is rebuilt"; stale or released domains are "built again by the next query")
   that a rebuild of A is not an extra built domain, and relaxed only that assertion to "never
   falls" (revision 1, recorded with the diff in `notes.md`). `domains_built`, releases,
   fallbacks and exact answers stay strict.
4. Isolated negative (revision 0, author's `coldlink` call removed from the step, original
   `layer/index.c`): `COLDMOVE_PASS` on all lowers, then `MOOBFS_VM_FAIL:
   kernel-diagnostic-layer`, warnings at `layer/index.c:182` in `charge` and
   `rec_set_partition`, `Comm: coldmove`.
5. Revision 1 positive (current code, both fixtures): `COLD_LINK_PASS`, `COLDMOVE_PASS` and
   `PERMISSION_GAPS_PASS` on ext4, btrfs and XFS, `MOOBFS_ONLY_PASS`, exit 0 (07:33:51).
6. Revision 1 negative (original `layer/index.c`, only `coldmove`): `COLDMOVE_PASS` on the
   three lowers, then `MOOBFS_VM_FAIL: kernel-diagnostic-layer`, warnings triggered by
   `coldmove` (07:34:48). Source hashes unchanged in every run.

The negative control detects the defect through the harness's kernel-diagnostic gate, not
through the test's userspace assertions, which pass on the old code: the defect corrupts
accounting without changing answers, as journal 0259 found for its own fixture. The task
named the diagnostic gate as part of the harness.

## Result and limits

S20 acceptance (checklist in `CONTRIBUTING.md`, one change through the procedure) is met.
Limits: isolation by instruction, not sandbox; the procedure was applied after merge;
the writer's index-size assertion (a16) passed but rests on an assumption it documented.
No KASAN run was made for this test-only change; the batch KASAN suite before publication
covers it. Next: S21 (journal to follow).
