# ADR-0029 — S9's remaining limits are met by the existing bounds; no new per-user counter

Date: 2026-10-29. Status: **accepted by the maintainer on 2026-10-29** (journal 0235), with option A: the memory
cgroup is the per-user cap, and the deployment recipe sets `MemoryMax=`.
Estimated effort: S (hours) for the decision and its tests. Closes the last two bullets of
security roadmap S9 if accepted. No kernel, UAPI or format change (ADR-0022).

## Context

After ADR-0027 two bullets of S9 were left: "a per-mount and per-user cap on `QUERY` results and
on index memory, already partly covered by `index_budget`; test the behavior when the cap is
hit", and "cost of the mount-time scan with a large lower layer; `index=off` and a lazy index
option as the escape hatch". Both were written before ADR-0012 (index built per domain on
demand), ADR-0018 (memcg accounting), ADR-0019 and ADR-0020 (build work, deadline and
frequency) and ADR-0027 (fair shares). Journal 0235 checked what each one already gives, on the
code of `main` at 7ac52cd, and measured it.

What the code already does:

- **Results per call.** `QUERY` and `QUERY2` copy at most `limit` results, 1 to 4,096
  (`MOOB_QUERY_MAX_LIMIT`; 0 or 4,097 is `EINVAL`, `tests/query.c` `q2invalid`); the rest is
  reached with `truncated` and the cursor; a batch holds at most eight requests. A refused query
  copies nothing back.
- **Memory held for results.** The kernel buffer a query fills before copying out is kept with
  the open file and reused (journal 0139), at most 4,096 × 272 = 1,114,112 bytes requested per
  open file; `kvmalloc` first tries a physically contiguous block, so it may take 2 MiB
  (journal 0180 measured 2,015,232 bytes). It is charged to the caller's memory cgroup
  (ADR-0018) and freed when the file is closed.
- **Index memory.** `index_budget` per mount (1 MiB to 4 GiB); within it each filesystem uid's
  share (ADR-0027); every index and scan allocation charged to the caller's memory cgroup.
- **Mount.** No scan: `moobl_fill_super()` resolves `lower=`, reads the format marker on the lower
  root and `moobl_index_mount()` asks the lower root for `listxattr` once. A domain is scanned on
  its first `QUERY2`, bounded by `index_build_work` (`E2BIG`), the 10 s cooperative deadline
  (`ETIMEDOUT`) and the per-uid and per-mount build frequency (`EDQUOT`). The mount-wide `QUERY`
  of an administrator builds every domain under the same work bound. `noindex` turns the index
  off; the on-demand build is the lazy index.

## Decision

1. **The per-call `limit` is the cap on `QUERY` results; the cursor is the way past it.** No
   per-user or per-mount count of results read over time: a result that was copied out holds no
   kernel memory.
2. **The memory cgroup is the per-user cap on query memory and on the index memory a user's
   builds allocate.** `index_budget` and the shares bound the index per mount and per uid; the
   cgroup's `memory.max` bounds what one user's processes pin in retained buffers and builds.
   Hitting either is `ENOMEM` to the caller, with nothing copied out and nothing left built.
3. **No new mount option.** The mount-time scan of the bullet no longer exists; `noindex` is the
   off switch and the on-demand build is the lazy index the bullet asked for. A domain too large
   to build is refused with `E2BIG`, and its users have the user-space scan (`moobfs-index`, the
   Python library's scan backend, ADR-0023) or a deeper `domain_depth`.
4. **Tests and numbers.** The suite step `limits` (full suite) hits each cap and checks the
   errno, that nothing was answered or left built, and that another uid is answered meanwhile;
   the optional step `mountscale` records the mount's time and memory from 100 to 200,100 files
   with and without `noindex`, and the first query of a domain over `index_build_work`.

## Alternatives

- **A per-user count of retained query buffers**, after the pipe buffers' per-user soft limit
  (Linux 6.12.112 `fs/pipe.c`, `too_many_pipe_buffers_soft()`: past `pipe-user-pages-soft` a new
  pipe of an unprivileged user gets one page instead of sixteen). Over the count a query would
  allocate and free its buffer instead of keeping it, so nothing is refused. Not taken now: the
  limit needs a tunable to fit different hosts (a sysctl or a mount option, UAPI under
  ADR-0022) or else an arbitrary constant; it bounds only idle buffers, not those of queries
  running at once; and the memory cgroup already bounds the same memory where a limit is set.
  It stays the candidate if the maintainer wants a bound on hosts without a cgroup limit.
- **Free the buffer after each query.** Journal 0139 measured +31 % to +145 % on the filtered
  shapes; rejected again.
- **A buffer sized by the domain's entry count.** Rejected in journal 0139 (needs the lock first,
  and the count can grow).
- **New `index=off` / `index=lazy` options.** They would rename what `noindex` and the default
  already do, and a new option is UAPI under the freeze.

## Prior art

- Linux 6.12.112 `mm/memcontrol.c`, `try_charge_memcg()` (line 2169): a charge over `memory.max`
  reclaims, then calls the cgroup's OOM killer (`mem_cgroup_oom()`, line 1617), and fails with
  `-ENOMEM` when that finds no victim; `mm/oom_kill.c` `out_of_memory()` (line 1113) does not
  panic for a cgroup OOM with nothing to kill (line 1166). The suite uses that: its test process
  sets `oom_score_adj` to −1000 so the cap is seen as `ENOMEM`, not as the process being killed.
- Linux 6.12.112 `mm/util.c`, `kmalloc_gfp_adjust()` (line 611) and `__kvmalloc_node_noprof()`
  (line 650): the contiguous attempt with `__GFP_NORETRY`, then `vmalloc` with the caller's flags;
  why a 1,114,112-byte buffer may take 2 MiB.
- Linux 6.12.112 `mm/oom_kill.c`, `oom_badness()` (line 203): the global OOM killer scores a
  process by its RSS, swap entries and page tables only, so kernel memory pinned by a process's
  open files does not make it the victim.
- Linux 6.12.112 `fs/pipe.c`, `too_many_pipe_buffers_soft()` (line 771): the per-user soft limit
  described under Alternatives.

## Consequences

- S9 is done (accepted 2026-10-29); its numbers are in journal 0235.
- **Residual risk:** on a host where the users' cgroups have no `memory.max` (the default of a
  Debian user slice), an unprivileged user can pin about 2 MiB of kernel memory per open file
  that queried with the maximum limit, bounded only by `RLIMIT_NOFILE` per process and the
  number of processes, and the global OOM killer does not count it to that user's processes.
  The deployment recipe of G2 has to set `MemoryMax=` (or `memory.max`) for the users of a
  shared mount; the alternative above is the kernel-side answer if that is not enough.
  `docs/operations.md` §5 carries that setting since the acceptance; the systemd drop-in itself
  has not been run in a VM.
- The per-user kernel counter (the alternative above) is deferred, not rejected: it is the
  answer for hosts where cgroups cannot be required, and its tunable is a UAPI question for after
  the freeze of ADR-0022 ends, once G6 has run.
