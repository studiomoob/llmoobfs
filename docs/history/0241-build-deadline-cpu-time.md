# 0241 — The domain-build deadline counts the builder's CPU time (ADR-0031, proposed)

Date: 2026-10-29. Status: **implemented on branch `claudiofp/b11-deadline`; VM step, negative round
and KASAN/lockdep run pending**. ADR-0031 is proposed and waits for the maintainer. Work started:
about 2026-10-29 23:05 UTC; ended about 23:50 UTC. Wall-clock time, not an engineering-effort
measurement. Estimated effort: M (ADR-0031); actual so far: S (the code, documents and local
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

## Pending (not run; must not be read as passed)

All on a dedicated VM, which was busy with a long syzkaller/fstests run during this session:

1. `MOOB_ONLY=buildtime make vm-test` on the Debian 6.12 kernel (the step is opt-in; it waits
   about two and a half minutes).
2. Negative round: the same step with the module of `main` (`fac2d4b`) must fail at
   `bt-sleep-refused`.
3. `MOOB_ONLY="buildtime buildlimit buildwork buildmoved"` under the KASAN/UBSAN/lockdep kernel.
4. Optionally journal 0238's case: `MOOB_TEST=bench MOOB_BENCH_CACHE=direct make bench-test`;
   the first `QUERY2` of `ns3` should now complete.
5. Before the batch's pull request, the full `make vm-test` and the full KASAN/lockdep suite.

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

Run the pending VM steps on the Mac mini when it is free; record the results here; ask the
maintainer to accept or reject ADR-0031; then this branch joins the next batch.
