# Comparison and when not to use MoobFS

Current comparison, updated on 2026-10-20 from the code and journals. The original
[value assessment](value-assessment.md) remains a dated analysis. Its recommendation still holds: **today, for anything outside a laboratory, SQLite or plain
files are the recommendation.** MoobFS is an experimental kernel module with source
pre-release tags, measurements limited to the recorded hosts and workloads, and one tested kernel series; the narrow case it targets is attribute
queries over many Unix users with the operating system's permissions deciding what each
caller sees, while the data stay ordinary files.

## 1. Comparison

The comparison separates implemented behaviour from measurements. Test results apply to the
recorded configurations, not arbitrary workloads or production use.

| Criterion | Plain files | SQLite next to the files (WAL, composite indexes, `ANALYZE`) | FUSE-based filesystems | MoobFS |
| --- | --- | --- | --- | --- |
| Attribute query | `find` + `getfattr`, linear scan | Composite indexes, full SQL (mature) | Depends on the project; logic in a user-space process | Domain-scoped `QUERY2`, up to four ANDed predicates and one ordering attribute; oracle checks and bounded measurements in journals 0048 and 0088 |
| Query authorization | POSIX per file, no query call | In the application | Usually in the daemon | Domain permission check and reader-set partition; per-result checks after permission changes or with security labels (spec-query-v2 §3) |
| Read latency by path | Reference | Reference (file) | Extra user-space round trip | One extra VFS hop; 2 to 5 µs per read by path in the M4 pilot (journal 0088), not a measurement of every later module |
| Writing a memory | Reference | File plus row (two writes) | Through the daemon | File plus xattr plus RAM update; durable-write latency measured, no demonstrated write-cost advantage (journal 0088); physical write amplification still unmeasured |
| Recovery between sessions | Nothing to rebuild | Opens the file | Depends | Linear scan of each domain at its first query |
| Memory | None | Page cache, pageable | Process memory | Non-pageable index; about 0.5 to 0.6 KiB per file in the M4 accounting, which undercounts; query buffers are additional |
| Maturity | Maximal | Decades | Varies | Experimental, one maintainer, no independent human review of the whole kernel code |
| Failure domain | Minimal | Process | Process (the kernel survives) | Host (ring 0) |
| Installation | None | Library | FUSE library and a process; often no root | Kernel module, root, module signing |
| Portability | Total | Total | Linux, macOS and others | Linux 6.12 in a test VM |
| Reversibility | Total | High (stable file format) | Depends | Total: unmount and the lower tree is plain files (proven) |
| Observability | `ls`, `getfattr` | `EXPLAIN`, shell | Process logs | `GET_INFO`, `moobfs-check`, `dmesg`, index tracepoints |
| Atomicity of data and metadata | No | Yes, inside the database | Depends | No; a client convention |

The FUSE column is a general description of the approach, not a measurement of any product;
no FUSE system has been benchmarked against MoobFS, so no measured comparison is available ([benchmark-plan.md](benchmark-plan.md)). MoobFS is native kernel code by design
([ADR-0001](decisions/0001-native-filesystem.md)), which is a trade-off, not a merit in
itself: a defect in the module can crash the host, where a FUSE defect kills a process.

## 2. Memory frameworks are clients, not competitors

mem0, Letta, ai-memory and similar projects decide what an agent remembers, how it is
summarized, ranked and injected into a prompt. MoobFS does none of that: it stores files and
answers attribute predicates. In the M4 plan these frameworks appear as **workloads** run on
top of the layer (a file backend or data directory over a MoobFS mount), not as baselines to
beat ([benchmark-plan.md](benchmark-plan.md); [ADR-0007](decisions/0007-stacked-memory-layer.md),
note on acceptance). The ai-memory test and LangGraph adapter provide bounded compatibility
and query evidence (journals [0088](history/0088-m4-report.md) and
[0089](history/0089-second-client-langgraph-adapter.md)), not proof of a benefit to every client.

## 3. When MoobFS is probably not worth it

- **Deep trees with domains.** Nothing more than 64 components below the mount root is indexed
  or answered by `QUERY2`; the files themselves still work (finding F14, journal 0156). A memory
  store laid out deeper than that gets no queries for those files.
- **Round 2 of the review** ([0155](history/0155-review-3627172-round-2.md)) found further defects
  in `v0.4.1-experimental`, F14 among them. They are fixed in journal 0156 or recorded as issues;
  `v0.4.1-experimental` still carries them, `v0.5.0-experimental` does not. Of those issues, 115 and 119 are fixed in journals 0221, 0222 and
  0224, and 117 in part (journal 0223: inodes without an index record no longer take the index
  lock on instantiation or eviction; inodes with one still do).

The two defects from [review 0144](history/0144-independent-review.md) remain in
`v0.4.1-experimental` (not in `v0.5.0-experimental`), and so does a third, from [review 0151](history/0151-review-d110d4e.md): a
chmod during the first build of a domain could leave the file in `QUERY2` answers to users who can
no longer read it (fixed in [0152](history/0152-chmod-during-build-fixed.md)). The review batch synchronizes backing-file flags after `F_SETFL`
([0149](history/0149-sync-backing-file-flags.md)) and propagates operational lower `getxattr`
errors ([0150](history/0150-propagate-index-xattr-read-errors.md)). Positive and negative regression
evidence exists for clearing append and for injected value-read errors, respectively.
The full integrated VM suite at `49098aa` passed, including clearing and setting append on
ext4, btrfs and XFS, the R2 injected-error regression, and the acceptance/layer suites.
The local batch KASAN/UBSAN/lockdep suite passed on Linux 6.12.112, and the
required pull-request checks passed before that batch merged. The corrections
are on `main`.
Injecting a value-read result is not a reproduction of
an actual lower-filesystem I/O fault. Keep using disposable test environments. The released
tag does not contain these corrections.

These limits update the cases discussed in the value assessment, §9.6.

- **A single process or a single agent owns the data.** There is no cross-user query to
  authorize; SQLite or files are faster to adopt and mature. The assessment rates this case
  "low probability of being worth it".
- **You need a mature database query engine.** `QUERY2` supports up to four ANDed predicates
  and ordering by one attribute. An optional SQLite virtual table provides SQL over it
  ([ADR-0013](decisions/0013-user-space-query-library.md)), but is a prototype. Full-text and
  vector retrieval belong in the client or a database.
- **The host is not Linux, or you do not have root.** Mounting requires `CAP_SYS_ADMIN` in
  the initial user namespace; user-namespace and idmapped mounts are refused. Hosted CI,
  serverless, macOS and Windows are out, and so is a rootless container on a host where nobody
  with root mounts the layer for it. A rootless Podman container can use a layer the host mounted,
  under its user's own uid ([container-integration.md](container-integration.md)).
- **The host runs SELinux enforcing and you cannot add a policy rule.** The shipped policies
  have no rule for `moobfs_layer`, so every file through the layer is unlabelled and confined
  processes are refused everything. With the rule `fs_use_xattr moobfs_layer`
  ([vm/selinux/moobfs_layer.cil](../vm/selinux/moobfs_layer.cil)) files keep their labels and
  the first test agreed with direct access; every `QUERY2` result is then checked for the
  caller, at a cost not measured ([journal 0104](history/0104-selinux-investigation.md)). One
  run, one kernel; Smack and AppArmor were not exercised.
- **Your kernel is outside the tested matrix.** Only Debian 6.12.111 (QEMU) and upstream
  6.12.112 (KASAN) have run the module; other kernels are not promised to compile or behave.
- **The corpus is large.** The index is non-pageable RAM, bounded by `index_budget`
  (default 128 MiB). A failed update marks its domain for rebuilding; its next query returns
  a rebuilt answer or an explicit error
  ([ADR-0016](decisions/0016-failed-index-update-rebuilds-its-domain.md)). This recovery does not
  by itself cover lower-read errors; R2 separately corrects that path in the local batch above. A domain larger than
  the budget fails with `ENOMEM`. Users still compete for the shared budget and can force
  repeated releases and scans above their shares (ADR-0027). A query buffer can additionally
  retain about 1.1 MB per open file until close (journal 0139), outside the index budget.
- **Untrusted users share a mount.** An authorized `QUERY2` can scan its
  entire domain on first use and after directory renames. The memory budget
  does not limit that scan's CPU or lower I/O. A budget release no longer
  drops a domain last built by another filesystem uid; the query returns
  `ENOMEM` when the remainder is too small, unless that uid is above its
  share and the builder below its own, in which case the builder takes the
  room back (ADR-0027). One uid alone may still occupy the whole budget.
  Bounded native-KVM examples of the scan cost are in
  [journal 0182](history/0182-query-build-resource-cost-investigation.md);
  the host regression of the release rule is
  [journal 0192](history/0192-same-fsuid-budget-release.md)
  ([ADR-0021](decisions/0021-budget-release-keeps-the-builders-domain.md)).
  `main` includes experimental work and frequency limits
  ([ADR-0019](decisions/0019-bound-query-build-work-and-frequency.md)),
  but not a strict CPU/I/O cap or fair memory shares. The 32,768-unit
  default denies the measured valid 17,000-file domain; an administrator
  can opt into up to 65,536 units per mount under the merged ADR-0020
  policy, which admitted that fixture in a dedicated VM. Issue 118 remains
  open for slow lower-device and larger real-client evidence.
  The opt-in native-KVM probe
  of ADR-0021 passed on the dedicated host, and so did `release`, `overbudget`
  and `buildcostevict` under KASAN, UBSAN and lockdep. The complete KASAN,
  UBSAN and lockdep suite of this change then exited 0 (`MOOBFS_KASAN_PASS`).
  The complete native-KVM suite of this change was not run.
- **You need it in production today, or in any critical setting.** Only source pre-release tags exist; the
  module is mostly one author's code; its own tests, two source reviews by AI assistants and private
  reports from an external reviewer found kernel-side defects, security ones among them (findings F1 to F17 of ADR-0011); there is no
  independent human review of the whole kernel code (the external reviewer's reports and the fix of F16 aside); fstests ran as its `quick` group only and syzkaller for five six-hour
  rounds. A module bug can mean a kernel panic or data loss.
- **The team cannot maintain an out-of-tree kernel module** across kernel updates, signing
  and DKMS; the project has DKMS packages tested in one VM only (journal 0229), no published
  package, and one supported kernel series.
- **Programs or device nodes live in the store.** The layer marks its superblock `SB_I_NOEXEC`
  and `SB_I_NODEV` (overlayfs does not; binfmt_misc and pidfs do): nothing runs from it
  (`execve` fails with `EACCES`, `mmap` with `PROT_EXEC` with `EPERM`, `access(X_OK)` with
  `EACCES`) and opening a device node in it fails with `EACCES`, whatever the mount options
  (`path_noexec()`, `fs/exec.c`; `may_open_dev()`, `fs/namei.c`).
  It is meant for data files; keep executables, scripts run by path and devices elsewhere
  (journal 0221).
- **Deduplication, reflink or snapshots are the attraction.** Those come from btrfs or XFS
  and work without MoobFS; the layer only forwards the calls.

## 4. When it could be worth evaluating

Conditions from the "Casos em que o MoobFS pode vencer" paragraph and §9.4; all of them are
hypotheses to test in a disposable VM, not recommendations for deployment.

- **Several agents with distinct uids share one memory store on one Linux host** and must
  query across it while each sees only what POSIX modes, groups and ACLs allow, with no
  second authorization layer to maintain in the application.
- **The data must stay readable by git, grep, rsync and backup tools** without an export
  step, and still be queryable by attribute while the layer is mounted.
- **No database daemon per host is wanted**; this matters mainly against server databases,
  since SQLite is also in-process.
- **The host has root, a fixed kernel and a corpus inside the index budget**, and the team
  accepts the recorded costs and the possibility that its own workload will not benefit.

## 5. What has been measured, and what is still missing

The [M4 report](history/0088-m4-report.md) is complete as a pilot, including losses. Its early
SQLite and client comparisons used few repetitions and are not fresh results for every later
tag. The [40-repetition confirmation](history/0143-syzkaller-fifth-run-threshold-met.md)
compares modules, with confidence intervals: it confirms the query-buffer regression is fixed
and leaves a 1 to 3 % residual in p50 against module 0102 on filtered queries, with larger
differences in some p99 results. It does not update the
SQLite comparison. See the [measurement plan](benchmark-plan.md) for the remaining methodology.

| Measurement | Evidence and remaining gap |
| --- | --- |
| Query latency against SQLite | Synthetic and real-client shapes measured (journals 0048, 0068, 0088); repeat with current modules, intervals and SQLite cache settings; SQLite does not perform the same filesystem permission checks |
| Durable writes and pass-through overhead | M4 pilot measured latency (journal 0088); physical write amplification still unmeasured |
| Mount and first-query cost | On-demand index measured cold and warm (journal 0098); one data set and host |
| Index budget | Small-budget release/rebuild and failed-update recovery tested (journals 0101, 0130); default-budget saturation series and actual RAM versus `index_used` still missing |
| Concurrency | Correctness races tested (journals 0052, 0098); throughput scaling across clients still missing |
| Real clients | ai-memory acceptance and structured queries, plus LangGraph adapter equivalence (journals 0057, 0068, 0089); no general client-performance claim |
| Authorization | Race and timing tests recorded (journals 0020, 0043, 0052, 0116); independent human review of the whole kernel code still missing (an external reviewer privately reported F15, F16 and GHSA-756j-cgwq-gvwm) |

A run in which the layer wins none of these is a valid result and, as
[ADR-0007](decisions/0007-stacked-memory-layer.md) states, should change the project's
direction. The criteria for ever moving from experiment to dependable technology (full
xfstests, continuous KASAN and syzkaller, two kernel series in CI, twelve months without a
data-loss bug, independent reproduction, published external review, more than one maintainer)
are listed in §9.6 of the value assessment.
