# ADR-0031 — The domain-build deadline counts the builder's CPU time; a 120 s elapsed backstop stays

Date: 2026-10-29. Status: **accepted** by the maintainer on 2026-10-30 (journal 0241), implemented on branch
`claudiofp/b11-deadline`; *update, 2026-10-30:* the dedicated-VM step, its negative round and the
KASAN/lockdep run passed (journal 0241); journal 0238's case, on the host's rotating disk with
other guests running, reached the 120 s backstop on the cold first build, and the query after it
completed. Amends ADR-0019 only in what its ten-second deadline measures; its admission, cumulative
work accounting, errnos and output-on-error contract stay. ADR-0020's `index_build_work` option is
unchanged. A fix under ADR-0022: no new mount option, module parameter for users, ioctl structure
or format change. Estimated effort: M (one to three days), of which the code is S (hours) and the
rest is the VM, negative and KASAN rounds and the public surfaces. Estimate, not labor time.

## Context

ADR-0019 bounds an unprivileged `QUERY2` domain build by a cumulative work allowance
(`index_build_work`, default 32,768 units: one per enumerated name, directory visit or attribute
load) and by a ten-second deadline on elapsed time (`MOOBL_BUILD_TIME_NS`, checked between
operations in `scan_check()`, `layer/index.c`). Exceeding the deadline returns `ETIMEDOUT` and
drops the partial domain.

The work allowance depends only on the request (the domain's shape). The elapsed deadline also
depends on the lower device and on everything else sharing it, and it refuses builds that the
work allowance admits:

- Journal 0238: with the bench lower opened `O_DIRECT` on the test host, the cold first `QUERY2`
  of `ns3` (7,459 files, well inside 32,768 units) exceeded ten seconds and was refused with
  `ETIMEDOUT`, twice, once on an idle host. The same domain took 2.7 s cold on the bare-metal
  reference. The workflow was moved to the host page cache to avoid it.
- Journal 0210: over `dm-delay`, a 5,000-file domain built in 9.8 s at 200 ms per I/O and was
  refused at 400 ms.
- Journal 0182: the cost a build controls is CPU, and it is small: the refused cold
  17,000-file ext4 build spent about 182 ms of process CPU; a 7,500-file build took about 0.2 s.

A refusal throws the build away; the next query pays admission and starts again, and on a device
that is consistently that slow it is refused again, so a domain that fits every documented limit
cannot be queried at all. Since the elapsed clock also counts waiting for the disk and for a CPU,
another user who saturates the same disk or CPUs can turn a victim's legitimate build into a
refusal. Both are availability defects of the limit, not of the request: the deadline denies what
ADR-0019 and ADR-0020 meant to admit.

What the elapsed bound still protects: a build holds its domain's `build` mutex
(`domain_ensure_dom()`), so other queries of the same domain wait for it (killably), and the
caller stays in the ioctl. Without any elapsed bound, a stalled lower could hold them as long as
`index_build_work` operations take, minutes to hours at hundreds of milliseconds per I/O.

## Prior art (upstream v6.12 sources; Debian's 6.12 tree has the same code paths)

- **XFS online scrub** (`fs/xfs/scrub/scrub.h`, `xchk_maybe_relax()` and
  `xchk_should_terminate()`) and **btrfs balance and relocation** (`fs/btrfs/relocation.c`,
  `btrfs_should_cancel_balance()`): the kernel's long in-kernel tree walks are bounded by
  `cond_resched()` and fatal signals (and an explicit cancel request), not by a wall-clock
  deadline. They run as long as the device needs.
- **`RLIMIT_CPU`** (`kernel/time/posix-cpu-timers.c`, `check_thread_timers()` and
  `task_sample_cputime()`): the kernel's per-task time limit counts CPU time, sampled from
  `p->se.sum_exec_runtime`, and excludes time asleep. `task_sched_runtime()`
  (`kernel/sched/core.c`) adds the not yet accounted part of the current slice but is not
  exported to modules.
- **Hung-task detector** (`kernel/hung_task.c`; `CONFIG_DEFAULT_HUNG_TASK_TIMEOUT`, default 120 s
  in `lib/Kconfig.debug`): the kernel's wall-clock threshold after which a task blocked in
  uninterruptible sleep is reported as hung. It warns, it does not fail the operation.

The pattern: wall-clock bounds detect a stuck device; CPU-time bounds limit what a task itself
consumes; slow but progressing I/O is not an error.

## Options

- **A. Keep the ten-second elapsed deadline and document the envelope** (a build completes when
  the lower's cold cost per file times the file count stays under ten seconds). No code. Rejected:
  it leaves a domain that fits every limit permanently unqueryable on a slow but healthy device,
  and lets a noisy neighbour on the same disk cause refusals.
- **B. Count only the builder's CPU time.** Smallest change; matches `RLIMIT_CPU`. Rejected alone:
  it removes every elapsed bound on how long the `build` mutex is held.
- **C. Count the builder's CPU time against the ten seconds, and keep an elapsed backstop of 120 s,
  the hung-task default.** Two constants, the same errno, no interface. Chosen.
- **D. A mount option `index_build_time=` with a bounded range.** Follows ADR-0020, but adds an
  interface under the freeze and leaves the default refusing slow devices; the operator would
  have to know the device's latency in advance. Rejected.
- **E. A module parameter.** Global, an interface for users under the freeze, same objection as D.
  Rejected.
- **F. Scale the deadline with `index_build_work`.** The failing case uses the default allowance;
  scaling by work does not depend on the device. Rejected.
- **G. Exclude I/O wait through delay accounting** (`task_delay_info`). Needs
  `CONFIG_TASK_DELAY_ACCT` and the `delayacct` boot option or sysctl, off by default; not
  reliable on the supported kernels. Rejected.

## Decision

1. `MOOBL_BUILD_CPU_NS` = 10 s, compared against the building task's
   `current->se.sum_exec_runtime` since the build started. It lags by at most one tick, so the
   bound is cooperative and may overshoot by one tick plus one operation. Work done for the build
   by other threads (lower-filesystem workers, interrupt handlers) is not counted; the work
   allowance remains the principal bound, as ADR-0019 says.
2. `MOOBL_BUILD_TIME_NS` = 120 s elapsed, checked at the same points, as the backstop on how long a
   build holds its domain and its caller.
3. Either returns `ETIMEDOUT`, drops the partial domain and publishes no partial answer, exactly
   as before. The cumulative allowance across retries and moved-directory rescans is unchanged.
4. A test-only module parameter `scan_spin_us` (root only, 0 unless written, like the existing
   `scan_delay_us`) busy-waits per scanned file so that the suite can reach the CPU bound.

## Compatibility

Strictly more permissive for callers: the CPU time of one task is never more than the elapsed
time, so every build that finished before still finishes, and builds that used to be refused
between 10 s and 120 s elapsed now complete. A query on a slow lower can now block up to
120 s instead of 10 s, for its caller and for other queries of the same domain; both wait
killably, and the hung-task detector skips killable sleeps (`kernel/hung_task.c`), so a waiter is
not reported as hung at the edge of the backstop. No ioctl structure, command number, errno set, mount option or format change. G2's
build bound (`docs/g6-evaluation-protocol.md` §14.3) stays a measured 10 s threshold; the module no
longer implies it.

## Validation gate

- `make checkpatch` and `make lint` clean.
- `MOOB_ONLY=buildtime` in a dedicated VM: 60 files asleep 200 ms each (12 s elapsed, little CPU)
  build in full; 60 files at 200 ms of busy-wait (12 s of CPU) return `ETIMEDOUT` with untouched
  outputs and no built domain, and the next query builds; 60 files asleep 2.1 s each (126 s)
  return `ETIMEDOUT` the same way.
- Negative round: the module of `main` fails the first check (`bt-sleep-refused`).
- The same step under KASAN/UBSAN/lockdep, and `buildlimit`, `buildwork`, `buildmoved` unchanged.
- Optionally, journal 0238's case again: `MOOB_TEST=bench MOOB_BENCH_CACHE=direct`; the first
  `QUERY2` of `ns3` should complete.
