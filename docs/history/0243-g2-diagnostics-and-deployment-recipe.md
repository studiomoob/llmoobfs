# 0243 — G2: user-visible diagnostics and the deployment recipe (VM runs passed)

Date: 2026-10-29. Status: **implemented; dedicated-VM runs passed on 2026-10-30**. Implemented on
branch `claudiofp/b11-g2` (from `main` at fac2d4b), local commits, not pushed; offline tests
passed; the dedicated-VM runs that G2's acceptance needs and their negative rounds ran on
2026-10-30 (section "Dedicated-VM runs"). G2 closes when the maintainer accepts them. Work
started 23:09 UTC; offline part ended about 23:35 UTC; VM runs 2026-10-30 11:18 to 11:41 UTC
(wall-clock time, not an engineering-effort measurement).
Estimated effort: M (one to three days) for the item's remainder, including the VM runs;
actual so far: S (hours), the VM runs and their fixes not counted.

## Goal

Close what ROADMAP item G2 left open after journals 0228 and 0235 and ADR-0029: "the
user-visible diagnostics and the deployment recipe, which has to set `memory.max` for the users
of a shared mount". Under ADR-0022 no kernel, UAPI or format change: only what the frozen module
already exposes (errnos, `GET_INFO`, module parameters, tracepoints) and what the kernel exposes
about the caller (cgroup v2 files, rlimits).

## Prior art inspected

- Linux 6.12.112 `include/linux/memcontrol.h` `memcg_memory_event()` (line 1061) and
  `mm/memcontrol.c` `try_charge_memcg()` (line 2169, event at 2219): a charge over a limit counts
  `MEMCG_MAX` in the local events of the cgroup whose counter refused it (`mem_over_limit`) and in
  the hierarchical events of it and its ancestors. A process in a systemd session scope whose
  limit sits on `user-UID.slice` therefore sees the event in the slice's
  `memory.events.local`, not in its own cgroup. `Documentation/admin-guide/cgroup-v2.rst`:
  `memory.events` fields and `memory_localevents`. `mm/oom_kill.c:1180`: the log line of a
  cgroup OOM kill.
- systemd v257 (Debian 13): `user@.service(5)`, "Controlling resources for logged-in users" (a
  `user-.slice.d` drop-in applies to every user slice) and `systemd.unit(5)` (prefix drop-ins;
  drop-ins apply in lexicographic file-name order across directories). Context7
  (`/systemd/systemd/v257`, two queries) returned other documents, so the man page sources of
  the v257 tag were read.
- `quota(1)` (usage against each limit, per filesystem, for the caller) and `systemctl status`
  (memory beside `MemoryMax=`): the shape of a per-user report of the bound behind a refusal.
- The module itself (`layer/index.c` at fac2d4b): `ENOMEM` comes from the budget or a share
  (ADR-0027) or from a failed memcg charge, and the two are indistinguishable by errno;
  `build_admit()` (`EDQUOT`, 16 per uid and 64 per mount per minute) runs before the build and
  has no tracepoint; `moobfs_domain_build` carries `err`, `domain` (inode number of the domain
  directory) but no uid; the test-only parameters `scan_delay_us`, `scan_hold_us`,
  `scan_getxattr_errno_once` are readable by everyone.

## Decisions

- **One read-only subcommand, `moobfs diagnose [--json] <path>`,** rather than a separate tool or
  a daemon: it reports what bounds a query on `<path>` by this caller (domain role of the path,
  index settings, the caller's memory cgroup chain and the smallest `memory.max` above it, its
  usage and `max`/`oom`/`oom_kill` counts, `RLIMIT_NOFILE`, test-only module parameters left set,
  and for root the index's use of its budget and the tracepoints' state). Findings carry a
  stable code and a level; warnings make the exit status 2, as `moobfs-check`'s problems do.
  Nothing is written: tracepoints are reported, not enabled.
- **`ENOMEM` attribution in `moobfs query`:** the chain is read before the query and again after
  an `ENOMEM`; a level holding a limit whose local `max` or `oom` count grew is named as the
  cause, otherwise the index budget (with its size, from `GET_INFO`) or the uid's share. Exit
  status unchanged (75). Limitation: another process of the same cgroup reaching the limit at
  the same moment would be counted too; the message says "during the query".
- **Readers in a portable header, `tools/diag.h`,** with the paths as arguments, so that a unit
  test runs over a fake tree on macOS and Linux (`make test`) instead of an environment hook in
  the tool. A cgroup the root does not show (no cgroup v2 mounted there) is "unknown", not
  "unlimited".
- **The recipe stays the systemd drop-in of journal 0235's note,** extended in
  `docs/operations.md` §5 "Limits for a shared mount": the three bounds and their refusals, the
  drop-in with a `systemctl show` check, a per-user override, sizing, and the two cases it does
  not cover (clients run as system services; `sudo -u`/`runuser`, which keep the caller's
  cgroup). §8 gains the exit-75 and OOM-kill rows and how to read the tracepoints.
- **Alternatives not taken:** a `pam_limits`/`RLIMIT_NOFILE` cap on descriptors (bounds buffers
  per process only, not per user, and not the index); a user-space daemon that enables tracing
  and attributes builds to users (needs root and a new service; the trace has no uid);
  per-user kernel counters (ADR-0029's deferred alternative; UAPI under the freeze).

## Changes

- `tools/diag.h` (new), `tools/moobfs.c` (`diagnose`; `ENOMEM` attribution; `E2BIG` and
  `EDQUOT` messages name the bound and the way out), `Makefile` (`diag_test` in `tools` and
  `make test`; `moobfs` depends on `diag.h`), `man/moobfs.1`, `docs/tools.md`.
- `tests/diag_test.c` (new): the `0::` line among v1 lines and with ` (deleted)`; v1-only and
  unreadable files; no hierarchy at the root; the root cgroup; a cgroup the root does not show;
  a systemd-shaped chain (scope, `user-1001.slice` with the limit, `user.slice` with a larger
  one); local versus hierarchical counts; a hit on the limiting level and none on an unlimited
  level whose events grew; a moved process; levels without the memory controller; a malformed
  `memory.max`; the fallback without `memory.events.local`; module parameters; key prefixes.
- `tests/tools-json.sh`: `diagnose` in `--help`, exit 64 without a path, exit 69 and no output
  off a layer.
- `vm/suite.sh`: step `diag` in the full suite after `limits` (and in `MOOB_ONLY`); markers
  `DIAG_BUDGET_PASS`, `DIAG_MEMCG_PASS`, `DIAG_MEMCG_SKIPPED`.
- `vm/install-test/guest.sh` (phase 1, after `CLI_QUERY`) and `in-container.sh`: marker
  `MEMORY_LIMIT`: `moobfs diagnose` in bob's slice warns `no-memory-limit`; after the drop-in
  of §5 and `daemon-reload`, `systemctl show -P MemoryMax user-<alice>.slice` is 2147483648 and
  `diagnose` in alice's and bob's slices (`systemd-run --uid --slice`) names
  `/user.slice/user-<uid>.slice` with that limit.
- Surfaces: `docs/operations.md` §5 and §8, `docs/testing.md`, `ROADMAP.md` (G2 paragraph and
  row), `README.md` and `README.pt-BR.md` (retained-buffer limitation), `docs/status.md` and
  `docs/status.pt-BR.md`, `docs/security-roadmap.md` (S9 residual), `CHANGELOG.md`,
  `docs/references.md` §41, `docs/retired-claims.txt` (the two "open for the diagnostics and
  recipe" wordings).

## Evidence (offline, actually run)

- macOS host, Apple clang: `make test` (with `diag_test`): `DIAG_TEST_PASS`, image tests OK.
- Debian 13 container (arm64, Docker on the macOS host; no module loaded): `make test` with gcc
  and with clang `-fsanitize=address,undefined`: `DIAG_TEST_PASS`; `make attr-tools` with both,
  `-Werror` clean (a first gcc build caught a `-Wformat-truncation` in the test, fixed);
  `tests/tools-json.sh` with both builds: `TOOLS_JSON_PASS ... diagnose usage and no-layer
  status`; `tests/tools-walk.sh` (ASan): `TOOLS_WALK_PASS`; `tests/hostile-user.sh` (ASan, 5
  seeds): `HOSTILE_USER_PASS`; `make lint` (shellcheck and SPDX, in a git-initialised copy of
  the tree, since the worktree's git directory is not visible in the container): exit 0;
  `shellcheck` on `vm/suite.sh`, `vm/install-test/guest.sh`, `tests/tools-json.sh`: clean;
  `sh -n` on both VM scripts; `make man-lint`: clean; `scripts/check-retired-claims.sh`:
  `RETIRED_CLAIMS_OK`; `scripts/site-faq.py --check`: matches.
- Negative rounds of the unit test: `diag_hit()` without the `max` comparison fails it
  (`diag_hit(&a, &b) == 1`, line 140), and reading `memory.events` instead of
  `memory.events.local` fails it (lines 132, 133).

## Dedicated-VM runs (2026-10-30)

G2's acceptance asks for a dedicated VM. The commands as planned on 2026-10-29:

```sh
MOOB_ONLY="limits diag" make vm-test          # expect LIMITS_*_PASS, DIAG_BUDGET_PASS, DIAG_MEMCG_PASS, MOOBFS_ONLY_PASS
make install-test                             # expect MEMORY_LIMIT, then MOOBFS_INSTALL_PASS
```

Negative round for step `diag` (to show it fails without the attribution): in a scratch copy,
make `diag_hit()` in `tools/diag.h` return -1, then `MOOB_ONLY=diag make vm-test`; expected
`MOOBFS_VM_FAIL: dg-memcg-not-named`. For `MEMORY_LIMIT`, removing the `printf ... | tee` line
of the drop-in from `guest.sh` must stop at "the drop-in did not set MemoryMax on alice's
slice". Once per batch, before publication, the full `make vm-test` (step `diag` is in it).

Results. Host: the dedicated test host (x86-64, Debian 13, KVM), a copy of this branch's tree
(no `.git` is copied, so a local snapshot commit gives `git rev-parse` and `git ls-files` what
they need). Two working-copy changes on the host only, not committed: `vm/run.sh` and
`vm/install-test/run.sh` pass `--cpuset-cpus=$MOOB_CPUSET` to `docker run`, so that the guests
stayed on host CPUs 2 and 3 while another run used CPUs 4 and 5. The base cloud image of
2026-10-21 was copied from an earlier run's cache instead of downloaded.

| Run | Command | Result |
| --- | --- | --- |
| Suite steps | `MOOB_VM_KVM=1 MOOB_DEBIAN_KERNEL=6.12.113 MOOB_ONLY="limits diag" make vm-test` | exit 0 (2 min); `LIMITS_BUDGET_PASS`, `LIMITS_MEMCG_PASS`, `DIAG_BUDGET_PASS`, `DIAG_MEMCG_PASS` ("ENOMEM of a build in /moob-dg/leaf named the 4 MiB memory.max of /moob-dg; diagnose named the same limit, reached; built outside, equal to the oracle"), `MOOBFS_ONLY_PASS` |
| Install recipe | `make install-test` (KVM in the container) | exit 0 (9 min); `MEMORY_LIMIT: user-.slice.d drop-in sets MemoryMax=2G on each user's slice; moobfs diagnose reads it there`, `MOOBFS_INSTALL_PASS` |
| Negative, step `diag` | as above with `diag_hit()` returning -1 first, `MOOB_ONLY=diag` | exit 2; `DIAG_BUDGET_PASS`, then `MOOBFS_VM_FAIL: dg-memcg-not-named` |
| Negative, `MEMORY_LIMIT` | `make install-test` with the `printf ... \| tee` line of the drop-in removed from `guest.sh` | exit 2; `MOOBFS_INSTALL_FAIL: the drop-in did not set MemoryMax on alice's slice` |

`DIAG_MEMCG_SKIPPED` did not appear: the memory controller was available in the guest. The full
`make vm-test` (step `diag` is in it) remains for the batch, before publication.

## Limitations

- The messages, roles and JSON of `diagnose` on a mounted layer are checked by the step `diag`
  and by `MEMORY_LIMIT` on one kernel (Debian 6.12.113); other kernels were not run.
- The `ENOMEM` attribution is a before/after comparison of counters shared by every process of a
  cgroup; a concurrent process at the same limit gives a false "memory cgroup" answer. The
  share of ADR-0027 is not visible to a user (the counters are root's), so the budget message
  cannot say whether the budget or the share refused.
- When the cgroup OOM killer can find a victim the query's process may be killed instead of
  refused; `diagnose` then reports `oom-killed` afterwards. That path is not tested (the suite
  makes the process unkillable, as in journal 0235).
- Whether `daemon-reload` alone applies the drop-in to an already running user slice was not
  checked; the recipe says so, and the test stops bob's slice first.
- `docs/references.md` §37 and journal 0243 were numbered on this branch; other branches of the
  batch may need renumbering at merge.
  (Note of 2026-10-30: the batch-10 merge renumbered the section to §41, after §37 to §40 of
  journals 0239 to 0242.)

## Next step

The maintainer decides whether G2 closes; the batch's full `make vm-test` runs before
publication.
