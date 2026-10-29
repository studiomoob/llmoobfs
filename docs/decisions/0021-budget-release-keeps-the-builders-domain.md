# ADR-0021 — A budget release drops only the builder's own domain

Date: 2026-10-25. Status: **implemented and merged by PR 139** (`e57d8c5`).
The host regression, the opt-in native-KVM probe, the steps `release`,
`overbudget` and `buildcostevict` under KASAN, and the complete KASAN,
UBSAN and lockdep suite passed (`MOOBFS_KASAN_PASS`). The complete
native-KVM suite of this change was not run. The opening status originally
ended with "Not published."
*Note, 2026-10-28:* decision 4 (no reserved fraction, one uid may fill the budget) is amended by
[ADR-0027](0027-fair-shares-of-the-index-budget.md): a uid above its share lends its room back to
a uid below its own. Decisions 1 to 3, 5 and 6 stand.
Estimated effort: M (one to three days). This is an estimate, not recorded
labor time. Journal 0192 records the host validation window.
This is ADR-0021 because ADR-0020, the per-mount query-build work policy,
and journal 0187 are already on main. This wave is journals 0188–0192.

## Context

ADR-0011 finding F2 leaves one `index_budget` for the whole mount. A reserved
fraction per uid stays in S9. ADR-0012 builds a domain on demand and, when a
build returns `ENOMEM` after the budget was charged, releases the least
recently queried built domain and retries. ADR-0019 bounds how much work one
build may do and how often an unprivileged uid may start one. It does not
split the budget. Journal 0182 had already seen two users release each
other's domains. Journal 0191 measured the same ping-pong on the module that
includes ADR-0019: with `index_budget=1M` and 2 000 files, the second build
released the first, both across uid 1001 and uid 1002 and within one uid.
Each search still returned every file, because the released domain was
rebuilt from the lower tree. Only one domain stayed built.

Section 9 of the evaluation protocol registered the property before a
mechanism: a build charged to uid 1002 must not release uid 1001's domain;
if it does not fit without that release, the search returns a resource
errno and no list; uid 1001's next search must not increase
`domain_builds`. One uid may still use the whole budget by releasing that
uid's own domains. No latency ceiling is set, so meeting the property does
not complete G2.

## Decision

1. A budget release, the `ENOMEM` path in `domain_ensure()` when
   `may_release` is set, may drop only a domain whose last successful build
   was done by the same filesystem uid as the build now in progress. The
   uid is `current_fsuid()`, sampled before `override_creds()` replaces the
   query credentials with the mounter's. It is stored as the init-namespace
   uid (`from_kuid(&init_user_ns, ...)`). User-namespace mounts are already
   refused.
2. If no domain of that filesystem uid can be released and the build still
   does not fit, the search returns `ENOMEM`. The partial domain is dropped
   by the existing failure path. `domain_builds` does not increase.
   Another filesystem uid's domain stays built.
3. The shrinker is unchanged. Under memory pressure it may drop any idle
   domain, of any uid.
4. One filesystem uid may still fill the whole `index_budget`. A second
   domain of that same uid is still released. There is no reserved
   fraction. Holding a fraction back would refuse a first domain that fits
   the budget alone, which contradicts the registered property.
5. The identity is the filesystem uid, not the real uid. A process that
   drops privileges with `setresuid(0, uid, 0)` keeps real uid 0.
   `current_uid()` then treats every such caller as one builder. The first
   build of this filter did that, and the host regression still showed the
   release. `build_admit()` still keys its counter on `current_uid()`, as
   ADR-0019 implemented. This decision does not change that counter.

The unlocked walk that picks a candidate can observe a stale uid. The
check under the index lock refuses a mismatch and continues with the next
candidate. A stale skip fails toward `ENOMEM`, which leaves the other
domain built.

## Alternatives

- **Reserved or equal shares.** Rejected. The property says one uid may use
  the whole budget. A reserved half would also make the measured 840600-byte
  domain fail inside a 1 MiB budget.
- **Keep the cross-uid least-recently-used release.** Rejected. It is the
  failure journal 0191 recorded. Answers stay complete only by rebuilding
  the domain that was just dropped.
- **Key the filter on `current_uid()`, the same real uid `build_admit()`
  charges.** Rejected after the host run described above. The project's
  query harness, and any process that only changes the effective id, would
  still share one builder.
- **Apply the same filter to the shrinker.** Rejected. Memory reclaim is
  the relief valve for the machine, not the fairness policy. An idle
  domain must still be droppable under pressure.

## Prior art

Linux 6.12.112 `try_charge_memcg()` in `mm/memcontrol.c` reclaims the
cgroup whose counter rejected the charge
(`try_to_free_mem_cgroup_pages()` on `mem_over_limit`) and fails the charge
when that reclaim does not make room. It does not take a sibling cgroup's
pages to satisfy `memory.max`
(`Documentation/admin-guide/cgroup-v2.rst`). The index budget is not a
memory cgroup. The same shape is used: release inside the builder's own
domains, or return `ENOMEM`. The citation is in `docs/references.md`.

## Consequences

Root, filesystem uid 0, no longer releases an unprivileged user's domain to
make budget room. Root's build gets `ENOMEM` when the remainder is too
small. The shrinker can still drop that domain once it is idle. Queries
that already share one filesystem uid, including the root-run release and
overbudget suite steps, still release one another's domains.

G2 is not complete. No latency ceiling, no noisy-neighbor envelope, no
reserved share, and no CPU or I/O cap. Issue 118 stays open. Lock order is
unchanged: the same locks, with an extra continue when the locked uid
check fails. Two fields were added on `struct moobl_domain`.
`has_builder` is cleared in `domain_takedown()`.

## Evidence

Journal 0192. `scripts/checkpatch.sh` exited 0 on the layer and kernel
sources. `MOOB_ONLY=buildcostevict` passed under native KVM on the
dedicated host (`BUILD_COST_CROSS_USER_HELD`, budget 1762216) and failed
on the pre-filter module (`MOOBFS_VM_FAIL: bce-second-user`). That step is
not the full suite. The same three steps then passed under KASAN, UBSAN
and lockdep (`MOOBFS_KASAN_ONLY_PASS`). The complete `vm/kasan-local.sh`
run then exited 0 (`MOOBFS_KASAN_PASS`), with `RELEASE_PASS` and
`OVERBUDGET_PASS` on ext4, btrfs and XFS and `FAULT_INJECTION_PASS`. The
complete native-KVM suite was not run.
