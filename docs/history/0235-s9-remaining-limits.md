# 0235 — S9's remaining limits: caps hit in the suite, mount cost measured (ADR-0029)

Date: 2026-10-29. Status: branch `claudiofp/b8-s9-limits` (from `main` at 7ac52cd), local
commits, not pushed. Work started 12:35 UTC; ended 13:40 UTC (wall-clock time, including waits
for a shared VM host, not an engineering-effort measurement). Estimated effort: S; actual work
about one hours of that time.

## Goal

Close the last two bullets of security roadmap S9 left after ADR-0027 (journal 0228): "a
per-mount and per-user cap on `QUERY` results and on index memory ... test the behavior when the
cap is hit", and "cost of the mount-time scan with a large lower layer; `index=off` and a lazy
index option as the escape hatch", whose done-criterion is "limit tests in the suite and the
numbers recorded". Prefer tests and measurements over new mechanisms; under ADR-0022 no new UAPI
or format change.

## What was already true on `main` (assessment)

Read in `layer/main.c`, `layer/index.c` and `include/moobfs_ioctl.h` at 7ac52cd:

- **Results per call:** `limit` is 1 to 4,096 (`MOOB_QUERY_MAX_LIMIT`), `EINVAL` outside, already
  tested by `query q2invalid`; `truncated` and the cursor page past it; a batch has at most eight
  requests. A failed ioctl copies nothing back.
- **Retained buffer:** `moobl_qbuf_get()` keeps one buffer of up to 4,096 × 272 bytes per open
  file (journal 0139), `GFP_KERNEL_ACCOUNT` (ADR-0018), freed on release.
- **Index memory:** `index_budget` per mount, shares per uid (ADR-0027), memcg-charged.
- **Mount:** no scan. `moobl_fill_super()` resolves `lower=`, reads the format marker, and
  `moobl_index_mount()` calls `vfs_listxattr()` once on the lower root ("Mount: nothing is read
  but whether the lower root answers listxattr"). Since ADR-0012 a domain is built on its first
  `QUERY2`, bounded by `index_build_work` (`E2BIG`), the 10 s deadline (`ETIMEDOUT`) and the
  build admission (`EDQUOT`); the administrator's mount-wide `QUERY` (`domain_ensure_all()`)
  builds every domain under the same per-build work bound. The option the bullet calls
  `index=off` exists as `noindex` (`QUERY2` then returns `EOPNOTSUPP`); the lazy index is the
  default.

So no part needed a new mechanism; what was missing was a test of each cap being hit and the
numbers. One point is a real residual risk, recorded below and in ADR-0029, not fixed here.

## Decisions

- **ADR-0029 (proposed, waits for the maintainer):** the per-call `limit` is the cap on results;
  the memory cgroup is the per-user cap on query memory and on the index memory a user's builds
  allocate, beside `index_budget` and the shares; no new mount option (`noindex` and the
  on-demand build are the escape hatches). Alternatives recorded there: a per-user count of
  retained buffers after `fs/pipe.c`'s `pipe-user-pages-soft` (needs a tunable, UAPI under the
  freeze, or an arbitrary constant; kept as the candidate), freeing the buffer per call (journal
  0139's regression), new `index=` options (rename what exists; UAPI).
- **Test method for the memcg cap:** the test process sets `oom_score_adj` to −1000, so the
  cgroup's OOM killer finds no victim and the charge fails with `ENOMEM`
  (`try_charge_memcg()` in Linux 6.12.112); otherwise the holder would be killed and its buffers
  freed, and the test could not show another uid answered while the cap is held. It pre-faults
  its own memory and closes two descriptors before printing, so that it does not fault at the
  limit.
- `mountscale` is optional (`MOOB_ONLY` only), as `builddenial` is: it reformats the ext4 disk.

## Changes

- `vm/suite.sh`: step `limits` in the full suite, after `fairshare`; optional step `mountscale`
  after `builddenial`; both in the header and the `MOOB_ONLY` list.
- `tests/memcg-query.c`: modes `hold` (descriptors with retained maximum-limit buffers until a
  query fails; at most 64; passes only on `ENOMEM` with the request untouched, then waits
  holding the rest) and `build` (one build in a too-small cgroup must fail with `ENOMEM`,
  request untouched).
- `tests/mount-scale.c` (new, in `vm/build-tests.sh`): a timed `mount(2)`/`umount(2)` with the
  `Slab`/`SUnreclaim` change and `GET_INFO`'s index bytes after the mount.
- `docs/decisions/0029-s9-limits-met-by-existing-bounds.md`; `docs/references.md` §33.
- Surfaces: `docs/security-roadmap.md` (S9), `ROADMAP.md` (P0 paragraph, S1 row, G2 row,
  remaining-items row), `README.md` and `README.pt-BR.md` (limitation on retained buffers, which
  still said fair shares were missing), `SECURITY.md` and `docs/when-not-to-use.md` (same stale
  wording since journal 0228), `docs/status.md` and `docs/status.pt-BR.md`, `docs/testing.md`
  (marker table), `docs/distribution-strategy.md` (index-memory row), `CHANGELOG.md`; old
  wordings in `docs/retired-claims.txt`.

## Evidence

Dedicated host (Debian 13, 8 threads), `make vm-test` guest Debian `6.12.111+deb13-amd64`,
`MOOB_VM_KVM=1`, 512 MiB, two vCPUs. Up to four other agents' VMs ran on the same host
(load average about 21 at the start): every timing below is from a contended host.

- `sh vm/build-tests.sh`, `sh -n vm/suite.sh`, `make lint` (shellcheck and SPDX) on the host:
  clean. `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- Run r1, `MOOB_ONLY="limits mountscale" make vm-test` (13:10–13:16 UTC): exit 0,
  `MOOBFS_ONLY_PASS`, no kernel diagnostic.
  - `LIMITS_BUDGET_PASS: c=4556120 bytes refused with ENOMEM on a 1048576-byte budget;
    a=114020 bytes kept, no release, no rebuild`.
  - `MEMCG_HOLD_CAPPED: descriptors=5 errno=12 bytes_per_buffer=1114112 before=1310720
    current=11784192` (seven buffers held at the cap, two closed before printing: about
    2.09 MiB per buffer, the contiguous `kvmalloc` block); `MEMCG_BUILD_CAPPED`;
    `LIMITS_MEMCG_PASS: ... uid 1002 answered meanwhile; freed to 180224; a 6834120-byte build
    refused with ENOMEM in a 4 MiB cgroup and built outside it`.
  - `mountscale`, ext4 formatted with `-I 128 -N 210000` (200,118 inodes used at the end). Wall
    time of `mount(2)` of the layer, median (min–max) of five, µs:

    | Files | Index | Cold | Warm |
    | --- | --- | --- | --- |
    | 100 | default | 413 (372–8,904) | 76 (72–209) |
    | 100 | `noindex` | 1,534 (412–2,492) | 77 (72–181) |
    | 50,100 | default | 454 (373–858) | 86 (78–185) |
    | 50,100 | `noindex` | 1,129 (390–1,446) | 79 (77–190) |
    | 200,100 | default | 960 (384–4,716) | 84 (70–98) |
    | 200,100 | `noindex` | 463 (310–4,052) | 80 (71–269) |

    "Cold": caches dropped and the lower mounted again before the layer's mount. Unmount: 0.3
    to 2.1 ms median, outliers to 12.5 ms. `Slab` and `SUnreclaim` changed by 0 kB across every
    mount and unmount, and `index_used` was 0 with no built domain after every mount. The spread
    does not grow with the file count; the differences between rows are noise of a shared host.
  - First `QUERY2` of the bulk domain, cold, uid 1001: 100 files `outcome=full` in 1.1 ms;
    50,100 files `outcome=denied` (`E2BIG`) in 541 ms, 536 ms system CPU, 6.65 MB read; 200,100
    files `denied` in 525 ms, 512 ms system CPU, 6.70 MB read: the refusal stops at
    `index_build_work`, so it costs the same at both sizes. Nothing built (`domain_builds`
    unchanged). The small domain was then answered (3.3 to 12.9 ms) and held 114,220 index
    bytes; the administrator's mount-wide `QUERY` was refused with `E2BIG` as well; with
    `noindex`, `QUERY2` returned `EOPNOTSUPP` (95).
- Negative round n1 (13:21–13:24 UTC): the same tree with `moobl_qbuf_get()`'s two
  `kvmalloc_array(..., GFP_KERNEL_ACCOUNT)` changed to `GFP_KERNEL` (the retained buffer no
  longer charged to the cgroup), `MOOB_ONLY=limits`: `LIMITS_BUDGET_PASS`, then
  `MEMCG_HOLD_FAILED: descriptors=62 errno=0 ... current=1220608` (64 buffers held without the
  cgroup seeing them) and `MOOBFS_VM_FAIL: lm-hold-not-capped`, exit 2.
- Run r2 on the final tree (after `hold` was bounded to 64 descriptors for the negative round),
  `MOOB_ONLY="limits memcg fairshare"` (13:29–13:33 UTC): exit 0, `MOOBFS_ONLY_PASS`, no kernel
  diagnostic; `MEMCG_ACCOUNT_PASS` (buffer delta 2,015,232 bytes, as in journal 0180),
  `FAIRSHARE_PASS`, `LIMITS_BUDGET_PASS` with the same numbers, `MEMCG_HOLD_CAPPED:
  descriptors=5 errno=12 ... current=11620352`, `MEMCG_BUILD_CAPPED`, `LIMITS_MEMCG_PASS`
  (freed to 274,432 bytes).

## Limitations

- **Residual risk (ADR-0029):** without a `memory.max` on a user's cgroup (the default of a
  Debian user slice), retained buffers of about 2 MiB per open descriptor are bounded only by
  `RLIMIT_NOFILE` and the number of processes, and the global OOM killer does not count them to
  the process. The deployment recipe of G2 has to set `MemoryMax=`.
- The memcg half runs only in a guest with the memory controller; the KASAN kernel lacks it
  (`LIMITS_MEMCG_SKIPPED` there). Only the retained-buffer path got a negative round; the
  build-in-a-cgroup assertion's negative round was not run.
- The OOM-killer path (a killable holder) is not tested; the test makes the holder unkillable.
- Timings come from one guest on a host shared with other VMs; five repetitions, no interval.
  Only ext4; files were empty (the mount reads none of them, so their attributes do not matter
  to it), and the 200,100-file domain could never be built: `index_build_work` stops at 65,536
  units, so a larger domain has the user-space scan or a deeper `domain_depth`.
- No kernel change, so no `checkpatch`, KASAN or full-suite run was owed by this unit; the full
  `make vm-test` of the batch is still to run before publication.

## Next step

The maintainer accepts or amends ADR-0029; on acceptance S9 is marked done in the security
roadmap, ROADMAP and status pages. G2's deployment recipe sets `memory.max` for the users of a
shared mount.

*Note, 2026-10-29, by the coordinator of the batch:* the maintainer accepted ADR-0029 with
option A (the memory cgroup is the per-user cap). S9 is marked done, and `docs/operations.md` §5
now sets `MemoryMax=` for the users of a shared mount through a systemd drop-in for user
slices; that drop-in was not run in a VM. The per-user kernel counter stays a deferred
alternative.
