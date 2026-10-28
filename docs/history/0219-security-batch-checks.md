# 0219 — Security batch 2: full suite and KASAN before publication

Date: 2026-10-27 to 2026-10-28. Status: the branch `sec/batch2` (`main` at `5fa567b` with the
fixes of journals 0215, 0216 and 0218), published with the batch's pull request together with
the three advisories. Work started: 2026-10-27 20:46 America/Sao_Paulo; ended 2026-10-28 08:45.
Wall-clock time, mostly waiting on the VM runs, not an engineering-effort measurement.
Estimated effort: S.

## Goal

AGENTS.md asks, once per batch and before publication, for `make checkpatch`, the full
`make vm-test` and the full local KASAN/lockdep suite. This batch holds GHSA-95h9-j7c2-gcw6
(search-only directories, journal 0215, ADR-0025) and GHSA-756j-cgwq-gvwm (generation of
domain and file records, journal 0216). Each fix had passed its own steps, a negative round and
those steps under KASAN; the full runs had not been run. GHSA-6xf4-r246-92r6 (write credentials
and `CAP_FSETID`, journal 0218), whose fix the external reviewer wrote, joined the batch on
2026-10-28 (runs 5 and 6); its own journal records no KASAN run.

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
5. `3e85052`, the reviewer's two commits of GHSA-6xf4-r246-92r6 taken onto the branch with
   their sign-off (07:05–08:10): `make checkpatch` nothing reported; the full KASAN/UBSAN/lockdep
   suite `MOOBFS_KASAN_PASS`, no kernel diagnostic. The change touches credentials, object
   lifetime under AGENTS.md, and this run covers its steps (`ext4`, `btrfs`, `xfs`, `reserve`)
   with every other. Negative round: `layer/` of `cc0c718` (before the fix) with the new
   `tests/layer.c`, `MOOB_ONLY=ext4 sh vm/kasan-local.sh`: `MOOBFS_VM_FAIL: layer-tests-ext4`,
   where the fixed module passes the same step; `layer/` is the only difference. The console
   does not carry the test's `FAIL line` message, so the failing check is not shown; the
   reviewer's run reported `FAIL line 714`, the new set-user-ID check. A first attempt of this
   round lost its console when its worktree was removed and was repeated.
6. `fd3de87` (08:11–08:31): `MOOB_VM_KVM=1 make vm-test`, the committed harness with the guest
   under KVM: `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, no
   `MOOBFS_VM_FAIL` and no kernel diagnostic; the guest powered down after 1145 s, against
   2575 s under TCG in run 4.

The external reviewer, working on the advisory forks the same night, found the two suite
defects of runs 1 and 2 on their own host and fixed them in a commit of their pull request for
GHSA-95h9-j7c2-gcw6. The fixes of this journal were already on `sec/batch2`; the batch keeps
them, and the fork pull requests are closed in favour of the batch's.

## Changes

- `vm/suite.sh`: `pm-mv` and `buildmoved` mount tracefs only when `/sys/kernel/tracing/trace` is
  absent (`af61f0d`); `tr_on()` turns `tracing_on` on (`db0d361`).
- `vm/run.sh`, `vm/in-container.sh`, `docs/testing.md` (`fd3de87`): `MOOB_VM_KVM=1` passes a
  writable `/dev/kvm` to the container and runs the guest with `-accel kvm`; without it nothing
  changes, so CI keeps TCG. The guard against a hung guest goes from 2400 s to 3600 s, since
  the complete suite needed 2575 s under TCG on the dedicated host.
- Journal 0218 and the reviewer's commits `d9a6758` and `3e85052` (GHSA-6xf4-r246-92r6); this
  journal was renumbered from 0218 to 0219 for it.
- Public surfaces, updated for the publication in the same batch: README, status, SECURITY.md,
  the security roadmap and operations, ADR-0011 notes, FAQ, limits, overview, help list,
  roadmap, CONTRIBUTING and the site, in both languages where they have two; the review status
  is qualified with the reviewer's reports; the retired wording is in `docs/retired-claims.txt`.

## Decisions

- Disclosure: the maintainer publishes the three advisories with the batch's merge, without a
  date agreed beforehand with the reporter; SECURITY.md lets the maintainer and reporter agree on
  it, and nothing in this batch depends on the reporter.
- No CVE is requested for these advisories: there is no supported release, only experimental
  source snapshots. SECURITY.md now states that policy.
- Credit: the repository describes the reporter by role and links the advisory, which carries
  the credit they accepted.
- Merge rule: CONTRIBUTING asks a recorded review by someone other than the author and the
  maintainer for a change to `layer/`. For 0215 and 0216 the reviewer rebased and ran them on
  their host (their fork pull requests); 0218 is the reviewer's own code, reviewed by the
  maintainer's assistant and run under KASAN here. The batch merges on that basis, an exception
  recorded here.

## Limitations

- Run 4's `make vm-test` ran with a 7200 s limit in the host's working copy; run 6 uses the
  committed harness, under KVM. The CI job runs it under TCG. `make vm-test` covers the Debian 6.12.111 guest; the 6.12.113
  guest runs in CI only.
- `make vm-test` reports `PYLIB_SKIPPED` (no python3 in its guest); the Python library ran
  under KASAN (`MOOBFS_PYLIB_PASS`).
- The host is a single i5; neither run measures performance.

## Next step

Open the batch's pull request from `sec/batch2`, merge after the required checks pass, close
the three fork pull requests, publish the advisories and delete their temporary forks. Open
issues for the limitations of journals 0213, 0215, 0216 and 0218 that no roadmap item tracks.
