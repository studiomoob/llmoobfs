# 0178 — Unprivileged queries under allocation faults (issue 114, T9)

Date: 2026-10-22. Status: **locally validated; PR 127 open, remote CI pending**.
Estimated effort: M (one to three days). Work started on 2026-10-22; exact
labor time has not yet been measured.

## Goal and scope

Complete the remaining T9 test gap of issue 114. Journal 0172 and PR 124
validated unprivileged queries before and after a real index release, but
allocation-fault queries in the enabled suite still ran only as root. This
unit adds an unprivileged fault exercise to the existing KASAN guest. The
negative run exposed a silent omission, so the unit also corrects production
error propagation. It does not change the UAPI or the resource policy.

## Starting evidence and decision

The signed historical draft commit `1e77ad5` preserved a six-case test for
fresh and built domains, plain, ordered and batched `QUERY2`. It was disabled
before PR 124 because the dedicated host was unavailable; its successful
execution was never claimed. Recover only its fault-test block into the
current `tests/permission-gaps.c` and enable it in `vm/kasan-guest.sh`, after
the existing root-only fault checks. Keep the already integrated release
test unchanged.

The dedicated host was reachable on 2026-10-22, with no QEMU, syzkaller or
build process found. `/dev/kvm` was writable, and the pinned Linux 6.12.112
source and built debug kernel were present in the KASAN cache. The `vng`
command was not on the host PATH. The exact cached Linux source at
`fs/proc/base.c` was inspected: `proc_fail_nth_write()` sets `fail_nth` on
the task named by the opened proc descriptor and has no write-time capability
check. The test opens its own descriptor before dropping credentials, then
opens the MoobFS domain and queries only after dropping to uid/gid 1001.

## Validation and next step

The recovered test compiled on the dedicated host with static GCC and
`-Wall -Wextra -Werror`; `sh -n vm/kasan-guest.sh` passed. The repository-pinned
`virtme-ng` 1.41 was installed in an isolated user virtual environment.

The first targeted `MOOB_ONLY=faults sh vm/kasan-local.sh` run failed with
`PERMISSION_GAPS_FAIL: unprivileged fault sweep terminates and injects real
faults`. The six-case test had been placed after the existing script cleared
the module-stack fault filters, so no test allocation could match the filter.
This was a harness placement error, not a production result. Move T9 before
the filter reset so that its allocations are actually eligible for injection,
then rerun.

The second targeted run injected a real fault at `nth=13` in a warm-domain
plain QUERY2. The ioctl returned success with count zero, although the lower
filesystem's readable `visible` file remained present. The newly added test
correctly failed. Inspection found that `caller_may_read()` collapsed every
`vfs_path_lookup()` or `inode_permission()` error to a boolean denial; an
allocation failure therefore silently omitted a readable result. Change this
helper to propagate `-ENOMEM` through `query_run()` while preserving ordinary
denial and stale-path filtering, so the ioctl refuses an incomplete answer.
This is a production correction within T9 and follows ADR-0016's complete-or-
explicitly-refused rule.

The corrected source passed `make checkpatch` and `git diff --check` locally.
The targeted `MOOB_ONLY=faults sh vm/kasan-local.sh` run passed in the pinned
Linux 6.12.112 guest: six combinations (built/fresh domain × plain/ordered/
batched QUERY2), 207 calls, 201 actually injected faults and zero unexpected
outcomes. It ended with `MOOBFS_KASAN_ONLY_PASS`, with no KASAN, UBSAN or
lockdep diagnostic. The preceding run of the same test against the unchanged
production module failed at warm-domain plain QUERY2, `nth=13`, with a real
injected fault and a successful zero-count answer; it is a decisive negative
control for the correction.

The full `sh vm/kasan-local.sh` run on the same source passed on the dedicated
host with `MOOBFS_KASAN_PASS`, no KASAN/UBSAN/lockdep diagnostics, and the
layer and acceptance markers. Its T9 substep made 210 calls across six cases,
injected 204 real faults and reported zero unexpected results. The existing
root fault step also passed with 322 calls and 300 injected faults. A hash
comparison confirmed that `layer/index.c`, `tests/permission-gaps.c` and
`vm/kasan-guest.sh` on the host matched this local worktree after the run.

The reference `make vm-test` launcher stopped before building or booting a VM
because the host has no Docker. The same host has the pinned Debian
6.12.111+deb13-amd64 kernel, headers and required QEMU tools. An initial
attempt to generate a direct wrapper from `vm/in-container.sh` produced an
empty file because of a shell substitution error; it ran no test. The wrapper
was corrected to substitute only the two `/work` paths with the copied source
directory, leaving the remaining harness intact. The maintainer then started
Docker on the local Mac; `make vm-test` ran there from this worktree with its
reference Dockerfile and launcher. It exited 0: `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
`MOOBFS_LAYER_PASS` were present, and no `MOOBFS_VM_FAIL` was found. The suite
included `PERMISSION_GAPS_PASS`, `UNPRIV_RELEASE_PASS`, `ROOTPATH_PASS` and
`OVERBUDGET_PASS` on ext4, btrfs and XFS. This is the official native VM
result. The two native runs used separate hosts and build directories.

The direct Mac mini TCG run reached `XATTRREAD_PASS` after all three lowers
passed permission, release, root-path and over-budget checks, but QEMU hit the
launcher guard at 2400 seconds before the final acceptance/layer markers. Its
exit was nonzero and it is **not** counted as a full pass. The official Docker
run above already passed. To finish the optional dedicated-host cross-check,
the direct wrapper was repeated with only `-accel tcg -cpu max` changed to
`-accel kvm -cpu host`; the pinned native guest kernel and suite stayed the
same. That full KVM run failed at `overbudget-race-ext4`, an issue 111
regression test outside T9, after 611 alternating queries of which 64 returned
`ENOMEM`. The dedicated host has no other active project workload in this
session. A targeted KVM rerun (`MOOB_ONLY=overbudget`) reproduced the failure:
584 queries, 48 `ENOMEM`, zero other errors. Its trace showed repeated x/y
build failures with `err=-12`, `tries=1`, `released=0`, amid roughly 48,000
builds and 110 releases. The full KVM run is **not** a pass. This repeatable
concurrent-budget finding needs its own investigation; the passing TCG
`make vm-test` and KASAN results remain valid for T9, but do not close the
KVM finding. No production fix for this separate finding is claimed here.

`make lint` could not start on the Mac because `shellcheck` is not installed.
Trying it in the disposable Docker image exposed a worktree mount detail:
`.git` points outside the mounted source, so its `git ls-files` yielded no
filenames. The equivalent checks were run separately: the host supplied the
19 tracked shell-script paths to ShellCheck inside the disposable container,
and `scripts/check-spdx.sh` ran on the host; both passed. This is not reported
as a successful literal `make lint` invocation.
Reconcile issue 114 only after the tested work is integrated. Investigate the
KVM over-budget failure as a separate stabilization unit before claiming the
budget race closed across native KVM schedules.

## Publication update — 2026-10-22

The signed T9 commit `935e8a0` was pushed as part of
[PR 127](https://github.com/studiomoob/llmoobfs/pull/127), alongside the
follow-up recorded in journal 0179. GitHub checks were pending when this note
was added. This publication does not establish a merge or close issue 114.
