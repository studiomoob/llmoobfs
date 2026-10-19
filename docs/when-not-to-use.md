# Comparison and when not to use MoobFS

A summary of [value-assessment.md](value-assessment.md), written so
that a reader can decide quickly whether the project is relevant to them. The short version,
copied from that assessment: **today, for anything outside a laboratory, SQLite or plain
files are the recommendation.** MoobFS is an experimental kernel module with only a source pre-release tag, no
measured performance and one tested kernel series; the narrow case it targets is attribute
queries over many Unix users with the operating system's permissions deciding what each
caller sees, while the data stay ordinary files.

## 1. Comparison

Rows copied from the "Comparação honesta" table of the value assessment. "Proven" means
tested in the project's VM at small scale; "not measured" means exactly that.

| Criterion | Plain files | SQLite next to the files (WAL, composite indexes, `ANALYZE`) | FUSE-based filesystems | MoobFS |
| --- | --- | --- | --- | --- |
| Attribute query | `find` + `getfattr`, linear scan | Composite indexes, full SQL (mature) | Depends on the project; logic in a user-space process | Conjunctive `QUERY`, in-RAM scan; correct against an oracle, speed not measured |
| Query authorization | POSIX per file, no query call | In the application | Usually in the daemon | POSIX per result, by the kernel (proven at test scale) |
| Read latency by path | Reference | Reference (file) | Extra user-space round trip | Reference plus one VFS hop; magnitude unknown |
| Writing a memory | Reference | File plus row (two writes) | Through the daemon | File plus xattr plus RAM update; amplification not measured |
| Recovery between sessions | Nothing to rebuild | Opens the file | Depends | Linear scan of each domain at its first query |
| Memory | None | Page cache, pageable | Process memory | Non-pageable index, estimated 0.3 to 2 KiB per inode |
| Maturity | Maximal | Decades | Varies | Months, one author |
| Failure domain | Minimal | Process | Process (the kernel survives) | Host (ring 0) |
| Installation | None | Library | FUSE library and a process; often no root | Kernel module, root, module signing |
| Portability | Total | Total | Linux, macOS and others | Linux 6.12 in a test VM |
| Reversibility | Total | High (stable file format) | Depends | Total: unmount and the lower tree is plain files (proven) |
| Observability | `ls`, `getfattr` | `EXPLAIN`, shell | Process logs | `GET_INFO`, `moobfs-check`, `dmesg` |
| Atomicity of data and metadata | No | Yes, inside the database | Depends | No; a client convention |

The FUSE column is a general description of the approach, not a measurement of any product;
no FUSE system has been benchmarked against MoobFS, and no comparison may be stated before
M4 ([benchmark-plan.md](benchmark-plan.md)). MoobFS is native kernel code by design
([ADR-0001](decisions/0001-native-filesystem.md)), which is a trade-off, not a merit in
itself: a defect in the module can crash the host, where a FUSE defect kills a process.

## 2. Memory frameworks are clients, not competitors

mem0, Letta, ai-memory and similar projects decide what an agent remembers, how it is
summarized, ranked and injected into a prompt. MoobFS does none of that: it stores files and
answers attribute predicates. In the M4 plan these frameworks appear as **workloads** run on
top of the layer (a file backend or data directory over a MoobFS mount), not as baselines to
beat ([benchmark-plan.md](benchmark-plan.md), "Sistemas comparados"; [ADR-0007](decisions/0007-stacked-memory-layer.md),
note on acceptance). Whether any of them gains anything from running over MoobFS is an open
question that only M4 can answer.

## 3. When MoobFS is probably not worth it

Each line comes from the "Casos em que provavelmente não vale a pena" and §9.6 of the value
assessment.

- **A single process or a single agent owns the data.** There is no cross-user query to
  authorize; SQLite or files are faster to adopt and mature. The assessment rates this case
  "low probability of being worth it".
- **You need OR, sorting, aggregation, joins, full-text or vector search.** `QUERY` is a
  conjunction of at most four attribute predicates; everything else belongs in the client or
  in a database.
- **The host is not Linux, or you do not have root.** Mounting requires `CAP_SYS_ADMIN` in
  the initial user namespace; user-namespace and idmapped mounts are refused. Hosted CI,
  rootless containers, serverless, macOS and Windows are out.
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
  (default 128 MiB, an estimated order of 10^5 inodes); near the budget, answers can be
  incomplete; each domain's part is rebuilt by a linear scan on its first query after a mount or
  a release.
- **You need it in production today, or in any critical setting.** Only a source pre-release tag exists; the
  module is one author's code; its own tests and two source reviews by AI assistants found
  kernel-side defects, security ones among them (findings F1 to F12 of ADR-0011); there is no
  outside human review; fstests ran as its `quick` group only and syzkaller for three six-hour
  rounds. A module bug can mean a kernel panic or data loss.
- **The team cannot maintain an out-of-tree kernel module** across kernel updates, signing
  and DKMS; none of that tooling exists in the project.
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
  accepts that a negative M4 result may end the experiment.

## 5. What M4 must show before any performance claim

From "Medições do M4 indispensáveis" in the value assessment and the harness specification
([benchmark-harness.md](benchmark-harness.md)). Until these runs exist, the project makes
**no** statement about speed, efficiency, memory use or scale.

| Measurement | Purpose |
| --- | --- |
| W3: p50/p95/p99 latency for the fixed query forms Q1 to Q9 against SQLite with composite indexes, after `ANALYZE`, gated by `EXPLAIN QUERY PLAN`; the cost of per-result authorization isolated | Whether the in-kernel index is at least equivalent |
| W1 and relative write amplification (`WA_relative`) at equal durability | Whether one store plus xattr beats file plus row |
| W2: pass-through overhead against the bare lower filesystem | The price of the extra VFS hop |
| W6/W7: index rebuild and remount at scales S1 to S6, cold and warm cache | Whether the RAM-only index is viable |
| Budget saturation from 50 to 100 %, real RAM versus `index_used` | What happens near the limit |
| W5/W8: concurrency and the global index lock | Scaling with concurrent clients |
| An unmodified real client (ai-memory or equivalent) over the mount | Compatibility beyond the own test client |
| `chmod`/`rename` racing `QUERY` under KASAN; a timing-channel assessment of authorization | Safety of the authorization path |

A run in which the layer wins none of these is a valid result and, as
[ADR-0007](decisions/0007-stacked-memory-layer.md) states, should change the project's
direction. The criteria for ever moving from experiment to dependable technology (full
xfstests, continuous KASAN and syzkaller, two kernel series in CI, twelve months without a
data-loss bug, independent reproduction, published external review, more than one maintainer)
are listed in §9.6 of the value assessment.
