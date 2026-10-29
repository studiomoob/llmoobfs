# 0230 — The moobfs CLI without /proc/self/mountinfo (issue #167)

Date: 2026-10-29. Status: merged into batch 6 with journal 0229 (C6), whose
evidence lists the batch's full `make vm-test` and KASAN runs. Work started: 2026-10-29 09:29 UTC; ended 09:45 UTC. Wall-clock time, with the VM
runs, not an engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

Issue #167: `moobfs query` stopped with exit 1 inside a Landlock sandbox that may list the domain
directory but not read files under `/proc`, although the `QUERY2` ioctl itself would answer. The
CLI read `/proc/self/mountinfo` (`find_mount()`, `tools/moobfs.c`) before every `query` and
`info`, and for `query` the mount record only gave `domain_depth` (the v1 fallback and the
wording of an `ENOTTY`). Acceptance: the same results under the sandbox as without it; a VM
suite step that runs the CLI under `tests/landlock_run.c`, which `vm/build-tests.sh` did not
build; `docs/tools.md` stating what the CLI reads.

## Decisions

- User space only (ADR-0022): no change to `layer/`, the UAPI or the format.
- The mount record stays the first source; it is now optional. When `/proc/self/mountinfo`
  cannot be opened, for any reason, the layer is recognised by `statfs` type `0x4d4f4f42` (the
  detection of client contract §7 and of `python/src/moobfs/domain.py`), defined locally in
  `tools/moobfs.c`. `MOOB_STAT_MAGIC` in `include/moobfs_format.h` has the same value but names
  the image format, so it was not reused.
- `query` without the record: `QUERY2` as before; on `ENOTTY` or `EOPNOTSUPP` it asks
  `GET_INFO` on the same descriptor, which only the mount root answers, and so learns the domain
  depth there. The mount-wide `QUERY` of a mount without domains is therefore kept, beyond what
  the issue required. Elsewhere the depth stays unknown and the `ENOTTY` message says so; the
  exit status is unchanged (69).
- `info` without the record: the mount root is taken as the highest directory of the real path
  on the same device, the method of coreutils' `find_mount_point()` (references §28). A bind
  mount of a subdirectory cannot be told from the root then, except by `GET_INFO` refusing it
  with `ENOTTY`, which `info` reports as such (69). `index_build_work` and `index_idle` appear
  only in the record: the text says "unknown" and `--json` prints `null` for both.
- Alternatives left out: a GET_INFO probe of the walked-up root from `query` (needs directory
  listing on the root, which a domain sandbox usually lacks, for the wording of one message);
  `/proc/self/mounts` (same restriction); replacing the record by `statfs` always (changes
  what works today for no gain).

## Changes

- `tools/moobfs.c` (commit 84beb88): `struct mnt` has `record`; `find_mount()` falls back to
  `statfs` and `top_of_device()`; `cmd_info()` skips the bind-mount check without the record,
  reports a refused `GET_INFO` and prints unknown options; `cmd_query()` keeps the depth as
  `-1` until `GET_INFO` tells it; `query_error()` has the unknown-depth wording.
- `vm/build-tests.sh`: builds `landlock_run` (static, `-Wall -Wextra -Werror`, no source change).
- `vm/suite.sh` (commit 5d30b9b): step `landlock`, in the full suite after `ixlock` and in
  `MOOB_ONLY`. On ext4, as uid 1001 under `landlock_run`, it first checks that the sandbox
  denies `/proc/self/mountinfo` (`tests/query.c ... open`), then compares the CLI with and
  without the sandbox (query on a domain in text and JSON, against `moobfs-index --domain` too;
  exit 69 for a directory that is not a domain and for a path off the layer; `info --json` on the
  root, the two options `null`; `info` from a domain directory; the mount-wide `QUERY` on a
  mount without domains). Without Landlock in the guest kernel it prints `LANDLOCK_SKIPPED`. The
  programs are copied to `/tmp/ll-bin`, as the KASAN harness keeps `BIN` where uid 1001 cannot
  reach it (journal 0198).
- `docs/tools.md` (what the CLI reads and what is lost without the record), `man/moobfs.1`,
  `docs/testing.md` (markers `LANDLOCK_PASS` and `LANDLOCK_SKIPPED`; `landlock_run` no longer a
  standalone helper; Landlock coverage in §9), `docs/retired-claims.txt` (the old wording),
  `docs/references.md` §28, `CHANGELOG.md`, this journal and its index row.

## Evidence

Dedicated host (Debian 13, kernel `6.12.111+deb13-amd64`), VM runs with `MOOB_VM_KVM=1` on the
Debian 6.12.111 guest kernel (Landlock ABI 6).

- Build: `make attr-tools` with gcc and with `CC=clang` (`-Werror`, `-Wconversion`,
  `-Wshadow`): no warning; `landlock_run.c` with `gcc` and `clang -static -O2 -Wall -Wextra
  -Werror`: no warning.
- On the host, without the module, a static build under `landlock_run /tmp/x /tmp/x moobfs query
  /tmp/x kind eq string x`: exit 69, "is not on a moobfs_layer mount" (statfs works under the
  sandbox).
- `MOOB_VM_KVM=1 MOOB_ONLY="ext4 landlock" make vm-test`: `MOOBFS_VM_PASS`, `CLI_PASS: ext4`
  (the paths with the mount record, unchanged), `LANDLOCK_PASS`, `MOOBFS_ONLY_PASS: steps ext4
  landlock`, exit 0.
- Negative round, the same suite with `tools/moobfs.c` of `main` (1d7376f),
  `MOOB_VM_KVM=1 MOOB_ONLY=landlock make vm-test`: `moobfs: /proc/self/mountinfo: Permission
  denied`, `MOOBFS_VM_FAIL: ll-query`, exit 2.
- `make lint` (shellcheck, SPDX): clean. `mandoc -Tlint -Wwarning man/moobfs.1`: clean (on the
  workstation; the test host has no mandoc). `scripts/check-retired-claims.sh`:
  `RETIRED_CLAIMS_OK`.
- Not run: the full `make vm-test` and the KASAN/lockdep suite (no kernel change; left for the
  batch's once-per-batch runs, where the step also runs).

## Limitations

- Without the record a missing module is reported as a path not on a layer (`/proc/filesystems`
  is not readable either), and `index_build_work` and `index_idle` are unknown.
- `info` needs directory listing on the mount root; a sandbox confined to its domain still
  cannot read `GET_INFO` (client contract §7, open point).
- The walk up the device stops below a bind mount of the same layer placed above the path, so
  `info` may then report the bind mount's root and refuse it.
- The path-based LSM limit on `QUERY2` results (`docs/faq.md`; journal 0030) is unchanged and
  separate.
- The KASAN kernel of `vm/kasan.config` has no Landlock, so the step prints `LANDLOCK_SKIPPED`
  there (batch run of journal 0229); it runs on the Debian kernels of `make vm-test` and CI.

## Next step

Merge into the batch; the full `make vm-test` and KASAN runs of the batch include the step
`landlock`. Then issue #167 can be closed with this journal.
