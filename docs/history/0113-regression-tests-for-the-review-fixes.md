# 0113 — Regression tests for the fixes of journal 0105

Date: 2026-10-14 (numbered 0113: the number 0111 is taken by an outside review in progress).
Started 18:00, ended 18:40 (calendar time, local). Status: tests added; run
in the local VM. Estimated effort: S.

## Goal

The maintainer set the focus on stabilizing what exists before adding features. The fixes of
journal 0105 were confirmed by a lockdep assertion and by the complete suite, but no test of the
suite exercised their new paths. Two tests added, each asserting something the code must keep.

## Tests

- **A bad v1 request is refused before anything is built.** On a mount with `domain_depth=0`, an
  ordinary user sends a v1 `QUERY` with an invalid version (`tests/query.c`, mode `v1bad`): the
  answer must be `EINVAL` and `GET_INFO` must still report no domain built. Negative run, with a
  build placed back before the checks (the order before journal 0105): fails at
  `v1-bad-request-built-ext4`.
- **The error path of the result copy.** A `QUERY2` with an unmapped results address
  (`--bad-results`) must fail with `EFAULT`; then a directory renamed in the domain forces a
  rebuild, which takes `query_sem` for writing, and the next query must complete within 30 s with
  the rebuild counted. This guards the new copy path (`results_to_user()`) against a regression
  that would leave the semaphore held or turn the error into something else. It does **not**
  prove the fix of finding F7: the code before journal 0105 also returned `EFAULT` and released
  the semaphore on that path. F7's evidence remains the lockdep assertion of journal 0105.

## Results

- `MOOB_ONLY=ext4 make vm-test`: pass with both tests.
- Negative run of the first test: fails as above.

## Next step

The outside review requested by the maintainer (another model, prompt prepared in this session);
its findings checked against the code before any change.
