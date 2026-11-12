# 0289 — Tests for the recorded exceptions of ADR-0030: V8, 0114 item 6, and an assessment of 0114 item 3

Date: 2026-11-12 to 2026-11-13. Work started 2026-11-12 about 20:30 Brasília time. Status:
**in progress.** Estimated effort M (one to three days); the time recorded is calendar time of
one night session, with the VM rounds queued behind the release-candidate runs of the same
night. Done without the maintainer, who was asleep: nothing pushed, posted or published; local
commits only.

## Goal

Six findings of Part A of [`docs/review-findings.md`](../review-findings.md) are recorded
exceptions to criterion (2) of ADR-0030 (amendment of 2026-11-11, about 13:00, journal 0284):
fixed, with no regression test that fails without the fix. Give a test to those that can have
one without changing `layer/` (frozen, ADR-0022), most feasible first: V8 (journal 0156), 0114
item 6 (journal 0120), and assess 0114 item 3 (journal 0120). M7, M8.2 and M8.3 were not taken
up: their reasons (no outcome differs; `vfs_getattr()` adds `AT_GETATTR_NOSEC` itself; no
supported lower has `->flush`) are statements about the kernels and lowers, not about a missing
harness.

## V8: a layer inode keeps the lower's `S_SWAPFILE`

### Analysis

The recorded reason was: "No flag outside the copied mask has an effect the VFS shows on the
layer inode after a rename that succeeds on ext4, btrfs or XFS; `S_SWAPFILE`, the one with a
clear effect, makes the rename fail first." That is true of the four rename sites. But the fix
`fb602d8` changed five sites, and the fifth is `moobl_inode_set()`, where every layer inode is
made: `fsstack_copy_attr_all()` copied the lower's whole `i_flags` there too. A lower file that
is an active swap area when its layer inode is made gives the layer inode `S_SWAPFILE`.
`moobl_copy_attr()` (getattr, revalidation, after I/O) updates only `MOOBL_COPY_I_FLAGS` with
`inode_set_flags()`, so nothing clears the copied bit when `swapoff` clears it on the lower
(`mm/swapfile.c`, `inode->i_flags &= ~S_SWAPFILE`). The VFS checks the bit on the layer inode
before the layer is called: `vfs_fallocate()` returns `ETXTBSY` (`fs/open.c`), `vfs_rename()`
`ETXTBSY` and `may_delete()` `EPERM` (`fs/namei.c`), all read in Linux 6.12.116, the KASAN
kernel. So, after `swapoff` on the lower, the old code refuses through the layer what the lower
allows.

### Test

`tests/passthrough.c`, new case `swapflag` (runs in every lower's `run_lower`, so in the full
suite on ext4, btrfs and XFS): a file made on the lower (so that the layer has no inode for it),
filled with a 64-page swap area (header of `union swap_header`, written, not allocated), made
`nocow` first on btrfs; `swapon(2)` on the lower path; `stat` and an `O_RDONLY` open through the
layer (the layer inode is made now and pinned); a control: growing it by one page through the
layer must fail with `ETXTBSY` while the area is active (the lower's refusal, forwarded);
`swapoff(2)` on the lower; then `fallocate`, `rename` and `unlink` through the layer must
succeed. Markers: `PT_SWAPFLAG active_fallocate=... after_swapoff: fallocate=... rename=...
unlink=...` and `PT_SWAPFLAG_PASS` or `PT_SWAPFLAG_FAIL`.

### What it does not cover

The four rename sites keep the original reason: through a rename that succeeds, no flag outside
the mask reaches the layer inode with a VFS-visible effect on the supported lowers. A regression
in those four sites alone would not fail `swapflag`. The negative round reverts the fix as a
whole, as `fb602d8` made it (the helper `moobl_copy_attr_nlink()` that all five sites call goes
back to `fsstack_copy_attr_all()`).

## 0114 item 6: long walks without `cond_resched()`

### Analysis

`domain_drop()` walks every name of the mount's index, and `moobl_index_destroy()` every inode
record, with the index lock (a semaphore) held for writing; journal 0120 added `cond_resched()`
between names and between inodes. The recorded reason was that the only symptom is scheduling
latency or a soft lockup. The soft-lockup detector is off in the KASAN kernel
(`# CONFIG_SOFTLOCKUP_DETECTOR is not set`) and would need a walk of seconds (twice
`watchdog_thresh`, at least 2 s). Scheduling latency, though, can be measured from user space
without a kernel change, as cyclictest of rt-tests does: a thread that sleeps to an absolute
time and records how late it wakes up.

On a kernel without forced preemption (`PREEMPT_NONE` or `PREEMPT_VOLUNTARY`, the mode of the
KASAN kernel and of Debian's 6.12 kernels, `CONFIG_PREEMPT_VOLUNTARY=y` with
`PREEMPT_DYNAMIC`), a task in the kernel gives the CPU to a woken real-time task only at a
scheduling point. A `SCHED_FIFO` thread pinned to the CPU that runs the walk is therefore held
off for the whole walk without `cond_resched()`, and for at most the time between two calls
with it. With full or lazy preemption the walk is preempted anyway, and the probe cannot tell
the two apart.

Both walks run in the calling task: `drop_caches` runs the shrinkers in the writing task
(`drop_slab()`, `mm/vmscan.c`), and the layer's shrinker releases built domains through
`domain_takedown()` → `domain_drop()` when the mount has `index_idle=0`; `umount(2)` runs
`cleanup_mnt()` as task work of the unmounting task (`fs/namespace.c`), which reaches
`moobl_index_destroy()`.

The fix calls `cond_resched()` once per name, and a name's entries are freed in an inner loop
without one. To measure the fix and not that inner loop, each planted file gets attribute names
no other file uses, so each name has one entry. (With names shared by every file, the inner loop
frees up to one entry per file of the domain without a scheduling point; a domain's files are
bounded by `index_build_work`, at most 65,536. That stretch is the fix's design, not a gap of
this finding; see "Observations".)

### Test

`tests/walklatency.c` (`plant` and `probe`) and the optional suite step `walklatency`
(only when named in `MOOB_ONLY`, since it formats the scratch disk and its limit is a wall-clock
time of a guest that may share its host). It formats the scratch disk as ext4 with 512-byte
inodes (the attributes stay in the inode), plants `FILES` files with `ATTRS` unique attribute
names each in one domain, mounts the layer with `domain_depth=1,index_budget=1G,
index_build_work=65536,index_idle=0`, builds the domain with a `QUERY2`, and runs two probes:
`drop_caches` (the shrinker's `domain_drop()`; the step also checks that the domain was
released) and, after a rebuild, `umount` (`moobl_index_destroy()`). Each probe pins itself and a
`SCHED_FIFO` priority-1 thread to CPU 0; the thread sleeps 1 ms at a time to an absolute
`CLOCK_MONOTONIC` time. Pass when the largest delay during the action is under `LIMIT_MS`;
inconclusive (the step fails, asking for more files) when the action takes less than `MIN_MS`.
`MOOB_WALKLAT=FILES,ATTRS,LIMIT_MS,MIN_MS`, default `12000,8,50,100`, passed through
`vm/run.sh` (kernel command line `moob_walklat=`, read by `vm/init`) and `vm/kasan-local.sh`.
Markers: `WALKLATENCY_PLANT`, `WALKLATENCY action=... action_ms=... max_latency_ms=...
baseline_ms=... wakeups=... limit_ms=...`, `WALKLATENCY_PASS`; failures `wl-drop-latency`,
`wl-destroy-latency`, `wl-drop-too-short`, `wl-destroy-too-short`.

## 0114 item 3: ref-walk revalidation and `moobl_instantiate()` (assessment only)

Not implemented; the recorded reason stands, with more detail from the 6.12.116 sources.

The defect: ref-walk `moobl_d_revalidate()` loaded `info->lower.dentry` without the parent's
lock and without RCU, while `moobl_instantiate()` (unlink and create of the same name, as SQLite
does with `-wal` and `-shm`) replaces it and drops the old lower dentry. The old dentry is freed
only after a grace period (`dentry_free()` → `call_rcu()`, `fs/dcache.c`; it is unhashed, since
the layer's reference made `d_delete()` drop it rather than turn it negative). A use after free
needs, between the load and the dereference (a few instructions), a call of `call_rcu()` on
another CPU and then a full grace period, which needs a quiescent state of the reader's CPU.
With preemptible RCU (`CONFIG_PREEMPT_RCU=y`, in the KASAN kernel) a tick that interrupts the
reader outside a read-side section and with preemption enabled reports one
(`rcu_flavor_sched_clock_irq()`, `kernel/rcu/tree_plugin.h`), so the window is not closed in
principle; but the reader must stay inside it for a tick and the rest of a grace period
(milliseconds), which only a preemption of the reader there (full preemption) or of the vCPU by
the host gives, by chance. A delay injected with a kprobe would run with preemption disabled
and so prevent the quiescent state it needs. A stress run under KASAN would pass on the fixed
code and fail only by chance on the old one: not a regression test.

A KCSAN run (the harness of journal 0274, `vm/kcsan.config`) could see the old plain load
racing with `WRITE_ONCE()` in `moobl_instantiate()`, and the current code marks it
(`READ_ONCE()`). That would test the marking, not the lifetime: removing `rcu_read_lock()` and
`lockref_get_not_dead()` while keeping `READ_ONCE()` would pass it. It would also have to be
told apart from other reports on the same path (`d_unhashed()` of a lower dentry that
`__d_drop()` clears under `d_lock`). Not written: a test that passes on the defect is worse
than none.

## Evidence

Pending: the VM rounds wait for the release-candidate queue of the same night
(`rc-8a8d09b`), which uses the test host until about 04:00–05:00.

## Changes

- `tests/passthrough.c`: case `swapflag`.
- `tests/walklatency.c`: new; `vm/build-tests.sh` builds it (`-pthread`).
- `vm/suite.sh`: step `walklatency` (optional, named in `MOOB_ONLY`).
- `vm/run.sh`, `vm/in-container.sh`, `vm/init`, `vm/kasan-local.sh`: `MOOB_WALKLAT`.
- Nothing under `layer/`.
