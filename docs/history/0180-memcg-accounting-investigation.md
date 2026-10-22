# 0180 — Memory-cgroup accounting of query-triggered allocations (issue 116)

Date: 2026-10-22. Status: **investigation in progress; no module change or VM result yet**.
Estimated effort: M (one to three days). Started 2026-10-22; labor time has
not yet been measured.

## Goal and starting evidence

Issue 116 records two costs: index and query allocations triggered by an
unprivileged caller use `GFP_KERNEL` without `__GFP_ACCOUNT`, and a result
buffer of up to 4096 × 272 bytes remains attached to each open file after a
query. Its code observation is supported by `layer/index.c` and
`moobl_qbuf_get()` in `layer/main.c`; the actual memory-cgroup effect has not
been measured. The buffer reuse fixed a measured query regression in journal
0139, so simply freeing it after every call would need a new performance
comparison. This work is separate from S9 fair sharing of the mount budget.

The branch `codex/issue116-memcg-0180` starts from `origin/main` at `d7fb946`.
PR 127, which corrects T9 and the concurrent budget release, was open and
unmerged when this investigation started. No change from that PR is included
here. The dedicated Mac mini was reachable and had no QEMU, virtme-ng or
syzkaller process at the initial check.

## Allocation API and test environment

The Linux 6.12.112 source cached for the project's KASAN build says in
`Documentation/core-api/memory-allocation.rst:53-56` that untrusted
userspace-triggered allocations should set `__GFP_ACCOUNT`, with
`GFP_KERNEL_ACCOUNT` as the shortcut. `mm/util.c:650-684` passes a kvmalloc
caller's flags to its vmalloc fallback; `mm/vmalloc.c:3734` counts vmalloc
pages for memcg when `__GFP_ACCOUNT` is set. The current upstream kernel
documentation retrieved via Context7 agrees with that guidance, but the
pinned 6.12.112 source is the implementation reference for this work.

The cached KASAN kernel has `# CONFIG_MEMCG is not set`, so its KASAN run
cannot prove accounting. The dedicated host's Debian 6.12.111 kernel has
`CONFIG_MEMCG=y`; a dedicated VM using that kernel is the proposed setting
for a bounded cgroup-v2 measurement. KASAN/lockdep remains a separate gate
for any eventual module change.

## Next experiment and limits

Construct a small guest test that joins a disposable memory cgroup, opens a
domain, issues an otherwise empty `QUERY2` with the maximum legal result
limit, and compares charged kernel memory while the file stays open and after
it closes. The empty answer avoids populating the userspace result array, so
the test isolates the retained kernel result buffer. Run it first against
unchanged code as a negative control, with no artificial low memory limit or
unbounded workload. Then audit the longer-lived index allocations and decide
which charges need a code change and whether a new ADR is required.

No memcg measurement, behavior correction, performance regression test or
compatibility conclusion is claimed yet. The next update must record actual
guest configuration, command, result and any discarded hypothesis before
making a change to allocation flags.
