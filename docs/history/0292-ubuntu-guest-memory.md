# 0292 — The Ubuntu guest panicked while booting in 512 MiB; it gets 1 GiB

Date: 2026-11-14. Work started 06:31 Brasília time; ended about 07:40. Status: **done; pull
request with the maintainer's agreement, after batch 27.** Estimated effort S; the time recorded
is calendar time of one morning session, most of it machine time on the test host.

## Goal

The CI job `Native Linux VM (Ubuntu 7.0.0-43)` failed on pull request #200 with a kernel panic
while the guest booted, before the module loaded. Rerun, it passed. Find out whether this was a
defect of the change, of the harness, or of the runner, and remove it if it is the harness's.

## Evidence before the change

The same job's results on the 13 CI runs from 2026-11-11 to 2026-11-13 (two of them
cancelled): three failures, all a panic within the first 1.4 s of boot, none after the module loaded:

| Run | Branch | Panic |
| --- | --- | --- |
| 43201204547 | `main`, 2026-11-12 | `System is deadlocked on memory` |
| 43328363771 | `main` after #199, 2026-11-12 | `System is deadlocked on memory` |
| 43516941467 | #200, 2026-11-13 | `bio: can't create integrity buf pool` |

The first pass of the release-candidate runs on the test host (journal 0291) panicked the same
way (`Out of memory and no killable processes`). In 6.12.116's `mm/oom_kill.c`,
`out_of_memory()` prints that line and then panics with `System is deadlocked on memory` when it
finds no process it can kill, which fits a panic before user space has started. The second message is not in
6.12.116's `block/`; Ubuntu's 7.0 sources were not read. It is taken to be another allocation
that failed during boot, not verified.

The guest had 512 MiB (`-m 512M` in `vm/in-container.sh`), for every kernel. The Debian jobs'
consoles were not searched for the same panic; none of the Debian runs of journals 0287 to 0291
failed at boot.

## Change

- `vm/in-container.sh`: the guest memory is a variable. Debian kernels keep 512 MiB; the Ubuntu
  guest gets 1 GiB. `MOOB_VM_MEM` overrides both, for experiments.
- `vm/run.sh` passes `MOOB_VM_MEM` into the container.
- `docs/development.md` (the boot line), `CHANGELOG.md`, and a dated note in journal 0291, which
  attributed the panic to a clone shared between runs.

Alternatives not taken: 1 GiB for every guest (it changes the memory the Debian runs have had
since the start, including the memory-pressure steps, for no observed failure); a retry of the
CI job on a boot panic (it hides the cause). No file under `layer/` changed.

## Evidence after the change

Test host, Debian 13, KVM, 8 CPUs and 15 GiB, one clone of commit `5a5e878` (same tree as
`b714ebf` on this branch), script `~/b11night/b28/q.sh`. Each round is `MOOB_VM_KVM=1
MOOB_UBUNTU_KERNEL=7.0.0-43 MOOB_ONLY=diag sh vm/run.sh` with `MOOB_VM_MEM` set; 512 MiB and
1 GiB alternated:

| Memory | Rounds | Times | Boot panics |
| --- | --- | --- | --- |
| 512 MiB | 10 | 06:35–06:48 | 2 (rounds 4 and 5, `System is deadlocked on memory`) |
| 1 GiB | 10 alternated, then 30 more | 06:36–07:33 | 0 |

Then the full Ubuntu suite at the new default, `MOOB_VM_KVM=1 MOOB_UBUNTU_KERNEL=7.0.0-43 sh
vm/run.sh`, 06:49–07:08: `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`.

## Limitations

- 40 boots without a panic bound the rate at 1 GiB; they do not prove it is zero. If it comes
  back in CI, the next step is the console of the failed boot, not more memory.
- Neither the runners' nor the test host's load during the failed boots is known; that both
  panicked the same way is what ties the cause to the guest, not to one host.
- The `bio: can't create integrity buf pool` panic is grouped with the other two by its timing and
  by the fix, not by reading its source.

## Next step

The pull request of this batch after batch 27's; the Ubuntu CI job then runs with 1 GiB.
