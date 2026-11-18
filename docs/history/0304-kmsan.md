# 0304 — KMSAN over the layer suite (security roadmap S8)

Date: 2026-11-17. Work started at 19:05 Brasília time. Status: **done: the full layer suite
passed under KMSAN (k4) with no report in the module.** Ended 2026-11-18 at 06:40. Estimated effort
M (one to three days; S8's remaining item); calendar time about 11.5 hours, most of it unattended
runs on the test host.

## Goal

Run the layer suite under KMSAN, the kernel's detector of uses of uninitialized memory, the one
item of S8 still open (`docs/security-roadmap.md`). The layer copies data the lower filesystem
returns, and copies to user space (`QUERY2` pages, `GET_INFO`), so an uninitialized byte would be
an information leak.

## Prior art read

Linux 6.12.116, on the test host: `Documentation/dev-tools/kmsan.rst` (Clang 14.0.6 or newer),
`lib/Kconfig.kmsan` (`KMSAN` depends on `!KASAN && !KCSAN` and on a Clang that accepts
`-fsanitize=kernel-memory`), `mm/kmsan/init.c` (boot line "Starting KernelMemorySanitizer"),
`mm/kmsan/report.c` (reports start "BUG: KMSAN: <type> in <function>"). The test host has Debian's
Clang 19.1.7.

## Decisions

- One more fragment, `vm/kmsan.config`: the KCSAN fragment of journal 0115 with KCSAN replaced by
  `CONFIG_KMSAN=y` and `CONFIG_KMSAN_CHECK_PARAM_RETVAL=y`. Like KCSAN, KMSAN needs a kernel of
  its own. Lockdep was in the first version of the fragment and is out since run k2 (below).
- `vm/kasan-local.sh` gets `KCC=clang`. `vng --kconfig` evaluates the configuration with GCC, which
  drops KMSAN, so the configuration is evaluated again with `make CC=clang olddefconfig` and the
  kernel and the module are built with Clang; the run stops if any `CONFIG_*=y` of the fragment is
  missing afterwards. With `KCC` empty (the default) nothing changes, and the kernel cache of
  earlier runs stays valid.
- A KMSAN report is a `BUG:` line, which `vm/kdiag.sh` already fails on. Whether reports of the
  kernel itself, outside the module, appear and need to be judged apart, as KCSAN's are, is left
  to the first run.
- `vm/kasan-guest.sh` names "KMSAN" in its final marker when the boot printed KMSAN's line.

## Runs

- k1, `b9c1816`, 19:06–19:38: the kernel built with Clang, then the run stopped with
  "kasan-local: CONFIG_KMSAN is not set with CC=clang", as the check meant it to, but only after
  a full kernel build without KMSAN. `vng --kconfig` evaluates the fragment with GCC and writes
  `# CONFIG_KMSAN is not set`; `olddefconfig` with Clang keeps an explicit choice. Every
  dependency held (`CC_IS_CLANG`, `HAVE_KMSAN_COMPILER`, no KASAN or KCSAN). Fix: the fragment is
  merged in again with `scripts/kconfig/merge_config.sh -m` before `make CC=clang olddefconfig`,
  and the check runs before the build. On the test host this gives `CONFIG_KMSAN=y`,
  `CONFIG_KMSAN_CHECK_PARAM_RETVAL=y` and every other `=y` of the fragment.

- k2, `16f4004`, from 19:40: `CONFIG_KMSAN=y`; kernel and module built with Clang 19.1.7. The
  guest's console stopped at 0.66 s, right after "Starting KernelMemorySanitizer", at 20:18, and
  QEMU ran at 100 % of a CPU with no more output until the run was stopped at 21:43. A normal
  KASAN console records the whole run, so this was not a quiet log.
- Boot-only tests (`vng -r ... --memory 4G --cpus 2 --verbose`, 20-minute limit), each kernel
  configured as the harness does:
  - the same fragment, 4 GiB instead of 3: stopped at the same place (0.75 s), 20 minutes;
    memory is not the cause;
  - virtme-ng's defaults with only `CONFIG_DEBUG_KERNEL` and `CONFIG_KMSAN`: booted, powered off
    at 37.7 s;
  - the whole fragment without `PROVE_LOCKING`, `LOCKDEP` and `PROVE_RCU`: booted, powered off
    at 36.2 s.
  So lockdep with KMSAN stalls the boot on this host; whether it is a hang or only very slow was
  not measured past 20 minutes. Decision: the KMSAN run goes without lockdep, which the KASAN run
  of every batch and the KCSAN run keep; `vm/kmsan.config` drops the three options.

- k3, `68d92fd`, from 23:15: KMSAN booted and the suite ran. At 685 s, in the first `fsync`
  through a layer over btrfs, KMSAN reported `use-after-free in __crc32c_le_base`, then many
  more (146 when first read: 128 in `__crc32c_le_base`, 18 in `vring_map_one_sg`). The first
  report's use is btrfs's checksum of one of its own tree blocks (`csum_tree_block`,
  `btree_csum_one_bio`, `btrfs_sync_log`, `btrfs_sync_file`, `vfs_fsync_range`), reached from
  `moobl_fsync`, which only passes the `fsync` to the lower; the memory was created by
  `free_unref_page` from `tlb_remove_table_rcu`, the kernel's freeing of page-table pages. Neither
  the use nor the memory is the module's. Whether it is a btrfs defect or a KMSAN false positive
  is not judged here.
  The run ended at 02:18 on 2026-11-18 with `MOOBFS_VM_FAIL: overbudget-race-ext4`, after every
  step before it had passed. The step did not fail on what it checks: `24 queries of x and y,
  0 EAGAIN, 0 ENOMEM, 0 other`, with 23 releases of x and y by the budget, then
  `OVERBUDGET_RACE_FAIL: only 24 queries before the time cap`. `tests/overbudget-race.c` runs
  until 30 queries, up to six times the seconds it is given; the suite gives 10, and under KMSAN
  a build of x or y took about 2.5 s, so 24 fit in the 60-second cap. Fix (`vm/suite.sh`): the
  step gives 30 seconds when the harness runs KMSAN (a 180-second cap), 10 otherwise; the pass
  condition is unchanged. The steps after it did not run in k3.
  Judged afterwards with the committed `vm/kdiag.sh`, the whole k3 console (40 438 KMSAN
  reports) gave exit 0 and `KMSAN_OUTSIDE: 40438 report(s) with neither the use nor the memory in
  moobfs_layer (39300 reached through it)`. A copy of the first, wrong rule left on the test host
  had been run over it first and blamed 35 197 of them on the module; it was deleted.

- A first k4 at 02:50 stopped at once: the background shell that started it lacked virtme-ng's
  environment on `PATH` (`kasan-local: vng (virtme-ng) not in PATH`). Nothing ran; it was started
  again in a fresh clone.
- k4, `8e406c6`, 02:51–06:02 (the kernel from the cache; the suite took about 3 h 10 min):
  `MOOBFS_KASAN_PASS: layer suite under KMSAN with no kernel diagnostics`, `kasan-local: pass`.
  Every step ran; `OVERBUDGET_PASS` on ext4, btrfs and XFS. Skipped, as in every run of this
  kernel configuration: `memcg`, `limits` and `diag`'s memory-controller parts (no
  `CONFIG_MEMCG`), `dirstall` (no userfaultfd for kernel faults) and `landlock`. The guest's
  `kdiag` printed `KMSAN_OUTSIDE` three times (its dmesg slices: 53, 53 and 3 reports).
  The console held 20 280 KMSAN reports: `use-after-free` in `__crc32c_le_base` (17 125) and in
  `vring_map_one_sg` (1 844), `uninit-value` in `__crc32c_le_base` (1 016), in
  `vring_map_one_sg` (289) and in `rcu_core` (6). `kdiag` over the whole console: exit 0,
  `KMSAN_OUTSIDE: 20280 report(s) with neither the use nor the memory in moobfs_layer (18969
  reached through it)`. None has its use, or the creation or store of its memory, in the module.

## The console is judged as well as the dmesg

k4 showed that the guest's `dmesg` held 53 of the 20 280 reports: the kernel's ring buffer keeps
only the latest messages, and the guest judges it once, after the suite (the fault-injection blocks
judge and clear their own slices). A report of the module early in a long run could have been
overwritten before the guest read it. Fix: after the guest's own checks, `vm/kasan-local.sh`, and
the `KASAN and lockdep` and `KCSAN` workflows, run `kdiag` over the whole console with the guest's
patterns, and fail on what it finds. Checked on the test host against earlier consoles before it
was committed: four full KASAN, UBSAN and lockdep runs (b26/27, b28, b29 and the 6.12.116 release
candidate), one KCSAN run (`KCSAN_OUTSIDE: 30`) and k3 and k4 under KMSAN: exit 0 on all. Its first CI run is
the next section.

## KMSAN reports judged as KCSAN's are

`vm/kdiag.sh` now treats a KMSAN report as journal 0115 treats a KCSAN one. Each stack of a
report (the use, then each "Uninit was stored to memory at:", "Uninit was created at:" or "Local
variable ... created at:") is decided by its first frame that is not a generic helper (an
allocator, a copy, KMSAN itself). One in `moobfs_layer` fails the run, as the use or as the
origin of the memory; this catches an uninitialized buffer of the layer copied to user space,
whose use starts in `instrument_copy_to_user` and whose memory was allocated by the layer. The
module further down a stack, as the caller that passed an `fsync` or a `rename` to the lower,
does not fail it. Other reports are counted on one `KMSAN_OUTSIDE` line, with how many were
reached through the layer.

The first version of the rule failed a report when `moobfs_layer` appeared anywhere in an origin
stack. Run on the k3 console, it blamed the module for nearly all of btrfs's reports: they have an
"Uninit was stored to memory at:" stack in which btrfs's `__write_extent_buffer`, during the
copy-on-write of its own tree block, is reached from a `rename` that `moobl_rename` passed to
`vfs_rename`. The store is btrfs's, and so is the memory (created by `free_unref_page`); the rule
was narrowed to the first non-helper frame before anything was committed with it.

`tests/kdiag/run.sh`, now part of `make test`, runs `kdiag` on six captures: the first k3
report with its "stored to memory at" stack through `moobl_rename`, and a `vring_map_one_sg`
one (outside, counted); four made-up reports (use in the module; memory allocated by the module
and copied to user space; a local variable of the module copied to user space; a `kmalloc` of
the module behind the allocator's own frames); and a plain `WARNING:`. `KDIAG_TEST_PASS` with
BSD awk (macOS), gawk 5.2.1 (Debian 13) and mawk (Ubuntu 26.04, the CI runner's awk). Negative
round: with the `kdiag.sh` of `main`, five of the six fail (`KDIAG_TEST_FAIL: 5`): every KMSAN
report fails the run, the outside ones included, and none names why.

Over the whole k3 console as far as it had run (23 242 KMSAN reports): exit 0 and one line,
`KMSAN_OUTSIDE: 23242 report(s) with neither the use nor the memory in moobfs_layer (22148
reached through it)`. The kinds: `use-after-free` and `uninit-value` in `__crc32c_le_base` and
in `vring_map_one_sg` (btrfs's tree-block checksums and their trip to the virtual disk), and
three `uninit-value in rcu_core` on an idle CPU, from a local variable of RCU itself.

## Checks before publication

On the test host, in a fresh clone of the branch: `make lint`, `make checkpatch`, `make test`
(with `tests/kdiag/run.sh`), `make pylib-test`, `make hostile-test`, `make mutants-check`,
`scripts/check-retired-claims.sh`, `scripts/check-pins.sh`, `scripts/check-matrix-page.sh`,
`scripts/check-site.py` and `git diff --check main...HEAD`. The first pass failed twice, and both
were fixed: shellcheck SC2013 on the loop of `vm/kasan-local.sh` that checks the fragment's options
(rewritten as a `while read` that collects the missing ones; checked against the cached KMSAN
kernel's `.config`, where it passes, and with a made-up option added, where it stops and names
it), and trailing spaces in `tests/kdiag/` captures, which are the kernel's own empty report
lines (`.gitattributes` exempts those captures from whitespace checks). The second pass: all
`PASS`. No C code changed, so the sanitizer build of the tools and the Ubuntu test build were not
run; the CI runs both.

## The first CI run of the console check

On pull request 213 the four `KASAN and lockdep` legs (6.12.116, 7.0.14, 7.3.3, 7.3.4) failed on
the new check, after `MOOBFS_KASAN_PASS` in each. The cause was not the kernel: the workflows' console
also has the host's QEMU, which printed `WARNING: Image format was not specified for '...img' and
probing guessed raw.` for each of the five raw disks, and `^WARNING:` matched it. The local
consoles of the test host had no such line, so the check against them before the push did not
show it. Fix: the check reads only the console's kernel lines, those that start with the kernel's
timestamp, which is what the guest's dmesg holds. Checked: the four consoles of that CI run fail
without the filter and pass with it; the seven local consoles above pass; a KASAN console with
QEMU's line added passes, and with a KMSAN report of the module or a kernel `WARNING:` in the
module added it fails.

## Decisions after the runs

- KMSAN runs on the test host, by hand, not in a workflow: k4 took about 3 h 10 min of suite on a
  KVM host after a Clang build of about 30 minutes, and a hosted runner gives 4 CPUs and a 6-hour job limit,
  with no kernel cache between runs. It is run again when the module's copies to user space or
  its handling of lower data change, as the KCSAN run is for races.
- S8 is complete: KCSAN (journal 0127), the hardening options of journal 0237 and now KMSAN.

## Limitations

- One KMSAN run, on 6.12.116 with Clang 19.1.7, x86-64; not on 7.x, not with lockdep.
- KMSAN sees only paths the suite takes. The fault-injection steps ran, so error paths that fail
  allocations were covered as far as `failnth` reaches.
- The reports of btrfs and virtio were not investigated as kernel defects; they are counted.

## Next step

None for S8. The pull request of this batch runs the new console check in the `KASAN and lockdep`
and `KCSAN` workflows.
