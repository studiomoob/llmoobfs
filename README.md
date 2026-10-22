# MoobFS

**An agent's memory as ordinary files that the kernel can query, with file permissions deciding who sees what.**

[Project site](https://studiomoob.github.io/llmoobfs/) · [Measurements](docs/history/0088-m4-report.md) · [Try it in a VM](#try-it-in-a-vm) · [How to help](docs/help-wanted.md) · [Docs index](docs/README.md) · [FAQ](docs/faq.md) · [Roadmap](ROADMAP.md) · [Português](README.pt-BR.md)

[![CI](https://github.com/studiomoob/llmoobfs/actions/workflows/ci.yml/badge.svg)](https://github.com/studiomoob/llmoobfs/actions/workflows/ci.yml)
[![License: GPL-2.0-only](https://img.shields.io/badge/license-GPL--2.0--only-blue)](LICENSE)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/studiomoob/llmoobfs/badge)](https://scorecard.dev/viewer/?uri=github.com/studiomoob/llmoobfs)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/15408/badge)](https://www.bestpractices.dev/projects/15408)
[![Zenodo DOI: experimental snapshot](https://zenodo.org/badge/DOI/10.5281/zenodo.23514721.svg)](https://doi.org/10.5281/zenodo.23514721)

> **Experimental. Not for production.** One kernel series (6.12), tested in a VM only; no outside
> human review yet (two independent source reviews by AI assistants, journals 0111 and 0114). The last
> tag, `v0.4.1-experimental` (2026-10-20), is a source snapshot before this review batch.
> The two findings of the [independent review](docs/history/0144-independent-review.md)
> have corrections on `main`; the review batch passed the full integrated VM suite
> and local KASAN/UBSAN/lockdep. The required pull-request checks passed before merge.
> `v0.4.1-experimental` still contains both defects. A later review (journal 0151) found a third,
> a chmod during an index build that left the file in `QUERY2` answers; fixed in journal 0152.
> See [Limitations](#limitations).

The later domain-rename and concurrent index-budget corrections were merged
through [PR 126](https://github.com/studiomoob/llmoobfs/pull/126) after its
11 reported checks passed; issues 110 and 111 were closed. A subsequent native
KVM run found another concurrent-budget refusal. Trying another eligible
domain when the oldest one's build mutex is busy, and the remaining
unprivileged allocation-fault test for T9, were integrated by
[PR 127](https://github.com/studiomoob/llmoobfs/pull/127) after all eleven
checks passed, including KASAN/lockdep ([journals 0178](docs/history/0178-unprivileged-allocation-fault-queries.md)
and [0179](docs/history/0179-kvm-overbudget-race-followup.md)). Issue 114 is
closed. Resource-cost issue 118 remains open; issue 116 is under separate
validation in journal 0180 ([roadmap](ROADMAP.md)).

## The problem

An LLM agent that remembers across sessions usually keeps its memories as files and, beside them,
a database or index that records what each one is: its kind, its project, its date. The
application has to keep the two in step on every write. And which user or agent may see which
memory is decided in application code, not by the operating system that already holds the files'
permissions.

## What MoobFS is

MoobFS is an experimental Linux kernel module (GPL-2.0, no FUSE, no daemon) that stacks over a
directory of ext4, btrfs or XFS and moves those two jobs into the filesystem. It has three parts:

1. **Typed attributes on ordinary files.** A memory is a file; its fields are `user.moob.*`
   extended attributes, each with a type the kernel checks on write. Without the module the
   directory is still a plain tree that `grep`, `getfattr`, `git` and `rsync` read.
2. **An index in the kernel.** The module indexes those attributes in RAM and updates the index
   when an attribute is written. Nothing is scanned at mount: the first query on a directory
   builds that directory's part of the index, which is released again when memory is short.
3. **A query scoped by directory and authorized by file permissions.** One `ioctl` (`QUERY2`) on
   a directory answers up to four comparisons joined by AND, optionally ordered by one
   attribute. It returns only files that everyone allowed into that directory may read.

It stores and finds memories. Choosing what is relevant, embeddings, ranking and building the
prompt stay in the application.

For batch queries, use output only after success; discard every result and output
cursor on error ([client contract](docs/client-contract.md#6-writing-and-querying),
[regression scope](docs/testing.md), journal 0173).

## Four numbers

All four are pilot-grade and from one host (a dedicated machine with a SATA SSD, Debian 13,
kernel 6.12.111, ext4 below the layer): one data-set seed, and one run per scale for the client
comparison. The first three compare `QUERY2` with the SQLite database of one real memory client,
on that client's own six structured queries, issued by a test tool on both sides.

| | What | Condition | Source |
| --- | --- | --- | --- |
| **0 differences** | Answers of `QUERY2` against the client's SQLite | Every check, from 150 to 60 000 memories | [report, question 4](docs/history/0088-m4-report.md); journals [0064](docs/history/0064-client-stage2-first-run.md), [0068](docs/history/0068-client-stage2-scale.md) |
| **5 to 6.5 times faster** | The layer wins on one query shape (a set of kinds); warm, median | 2 000 to 60 000 memories. Part of the gap is this client's schema, which keeps the field in a JSON column | [report, question 4](docs/history/0088-m4-report.md); [journal 0068](docs/history/0068-client-stage2-scale.md) |
| **0.71 to 0.51 of the speed** | The layer loses on the long ordered traversal; warm, median | Slower at every scale and by more as the answer grows: about half of SQLite's speed at 60 000 memories | [report, question 4](docs/history/0088-m4-report.md); [journal 0068](docs/history/0068-client-stage2-scale.md) |
| **2.7 s** | The first query on a domain costs a scan of that domain's files; the mount itself takes 0.3 s | A domain of 7 459 files out of 60 000, cold cache, on the SSD (0.1 s warm). Before journal 0098 the mount scanned every file: 19 s cold. The client's SQLite file opened cold in 17 to 23 ms | [journal 0098](docs/history/0098-index-on-demand.md); [report, question 2](docs/history/0088-m4-report.md) |

Of the other four shapes, the layer is faster on two and ties on two. The comparison counts
only the paths returned, not reading the fields of each result, which SQL returns in the same
call. More under [What was measured](#what-was-measured).

## See it run

Each top-level directory of the mount is a permission domain. Root tags four memory files;
Alice (uid 1001) then queries. She sees her own memory, cannot enter Bob's domain, and cannot
run the mount-wide query, which is for administrators:

```text
# (prepared on the lower ext4: alice/ uid 1001 0700, bob/ uid 1002 0700, team/ 0755)
# mount -t moobfs_layer -o lower=/lower none /mnt/layer
# moobfs-attr set /mnt/layer/alice/cache.md kind string decision
# moobfs-attr set /mnt/layer/bob/deploy.md kind string decision
# moobfs-attr set /mnt/layer/team/style.md kind string decision
# moobfs-attr set /mnt/layer/team/salary.md kind string decision
# moobfs-attr list /lower/alice/cache.md
kind	string	decision
# stat -c '%A %u %n' /mnt/layer/team/*
-rw------- 1002 /mnt/layer/team/salary.md
-rw-r--r-- 0 /mnt/layer/team/style.md
# query --uid 1001 /mnt/layer/alice q2 kind eq string decision
16	cache.md
# query --uid 1001 /mnt/layer/team q2 kind eq string decision
18	style.md
# query --uid 1001 /mnt/layer/bob q2 kind eq string decision
open: Permission denied
# query --uid 1001 /mnt/layer kind eq string decision
MOOB_IOC_QUERY: Operation not permitted
# query /mnt/layer kind eq string decision
16	alice/cache.md
17	bob/deploy.md
18	team/style.md
19	team/salary.md
```

In the shared `team/` domain she gets `style.md` only. `salary.md` is readable by Bob alone, and
a domain indexes only files that everyone who can enter it can read, so it is outside `team/`'s
index for every caller, Bob and root included; private files belong in their own domain
([spec-query-v2](docs/spec-query-v2.md) §3).

This is real output of the demonstration step of the project's VM suite (Debian 6.12.111 in
QEMU, ext4 lower): the `DEMO| ` lines of `build/vm/console.log` with the prefix removed and the
two closing `GET_INFO` calls left out. Lines starting with `#` are commands run as root. `query`
is the test client in [`tests/query.c`](tests/query.c), since there is no CLI yet; `--uid 1001`
switches to that user before the call, and the `q2` form is `MOOB_IOC_QUERY2` on a domain
directory. The script is the demonstration block of [`vm/suite.sh`](vm/suite.sh). To get it
from your own run, see [Try it in a VM](#try-it-in-a-vm).

## Status

**Experimental. Source pre-release tags only; the last one, `v0.4.1-experimental` (2026-10-20), has
the fixes of findings F1 to F12 of ADR-0011 except the fair-shares part of F2. Not for
production.** Three later findings affect it and are fixed on `main` only: stale file-status flags
after `F_SETFL` and attribute-read errors taken as "no attribute" while indexing (review
[0144](docs/history/0144-independent-review.md), R1 and R2), and a chmod during the first build of a
domain that could leave the file in `QUERY2` answers to users who can no longer read it (journals
[0151](docs/history/0151-review-d110d4e.md), [0152](docs/history/0152-chmod-during-build-fixed.md)).
Facts as of 2026-10-20:

- **One kernel series**, in a VM: Debian 6.12.111 in QEMU and upstream 6.12.112 under
  KASAN/UBSAN/lockdep, required on every pull request that touches the module; x86-64; lower
  filesystems ext4, btrfs and XFS only. The attribute encoding's tests also pass on big-endian
  s390x under emulation. The module also runs on arm64 under emulation, without KVM, in an
  exploratory job: the ext4, btrfs and XFS steps passed under KASAN ([journal 0137](docs/history/0137-arm64-guest-boots-under-emulation.md)).
- **Tested with** fault injection inside the module, fstests' `quick` group on the three lower
  filesystems, syzkaller (five six-hour runs; one defect found and fixed), a first KCSAN run (no
  race in the module) and SELinux enforcing (journals 0091, 0096, 0104, 0109, 0112, 0115, 0133, 0138, 0143).
- **No outside human review yet.** It was requested on 2026-10-13. Two independent source reviews
  made by AI assistants ([journal 0111](docs/history/0111-review-5582e06.md),
  [journal 0114](docs/history/0114-second-inspection-of-5582e06.md)) and verified against the
  kernel found four security defects (findings F9 to F12 of ADR-0011), reported as private
  advisories and fixed in journals 0116 and 0117; our own tests and reviews found others before.
  Tags up to `v0.4.0-experimental` carry defects fixed since ([changelog](CHANGELOG.md)).
- **The measurements are pilot-grade**, from one host; the report that gathers them is
  [journal 0088](docs/history/0088-m4-report.md). They are not a general speed claim.
- The index lives **in RAM only** and cannot be paged out. It is built per domain by the first
  query on it, and released again under memory pressure or to stay within its budget, least
  recently queried first; a query that needs every domain (the
  mount-wide `QUERY`) costs what the mount scan used to.
- Mounting needs **`CAP_SYS_ADMIN` in the initial user namespace**.
- **The interface may change**, with no compatibility promise.

For anything beyond a disposable VM, SQLite or plain files remain the recommendation
([when-not-to-use.md](docs/when-not-to-use.md)). The full list is under
[Limitations](#limitations).

## How to help

The project has one maintainer and no outside human review yet. What would help most, by how much
kernel knowledge it takes, is in **[docs/help-wanted.md](docs/help-wanted.md)**: a review of the
credential model or of the index locking, if you know the VFS; a check of whether your memory
system fits, if you build one; a run of the tests on your machine or on another kernel; and
documentation or reproducibility corrections that need no kernel knowledge. Rules for changes are in
[CONTRIBUTING.md](CONTRIBUTING.md).

---

## How it works

```text
client (git, sqlite3, a script, an agent)
   │ write tmp, setxattr user.moob.*, fsync, rename        │ QUERY2 ioctl on a domain directory
   ▼                                                        ▼
Linux VFS ──► moobfs_layer.ko: validates typed attributes, keeps an in-RAM index
              partitioned by domain, checks the caller's permission on the directory,
              delegates I/O through fs/backing-file.c
   ▼
lower filesystem (ext4, btrfs or XFS): real files, real xattrs, journal, reflink
```

Writing a memory is an ordinary POSIX write plus `setxattr`; the layer validates the encoding
(type byte plus payload) before the xattr reaches the lower filesystem and updates its index.
Finding memories is one `QUERY2` call on a domain directory: up to four attribute predicates
joined by AND, answered from that domain's partition of the index after one permission check on
the directory. A file that not everyone entering the domain may read is left out of the
partition ([spec-query-v2](docs/spec-query-v2.md) §3). The client then opens the file
normally. For IN lists, OR, sorting and counts there is an optional SQLite extension
([sqlite/](sqlite/README.md), prototype): SQL over a domain with no database, every row still a
`QUERY2` answer. Unmount the layer and the lower
directory is a plain tree that `grep`, `getfattr`, `git` and `rsync` read without the module.
Details with source citations: [architecture overview](docs/architecture-overview.md); the
same flow as a diagram: [site/plan.html](https://studiomoob.github.io/llmoobfs/plan.html#flow).

**What it is.** A native Linux kernel module, `moobfs_layer.ko`, mounted as a stacked layer
over a directory of a mature filesystem (ext4, btrfs or XFS). Memories are ordinary files;
their metadata are typed extended attributes (`user.moob.*`) that the module validates on
write and indexes in RAM, partitioned by permission domain (by default, each top-level
directory). A `QUERY2` ioctl on a domain directory answers conjunctive attribute predicates from
the kernel, returning only files that everyone who can enter the domain may read; the older
whole-mount `QUERY` checks each result against POSIX permissions and ACLs and, with domains on
(the default since phase Q2a), is for administrators only. Everything stays readable with plain
Unix tools without the layer. A read-only M1 image format with shared physical payloads is kept
as a regression.

**What it is not.** Not FUSE and not a daemon: there is no user-space server, socket or
background process. No semantic or vector search in the kernel: ranking, embeddings and
context assembly belong to the client. Not a database replacement; for anything beyond a
disposable VM today, SQLite or plain files remain the recommendation
([value assessment](docs/value-assessment.md)). No persistence of the index, no snapshots and no
transactions of its own; durability, reflink and dedupe are the lower filesystem's.

**The goal.** MoobFS aims to provide a filesystem-level storage foundation for persistent memory
and context consumed by LLM-based agents: to retain memories such as observations, decisions and
task records across sessions, share repeated content and eventually preserve revisions that
agents can retrieve as context. The filesystem stores and exposes memory as files. The agent
application is responsible for selecting relevant material and assembling the LLM's context.
Semantic retrieval and context assembly still need to be designed and integrated; MoobFS does
not currently provide them or extend a model's context window.

**What a memory system needs to use it.** One file per memory, fields as typed attributes, and
every scope it isolates as a directory at one fixed depth:
[client-contract.md](docs/client-contract.md). The risks it takes by doing so, and what answers
each: [adoption-risks.md](docs/adoption-risks.md).

**How to check any claim here.** Run `make vm-test` and read `build/vm/console.log`
([testing guide](docs/testing.md)); set up the environment with
[docs/development.md](docs/development.md).
Require the final markers and no `MOOBFS_VM_FAIL`; QEMU exit 0 alone is insufficient.
`CASEFOLD_SKIPPED`, `STALL_SKIPPED` and an idmapped lower marked `not tested`
remain coverage exclusions. `ipc` is built but not invoked by the suite.

For targeted permission and fixed independent-encoding checks, use
`MOOB_ONLY=permgaps make vm-test` ([journal 0172](docs/history/0172-permission-test-gaps.md)).
The release step also queries as uid 1001. The KASAN fault step tests
unprivileged queries under injected allocation failures. Its targeted and
full KASAN runs and the native `make vm-test` suite passed locally, followed
by the successful PR 127 checks and merge ([journal 0178](docs/history/0178-unprivileged-allocation-fault-queries.md)).
The budget-race follow-up was reproduced before its local fix and passed in
the complete Docker VM, dedicated-host KVM and KASAN suites afterward
([journal 0179](docs/history/0179-kvm-overbudget-race-followup.md)).


## What was measured

The [M4 report](docs/history/0088-m4-report.md) gathers the early campaigns: one data-set seed,
three repetitions at most, often one run, and run-to-run noise of about 20 to 25 % on values of
tens of microseconds. Those are historical results for the modules named in each journal, not
fresh measurements of this tag. Numbers from different hosts are not compared with each other.

The later module-to-module comparison uses 40 repetitions, held CPU frequency, rotated module
order and 95 % bootstrap intervals ([method](docs/history/0134-measurement-method-review.md),
[journal 0139](docs/history/0139-query-buffer-regression.md),
[confirmation](docs/history/0143-syzkaller-fifth-run-threshold-met.md)). It found and fixed a
roughly 2x query regression. The fixed module remains 1 to 3 % slower in median latency (p50) than module 0102 on three
filtered shapes; the on-demand design in 0102 costs 7.6 to 12.5 % against the earlier module
that built its index at mount. The cause of the remaining 1 to 3 % is unresolved. This is one
host and one data set; it does not rerun the comparisons with SQLite or the real client.
Tail latency differs more on some shapes: the median of per-run p99 for the two-equality
query is 54 % above module 0102 in that confirmation (95 % interval: +51 to +55 %).

What the layer added per operation in the M4 report's campaign (10 000 memories, medians, bare
lower against the layer): a read by path went from 8.5 to 11.0 µs on ext4 (10.1 to 12.0 on btrfs,
9.8 to 15.0 on XFS); setting one attribute from 8.9 to 21.7 µs (8.9 to 21.3; 6.0 to 11.6);
creating a memory durably was unchanged within noise, because device flushes took 12 to 14 ms.

What a side database costs at write time, which this project had assumed and never measured:
almost nothing. Publishing a memory durably takes 6.6 to 8.5 ms with the file alone, and 6.6 to
8.7 ms with a row added to a SQLite database in its usual mode; through the layer, with eight
attributes, 7.0 to 9.8 ms. So the layer does not make writes cheaper. What it removes is the
second copy of the fields, which can disagree with the files after a crash.

Where the layer wins:

- **Selective queries inside one domain.** On a synthetic set of 60 000 memories, `QUERY2` was
  1.1 to 6 times faster than SQLite with composite indexes on the five shapes measured, the
  low end being a tie at SQLite's best case
  (journals [0044](docs/history/0044-q2-measured-equality-plan.md),
  [0047](docs/history/0047-sqlite-baseline-macmini.md),
  [0048](docs/history/0048-recent-optimization.md), [0049](docs/history/0049-q2-btrfs-xfs.md)).
- **Three of a real client's six query shapes** are faster, by 5 to 6.5 times and 1.6 to 3.8
  times on the two clearest, with equal answers in every check from 150 to 60 000 memories
  ([journal 0068](docs/history/0068-client-stage2-scale.md)).
- **That client runs unchanged on the layer**: its 1 147 tests pass and its retrieval results
  are identical, after a layer defect it exposed was fixed
  (journals [0054](docs/history/0054-real-client-plan.md),
  [0057](docs/history/0057-rename-over-deleted-name.md)).

Where it loses, ties or costs:

- **Long ordered traversal**: 0.71 to 0.51 of SQLite's speed, worse as the answer grows. Two
  other shapes of the same client are ties.
- **The first interface** (v1 `QUERY`, on the mount root) is slower than SQLite on every shape
  that returns results: 18 times slower at 60 000 memories in one campaign
  ([journal 0029](docs/history/0029-query-scale-series.md)).
- **Fields of the results.** A query returns inode numbers and paths. Reading all attributes of
  a result costs about 16 µs per file, against about 0.9 µs per result for the query; SQL
  returns columns in the same call, and no comparison above counts this
  ([journal 0072](docs/history/0072-m3-phase-3c.md)).
- **Mount time and first query.** With the index built on demand, 0.31 s cold and 0.004 s warm
  to mount 60 000 files on the SATA SSD; the first `QUERY2` on a domain of 7 459 of them, 2.7 s
  cold and 0.10 s warm; the mount-wide `QUERY` right after mounting, 19.7 s cold and 0.74 s warm
  ([journal 0098](docs/history/0098-index-on-demand.md)). Before, the mount scanned everything:
  0.7 s warm and 18.5 to 19.3 s cold; 126 s cold on a cloud disk (journals
  [0031](docs/history/0031-remount-time.md), [0055](docs/history/0055-measurement-gaps.md)).
- **Memory.** About 0.5 to 0.6 KiB of kernel memory per file that cannot be paged out, by the
  index's own accounting, which undercounts. The 128 MiB default budget was never reached in a
  test. Query buffers are additional: up to about 1.1 MB is retained per open file that queried
  with the maximum result limit, until close (journal 0139).
- **Writes of the real client** were about 10 % slower in one run, and a page it writes is
  queryable about 11 ms later, not when the write returns: a separate process copies its fields
  into attributes ([journal 0071](docs/history/0071-client-stage2-live-mirror.md)). The client
  itself never called `QUERY2`; a test tool issued the queries.
- **Reflink and dedupe** work through the layer, but the saving is the lower filesystem's and is
  small for small files: 6.7 % on btrfs and 13.6 % on XFS on a set where 15 % of the files are
  copies, none on ext4 ([journal 0085](docs/history/0085-m4-question-3-space.md)).

Still missing: CPU and memory per query, throughput under concurrency, a second client's
latency, and a repeat of the SQLite/client comparisons with confidence intervals. Raw results: [bench/results/](bench/results/README.md).

## Try it in a VM

Quick start. With Docker running, from the repository root. The full run takes 7 to 8 minutes on
a GitHub-hosted x86-64 runner (the CI job "Native Linux VM" runs exactly this command); on Apple
Silicon the VM runs under CPU emulation and takes longer:

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make vm-test                          # builds the tools and modules, boots a disposable QEMU VM
grep '^DEMO| ' build/vm/console.log   # the demonstration above, from your own run
```

For the demonstration alone, which is shorter:

```sh
MOOB_ONLY=demo make vm-test           # ends in MOOBFS_ONLY_PASS, which is not a complete run
grep '^DEMO| ' build/vm/console.log
```

The full run builds the Linux tools and both modules, then boots a dedicated QEMU x86-64 VM using
Debian kernel **6.12.111+deb13-amd64**. No privileged container or host module loading is
required; never load the modules on your host. On Apple Silicon it uses CPU emulation; the
first build downloads kernel packages and takes longer. Environment setup is in
[docs/development.md](docs/development.md); what each marker proves and does not prove is in
[docs/testing.md](docs/testing.md). A successful run ends with:

```text
SHARED: inode=2/3 physical=12288 length=16384
MOUNTED_READ_TESTS_PASS
WRITABLE_MOUNT_REJECTED
MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly, remount, 15 malformed images
LAYER_LOWER_PASS: ext4 reflink=no
LAYER_LOWER_PASS: btrfs reflink=yes
LAYER_LOWER_PASS: xfs reflink=yes
DEMO_PASS: QUERY2 per domain, v1 refused to a user, heterogeneous file left out
MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey
MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; typed attributes and tools; index and QUERY equal to the oracle; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload
```

### The M1 image tools (regression)

The read-only M1 milestone validated that separate files can reference the same immutable
bytes: an offline tool creates the image and a native module serves reads through the VFS.
It demonstrates physical sharing, not an agent memory workflow. The tools need a C11 compiler,
Make and Python 3 on Linux or macOS:

```sh
make test
printf 'A memory shared by two files.\n' > build/memory.txt
build/host/mkfs.moobfs build/demo.img alpha.txt=build/memory.txt beta.txt=build/memory.txt
build/host/fsck.moobfs build/demo.img
```

The JSON report shows two inode numbers referencing the same object and physical offset. The
creator refuses to overwrite an existing image; `fsck.moobfs` checks images and does not
repair them. The VM fixture contains 12,562 logical bytes referenced by each file, stored
once; FIEMAP reports the same 16,384-byte allocation and the shared flag for both inodes.
Metadata and alignment overhead still count toward total image size.

| Component | M1 behavior |
| --- | --- |
| Native module | VFS mounting, flat directory, buffered reads via iomap, FIEMAP |
| Image creator | Deterministic offline creation, exact-byte deduplication, no overwrites |
| Verifier | Structural checks, bounds, CRC32C metadata, SHA-256 payload checks |
| Limits | 128 files/objects, 64 KiB per object, printable ASCII names up to 63 bytes |
| Validation | 9 tool tests, sanitizers for tools, actual QEMU mounts and 15 malformed images |

Images are immutable while mounted. Files have independent inodes and link count 1; this is
not hard linking. The module validates all content at mount, which adds a full scan. Disk
sharing does not guarantee page-cache sharing. The M1 module has not run under KASAN (the
KASAN harness runs the layer suite), and no benchmark exists.

## Limitations

- **The review batch passed the full integrated VM suite and local KASAN/UBSAN/lockdep.**
  `v0.4.1-experimental` contains both defects identified in
  [review 0144](docs/history/0144-independent-review.md): stale backing-file flags after
  `F_SETFL`, and lower `getxattr` errors silently skipped during indexing. R1 synchronizes
  mutable flags before delegated I/O ([0149](docs/history/0149-sync-backing-file-flags.md));
  clearing and setting append both passed on ext4, btrfs and XFS in the integrated run at
  `49098aa`. R2 propagates operational read errors and preserves the existing exclusions for
  missing, oversized and malformed attributes ([0150](docs/history/0150-propagate-index-xattr-read-errors.md));
  the same full VM run passed its injected-error regression and the acceptance/layer suites.
  A third review ([0151](docs/history/0151-review-d110d4e.md)) found that a chmod during the
  first build of a domain could leave the file in `QUERY2` answers to users who can no longer read
  it; fixed in [0152](docs/history/0152-chmod-during-build-fixed.md), with a suite test and a negative round.
  Negative rounds detected both original defects. Injecting the read result does not reproduce
  an actual lower-filesystem I/O failure. The full local sanitizer run at `49098aa`
  reported `XATTRREAD_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS` and
  `MOOBFS_KASAN_PASS` on Linux 6.12.112. Pull-request CI remains a separate pre-merge gate.

- One kernel series tested (Debian 6.12.111, upstream 6.12.112 under KASAN), x86-64, in a VM;
  kernel APIs are version-specific and the pinned test target is not a compatibility promise. On
  another architecture the module ran only in the exploratory arm64 job (emulated, three lower
  filesystems, journal 0137); the user-space encoding tests also ran on big-endian.
- The index is RAM only, non-pageable and bounded by `index_budget` (default 128 MiB). Each
  domain is built by a linear scan on its first query after mount, and again after it was
  released or a directory in it was renamed; the budget and the shrinker were tested on small
  trees only (journal 0101).
- `QUERY2` can retain a result buffer of up to 1,114,112 bytes per open descriptor. This
  code charges that buffer and user-triggered index/query allocations to the allocating
  task's memory cgroup where supported (issue 116, journal 0180). The mount-wide index
  budget is still shared, so this does not provide per-user fair sharing (S9).
- Mounting needs `CAP_SYS_ADMIN` in the initial user namespace; user-namespace and idmapped
  mounts are refused, and so is an idmapped mount as `lower=`; no rootless containers.
- UAPI v1 and v2 may change, with no compatibility promise ([compatibility.md](docs/compatibility.md)); the `-experimental` tags are source snapshots, with no binaries, packaging or support.
- Early SQLite/client figures are pilots: two hosts, one data-set seed, three repetitions at
  most (journals 0025 to 0085). Later module comparisons use 40 repetitions and intervals
  (journals 0139 and 0143). No speed or efficiency claim beyond the conditions stated in
  [What was measured](#what-was-measured) may be made from them; no comparison with FUSE
  exists, and the comparison with the bare lower filesystem on the current module is pending.
- No outside human review. Two independent source reviews by AI assistants (journals
  [0111](docs/history/0111-review-5582e06.md) and [0114](docs/history/0114-second-inspection-of-5582e06.md))
  and the project's own tests found and fixed kernel-side defects, security ones among them; the
  findings are listed in [ADR-0011](docs/decisions/0011-threat-and-credential-model.md) (F1 to F14),
  the earlier defects in journals [0015](docs/history/0015-phase-2e-investigation.md),
  [0016](docs/history/0016-kasan-lockdep-harness.md),
  [0035](docs/history/0035-phase-q2a-domain-query.md),
  [0082](docs/history/0082-index-lock-and-reclaim.md) and
  [0083](docs/history/0083-index-inspection-fixes.md). Tags up to `v0.4.0-experimental` carry
  defects fixed later; fixes after `v0.4.1-experimental` land on `main`. fstests ran as the `quick` group only ([vm/fstests/](vm/fstests/README.md));
  syzkaller ran for five six-hour rounds, above the aggregate PC threshold of
  [security roadmap](docs/security-roadmap.md) S3; `moobl_xattr_get` was at 0 PCs in that run's report, because the corpus kept almost no
  attribute read inside a mounted layer; replayed with per-call coverage, its seeds reach it (journal 0154). SELinux enforcing was exercised once (journal
  0104); AppArmor and Smack were not.
- `QUERY` authorization holds at query time only (TOCTOU contract). A directory rename through
  the layer makes its domain stale and the next query rebuilds it; direct lower-tree changes
  remain untracked ([spec-m3](docs/spec-m3.md) §3.1 and its correction note).
- File/metadata atomicity is a client convention; durability, reflink and dedupe are the lower
  filesystem's. Acceptance ran over ext4 only.
- Inode numbers are the lower's, and a file is identified by that number. A btrfs subvolume or
  snapshot **inside** the lower tree numbers its files from the start again, so the layer does
  not enter it (`EXDEV`) and leaves it out of the index. Keep snapshots outside the directory the
  layer covers.
- Data is written with the mounter's credentials without `CAP_SYS_RESOURCE`, so quota limits hold,
  and with the writer's fsuid and groups, so ext4's reserved blocks go only to whom ext4 grants
  them directly (finding F4, journal 0132).
- `noatime`, `relatime` and `strictatime` on the layer's mount have no effect; access times
  follow the mount options of the lower filesystem.
- `sync_file_range(2)` through the layer succeeds and writes nothing: the pages live in the
  lower file's cache, which it does not reach (V2 of journal 0155; overlayfs has the same gap).
  Use `fsync` or `fdatasync`.
- With domains on, nothing more than 64 components below the mount root is indexed or answered by
  `QUERY2`: the write paths cannot place deeper names (finding F14, journal 0156). The files are
  still read and written normally.

## What is implemented, with evidence

"Tested on" names the kernel and lower filesystems that actually ran; nothing here means
production readiness. Since [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) MoobFS is
built as a native stacked layer over a mature filesystem; the M1 experiment is frozen and runs
as a regression in the same VM.

| Item | Journal | Tested on |
| --- | --- | --- |
| M1 read-only image: offline creator, verifier, native module, two inodes sharing one payload (FIEMAP), 15 malformed images rejected | [0002](docs/history/0002-native-read-milestone.md), [format](docs/format-m1.md) | Debian 6.12.111 (QEMU), loop device |
| Phase 2a–2b: stacked pass-through (`mount -t moobfs_layer -o lower=<dir>[,nolinks]`): files, directories, rename, links, xattr, mmap, fsync; delegated reflink/dedupe/`copy_file_range`, FIEMAP; `fsx` | [0011](docs/history/0011-phase-2a-stacked-passthrough.md), [0012](docs/history/0012-phase-2b-full-passthrough.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase 2c: typed `user.moob.*` attributes validated in the kernel; `moobfs-attr`, `moobfs-check`; host unit tests; libFuzzer target | [0013](docs/history/0013-phase-2c-typed-attributes.md) | Host tests (Linux/macOS), VM over the three lowers |
| Phase 2d: in-memory index, `QUERY` and `GET_INFO` (UAPI v1), answers identical to the `moobfs-index` oracle, also after rename, unlink, removexattr and remount; `noindex`, `index_budget` | [0014](docs/history/0014-phase-2d-index-query.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase 2e: acceptance with git, concurrent WAL-mode SQLite, own memory client; power-loss replay with `dm-log-writes`; I/O errors with `dm-flakey`; two layer bugs found and fixed | [0015](docs/history/0015-phase-2e-investigation.md) | Debian 6.12.111; ext4 only |
| KASAN/UBSAN/lockdep harness running the same suite; a third defect found and fixed | [0016](docs/history/0016-kasan-lockdep-harness.md) | Upstream 6.12.112, virtme-ng |
| M3 phase 3b: per-result POSIX authorization in `QUERY`; two uids, two namespaces, chmod and ACLs, remount, idmapped and user-namespace mounts refused, 5-second race of chmod/rename/unlink against `QUERY` | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 | Debian 6.12.111 and KASAN 6.12.112; ext4, btrfs, XFS |
| Phase Q2a: domain-scoped `QUERY2` (UAPI v2) on a domain directory, index partitioned by domain and a homogeneity rule, v1 restricted to administrators while `domain_depth` is on, `EXDEV` for directories across domains; equal to `moobfs-index --domain` (measured: journals [0043](docs/history/0043-timing-channel-query2.md), [0044](docs/history/0044-q2-measured-equality-plan.md), [0047](docs/history/0047-sqlite-baseline-macmini.md)) | [0035](docs/history/0035-phase-q2a-domain-query.md), [spec-query-v2](docs/spec-query-v2.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase Q2b: `QUERY2` ordered by an attribute, ascending or descending, and paged by an authenticated cursor; equal to `moobfs-index --domain --order`; "N most recent" measured and optimized ([0048](docs/history/0048-recent-optimization.md)) | [0036](docs/history/0036-phase-q2b-order-cursor.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase Q2c: leapfrog intersection of equality terms in `QUERY2`; equal to the oracle, plan checked by result order, measured in [0044](docs/history/0044-q2-measured-equality-plan.md) | [0037](docs/history/0037-phase-q2c-leapfrog.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase Q2d: "missing or ..." terms and batches of up to 8 `QUERY2` requests per call; equal to the oracle | [0038](docs/history/0038-phase-q2d-missing-batch.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Testing under the module: fstests `quick` group with an exclusion list, systematic allocation-failure injection inside the module | [0091](docs/history/0091-s2-fstests-clean-with-exclusions.md), [0096](docs/history/0096-fault-injection.md) | Debian 6.12.111; KASAN 6.12.112; ext4, btrfs, XFS |
| Index built per domain by the first query, released under memory pressure or over its budget (shrinker); tracepoints | [0098](docs/history/0098-index-on-demand.md), [0101](docs/history/0101-domains-released.md), [0102](docs/history/0102-index-tracepoints.md), [ADR-0012](docs/decisions/0012-index-on-demand-and-reclaimable.md) | KASAN 6.12.112; ext4, btrfs, XFS |
| SELinux enforcing; per-result checks when an LSM labels the layer's inodes | [0104](docs/history/0104-selinux-investigation.md) | 6.12.112 with the Debian reference policy |
| SQL over a domain through a SQLite virtual table (prototype) | [0108](docs/history/0108-sqlite-virtual-table-prototype.md), [ADR-0013](docs/decisions/0013-user-space-query-library.md) | KASAN 6.12.112; ext4 |
| syzkaller descriptions of the mount, ioctls and attributes; five six-hour runs | [0109](docs/history/0109-syzkaller-first-runs.md), [0112](docs/history/0112-mount-leak-and-sql-omit.md), [0133](docs/history/0133-syzkaller-third-run.md), [0138](docs/history/0138-syzkaller-fourth-run.md), [0143](docs/history/0143-syzkaller-fifth-run-threshold-met.md) | upstream 6.12.112 with KCOV |
| Security fixes of findings F9 to F12 (homogeneity rule, checks without the query semaphore, write access on the lower mount, idmapped lower refused), each with a test and a negative round | [0116](docs/history/0116-homogeneity-classes-and-checks-without-query-sem.md), [0117](docs/history/0117-lower-mount-write-access.md) | KASAN 6.12.112; ext4, btrfs, XFS |

Not implemented: a persistent index, user-namespace or idmapped mounts, batch reads, a change
cursor, revisions, packaging, a stable UAPI, a CLI, the full xfstests suite, and the complete
M4 method (only pilot-grade campaigns exist, in [bench/results/](bench/results/README.md);
see [What was measured](#what-was-measured)). See [ROADMAP.md](ROADMAP.md) for the planned and validation-dependent items.

## Compared with plain files, SQLite and memory frameworks

**Plain files.** Same data model (files plus xattrs), no module, no root, total portability;
queries are `find` plus `getfattr`, a linear scan with no query call. If attribute queries are
rare, plain files win.

**SQLite next to the files.** Full SQL, composite indexes, decades of maturity, failures
contained in a process, atomic data-and-metadata transactions inside the database; the
application implements authorization and keeps the files and rows consistent. Today this is
the recommendation for anything outside a lab.

**Memory frameworks (mem0, Letta, ai-memory).** They decide what to remember and how to build
a prompt; MoobFS stores files and answers attribute predicates. They are potential clients on
top of the layer, not competitors; the M4 plan uses them as workloads.

**MoobFS.** One store, per-result authorization by the OS, no daemon, data readable without
the module; in exchange, a kernel module from one author, one tested kernel, a RAM-only index
and a measured
advantage that is narrow and pilot-grade ([What was measured](#what-was-measured)). Full comparison table and the conditions under which it may be
worth evaluating: [when-not-to-use.md](docs/when-not-to-use.md).

## Documentation map

Every document under `docs/`, with its language, is listed in the [docs index](docs/README.md).

| Question | Document | Language |
| --- | --- | --- |
| How is the layer built, with source citations? | [architecture-overview.md](docs/architecture-overview.md) | EN |
| Should I use it, compared with files, SQLite, FUSE and memory frameworks? | [when-not-to-use.md](docs/when-not-to-use.md), [value-assessment.md](docs/value-assessment.md) | EN, PT |
| Short answers | [faq.md](docs/faq.md) | EN |
| How do I build, test and read the logs? | [development.md](docs/development.md), [testing.md](docs/testing.md) | EN |
| How do I use the command-line tools? | [tools.md](docs/tools.md), manual pages in [man/](man/) (`man ./man/moobfs-attr.1`) | EN |
| What does the M2 layer specify? | [spec-m2.md](docs/spec-m2.md), [kernel-infrastructure.md](docs/kernel-infrastructure.md), [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) | EN |
| What does M3 specify (identity, isolation, provenance, change cursor, batch reads)? | [spec-m3.md](docs/spec-m3.md) | EN |
| How will performance be measured? | [benchmark-plan.md](docs/benchmark-plan.md), [benchmark-harness.md](docs/benchmark-harness.md) | PT (English translation in review) |
| What does a memory system need to use the layer, and what does it risk? | [client-contract.md](docs/client-contract.md), [adoption-risks.md](docs/adoption-risks.md) | EN |
| What has been measured so far (pilot-grade)? | [M4 report](docs/history/0088-m4-report.md); [bench/results/](bench/results/README.md); raw archives in the [`bench-evidence`](https://github.com/studiomoob/llmoobfs/releases/tag/bench-evidence) release | EN |
| What does the domain-scoped `QUERY2` (UAPI v2) specify? | [spec-query-v2.md](docs/spec-query-v2.md) | EN |
| What is the M1 image format? | [format-m1.md](docs/format-m1.md) | EN |
| What was the original full MVP proposal? | [native-mvp.md](docs/native-mvp.md) (write interface superseded by ADR-0007) | EN |
| Why was each decision taken? | [docs/decisions/](docs/decisions/), [technical journal](docs/history/) | EN |
| Which free software is relevant around the project? | [ecosystem.md](docs/ecosystem.md) | EN |
| How is AI-assisted development governed? | [ai-workflow.md](docs/ai-workflow.md) (shared through `AGENTS.md`, `CLAUDE.md` and Cursor rules) | EN |
| Prior art, what may and may not be claimed | [research-originality.md](docs/research-originality.md) | EN |
| Which kernels are supported, and what may change between versions? | [compatibility.md](docs/compatibility.md) | EN |
| How can I help? | [help-wanted.md](docs/help-wanted.md) | EN |
| Changes, governance, conduct, security | [CHANGELOG.md](CHANGELOG.md), [GOVERNANCE.md](GOVERNANCE.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), [SECURITY.md](SECURITY.md) | EN |

Git records exact changes; journals record concise rationale, experiments and results. Future
work must distinguish proposals from implemented and tested behavior.

## Prior work and positioning

MoobFS builds on work it acknowledges: the Semantic File System (1991) and the Be File System
(1997) for attribute queries, overlayfs and ecryptfs for the stacked-filesystem model, and
YoloFS (2026), which already demonstrates a stacked kernel module aimed at agents. The
domain-scoped `QUERY2` follows research on access-controlled search: partitioning the index by
reader set (Singh et al. 2007; GUFI, SC22) closes the leaks that filtering after the search leaves
open (Büttcher and Clarke, FAST 2005; Wang et al., S&P 2017). The project claims none of these ideas
as new; what it explores is their combination in a stacked kernel layer, and whether that has
measurable value ([research-originality.md](docs/research-originality.md) §4, §5, §9). Every work,
project and tool used is listed with verified sources in [references.md](docs/references.md).

Thank you to everyone whose published work this project stands on: the authors of those papers and
the maintainers of the kernel, of the filesystems and of the test tools named there.

## Contributing and security

Bug reports, format review, kernel review and reproducibility improvements are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) (DCO sign-off, testing requirements, review rules).
Questions go to GitHub Discussions or an issue. AI-assisted contributions are welcome with
human review and accurate test evidence. Do not put private vulnerability details in public
issues; use the [security contact](SECURITY.md).

## External project records

Records checked on 2026-10-20. These links describe release tracking and source discovery;
see [Status](#status) for tested behavior and limitations.

| Service | Record |
| --- | --- |
| [Anitya](https://release-monitoring.org/project/392843/) | Tracks the experimental pre-releases; no distribution package mapping. |
| [Open Hub](https://openhub.net/p/moobfs) | Public project entry; code analysis is not complete. |
| [XERJ Corpus Hub](https://hub.xerj.org/corpus/studiomoob--llmoobfs) | Source snapshot selected; license review is pending, so the corpus is not installable yet. |
| [TeleAI — Awesome Agent Memory](https://github.com/TeleAI-UAGI/Awesome-Agent-Memory) | Listed under **Emerging projects** as an experimental agent-memory storage project. |

## Citing

To cite the archived experimental source snapshot at commit
[`02381fba`](https://github.com/studiomoob/llmoobfs/commit/02381fba3f0a908623cac52180a39f24e02e98a6), use:

> Studio Moob. (2026). *MoobFS: experimental Linux filesystem layer for LLM-agent memory — source snapshot 02381fba*
> (Version `0.4.1-experimental+git.02381fba`) [Computer software]. Zenodo.
> [https://doi.org/10.5281/zenodo.23514721](https://doi.org/10.5281/zenodo.23514721)

This archive is newer than the `v0.4.1-experimental` tag. The DOI identifies that
fixed snapshot; `main` may contain later changes. The tag's citation metadata
remains in [CITATION.cff](CITATION.cff).

## License

The entire project is licensed under **GPL-2.0-only**. See [LICENSE](LICENSE).
