# 0291 — Release-candidate runs on `8a8d09b` for `v0.6.0-experimental`

Date: 2026-11-12 and 13. Runs 20:24 on the 12th to 02:05 on the 13th, Brasília time. Status:
**done; the candidate passes every run; the tag waits for the stage-2 declaration (journal
0284).** Estimated effort S; the time recorded is machine time on the test host.

## Goal

Run, on the `main` of the evening (`8a8d09b`, after #197, #198 and #199), every suite a release
of this series is held to, before the tag `v0.6.0-experimental` (journal 0284: cut only when
stage 2 is declared).

## Runs (test host, Debian 13, KVM)

First pass, one clone for all runs, in sequence:

| Run | Times | Result |
| --- | --- | --- |
| `MOOB_VM_KVM=1 MOOB_DEBIAN_KERNEL=6.12.111 sh vm/run.sh` | 20:24–20:50 | `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` |
| same, `6.12.113` | 20:50–21:16 | same three markers |
| same, `6.12.115` | 21:16–21:42 | same three markers |
| same, `7.3.3` | 21:42–22:09 | `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` (M1 module not built on 7.3) |
| same, `7.3.4` | 22:09–22:33 | `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` |
| `MOOB_UBUNTU_KERNEL=7.0.0-43` | 22:33–22:39 | failed: the guest panicked at boot, `Out of memory and no killable processes` |
| KASAN, `make install-test`, `container-test`, `package-test`, `repro-test` | 22:39 | did not start: `Permission denied` on `build/` |

The failures were the queue's, not the project's: every run shared one clone, and the
container runs of `vm/run.sh` leave `build/` owned by root, so the later runs could not write
there. The Ubuntu guest's boot panic in the same clone is attributed to that shared `build/vm`
(the guest has 512 MiB); this is a hypothesis, not verified. *Note, 2026-11-14: wrong. The
Ubuntu guest panics at boot in 512 MiB in about one boot in five, also in CI's fresh checkouts; it gets
1 GiB since journal 0292.*

Second pass, one fresh clone per run:

| Run | Times | Result |
| --- | --- | --- |
| `MOOB_VM_KVM=1 MOOB_UBUNTU_KERNEL=7.0.0-43 sh vm/run.sh` | 23:36–00:02 | `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` |
| `sh vm/kasan-local.sh` (upstream 6.12.116) | 00:02–01:23 | `MOOBFS_KASAN_PASS` (with `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_PYLIB_PASS`) |
| `make install-test` | 01:23–01:31 | `MOOBFS_INSTALL_PASS` |
| `make container-test` | 01:31–01:45 | `MOOBFS_CONTAINER_PASS` |
| `make package-test` | 01:45–01:56 | `MOOBFS_PACKAGE_PASS` |
| `make repro-test` | 01:56–02:05 | `MOOBFS_REPRO_PASS`, `MOOBFS_REPRO_DEB_PASS` |

The upstream 7.3.3, 7.3.4 and 7.0.14 KASAN legs were not rerun here: each passed in CI on the
pull requests that led to `8a8d09b` (#197, #198), and 7.3.4's on the test host (journal 0287).

Exploratory runs on s390x (2026-11-14, a LinuxONE guest with 2 CPUs and 7 GB, KVM, from the IBM
LinuxONE open source cloud, journal 0280), each on a fresh clone of `main` at `f960474` (#200):

| Run | Times | Result |
| --- | --- | --- |
| `MOOB_ARCH=s390x MOOB_VM_KVM=1 sh vm/run.sh` | 07:58–08:13 | `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` |
| same, again | 08:13–08:26 | same three markers |
| `MOOB_ONLY="ext4 btrfs xfs" sh vm/kasan-local.sh` (upstream 6.12.116 built for s390x) | 08:26–10:09 | `MOOBFS_KASAN_ONLY_PASS`, no kernel diagnostics |

The KASAN run covers only the per-lower steps (a partial run, chosen for the machine's two CPUs);
the full KASAN suite has not run on s390x.

Note of 2026-11-14: the last sentence is wrong. The full KASAN suite ran on s390x in journal 0280
and stopped at the step `release`, on the build's 10 s CPU bound. It had not run on `f960474`.

The full KASAN suite on s390x, on a fresh clone of `43ac8e9` (batch 28, before #202's merge), same
guest:

| Run | Times | Result |
| --- | --- | --- |
| `sh vm/kasan-local.sh` (upstream 6.12.116 built for s390x) | 10:33–13:45 | **failed** at `release`, `MOOBFS_VM_FAIL: rl-measure-ext4` |
| the step `release` alone, with the tracepoint `moobfs_domain_build` enabled (a test clone; the repository unchanged) | 13:56–14:05 | **failed** the same way; `moobfs_domain_build: ... err=-110 tries=1 released=0 bytes=0 ns=12809729731` |

The same failure as journal 0280, with the same cause. The first build of the step's 500-file
domain needs 12.8 s of CPU under KASAN on this 2-CPU guest (12.7 s in 0280), more than
`MOOBL_BUILD_CPU_NS` (10 s, ADR-0031). The module refuses the build with `ETIMEDOUT`, as
specified. This is the known limit of slow debug kernels in `docs/testing.md` §9 and
`docs/matrix-and-limits.md`, left unchanged by the maintainer's decision of 2026-11-10. No
surface changes. The steps after `release` still have not run in one full KASAN suite on s390x;
journal 0280 ran them by name.

## Limitations

- The tag's commit will be later than `8a8d09b` (batches 26 and 27 at least); journal 0284's
  rule 3 says what must be rerun then.
- The cause of the first Ubuntu boot failure was not isolated.

## Next step

The stage-2 declaration, after the independent reproduction of issue #195.

## Merges recorded here

- #199 (batch 25, journal 0288) entered `main` on 2026-11-12 at 20:22 as `8a8d09b`, the commit
  these runs used.
- #200 (batch 26, journal 0289) entered `main` on 2026-11-14 at 07:23 as `f960474`. Its
  pre-push checks, rerun after the power failure, passed on `c3de5d2` (2026-11-13, 18:55–19:01).
  In its CI the `Native Linux VM (Ubuntu 7.0.0-43)` job panicked while the guest booted, before
  the module loaded ("bio: can't create integrity buf pool"); rerun, it passed. The same job had
  panicked at boot on `main` twice on 2026-11-12 (runs 43201204547 and 43328363771, "System is
  deadlocked on memory").
- #201 (batch 27, journal 0290) entered `main` on 2026-11-14 at 08:48 as `138f250`.
- #202 (batch 28, journals 0292 and 0293) entered `main` on 2026-11-14 at 13:34 as `f4060d8`.
  Its CI passed on `43ac8e9`, the Ubuntu 7.0.0-43 job with the guest's 1 GiB and each KASAN
  job with `FAULTS_E2FSCK: exit 1 after the injected faults`.
- #203 (batch 29, journal 0294) entered `main` on 2026-11-14 at 21:31 as `cdf36b1`. Its first CI
  run failed Lint: the matrix page had 71 lines, over the limit of 70 of
  `scripts/check-matrix-page.sh`. The fix, `6171632`, passed all 21 jobs; the test host's pre-push
  checks now run that script too.
- #204 (batch 30, journals 0291 and 0295) entered `main` on 2026-11-15 at 10:01 as `1f9dd80`.
  Its first CI run failed the static-analysis job: the justified Coccinelle entry of
  `tests/static/baseline.txt` quoted the old line of `result_path()`. The fix, `a11f62b`, passed
  all 26 jobs. The test host's pre-push checks did not run `make static`; they do now.
  The merge also closed issue #117: the pull request body ended with "Closes #117 after merge
  (manual close)", which GitHub reads as a closing keyword. The maintainer added the closing
  comment afterwards, with the fix, the evidence and the limits:
  <https://github.com/studiomoob/llmoobfs/issues/117#issuecomment-6894925826>. Later pull request
  bodies use a closing keyword only when the automatic close is meant.
- #205 (batch 31, journals 0296 and 0297) entered `main` on 2026-11-16 at 06:50 as `b40e3b6`,
  after all 22 jobs passed. The tag `v0.6.0-experimental` is on that commit (journal 0297).
