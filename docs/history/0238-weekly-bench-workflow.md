# 0238 — Weekly benchmark workflow: the on-demand index benchmark under KVM on GitHub's runner

Date: 2026-10-29. Status: implemented on branch `claudiofp/b9-bench` (batch 9); smoke-tested on
the dedicated host; the first run on GitHub happens in the batch's pull request. Work started:
about 2026-10-29 16:13 UTC; ended about 19:20 UTC. Wall-clock time, including waiting for the test
host, not an engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

Run the on-demand index benchmark of issue #68 (`bench/ondemand.sh`; reference results
`bench/results/2026-10-13-macmini-ondemand/`) every week on GitHub's amd64 runner, inside a
disposable Debian 13 VM, as a trend and regression monitor. Suggested by the external reviewer,
with a matrix job per variant as in thumbor's release workflow (references §36).

## Decisions

- **A trend monitor, not a reference measurement.** One run per week on a shared virtual
  machine whose CPU and disk may change between runs, with the CPU frequency out of reach
  (`bench/cpu-freq.sh` reports it absent in a guest). The numbers can show a trend or a large
  regression; they are not M4 results, are not committed under `bench/results/` (they stay
  Actions artifacts for 90 days) and do not count as "more than one machine" in
  `docs/benchmark-plan.md`. **Issue #68 stays open**: it asks for other physical hardware.
- **The job never fails on a timing.** It fails when the harness fails or when the counts differ
  from the reference's (`results=3030` for the query, 7 459 inodes with `ns3` built, 60 000 with
  all): the data set is deterministic (`bench-gen --seed 1`), so a different count is a
  correctness or design change, not noise. A timing threshold on shared runners would fail on
  noise.
- **amd64 only.** GitHub's arm64 runners expose no `/dev/kvm` (journal 0124); timings under TCG
  emulation mean nothing (`docs/benchmark-plan.md`, "Dedicated VM with KVM"), and the module's
  kernel list (`scripts/supported-kernels`) is amd64 only. The harness refuses to run the bench
  mode without a writable `/dev/kvm`.
- **Matrix axis: the lower filesystem** (ext4, btrfs, XFS; `fail-fast: false`), the variable the
  M4 campaigns already varied (journal 0049). Kernel: the cloud image's own
  `6.12.113+deb13-amd64`, a listed kernel.
- **Reuse `vm/install-test/`.** A new `MOOB_TEST=bench` mode with one phase
  (`vm/bench-test/guest.sh`), selected filesystem in `MOOB_BENCH_FS`. The guest follows issue
  #68's steps: `make layer bench-tools attr-tools`, `insmod`, mount, `bench-gen --seed 1 --inodes
  60000 --profile 8`, `bench-load --workload W1 --procs 4`, `bench/ondemand.sh <mount> ns3`, then
  `bench/report.py` on the steady-state repetitions.
- **The lower on a second virtual disk in the host's page cache (`MOOB_BENCH_CACHE=host`,
  QEMU `cache=unsafe`), so the "cold" rows are cold in the guest only.** The first design opened
  the disk image with O_DIRECT so that a cold read in the guest would reach the host's disk.
  Two findings changed it. With the guest's flushes honoured, W1 (two `fsync` per memory,
  120 000 in all) had written about 400 MB in 30 minutes on the test host and was stopped; the
  guest's flushes are now ignored (the disk is thrown away). With O_DIRECT and flushes ignored
  (`MOOB_BENCH_CACHE=direct`, kept as an option), **the cold first `QUERY2` of `ns3` (7 459
  files) exceeded the ten-second build deadline of ADR-0019 and was refused with `ETIMEDOUT`**
  (`moobfs_layer: building the index of a domain failed (-110)`), twice: once beside two other
  VMs, once on the idle host. The same domain took 2.7 s cold on the bare-metal reference.
  A runner's disk is not faster than that host's SATA SSD seen through virtio, so with
  O_DIRECT the core row would be a refusal on most runs. The host page cache makes the run
  depend on the runner's memory and CPU rather than its disk; the workflow's summary, `system.txt`
  and `docs/testing.md` say that the cold rows are cold in the guest only. The guest refuses a
  second disk that already carries a filesystem (`blkid -p`) before `mkfs`.
- **`bench/ondemand.sh` predates ADR-0019 and ADR-0020.** It was written in journal 0098,
  before the build limits (`index_build_work`, default 32 768 units; the ten-second deadline).
  Its mount-wide `QUERY` builds every domain, about 20 s cold on the reference host, so on a real
  disk it is expected to be refused with `ETIMEDOUT` or `E2BIG`; under `set -e` that stopped the
  script before the steady-state part. It now records `mount_wide_query=refused(<errno>)
  after_ms=<n>` in `times.txt` and goes on; any other error still stops it. The per-domain
  queries stay strict: a refused first `QUERY2` is a failure. The steps of issue #68 on current
  `main` hit both problems for an external tester: `$BIN/query` was never built (below), and on
  a disk as slow as the reference's the mount-wide `QUERY` would stop the script; the first is
  fixed here, the second is recorded instead of fatal. `index_build_work=65536` was not used:
  the per-domain build fits the default, and the reference had no such option.
- **User 1001.** `ondemand.sh` runs its steady-state queries with `bench-load --uid 1001`, which
  only calls `setgid`/`setuid` (no account needed); the files W1 writes are `0644` in `0755`
  directories, readable by that uid. The guest still creates `moobbench` with uid 1001 so the
  identity is explicit (the cloud-init user is 1000).
- **`$BIN/query` built by `make bench-tools`.** `bench/ondemand.sh` calls `$BIN/query`
  (`tests/query.c`), which no make target built: `bench/host-setup.sh` and the docs compiled it
  by hand, and issue #68's steps had no such line, so they would stop at `ondemand.sh`. The new
  rule `$(BUILD)/query` uses the flags of `vm/build-tests.sh` (`-Wall -Wextra -Werror`, not the
  stricter `$(WARN)` the program was never written for). The issue text itself is not changed
  here (GitHub is outside this unit).
- **The workflow** (`.github/workflows/bench.yml`): `schedule` (Monday 05:23 UTC),
  `workflow_dispatch`, and `pull_request` limited to the bench workflow and harness files, so the
  batch's pull request validates it once without running on every pull request. `permissions:
  contents: read`; actions pinned by the commit hashes already used in this repository
  (`checkout`, `cache`, `upload-artifact`); the KVM udev rule of `interop.yml`; the cloud image
  cached under its date; no secrets, no external services besides the Debian image download, no
  results pushed back. `timeout-minutes: 60` per job.

## Alternatives

- **QEMU directly on the runner (`MOOB_HOST_QEMU=1`)** would skip the Docker build (about two
  minutes). Not taken: the container path is the one tested locally, and its QEMU and tools are
  the pinned Debian 13 ones rather than Ubuntu's.
- **A loop file on the guest's root disk** for the lower: simpler, but the lower would then sit
  on ext4 over a qcow2 overlay with the host's page cache in front, and the cold rows would not
  be cold.
- **An arm64 job under TCG**: rejected above. **A self-hosted runner on the Mac mini**: rejected,
  a public repository's pull requests could run code on it.
- **Failing on a slowdown against the reference**: rejected above; the summary shows the numbers
  for a human to read.

## Changes

- `Makefile`: `$(BUILD)/query` in `bench-tools`; `make bench-test`.
- `vm/install-test/run.sh`, `vm/install-test/in-container.sh`: `MOOB_TEST=bench`
  (`build/bench-test/`), `MOOB_BENCH_FS` passed into the container and the guest, the second
  disk, KVM required, `results.tar` copied out, marker `MOOBFS_BENCH_PASS`.
- `vm/bench-test/guest.sh`: new guest script.
- `bench/ondemand-summary.py`: the Markdown table against the reference, exit 1 on different
  counts.
- `bench/ondemand.sh`: a mount-wide `QUERY` refused with `E2BIG` or `ETIMEDOUT` is recorded,
  not fatal.
- `.github/workflows/bench.yml`: new workflow; `.github/labeler.yml`: `area/bench-m4` for it.
- Docs: `docs/testing.md` (target row and CI paragraph), `docs/benchmark-plan.md` (dated
  addition), `bench/results/README.md`, `ROADMAP.md` (M4 report row), `docs/references.md` §36,
  `CHANGELOG.md`.

## Evidence

On the dedicated host (Mac mini, Intel i7-2635QM, SATA SSD, Debian 13, KVM), from a copy of the
branch committed in a scratch Git repository, `MOOB_TEST=bench MOOB_BENCH_FS=ext4 sh
vm/install-test/run.sh`:

| Run | Lower disk | Result |
| --- | --- | --- |
| 1 | O_DIRECT, guest flushes honoured, two other VMs on the host | W1 stopped by hand after about 30 min (about 400 MB written) |
| 2 | O_DIRECT, flushes ignored, one other VM | W1 469 s; cold first `QUERY2` of `ns3` refused with `ETIMEDOUT`, twice; `MOOBFS_BENCH_FAIL` |
| 3 | same, host idle | W1 447 s; the same `ETIMEDOUT` (console: `building the index of a domain failed (-110)`) |
| 4 | host page cache (`MOOB_BENCH_CACHE=host`), host idle | `MOOBFS_BENCH_PASS`, 296 s from boot to the end; W1 32 s, `ondemand.sh` 18 s |

Run 4 through `bench/ondemand-summary.py` against `bench/results/2026-10-13-macmini-ondemand/new/`
(exit 0; smoke test of the harness, one run, not a measurement; "cold" is cold in the guest
only, so it is not comparable with the reference's cold rows):

| | reference (bare metal, 6.12.111, module of journal 0098) | run 4 (KVM guest, 6.12.113, this branch) |
| --- | --- | --- |
| mount, cold | 308 ms, 308 ms | 33 ms, 48 ms |
| mount, warm | 4 ms, 5 ms | 9 ms, 10 ms |
| first `QUERY2` on `ns3`, cold | 2.699 s, 2.681 s | 1.098 s, 1.088 s |
| first `QUERY2` on `ns3`, warm | 100 ms, 97 ms | 137 ms, 146 ms |
| second `QUERY2` | 6 ms (four times) | 18, 19, 20, 11 ms |
| index memory, one domain | 4.5 MB | 4.6 MB (4 592 879 bytes) |
| mount-wide `QUERY`, cold / warm | 19.668 s / 742 ms | 8.488 s / 864 ms (not refused) |
| index after everything | 36.4 MB, 437 882 entries, 60 000 inodes | 36.9 MB, 437 882 entries, 60 000 inodes |
| results; inodes with one domain | 3 030; 7 459 | 3 030; 7 459 |

Run 4 used `bench/ondemand.sh` with the refusal handling; its mount-wide `QUERY` was not
refused, so that path ran only on a synthetic `times.txt` through `ondemand-summary.py` (refused
row shown, mount-wide count not compared, exit 0; a changed per-domain count gives exit 1, checked
with the old module's `times.txt`).

Checks: `make lint` (shellcheck and SPDX) passed on the host; `make bench-tools` with `gcc` and
`clang` built `query` with `-Wall -Wextra -Werror`; the workflow parsed with PyYAML and passed
actionlint 1.7.7 (`rhysd/actionlint@sha256:887a259a5a534f3c4f36cb02dca341673c6089431057242cdc931e9f133147e9`,
with its shellcheck integration); `scripts/check-retired-claims.sh` passed. btrfs and XFS were not
run locally; nothing ran on GitHub yet.

## Limitations

- Not yet run on GitHub: the runner's KVM, its memory and the btrfs and XFS jobs are first seen
  in the batch's pull request. Only ext4 ran locally.
- The "cold" rows are cold in the guest only: the lower's disk image is in the host's page
  cache. The workflow watches the CPU and memory path of the build and the query, not the disk.
- The refusal path of `bench/ondemand.sh` has not run against the module (run 4's mount-wide
  `QUERY` finished in 8.5 s).
- The reference is ext4 only, from the module of journal 0098 on kernel 6.12.111; btrfs and XFS
  runs are compared with it for orientation only, and the module has changed since (releases,
  fair shares).
- One run per week and per filesystem, no repetitions, no intervals: the method of
  `docs/benchmark-plan.md` for a reported comparison is not met, by design.
- The cloud image and its kernel are pinned (`20261021-2638`, `6.12.113+deb13-amd64`). When
  Debian removes `linux-headers-6.12.113+deb13-amd64` from its mirror the guest's `apt-get` will
  fail; that failure is the signal to move the pin of `vm/install-test/` and
  `scripts/supported-kernels` together, as for `make install-test`.

## Next steps

- First run in the batch-9 pull request (all three filesystems); read the runner's CPU and disk
  from `system.txt` and record the numbers in this journal with the run's link.
- Issue #68 stays open for a run on other physical hardware.
