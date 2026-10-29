# ADR-0019 — Bound QUERY2 domain-build work and frequency

Date: 2026-10-23. Status: **merged by PR 135; the fixed work number is superseded by ADR-0020**. The opening status originally read “experimental implementation locally validated; not published”.
Estimated effort: L (a week or more) for implementation, negative tests,
performance comparison and KVM/KASAN validation. This is an estimate, not
recorded labor time.

*Status update, 2026-10-24 (journal 0184):* the experimental mechanism was
merged into `main` by PR 135 at `2396c16`, after all eleven reported checks
passed on head `94dc454`. The earlier status describes the local checkpoint.
The numerical policy remains experimental; the 17,000-file denial and the
absence of strict CPU-time or physical-I/O bounds remain known limitations.

*Status update, 2026-10-25 (journal 0193):* ADR-0020 supersedes this ADR's
fixed 32,768-unit choice with a bounded per-mount `index_build_work` option,
merged by PR 138 after twelve green checks. The default stays 32,768;
the admission, accounting, deadline and output-on-error decisions here
remain in force. The earlier checkpoints below retain their dates and
do not describe the new opt-in setting. Issue 118 stays open.

*Note, 2026-10-29 (journal 0241):* ADR-0031 (proposed) changes what the
ten-second deadline measures: the building task's CPU time, with a 120 s
elapsed backstop. If it is accepted, the "elapsed-time deadline" below
describes the earlier policy. Admission, accounting and errnos stay.

## Context

ADR-0012 intentionally builds a domain synchronously on its first query,
after invalidation and after index-budget release. Its memory budget does not
bound the work to rebuild. ADR-0011 leaves fair sharing and resource limits
outside its promises. Journal 0182 measured a 1,000-file cold-cache build
with 163,840 read bytes and a 7,500-file build with 1,990,656 read bytes in
one KVM guest; directory renames caused seven more builds, and two users
alternately evicted each other's domains under a shared budget. These are
bounded observations, not safe thresholds for every device or workload.

The maintainer chose the resource-limit direction after these measurements:
queries that exceed a limit may fail. This supersedes ADR-0012 only where it
allowed an unbounded synchronous rebuild. Its on-demand and reclaimable
index, authorization model and successful-query semantics remain.

## Decision and initial mechanism

1. Before starting a `QUERY2` build of a stale or absent domain, reserve admission
   against both a mount-wide budget and a caller-UID budget. A built, fresh
   domain consumes no build admission. Waiting on another builder must
   recheck freshness before any admission charge. Count each attempted
   build, including failed and interrupted attempts; otherwise repeated
   failure is a way around the rate limit. Charge the UID sampled from the
   original query credentials, before the mounter's credentials override
   them. A bounded UID table must fail closed when full, without losing
   existing accounting. `CAP_SYS_ADMIN` callers bypass frequency admission;
   the work and time limit still applies to each build. Legacy mount-wide
   `QUERY` with `domain_depth=0` also bypasses frequency admission to
   preserve its existing race and client behavior. The mount count therefore
   limits aggregate unprivileged `QUERY2` domain builds, not administrator
   activity or legacy whole-mount `QUERY`. The latter remains a residual
   availability risk on mounts that disable domains.
2. A single admitted build gets a cumulative work allowance across all
   retries and moved-directory rescans. Count each enumerated name before
   lookup, each directory traversal and each attribute-load attempt. Check
   an elapsed-time deadline between operations and before starting another
   batch or retry. The same allowance follows the whole `domain_ensure()`
   call; a rename or budget-release retry cannot reset it. Keep checks outside
   the index spinlock and preserve the existing signal interruption path.
3. If admission or the work allowance is exhausted, return a distinct
   documented errno and drop the partially built domain through the existing
   failure path. An individual QUERY2 must not publish a successful partial
   answer or cursor. QUERY2_BATCH already copies earlier subquery result
   buffers before a later failure; its existing error contract requires the
   caller to discard **all** outputs on error. Preserve and test that contract
   rather than claiming that no bytes were copied to the caller.
   Define whether a batch pays once per domain build, not once per subquery.
   A later call may retry after the admission window, but a domain too large
   for the work allowance remains unqueryable until it is split or a later
   version changes the fixed policy. No mount option is introduced in this
   experimental implementation.

The admission window and work counters are enforceable bounds on repeated
scan *attempts* and operations the layer starts. Elapsed time is checked
cooperatively: one lower-filesystem operation can block beyond the deadline.
An enumerated-name or attribute-attempt cap is **not** a hard CPU-time or
physical-I/O-byte cap. `/proc/self/io` and block-device counters from the
guest are measurement evidence, not suitable authorization/accounting
counters for this policy. Do not claim a strict CPU or I/O cap until the
kernel offers a verified mechanism and tests show it includes the lower
work. The chosen direction is to bound their principal causes and rebuild
frequency, then measure the residual cost.

The initial experimental constants are 32,768 combined work units per build
(one per enumerated name, directory visit or `index_load` attempt), a
cooperatively checked ten-second deadline, 16 attempts per unprivileged UID
and 64 per mount in fixed 60-second windows, and 64 UID accounting slots per
mount. Exhausting the admission count or UID table returns `EDQUOT`; the
work cap returns `E2BIG`, and the deadline returns `ETIMEDOUT`. A fresh
built domain does not consume admission. The fixed window permits boundary
bursts; it is not a sliding-window guarantee. The UID table can itself be
filled by many distinct UIDs, denying additional callers until an entry
expires. These are intentional availability tradeoffs, not fair index-memory
shares (S9). The scope restriction to `QUERY2` followed the failed full-suite
legacy-query race in journal 0182; it is an explicit compatibility choice.

Three repeated ext4 KVM probes of each measured size (journal 0182) gave
45.932–49.011 ms and 159,744–167,936 read bytes for 1,000 files, and
196.926–210.976 ms and 1,986,560–2,002,944 read bytes for 7,500 files.
The 7,500-file shape needs about 15,000 work units and passed on ext4; opt-in
over-limit fixtures also passed on btrfs and XFS. This supports those cases,
not universal defaults for slower devices or different client shapes. The
constants must be reviewed against the measured denial and performance
cost before publication. No on-disk format or ioctl structure changes. The
query compatibility decision is explicit: a sufficiently large or frequently
rebuilt domain that once returned results can now fail with these errnos.

## Rejected alternatives and tradeoffs

- Documentation-only acceptance leaves an unprivileged caller able to
  force repeated builds and cross-user eviction. It does not address issue
  118's measured cost.
- Updating paths in the index on directory rename could remove one trigger,
  but not cold first builds or budget-eviction rebuilds; it also changes
  concurrency and correctness behavior beyond this issue.
- An I/O-byte check based only on the calling process's `read_bytes` would
  be untrustworthy for a kernel stack and has no demonstrated attribution
  across all supported lower filesystems.

## Verification gate

Before publication, require a regression that verifies successful normal
builds, explicit denial and the established output-on-error contract,
recovery after the admission window, cumulative counting across retries,
separate UIDs and the mount cap. Run a negative test with the guard removed,
the complete native KVM suite, KASAN/lockdep and a before/after
query-performance comparison. Record both the measured work reduction and
the legitimate queries denied. Journal 0182 separates checks already run
from those still pending. The cooperative boundary remains a limitation
even after the tests pass.

Local verification on 2026-10-23 covered the named regressions: the negative
pre-limit module failed the work-cap assertion; positive regressions covered
normal and oversized builds, UID and mount admission, 61-second recovery,
the cooperative timeout and a moved-directory rescan sharing its budget.
The full native-KVM suite and full KASAN/UBSAN/lockdep suite passed. A
30-pair, order-alternated ext4 cold-query comparison measured a **+0.9%**
wall-median difference for the limited module (95% bootstrap interval
**+0.3% to +1.8%**, journal 0182). This is a measured cost for that shape,
not a universal regression bound. An otherwise legitimate 17,000-file
domain was denied with `E2BIG` on all three supported lower filesystems.
At the 2026-10-23 checkpoint, the cap stopped a 17,000-file fixture early,
but the reduction in CPU time or physical reads for that denied query had not
been measured against the unlimited run. Publication still needs the
maintainer's review of that availability and latency tradeoff; slower devices,
other client shapes and strict CPU/I/O limits remain outside the verified
contract. The issue remains open.

On 2026-10-24, journal 0182 added 30 alternating, fixed-frequency pairs of
that exact cold 17,000-file ext4 query against the pre-limit module. The
guarded query returned `E2BIG` in every run, while the earlier module
completed; median query CPU was 182.045 versus 184.629 ms, with a paired
median saving of 3.370 ms (bootstrap 95% interval 1.213–4.611 ms). Median
task-attributed reads were 4,636,672 versus 4,648,960 B, with a paired
median saving of 20,480 B (interval 8,192–28,672 B). Wall time fell by a
paired median 3.490 ms. These observations resolve the earlier unmeasured
near-threshold cost, but the refused query still spends about 182 ms of
process CPU and the test does not establish a physical-I/O cap. The numerical
policy remains provisional until the maintainer weighs the legitimate denial
against representative larger domains and slower lower devices. This note
does not change the experimental mechanism or authorize publication.

The documented ai-memory stage-2 scale had 60,000 files in ten roughly
6,000-file project domains (journal 0068). A 2026-10-24 opt-in VM control
with one 6,000-file domain and six matching attribute names/types completed
one unprivileged cold `QUERY2` under this guard, building 36,000 entries.
This is a representative per-domain shape, not a rerun of the full client
or evidence for arbitrarily large domains. The 17,000-file denial and its
compatibility cost remain. The policy is still provisional.

A subsequent 2026-10-24 dedicated-VM run on the Mac mini replayed the
historical ai-memory stage-2 bulk scale with the guarded module: 60,000
pages in ten 6,000-page project domains, all pages mirrored, and all 60
SQLite-versus-QUERY2 answer comparisons equal (journal 0182). This is
stronger compatibility evidence for that one client shape than the earlier
single-domain synthetic control. It does not cover MCP writes, a live
sidecar, latency, projects above 6,000 pages or a slow lower device. The
experimental cap and the legitimate 17,000-file denial remain unchanged;
publication still requires the maintainer's explicit choice.
