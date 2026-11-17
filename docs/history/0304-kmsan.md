# 0304 — KMSAN over the layer suite (security roadmap S8) (in progress)

Date: 2026-11-17. Work started at 19:05 Brasília time. Status: **in progress; the harness is
written; the first run is pending.** Estimated effort M (one to three days; S8's remaining item);
the time recorded is calendar time.

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
  `CONFIG_KMSAN=y` and `CONFIG_KMSAN_CHECK_PARAM_RETVAL=y`; lockdep stays. Like KCSAN, KMSAN needs
  a kernel of its own.
- `vm/kasan-local.sh` gets `KCC=clang`. `vng --kconfig` evaluates the configuration with GCC, which
  drops KMSAN, so the configuration is evaluated again with `make CC=clang olddefconfig` and the
  kernel and the module are built with Clang; the run stops if any `CONFIG_*=y` of the fragment is
  missing afterwards. With `KCC` empty (the default) nothing changes, and the kernel cache of
  earlier runs stays valid.
- A KMSAN report is a `BUG:` line, which `vm/kdiag.sh` already fails on. Whether reports of the
  kernel itself, outside the module, appear and need to be judged apart, as KCSAN's are, is left
  to the first run.
- `vm/kasan-guest.sh` names "KMSAN and lockdep" in its final marker when the boot printed
  KMSAN's line.

## Runs

- k1, `b9c1816`, 19:06–19:38: the kernel built with Clang, then the run stopped with
  "kasan-local: CONFIG_KMSAN is not set with CC=clang", as the check meant it to, but only after
  a full kernel build without KMSAN. `vng --kconfig` evaluates the fragment with GCC and writes
  `# CONFIG_KMSAN is not set`; `olddefconfig` with Clang keeps an explicit choice. Every
  dependency held (`CC_IS_CLANG`, `HAVE_KMSAN_COMPILER`, no KASAN or KCSAN). Fix: the fragment is
  merged in again with `scripts/kconfig/merge_config.sh -m` before `make CC=clang olddefconfig`,
  and the check runs before the build. On the test host this gives `CONFIG_KMSAN=y`,
  `CONFIG_KMSAN_CHECK_PARAM_RETVAL=y` and every other `=y` of the fragment.

## Next step

The first full run on the test host:
`KCONFIG=vm/kmsan.config KCC=clang KCACHE=$HOME/.cache/moobfs-kmsan sh vm/kasan-local.sh`.
