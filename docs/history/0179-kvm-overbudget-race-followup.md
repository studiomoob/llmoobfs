# 0179 — Concurrent budget release under native KVM

Date: 2026-10-22. Status: **locally validated; PR 127 open, remote CI pending**.

## Goal and evidence

Investigate a repeatable failure of the issue 111 `overbudget-race` regression
on the dedicated Mac mini's native KVM guest. The reference Docker `make vm-test`
on the local Mac and the full KASAN run on the dedicated host passed on the T9
source (journal 0178). An additional direct native KVM run on the dedicated
host failed at `overbudget-race-ext4`: 611 alternating x/y queries, 64
`ENOMEM`, zero other errors. `MOOB_ONLY=overbudget` reproduced the failure on
ext4: 584 alternating queries, 48 `ENOMEM`, zero other errors. The trace showed
x/y builds with `err=-12`, `tries=1`, `released=0` amid roughly 48,000 other
builds and 110 releases. Neither failed run is a suite pass. The host is a
dedicated test machine; no other project workload was active in this session.

## Hypothesis and targeted experiment

`domain_release()` selects the oldest built domain and returns false if that
single domain's build mutex is busy. Four churners rebuild their small domains
continuously. A busy oldest candidate can therefore prevent release of another
built domain, leaving a domain that fits alone refused. This is a code-based
hypothesis, not a direct trace of the failed `mutex_trylock()` call. A narrow
change in `layer/index.c` makes `domain_release()` try the next eligible
candidate if a build mutex is busy, preserving least-recently-used order among
available candidates. The search is bounded by the number of built domains at
entry and keeps the existing lock order. No UAPI or budget-policy change was
intended. An alternative was to wait for the busy mutex, but two concurrent
builds can wait for one another; the existing nonblocking policy is retained.

The same targeted native KVM command, `MOOB_ONLY=overbudget sh
build/native-kvm.sh`, passed with the changed module: ext4 496, btrfs 277 and
XFS 390 alternating queries, with zero `EAGAIN`, `ENOMEM` or other errors on
each lower; `OVERBUDGET_PASS` appeared three times, `MOOBFS_ONLY_PASS` and
launcher exit 0. The preceding targeted run on the unchanged module is this
test's negative control and failed on ext4 with 48 `ENOMEM`. The result
supports the busy-candidate hypothesis but does not prove it exhaustively.
The reference Docker `make vm-test` completed on the local Mac with exit 0,
`MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`, without a
`MOOBFS_VM_FAIL` marker. Its concurrent-budget step passed on ext4 (70),
btrfs (40) and XFS (66 alternating queries), each with zero `ENOMEM` or other
errors. The full native KVM run on the dedicated host also completed with exit
0, `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`, and no
`MOOBFS_VM_FAIL` marker. Its concurrent-budget step passed on ext4 (533),
btrfs (281) and XFS (412 alternating queries), each with zero `ENOMEM` or
other errors. These full runs used separate hosts. The source copied to the
dedicated host had the same C behavior as this worktree; a subsequent change
only clarified comments before KASAN.

The full `sh vm/kasan-local.sh` run on the dedicated host completed with exit
0 and `MOOBFS_KASAN_PASS`, `MOOBFS_ACCEPTANCE_PASS`, and `MOOBFS_LAYER_PASS`.
The concurrent-budget test passed on ext4 (32), btrfs (30), and XFS (35
alternating queries), each with zero `ENOMEM` or other errors. T9's six
unprivileged fault cases also passed: 210 calls, 204 injected faults, zero
unexpected outcomes. The saved `dmesg.txt` was present and its KASAN, UBSAN,
Oops, warning and lockdep diagnostic filter matched nothing; no
`MOOBFS_VM_FAIL` appeared. The copied `layer/index.c` matched the local file
by SHA-256 before this run. Local `make checkpatch`, `make test` (60 attribute
checks and nine image tests), `git diff --check`, retired-claims, SPDX and
site checks passed. Literal `make lint` was not rerun: the local Mac lacks
ShellCheck; journal 0178 records an equivalent disposable-container check of
all tracked shell scripts, which this unit did not change.

## Limits

The 10-second concurrency test samples schedules; a pass cannot prove the
absence of every race. The direct native KVM wrapper substitutes only the
repository path and QEMU acceleration in the reference suite; it is additional
evidence, not a literal `make vm-test` invocation. A finite KVM/KASAN sample
cannot prove the absence of every race. No PR, remote CI, merge or issue
reopening/closure is claimed. The next stabilization units are the resource-
cost investigations of issues 116 and 118, after publication is authorized.

## Publication update — 2026-10-22

The three signed commits on `codex/overbudget-kvm-0179` were pushed and
[PR 127](https://github.com/studiomoob/llmoobfs/pull/127) was opened against
`main`. GitHub reported the branch mergeable and its checks queued or in
progress at publication. The prior local validation remains the evidence for
the technical result; remote checks have not yet been counted as passed. No
merge or issue state change was performed. The next work is to inspect the PR
checks and review, then reconcile the journal and issue states after any
authorized integration.
