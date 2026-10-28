# 0219 — Security batch 2: full suite and KASAN before publication

Date: 2026-10-27 to 2026-10-28. Status: on the private branch `sec/batch2` (`main` at `5fa567b`
with the fixes of journals 0215 and 0216); embargoed until the advisories are published. Work
started: 2026-10-27 20:46 America/Sao_Paulo; ended 2026-10-28 01:15. Wall-clock time,
mostly waiting on the VM runs, not an engineering-effort measurement. Estimated effort: S.

## Goal

AGENTS.md asks, once per batch and before publication, for `make checkpatch`, the full
`make vm-test` and the full local KASAN/lockdep suite. This batch holds GHSA-95h9-j7c2-gcw6
(search-only directories, journal 0215, ADR-0025) and GHSA-756j-cgwq-gvwm (generation of
domain and file records, journal 0216). Each fix had passed its own steps, a negative round and
those steps under KASAN; the full runs had not been run.

All runs were on the dedicated Debian 13 host: `make vm-test` in Docker (Debian 6.12.111 and
6.12.113 guests under QEMU TCG, `vm/run.sh`), then `vm/kasan-local.sh` (upstream 6.12.112 with
KASAN, UBSAN and lockdep under KVM). Docker was installed on that host for this batch.

## Runs

1. `4ea8480` (20:46–21:31). `make checkpatch`: nothing reported. `make vm-test` stopped with
   `MOOBFS_VM_FAIL: pm-mv-tracefs`: the new `pm-mv` case of journal 0215 used `mountpoint`,
   which the busybox guest of `make vm-test` lacks; `mount` then failed with `EBUSY` on the
   tracefs already mounted. The `bm-tracefs` line of `buildmoved`, already on `main`, had the
   same defect; it had not shown because an earlier step mounts tracefs first. The KASAN suite,
   run after it, failed to build: `make vm-test` runs as root in Docker and had left root-owned
   files in `layer/`.
2. `af61f0d` (21:34–22:32): both lines test for `/sys/kernel/tracing/trace` instead, as
   `tr_on()` does; the run script runs KASAN first and gives the tree back to the user at the
   end. `make checkpatch`: nothing reported. KASAN stopped with
   `MOOBFS_VM_FAIL: bc-trace-build-count-0`, with no kernel diagnostic: the build counter of
   `buildcost` was right, but its trace buffer held no event. `tests/query-build-moved.c`, which
   `pm-mv` now runs before `buildcost` (on `main` only `buildmoved` ran it, later in the suite),
   leaves `tracing_on` at 0, as `tests/deadscan.c` does. The `make vm-test` that followed was
   stopped by hand, since it would stop at the same step.
3. `db0d361` (22:33–): `tr_on()` writes 1 to `tracing_on` before clearing the buffer, so that
   no step depends on what an earlier test left there. `make checkpatch`: nothing reported. The
   full KASAN/UBSAN/lockdep suite: `MOOBFS_KASAN_PASS`, no kernel diagnostic (22:33–23:37).
   `make vm-test` passed every step up to `STALL_PASS`, then QEMU was stopped by the
   `timeout 2400` of `vm/in-container.sh` (00:18) while the next step, `bigdir`, ran. No hung
   task, soft lockup or RCU stall was reported, and the same code had just passed `bigdir` and
   every later step under KASAN. The CI job `Native Linux VM` runs this suite in about 8 minutes;
   TCG on the dedicated host's i5 is several times slower, and the whole suite does not fit 2400 s
   there.
4. `db0d361`, `make vm-test` again (00:21–01:04) with the limit of `vm/in-container.sh` raised
   to 7200 s in the host's working copy only, not committed, and put back afterwards. Same
   suite, same TCG guest: `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, no
   `MOOBFS_VM_FAIL` and no kernel diagnostic; the guest powered down after 2575 s. Among the
   markers: `PERMMATRIX_STEP_PASS` (281 answers the rule before journal 0116 gave wrongly, 12
   the rule before 0215), `QUERY_BUILD_MOVED_PASS`, `BIGDIR_PASS`, `BUILD_COST_OBSERVED`,
   `DOMAINRECS_PASS` and `DOMAINREUSE_PASS`.

## Changes

- `vm/suite.sh`: `pm-mv` and `buildmoved` mount tracefs only when `/sys/kernel/tracing/trace` is
  absent (`af61f0d`); `tr_on()` turns `tracing_on` on (`db0d361`). Test harness only; no change
  to `layer/`, the UAPI or the format.

## Limitations

- The full `make vm-test` that passed ran with a 7200 s limit, not the committed 2400 s; on this
  host the committed limit stops the whole suite (run 3). The CI job runs the committed harness
  and is the check of the limit. `make vm-test` covers the Debian 6.12.111 guest; the 6.12.113
  guest runs in CI only.
- `make vm-test` reports `PYLIB_SKIPPED` (no python3 in its guest); the Python library ran
  under KASAN (`MOOBFS_PYLIB_PASS`).
- The host is a single i5; neither run measures performance.

## Next step

Agree the disclosure date with the reporter; then open the batch's pull request from
`sec/batch2`, rebased on `main`, and merge after the required checks pass and with the
maintainer's approval. The advisories are published after the merge.
