# ADR-0027 — Fair shares of the index budget: a uid over its share lends its room back

Date: 2026-10-28. Status: **accepted by the maintainer on 2026-10-28** (rule chosen with the
start of security batch 5, journal 0228). Estimated effort: M (one to three days). Amends
[ADR-0021](0021-budget-release-keeps-the-builders-domain.md) decision 4 and the G2 fairness
property of `docs/g6-evaluation-protocol.md` §9, which carries a dated note pointing here.
Closes the remaining part of finding F2 of ADR-0011 (security roadmap S9). Kernel hardening
under ADR-0022: no UAPI or format change.

## Context

ADR-0021 lets a build over the budget release only domains last built by the same filesystem
uid. It stopped the cross-user ping-pong of journal 0191, but one uid may still fill the whole
`index_budget`; from then on the first domain of any other uid gets `ENOMEM` until the shrinker
or an unmount frees room. That is the cross-user degradation F2 described, now as a refusal
instead of a stale answer. ADR-0021 rejected a reserved fraction because it would refuse a
domain that fits the budget alone; the property of §9 keeps that: one uid may use the whole
budget when nobody else needs it.

## Decision

1. **Each domain counts its own bytes.** Every `charge()` and `uncharge()` of the index names
   the partition it belongs to, and the domain record (the outside part for key 0) keeps
   `used`, the bytes of its names, entries, inode records and recorded paths, exactly as
   `ix->used` counts them for the mount. An inode record moved to another partition
   (`rec_set_partition()`) moves its bytes with it. A uid cannot grow a domain by writes after
   its build without the count following, which a size taken at build time would allow.
2. **A uid's usage** is the sum of `used` over the built domains whose last build was by that
   filesystem uid (ADR-0021's `built_uid`), plus, for the builder, the domain being built.
3. **The share** is `index_budget` divided by the number of filesystem uids with a built domain,
   the builder counted once even if it has none yet.
4. **Order of a release** when a build does not fit (`domain_ensure_dom()`, `may_release`):
   1. the builder's own least recently queried built domains, as ADR-0021 decided;
   2. if none is left and the builder's usage is below the share: the least recently queried
      built domain of a uid whose usage is above the share;
   3. otherwise the search returns `ENOMEM` with no result list, as before.
   A domain is released whole, so a uid may end a release somewhat below its share; it is never
   released while at or below it.
5. **Selection and check.** The candidate is chosen by the existing unlocked walk; the usage of
   both uids and the share are computed again under the index lock before the takedown, and a
   candidate that no longer qualifies is skipped, failing toward `ENOMEM` as ADR-0021's stale
   skip does. The number of domains of one mount stays small (`domain_lru()`'s note), so each
   check is a walk of the domain records.
6. **Unchanged:** the shrinker may still drop any idle domain of any uid (memory reclaim is the
   machine's relief valve, not the fairness policy); a domain without a recorded builder is not
   released by a build of another uid; `build_admit()` keeps its counter.

The amended property (§9): a build charged to uid B does not release a domain of uid A while A's
usage is at or below the share; when A is above it, the build may release A's least recently
queried domains until A is not; if the build still does not fit, the search returns a resource
errno with no list. One uid alone may still use the whole budget.

## Alternatives

- **Keep ADR-0021 and accept F2's remainder.** Rejected by the maintainer: one uid can then deny
  every other uid's first domain.
- **A reserved or equal fraction per uid.** Rejected again, for ADR-0021's reason: it refuses a
  domain that fits the budget alone and wastes room nobody uses.
- **Shares proportional to a declared weight per uid** (memcg's `memory.low`). No mount option or
  interface declares weights, and adding one is a UAPI change under the freeze; equal weights
  are the special case kept here.
- **Size taken at build time instead of exact counting.** Rejected: writes after the build would
  grow a domain without the share seeing it (decision 1).

## Prior art

- Linux 6.12.112 `mm/page_counter.c`, `effective_protection()` (line 335): memcg protection is
  `min(usage, setting)`, reclaim takes only usage above it, and unused protection stays available
  to siblings ("work-conserving"). Decision 4 applies the same rule with an equal setting, the
  share, and the build's release in place of reclaim.
- Max-min fairness (Bertsekas and Gallager, *Data Networks*, 2nd ed., §6.5.2): no user's
  allocation grows at the expense of a user with a smaller or equal one.
- Shared caches between tenants: Cidon et al., *Memshare: a Dynamic Multi-tenant Key-value
  Cache* (USENIX ATC 2017) reassigns memory a tenant does not need; Pu et al., *FairRide:
  Near-Optimal, Fair Cache Sharing* (NSDI 2016) on the isolation-guarantee goal. Both evict per
  item; the index releases whole domains, so the guarantee here is per domain, not per byte.

## Consequences

- F2 is closed: a uid at or below its share keeps its domains against any other uid's build.
- Two uids that both want more than a share can still release each other's domains above the
  share; the rate limits of ADR-0019 bound how often.
- Every charge path gains a domain lookup; the cost is measured with G2's latency bound
  (protocol §14).
- The suite steps and `bench/client/g2_share.py` that asserted ADR-0021's refusal across uids
  change with the amended property (journal 0228).
