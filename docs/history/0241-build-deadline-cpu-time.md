# 0241 — The domain-build deadline counts the builder's CPU time (ADR-0031, proposed)

Date: 2026-10-29. Status: **implemented on branch `claudiofp/b11-deadline`; VM step, negative round
and KASAN/lockdep run passed on 2026-10-30**. ADR-0031 was accepted by the maintainer on 2026-10-30, with the 120 s backstop unchanged.
Work started: about 2026-10-29 23:05 UTC; ended about 23:50 UTC; VM rounds 2026-10-30 11:16 to
12:05 UTC. Wall-clock time, not an engineering-effort measurement. Estimated effort: M (ADR-0031); actual so far: S (the code, documents and local
checks), the VM rounds not counted.

## Goal

Decide whether the ten-second `QUERY2` build deadline of ADR-0019 (`MOOBL_BUILD_TIME_NS`,
`scan_check()` in `layer/index.c`) is a defect, after journal 0238 recorded that with the bench
lower opened `O_DIRECT` the cold first `QUERY2` of `ns3` (7,459 files) exceeded it and was refused
with `ETIMEDOUT`; and apply the smallest correct fix. Journal 0228 (G2's build bound refers to
the deadline) and journal 0235 (S9's limits list it) were read for what depends on it.

## Decisions

- **It is a defect of the limit.** The deadline counted elapsed time, so it refused a build the
  work allowance admits whenever the lower was slow (journal 0238; journal 0210's `dm-delay` run
  at 400 ms per I/O) or shared with a neighbour saturating it. A refused build is thrown away,
  and on a device that is consistently slow every retry is refused too: a domain that fits
  `index_build_work` could not be queried at all. The cost the build itself controls is CPU, and
  it is small (about 182 ms for the refused 17,000-file build, journal 0182).
- **Option C of ADR-0031**: the ten seconds count the building task's own CPU time
  (`current->se.sum_exec_runtime`, the clock `RLIMIT_CPU` samples), and a 120 s elapsed backstop
  (the hung-task detector's default timeout) is checked at the same points, so that the time a
  build holds its domain's `build` mutex stays finite. Same errno, same drop of the partial
  domain, same cumulative allowance. Strictly more permissive than before.
- **Prior art** (upstream v6.12 sources, fetched to a scratch directory; no Debian source tree
  was at hand, the code paths are the same in Debian's 6.12): XFS online scrub
  (`xchk_maybe_relax()`, `xchk_should_terminate()`) and btrfs relocation
  (`btrfs_should_cancel_balance()`) bound long in-kernel walks by `cond_resched()` and fatal
  signals, not by a wall clock; `RLIMIT_CPU` (`check_thread_timers()`,
  `task_sample_cputime()`) counts CPU time from `se.sum_exec_runtime`; `task_sched_runtime()` is
  not exported to modules, so the module reads the field, which lags by at most one tick; the
  hung-task detector's 120 s (`lib/Kconfig.debug`) is the kernel's wall-clock threshold for a
  stuck task.
- **Rejected**: keeping the deadline and documenting the envelope (leaves domains unqueryable on
  slow healthy disks); CPU time alone (no elapsed bound on the mutex holder); a mount option or a
  module parameter (an interface under the ADR-0022 freeze, and the default would still refuse);
  scaling with `index_build_work` (independent of the device); delay accounting for I/O wait (off
  by default, `kernel/delayacct.c`).
- **Test knob** `scan_spin_us`: like `scan_delay_us` (root only, 0 unless written, one predicted
  branch per file while 0) but busy-waiting in steps of at most 1 ms with `cond_resched()`
  between them, so the suite can reach the CPU bound without a slow device.

## Changes

- `layer/index.c`: `MOOBL_BUILD_CPU_NS` (10 s) and `MOOBL_BUILD_TIME_NS` raised to 120 s;
  `struct scan_job` records `started_cpu_ns`; `build_cpu_ns()` reads
  `current->se.sum_exec_runtime`; `scan_check()` returns `ETIMEDOUT` on either bound; the
  test-only `scan_spin_us` parameter and `scan_spin()`.
- `vm/suite.sh`, step `buildtime` (opt-in, `MOOB_ONLY=buildtime`): 60 planted files; with
  `scan_delay_us=200000` (12 s asleep) the build must complete (`bt-sleep-refused` otherwise);
  with `scan_spin_us=200000` (12 s on the CPU) `ETIMEDOUT` with untouched outputs and no built
  domain, then a full rebuild; with `scan_delay_us=2100000` (126 s asleep) `ETIMEDOUT` at the
  backstop, then a full rebuild. Each part on a fresh mount. Marker `BUILD_TIME_PASS`. The old
  step expected `ETIMEDOUT` from 12 s asleep, which is now the behavior removed.
- `include/moobfs_ioctl.h`: the comment on `ETIMEDOUT` (no structure or number change).
- ADR-0031; a dated note in ADR-0019; dated notes in `docs/spec-query-v2.md`,
  `docs/g6-evaluation-protocol.md` (G2's 10 s build bound stays the protocol's measured
  threshold), `docs/status.md`, `docs/status.pt-BR.md`; current-state text in `README.md`,
  `README.pt-BR.md`, `SECURITY.md`, `docs/faq.md` (and `site/faq.html`), `docs/testing.md`,
  `docs/compatibility.md`, `scripts/site-journey.py` (and the generated `site/` pages),
  `site/index.html`, `site/guide.html`; wording in `bench/ondemand.sh` and
  `bench/ondemand-summary.py`; `docs/retired-claims.txt` gains the old present-tense wording;
  `CHANGELOG.md`; `docs/references.md` §39.
- Left as history: the 2026-10-27 milestone of `scripts/site-journey.py`, the dated entries of
  `ROADMAP.md`, `docs/status.md` and `SECURITY.md` about journal 0210, and the comment of
  `vm/install-test/in-container.sh` about journal 0238; they describe what was measured with the
  elapsed deadline.

## Checks run (on the maintainer's Mac, no VM)

- `make checkpatch`: exit 0, no warning (checkpatch.pl of the pinned stable tag over `layer/` and
  `kernel/`).
- `make lint`: exit 0 (shellcheck 0.10.0, the upstream release binary run from a scratch
  directory since none is installed on the Mac; SPDX headers).
- `make test` (offline tool tests: attribute encoding, image tool): exit 0.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`, with the new claims.
- `python3 scripts/site-journey.py .`, `python3 scripts/site-faq.py` and
  `python3 scripts/site-faq.py --check`: pages regenerated, FAQ page matches.
- `python3 scripts/check-site.py` (links, accessibility checks) and `sh scripts/check-pins.sh`:
  passed.
- `sh -n` on `vm/suite.sh` and `bench/ondemand.sh`; `py_compile` on the two Python scripts.
- The module was **not compiled** here (no Linux kernel headers on macOS); the first build is
  the VM round.

## Dedicated-VM rounds (2026-10-30)

Planned on 2026-10-29 (the VM was busy then):

1. `MOOB_ONLY=buildtime make vm-test` on the Debian 6.12 kernel (the step is opt-in; it waits
   about two and a half minutes).
2. Negative round: the same step with the module of `main` (`fac2d4b`) must fail at
   `bt-sleep-refused`.
3. `MOOB_ONLY="buildtime buildlimit buildwork buildmoved"` under the KASAN/UBSAN/lockdep kernel.
4. Optionally journal 0238's case: `MOOB_TEST=bench MOOB_BENCH_CACHE=direct make bench-test`;
   the first `QUERY2` of `ns3` should now complete.
5. Before the batch's pull request, the full `make vm-test` and the full KASAN/lockdep suite.

Results. Host: the dedicated test host (x86-64, Debian 13, KVM), a copy of this branch's tree
with a local snapshot commit (no `.git` is copied; `make checkpatch` needs `git ls-files`). One
working-copy change on the host only, not committed: `vm/run.sh` and `vm/install-test/run.sh`
pass `--cpuset-cpus=$MOOB_CPUSET` to `docker run`, so that each guest stayed on host CPUs that
no other run of this session used (6 and 7 for every run below; an unrelated run held CPUs 4 and
5).

| Run | Command | Result |
| --- | --- | --- |
| First compile | `make -C /lib/modules/6.12.113+deb13-amd64/build M=$PWD W=1 modules` in a copy of `layer/` (built only, never loaded on the host) | no warning |
| checkpatch | `make checkpatch` | exit 0, no warning |
| 1. VM step | `MOOB_VM_KVM=1 MOOB_DEBIAN_KERNEL=6.12.113 MOOB_ONLY=buildtime make vm-test` | exit 0 (4 min 19 s with the build); `BUILD_TIME_PASS: 12 s asleep built; 12 s on the CPU and 126 s asleep timed out with no results; the next query rebuilt`, `MOOBFS_ONLY_PASS` |
| 2. Negative | the same, with `layer/`, `include/` and `kernel/` of `main` (fac2d4b) and this branch's `vm/suite.sh` | exit 2; `MOOBFS_VM_FAIL: bt-sleep-refused` |
| 3. KASAN/UBSAN/lockdep | `MOOB_ONLY="buildtime buildlimit buildwork buildmoved" sh vm/kasan-local.sh` (upstream 6.12.112 with `vm/kasan.config`, cached build) | exit 0 (8 min); `BUILD_TIME_PASS`, `QUERY_BUILD_LIMIT_PASS` (errno 0 and 110 cases), `BUILD_WORK_OPTION_PASS`, `QUERY_BUILD_MOVED_PASS`, `BUILD_MOVED_PASS`, `MOOBFS_KASAN_ONLY_PASS: ... with no kernel diagnostics` |
| 4. Journal 0238's case | `MOOB_TEST=bench MOOB_BENCH_CACHE=direct make bench-test` (KVM in the container) | **failed**, exit 2, `MOOBFS_BENCH_FAIL: bench/ondemand.sh` (29 min): W1 loaded 60,000 memories in 563 s; the cold first `QUERY2` of `ns3` was refused with `ETIMEDOUT` after 120,056 ms and 120,107 ms (two rounds), now at the elapsed backstop instead of at 10 s; the query right after it completed in 89,631 ms and 57,895 ms; warm, 138 ms and 144 ms with 3,030 results and 7,459 inodes; the mount-wide `QUERY` was refused with `ETIMEDOUT` after about 120 s, cold and warm |
Item 4 is not a regression of this change: the lower image sits on the host's rotating disk
(`lsblk` reports it rotational, a 2.5-inch 7,200 rpm drive), opened `O_DIRECT` with no host
cache, while other guests (an unrelated fstests run, the mutation runs of journal 0242) read and
wrote the same disk. Before this change a refused build was refused again at
every retry; now the retry, with the lower's blocks already in the guest's page cache, completes.
The first build of `ns3` on that device needs more than 120 s of elapsed time, so the backstop,
not the CPU bound, refuses it. Measured: the refused builds ran 120,056 ms and 120,107 ms (the
backstop, so the time a full cold build needs on that disk is unknown, only above 120 s); the
retry after each completed in 89,631 ms and 57,895 ms, with part of the lower already cached.

In plain terms: ADR-0031 fixes the case of a build that is CPU-bound and the case of a moderately
slow disk (`BUILD_TIME_PASS`: a build that spends 12 s asleep now completes; journal 0210's
`dm-delay` case was not rerun), but not a disk as slow as this one under this load, where
a cold build of a 7,459-file domain still exceeds the 120 s backstop and only the retry succeeds.
The backstop value is unchanged; whether 120 s is right for such a disk is a question for
ADR-0031's review. The bench was not rerun with the host idle.

Item 5, the full `make vm-test` and the full KASAN/lockdep suite, stays for the batch, before
its pull request.


*Added 2026-10-30:* the maintainer accepted ADR-0031 as written: ten seconds of the builder's
CPU time and a 120 s elapsed backstop. The O_DIRECT bench case above, a cold build refused at the
backstop on a slow shared rotating disk and completed on the retry, stays a documented limit.

## Limitations

- The CPU bound is cooperative and per task: it lags by up to one tick plus one operation, and
  work done for the build by other threads (lower-filesystem workers, interrupt handlers) is not
  counted. `index_build_work` stays the principal bound, as in ADR-0019.
- A query over a slow lower can now block up to 120 s instead of 10 s, its caller and other
  queries of the same domain with it (killably).
- `docs/references.md` §37 and journal number 0241 were chosen while journals 0239 and 0240 are
  on other branches; renumber the references section at merge if another branch takes §37.
  (Note of 2026-10-30: the batch-10 merge renumbered the section to §39, after §37 of journal 0239
  and §38 of journal 0240.)
- `docs/g6-evaluation-protocol.md` may also be edited on the G2 branch; the note is one bullet.

## Next steps

Ask the maintainer to accept or reject ADR-0031; then this branch joins the next batch, whose full
`make vm-test` and full KASAN/lockdep suite run before publication.
