# Technical audit of the originality and prior art of MoobFS

Research date: 2026-10-10. Repository branch: `m3/3b-query-authz`. This document is a
technical analysis, not a legal opinion (see section 8). Web sources were treated as data.
Third-party repositories were cloned (`--depth 1`) and read on 2026-10-10; nothing was compiled
or executed. Star counts and dates come from the GitHub API on the same date.

Classification used in every conclusion:

- **proven**: verified by me in the code or in the text of the cited source, or by reproducible
  evidence from the MoobFS repository itself.
- **probable**: consistent with what I read, but depends on partial reading, inference or on
  execution I did not perform.
- **not verified**: not confirmed; it may be true or false.

## 0. State of MoobFS used as reference (proven, repository files)

| Claim | Class | Evidence |
| --- | --- | --- |
| Native kernel module (not FUSE), stacked over ext4, btrfs and XFS | proven | `layer/main.c` (`register_filesystem`, `MODULE_LICENSE("GPL")`); `docs/decisions/0007-stacked-memory-layer.md`; `docs/history/0011` and `0012` |
| I/O delegated to the lower via `fs/backing-file.c` (`backing_file_open`, `backing_file_read_iter`, `backing_file_write_iter`, `backing_file_mmap`, `backing_file_splice_*`) | proven | `layer/main.c` lines ~804-876 |
| Full POSIX passthrough validated with `fsx`, git, concurrent SQLite WAL, `dm-log-writes` replay, `dm-flakey` | proven (local VM, Debian kernel 6.12.111; run by the author) | `docs/history/0012`, `0015`; `README.md` |
| Typed attributes `user.moob.*` validated in the kernel on write | proven | `moobl_typed_check` in `layer/main.c`; `docs/history/0013` |
| In-memory attribute index, rebuilt at mount | proven | `moobl_index_build` in `layer/index.c`; `docs/spec-m2.md`; `docs/history/0014` |
| `QUERY` and `GET_INFO` (UAPI v1) equal to a user-space oracle | proven | `include/moobfs_ioctl.h`; `tools/moobfs-index`; `docs/history/0014` |
| Reflink and dedup delegated to the lower, with no implementation of its own | proven | `docs/decisions/0007` item 7; `layer/main.c` (`vfs_clone_file_range`, `vfs_dedupe_file_range_one`); `docs/history/0012` |
| Phase 3b (in progress): `QUERY` resolves each path with the caller's credentials | proven as implementation and as isolation test on ext4/btrfs/XFS; phase not closed (ACL, idmap/userns and identity passed in the VM on 2026-10-10; KASAN over the branch and the merge via PR remain) | `caller_may_read` and `do_query` in `layer/index.c`; `docs/history/0020` |
| Identity `(namespace, memory_id)` is a client convention; the kernel neither generates nor interprets ids | proven (decision); checker in progress | `docs/history/0020`, `docs/spec-m3.md` §2 |
| No semantic search in the kernel | proven | `docs/decisions/0005` item 5; README |
| No benchmark was measured; M4 is a plan | proven | `docs/benchmark-plan.md` ("No number here was measured"); `ROADMAP.md` |
| KASAN/UBSAN/lockdep workflow exists | proven (harness and one defect found) | `docs/history/0016`, `vm/kasan.config` |
| Two real defects found and fixed by the acceptance tests (reuse of a negative dentry with data loss in SQLite's WAL; concurrent `set_nlink` warning) | proven | `docs/history/0015` |
| Additional defect (uninitialized rwsem on a failed mount) found by the KASAN harness | proven | `docs/history/0016` |
| `moobl_permission` delegates to the lower's `inode_permission`; POSIX ACLs delegated after a fix | proven (code and ACL tests in the VM, `docs/history/0020`) | `docs/history/0020` (third-round finding), commit `d0c8911` |

Declared limits of the project itself: no full xfstests; index only in RAM with a budget
(128 MiB default); mount requires privilege (no `FS_USERNS_MOUNT`); no `FS_ALLOW_IDMAP`;
content+attributes atomicity is that of the lower.

## 1. Prior-art matrix

| Work | Year | Layer | Link | What it anticipates | What it does not do |
| --- | --- | --- | --- | --- | --- |
| Semantic File System (Gifford, Jouvelot, Sheldon, O'Toole, MIT) | 1991 | User-space NFS server over a UNIX FS; B-tree index in files | https://pages.cs.wisc.edu/~remzi/Classes/838/Fall2001/Papers/sfs-sosp91.pdf | Field-value attributes extracted automatically; query as a "virtual directory"; index maintained incrementally; associative access to an existing FS | Not a kernel module; indexes only publicly readable files (§4 of the paper), permission filtering left as future work; attributes come from transducers, not from typed xattrs; no ioctl |
| Be File System (Giampaolo, Meurillon) / Giampaolo 1999 | 1997 | Native kernel FS (BeOS); reimplemented in Haiku (OpenBFS) | https://en.wikipedia.org/wiki/Be_File_System ; https://github.com/haiku/haiku | Typed per-file attributes, per-attribute B+ tree indexes, equality/range queries, live queries, journaling | Its own format, not stacked; BeOS/Haiku, not Linux; in the Haiku code read (`QueryParser.h`) there is a TODO about filtering by user permission |
| Linux `befs` driver | 2001- | Linux kernel, its own FS (read-only) | https://docs.kernel.org/filesystems/befs.html | Reading BFS volumes on Linux | Read-only; the documentation mentions neither attributes nor indexes |
| Stacking precedents: overlayfs, ecryptfs | around 2006 and 2014 (dates from memory, not verified) | Kernel, stacked in-tree | https://docs.kernel.org/filesystems/overlayfs.html | Pattern of an FS stacked over a mature lower, mounter's credentials, rule of not changing the lower from outside | Do not index typed attributes nor query |
| TagFS (Bloehdorn et al.) / TMSU / Tagsistant | 2006 / 2011- / 2009- | WebDAV and later FUSE; RDF/SQLite databases | https://www.bibsonomy.org/bibtex/ab2f49f3779bb2ff505135917eae8975 ; https://tmsu.org/ | Tags and queries as paths; metadata outside the file | User space (FUSE or service); its own database, not xattrs on the lower |
| LiFS (Ames et al.) | 2005-2006 | User-space prototype | https://www.crss.ucsc.edu/pub/ames-msst06.html | Attributes and links assigned as FS metadata | Prototype, not Linux kernel (search result; I did not read the whole paper) |
| US7617250B2 (HP) | 2003 (granted 2009) | Patent | https://patents.google.com/patent/US7617250B2/en | Object-relation-object tuple model for semantic information in an FS | See §8 |
| US20230401171A1 (Dell) | 2022 | Patent application | https://patents.google.com/patent/US20230401171A1/en | Virtual FS generated from the metadata of a query | See §8 |
| YoloFS (Zhong et al., UW-Madison) | 2026 | C kernel module stacked over a local FS + Rust CLI | https://yolofs.github.io/ ; https://github.com/YoloFS/YoloFS | Stacked kernel module that "stacks on any local FS", ioctl at the mount root, no FUSE, staging, snapshots, per-path `allow/ask/deny` permissions | No typed attributes, no index, no query; policy by path rule, not by POSIX credentials |
| SMFS (Supermemory) | 2026 | Rust client: FUSE (Linux) / local NFSv3 (macOS) + SQLite + cloud | https://smfs.ai/research/memory-as-a-filesystem/ ; https://github.com/supermemoryai/smfs | Memory as a mounted directory; semantic `grep`; generated profile; SQLite cache | User space and cloud service; not a kernel module |
| VexFS (lspecian) | 2025 | README: three layers (FUSE + API + dashboard); `kernel_module/` in C with its own block FS | https://github.com/lspecian/vexfs | Vector search in the FS (intended) | Alpha; module with 3.4 thousand lines, `vector_ops.c` of 241 lines, README warns of panics; search "future" |
| Factory VFS | 2026 | Rust; virtual FS in SQLite mountable via FUSE or NFS | https://github.com/Factory-AI/vfs | Copy-on-write sandbox, audit of tool calls, portable sealed session | User space; no attribute index |
| Agent FS (desplega.ai) | 2026 | TypeScript/Bun, HTTP/MCP server, SQLite + S3, FUSE helper in Rust | https://github.com/desplega-ai/agent-fs | Semantic search, SQL via DuckDB, versioning, identity, per-drive roles (viewer/editor/admin) | Service and user space |
| RAGFS (Venere-Labs) | 2026 | Rust; FUSE + LanceDB + local embeddings | https://github.com/Venere-Labs/ragfs | Structured operations with JSON, soft-delete/undo, audit, local semantic search | FUSE; no permissions by its own credentials |
| "Filesystem-Based Memory for LLM Agents" (Zhou et al.) | 2026 | Empirical study; markdown tree | https://arxiv.org/abs/2607.26637 | Agent memory as a directory of files manipulated with generic tools | Does not propose an FS nor a kernel module |

## 2. Technical comparison by feature

Legend of the MoobFS columns: state and evidence file. Third-party columns: what I read
in the code/text (class in parentheses when uncertain).

| Feature | MoobFS | Closest works |
| --- | --- | --- |
| Native kernel vs FUSE | **proven**: kernel, no FUSE (`layer/main.c`, ADR-0001/0007) | BFS/befs: kernel (proven, kernel docs). YoloFS: C kernel (proven, `kmod/super.c`). SFS: NFS in user space. VexFS: FUSE/API on the functional path, alpha module. SMFS, Factory VFS, RAGFS, Agent FS, TagFS: FUSE/NFS/service |
| Stacked layer vs own FS | **proven**: stacked over ext4/btrfs/XFS (`layer/main.c`, README) | YoloFS: stacked (proven, `docs/architecture.md`). overlayfs/ecryptfs: stacked. BFS, befs, VexFS: own FS. SFS: stores the index in an ordinary UNIX FS, intermediate server |
| POSIX compatibility | **proven** in a VM (fsx, git, SQLite WAL, dm-log-writes) (`docs/history/0012`, `0015`) | YoloFS: the repository's e2e tests (not verified in depth). VexFS: README lists symlinks, hard links, full xattr, flock, mmap, ACL as pending (proven in `FUSE_STATUS.md`). BFS: partial POSIX by design (not verified) |
| Typed attributes | **proven**: `user.moob.*` with string/int64/double/bool/time/raw types validated in the kernel (`docs/history/0013`) | BFS: native typed attributes (proven, Wikipedia/Haiku). SFS: string/integer field-value from a transducer. Others: metadata in a database or front matter, not typed xattrs |
| Index in the kernel | **proven**: in-RAM index by (name, type, value, inode), rebuild at mount (`layer/index.c`) | BFS/OpenBFS: persistent B+ tree index in the kernel (proven, Haiku `bfs/Query.cpp`, Index). SFS: index in user space. Others: in-process SQLite/LanceDB |
| Structured queries | **proven**: up to 4 conjunctive terms, EQ/LT/LE/GT/GE/PREFIX/EXISTS via ioctl, verified against an oracle (`include/moobfs_ioctl.h`, `docs/history/0014`) | BFS: equality/range/logical and live queries (partly proven: `B_LIVE_QUERY` flags in the Haiku header). SFS: virtual directory paths. Agent FS: SQL via DuckDB (README). TagFS: path as a tag query |
| Isolation by POSIX permissions | **proven** as implementation and two-uid test on 3 lowers; **phase in progress** (`layer/index.c` `caller_may_read`; `docs/history/0020`) | SFS: indexes only public files (paper §4), filtering by requester "under investigation". Haiku `QueryParser.h`: TODO "check user permissions here" (proven, reading of 2026-10-10). YoloFS: permissions by path rules `allow/ask/deny`, not by uid/gid (proven in the documentation). Agent FS: per-drive roles in a service (`packages/core/src/identity/drives.ts`). Whether BFS/Haiku filters at another point of the VFS: **not verified** |
| Persistent memory across sessions | **proven** as a property of the storage (files on the lower; survives a crash: `docs/history/0015`) | All FSs persist data; YoloFS persists a session artifact in `.yolofs/`; Factory VFS seals sessions in a file; SMFS syncs with the cloud |
| Provenance and identity | **probable/partial**: identity `(namespace, memory_id)` is a client convention; provenance through client attributes; `moobfs-check` checker in progress (`docs/history/0020`). The kernel neither guarantees nor generates it | Factory VFS: audit of tool calls (proven, `docs/SPEC.md`). Agent FS: agent identity and versioning (README). YoloFS: change journal. ai-memory: `supersedes` in SQLite (`docs/research-ai-memory.md`) |
| Physical sharing | **proven, but delegated**: btrfs/XFS reflink/dedup (`docs/history/0012`); own M1 (shared extents) frozen | btrfs/XFS/overlayfs do this without MoobFS; Factory VFS and Agent FS: I did not verify dedup |
| Index rebuild | **proven**: at every mount; PATHS_STALE/INDEX_INCOMPLETE in `GET_INFO` (`layer/index.c`, `moobfs_ioctl.h`) | SFS: periodic and full reindexing (paper §5, 1 million attributes in 1h36 in 1991). ai-memory: rebuildable derived SQLite (`docs/research-ai-memory.md`). BFS: persistent index, manual `reindex` for existing files (Haiku docs) |
| Storage/semantics separation | **proven**: kernel only structured attributes; embeddings in the client (ADR-0005 item 5) | SFS: transducers (semantics per file type) outside the FS. SMFS, RAGFS, Agent FS, VexFS: semantics/embeddings built into the product |
| Reliability evidence | **proven** (author, VM, kernel 6.12.111): fsx 9000 ops per lower, dm-log-writes, dm-flakey, 2+1 defects found, KASAN/lockdep harness (`docs/history/0012`, `0015`, `0016`). No full xfstests | YoloFS: CI and `tests/` suite (existence proven; coverage not verified). VexFS: README admits risk of panics (proven). Others: application tests |
| Reproducible benchmarks | **not verified / nonexistent**: no number measured; plan in `docs/benchmark-plan.md` and `docs/benchmark-harness.md` | SMFS publishes xAFS (110 questions, 13 corpora) and reports fewer tokens; evaluation done by the authors themselves, without independent reproduction (report page). YoloFS: SOSP 2026 paper with evaluation (I did not read it; note of 2026-10-28: read, see §10). ai-memory: LongMemEval (`docs/research-ai-memory.md`) |

## 3. Analysis of the three architecturally closest competitors

Criterion: architectural overlap verified in code or primary source, not marketing.
VexFS and RAGFS were left out: VexFS promises vector search in the kernel but the code read is its own
block FS in alpha, with "future" search (`kernel_module/README.md`); RAGFS is FUSE with
LanceDB.

### 3.1 Be File System (BFS) and OpenBFS/Haiku

- What it is: the BeOS FS (1997), reimplemented in C++ as OpenBFS in Haiku (MIT license according to
  `docs/decisions/0005`; **I did not reverify the license today**). Haiku has 2 360 stars and
  a commit from 2026-10-10 (GitHub API).
- Features proven in the code/documentation: typed attributes, per-attribute indexes, queries
  with flags `B_LIVE_QUERY` and `B_QUERY_NON_INDEXED` (`headers/private/fs_shell/fssh_fs_query.h`),
  `Query::LiveUpdate` in `bfs/Query.cpp`, shared query engine `QueryParser.h`.
- Relation to MoobFS: it is the explicit conceptual reference (ADR-0005). BFS integrates attribute,
  index and query in the on-disk format; MoobFS reproduces a part (typed attribute, in-RAM
  index, conjunctive query) over someone else's FS. MoobFS does **not** implement live queries,
  a persistent index nor an on-disk B+ tree.
- Relevant BFS limitation: `QueryParser.h` contains the comment "TODO: check user permissions
  here - but which one?!" (reading of master on 2026-10-10). This is evidence about the shared
  code; **not verified** whether another point of Haiku's VFS filters the results.
- Linux `befs`: read-only, alpha (2001 documentation); it does not implement what matters.
  Class: proven (kernel docs).

### 3.2 Semantic File System (SFS, 1991)

- What it is: user-space NFS server, with per-file-type transducers that extract
  field-value pairs, an index (B-tree package + `refer`), virtual directories interpreted as
  queries (`/sfs/exports:/lookup_fault/ext:/c`). The kernel stores only the shared index;
  the authors say they chose user-space NFS "to simplify development".
- Evidence read in the PDF (pages 1-6): it indexes only publicly readable files and lists as
  investigation "limiting results to entities the requester can read"; full indexing
  of 1 042 832 attributes in 1h36 (Table 2), incremental reindexing every two minutes.
- Relation: SFS is direct prior art for "extracted attributes + index + query over an existing
  FS, compatible with unmodified clients" and for the *question* of permissions in the
  results. MoobFS differs by being in the kernel, using typed xattrs written by the client (no
  transducers) and answering via ioctl with a per-credential check; the difference is one of mechanism,
  not of idea.

### 3.3 YoloFS (SOSP 2026)

- What it is: kernel module (C, `kmod/`, about 4.1 thousand lines) stacked over ext4/XFS/btrfs,
  with CLI and tests in Rust (`user/`, `tests/`). GPL-2.0. 13 stars, last commit 2026-09-30,
  created on 2026-02-12.
- Proven features: staging and commit/abort, snapshots per command, time travel, per-path
  `allow/ask/deny` rules, ioctls at the mount root (`YOLO_IOC_*`), restore by journal
  (`docs/architecture.md`, `kmod/ioctl.c`, `kmod/journal.c`). Passthrough to the lower via
  `vfs_*()` (for example `vfs_truncate`, `vfs_mmap`), not via `fs/backing-file.c`.
- I did not find in `kmod/`: typed attributes, index, query (only passthrough `listxattr`
  in `kmod/inode.c`). Class: proven for the cloned commit (`8848c24`).
- Relation: it is the closest prior art to the *shape* of MoobFS (stacked kernel module,
  ioctl at the root, no FUSE, for agents). It differs in what the layer offers: control and
  reversal of effects versus queryable memory. MoobFS must not claim anything about
  "agent-native filesystem in the kernel" as a new category.

Other stacking precedents: overlayfs and ecryptfs (kernel docs); the project's ADR-0007 already
cites them as a model. Proven by the kernel docs: overlayfs keeps the credentials of whoever mounted
and requires not modifying the lower from outside.

## 4. What MoobFS cannot claim as novel

Each item below has prior art identified in this research.

1. **Filesystem queryable by attributes** (SFS 1991, BFS 1997). proven.
2. **Typed per-file attributes with per-attribute index and equality/range query**
   (BFS). proven.
3. **Query served by the FS itself without FUSE**: BFS/OpenBFS in the kernel. proven.
4. **Kernel module stacked over a mature FS, without FUSE, with ioctl at the mount root, for
   AI agents** (YoloFS, 2026). proven.
5. **Stacking pattern** (overlayfs, ecryptfs) and use of `fs/backing-file.c` (overlayfs,
   FUSE passthrough, according to ADR-0007). proven.
6. **Agent memory as files in a directory** (arXiv 2607.26637; SMFS; ai-memory;
   Letta). proven in the sources read.
7. **Mounting memory as a directory with semantic search** (SMFS, RAGFS, Agent FS, VexFS
   intended). proven.
8. **Reflink, dedup, snapshots, journaling, ACLs**: they belong to the lower (btrfs, XFS, ext4). proven.
9. **The question of permissions over query results**: formulated in 1991 (SFS) and noted as
   pending in the Haiku code. The question is not new; what MoobFS claims is a specific
   implementation. proven for the prior art of the *question*.
10. **Isolation between agents by access control** as a topic: YoloFS (by path
    rule), Agent FS (by drive role). proven.
11. **Memory identity/provenance** as a concept: ai-memory, Factory VFS, Agent FS.
    proven in the sources read.
12. **Patent ideas described in §8**: it is not possible to claim novelty over them without
    specialist assessment. not verified.

## 5. Potentially differentiating features

Differentiating is not being novel: these are combinations or implementation choices that **I did not find**
in the works listed, within the limits of this search. Absence of a finding does not prove absence of
prior art (search limited to 10 indicated works and a few open queries).

| Candidate | Class | Basis and caveats |
| --- | --- | --- |
| Typed attribute index (real xattrs on the lower) in the kernel + `QUERY` via ioctl, **over mature FSs without changing them**, with data readable without the layer | **probable** | MoobFS code: `layer/index.c`, ADR-0007 item 3. In the repositories read (YoloFS, VexFS, SMFS, Factory VFS, Agent FS, RAGFS) I found no equivalent (YoloFS: only `listxattr`). BFS has the feature, but in its own FS. A web search on 2026-10-10 returned no other project. |
| Stacked (not its own FS), with type validation in the kernel and index rebuild at mount | **probable** | Combination: YoloFS stacks without an index; BFS indexes without stacking. It is not a new individual feature. |
| `QUERY` results filtered by the caller's POSIX credentials via path resolution and `inode_permission` (includes ACL by delegation to the lower) | **proven** (implementation + local two-uid test on 3 lowers, `docs/history/0020`), **phase open** (ACL and idmap/userns tested in the VM; KASAN and merge pending) | Prior art of the question: SFS and Haiku's TODO. In the other repositories read I did not find filtering by uid/gid. Haiku: other points of the VFS not audited (**not verified**). Known limitation: false negative after a directory rename; documented TOCTOU contract. |
| "Nothing locked in" property: unmount and read with `grep`, `getfattr`, `git`, `rsync` | **proven** as design and for git/fsx on the layer (`docs/decisions/0007` item 3; `docs/history/0015`); the specific direct reading of the lower after unmount **probable** | It also holds for any stacked FS whose data stays on the lower (ecryptfs encrypts, so it does not hold). It is not exclusive. |
| Acceptance evidence with real unmodified clients (git, concurrent SQLite WAL, crash replay, `dm-flakey`) and two kernel defects found through it | **proven** (run by the author) | `docs/history/0015`; there is no independent reproduction; small SQLite workload (200 inserts, `0015` Limitations). |
| Explicit separation: kernel only structure; semantics in the client | **proven** as a decision | Project choice, also adopted by SFS (transducers outside). It is not a novelty. |

## 6. Additional evidence needed

Align with `docs/benchmark-plan.md` (M4) and `docs/benchmark-harness.md`. None of this was
executed.

1. **W3 vs SQLite baseline** (A and B of the plan): p50/p95/p99 latency of `QUERY` against an equivalent
   composite index in SQLite on the same FS, with an `EXPLAIN QUERY PLAN` gate. Only then
   can one talk about cost or benefit.
2. **Cost of the permission filter**: `QUERY` with and without `caller_may_read` for ratios of
   private candidates from 1:1 to 100:1; effect of the batch of 256 and of a deep directory tree.
3. **Index saturation** (`GET_INFO`: 50%..100% of the budget) and mount time in S1..S6.
4. **xfstests** (generic + the stacked set as in `./check --fs overlay`) and **KASAN/lockdep**
   on the 3b branch, with ACL, idmap/userns and `chmod`/`rename`/`unlink` concurrent with queries.
5. **Fuzzing of the ioctl and of `setxattr`** (syzkaller), according to `docs/security-roadmap.md`.
6. **Broadened prior-art search**: read the YoloFS paper (arXiv 2604.13536) and the Haiku code
   of `vfs.cpp` (`query_read`) to close the permission question in BFS; read LiFS in full;
   search kernel lists (LKML) and patent databases for "xattr index query filesystem".
7. **Independent reproduction** of the acceptance tests (another host, another LTS kernel) and
   recording of the environment (`environment.txt`).
8. **Real client**: ai-memory/Letta/mem0 in A, B and C of the plan, for any claim of
   usefulness for agents.

## 7. Risks of exaggerated claims

Phrases to avoid, with reason:

- "The first memory filesystem for LLMs": SMFS, Agent FS, RAGFS, Factory VFS, VexFS and YoloFS exist.
- "The only kernel filesystem for agents": YoloFS is a kernel module for agents.
- "We invented attribute queries in a filesystem": SFS (1991) and BFS (1997).
- "Faster than SQLite/FUSE/ext4" or any performance number: nothing was measured.
- "No overhead" or "zero-copy": not measured; it uses `backing_file`, but the cost is not quantified.
- "Secure" or "proven isolation between agents": phase 3b is in progress; there are documented
  limitations (TOCTOU, false negative after a directory rename) and there was no external audit.
- "Semantic search in the kernel": it does not exist; `QUERY` is a conjunction of predicates over attributes.
- "Long-term memory for LLMs solved in the filesystem": it is storage and query; context selection and assembly belong to the client.
- "MoobFS reflink/dedup": they belong to btrfs/XFS.
- "Broadly tested on Linux": one kernel (6.12.111, Debian) in a VM, three lowers.
- "Validated by xfstests" or "power-loss proof": full xfstests was not run; replay covers directed scenarios.
- "Patent-free" or "does not infringe": there is no FTO analysis (see section 8).
- "Replaces git/SQLite/ai-memory": the layer complements them and runs underneath them.
- "Production-ready" or "stable": it is experimental, without a release; UAPI v1 may change.

## 8. Patents

**No legal opinion on validity, infringement or freedom to operate (FTO) is given here.
Any decision requires assessment by an intellectual property specialist.** What follows is
only a technical description of what the Google Patents pages report on 2026-10-10.

### US7617250B2: "Semantic file system" (HP)

- Inventors: Xu, Karlsson, Tang, Karamanolis. Filing/priority 2003-09-22; grant
  2009-11-10. Google Patents indicates an expired status due to non-payment of fees, with an
  expiration date listed as 2026-11-06 (not verified in an official database).
- What it describes, technically: a data model in tuples (identifier of object 1, relation,
  identifier of object 2) to record semantic relations between files and derived data
  (content, properties, versions, dependencies, access control). Independent: claim
  1 (tuple data structure), 17 (method that triggers an action when the object is accessed and a
  condition stored in the tuple is satisfied), 32 (file system with tuple storage
  and handling of access events). Summary obtained by automated reading of the page; the
  exact claims were not compared word by word.
- Superficial technical relation to MoobFS: both associate structured metadata with files;
  MoobFS uses no relation tuples and does not trigger actions on access (proven in the code read).

### US20230401171A1: "Dynamic filesystem generation based on content metadata" (Dell)

- Inventors: Christner, Lincourt, Bruno. Filing 2022-06-14; publication 2023-12-14; the page
  indicates an abandoned application (discontinuation on 2026-01-06).
- What it describes: an administrator in a computer network receives a content query,
  extracts metadata, locates matching content in a repository, packages it as an ad hoc
  virtual filesystem and exposes it to a computer of the group, with permissions and expiration. Dependent claims
  cite a central repository and interaction with a filesystem driver.
- Superficial technical relation: query by metadata and a virtual view; MoobFS returns
  paths via ioctl and does not generate virtual filesystems.

Why this concludes nothing: I did not analyze families, citations, continuations, claims in
other jurisdictions, nor other patents. Status on third-party pages may be outdated.

## 9. Proposed public technical positioning

### English

MoobFS is an experimental GPL-2.0 Linux kernel module stacked over ext4, btrfs or XFS. It passes
POSIX operations through to the lower filesystem and adds typed `user.moob.*` attributes
validated in the kernel, an in-memory index rebuilt at mount, and structured queries through an
ioctl, checked against a user-space oracle. It builds on prior work we acknowledge: the Semantic
File System (1991) and the Be File System (1997) for attribute queries, overlayfs and ecryptfs
for the stacked-filesystem model, and YoloFS (2026), which already demonstrates a stacked
kernel module aimed at agents. In progress, phase 3b makes `QUERY` honor the caller's POSIX
permissions by resolving each path with the caller's credentials, with local isolation tests on
three lower filesystems. Data remains ordinary files and xattrs on the lower filesystem; reflink
and dedup belong to btrfs/XFS. Acceptance in a local VM covers git, WAL-mode SQLite, power-loss
replay and error injection, which exposed and fixed real defects; there are no measured
benchmarks, no full xfstests run and no independent audit yet.

## 10. Update of 2026-10-27 and 2026-10-28 (journal 0220)

A second search, in three parts: agent filesystems (kernel, FUSE, services), kernel-side
indexing or query of xattrs and metadata, and academic work of 2023 to 2026. Repositories were
cloned with `--depth 1` and read, never built or run. Classes as in the rest of this document.
lore.kernel.org refused automated reads, Semantic Scholar answered HTTP 429 and DBLP gave no
usable answer, so mailing lists and some venues were covered only through search engines.

**The works of §1 and §3, again.** None added typed attributes, an index, structured queries or
filtering by the caller's credentials. YoloFS, SMFS, Factory VFS and VexFS had no new commits
after 2026-10-10 (proven, GitHub API). RAGFS moved to `gianlucamazza/ragfs`, and its
`docs/adr/012-native-filesystem-vs-semantic-volume.md`, accepted on 2026-10-20, rejects a kernel
driver (proven). The YoloFS paper (arXiv 2604.13536v5) was read: no xattr feature, no search
index, no query; its access policy is a per-path rule (`ask`, `allow`, `deny` and two more)
that never grants more than DAC, and its related work cites neither SFS, BFS nor metadata
search (proven, except the part of its reference list that could not be fetched).

**New entries.**

| Work | Layer | Closest to MoobFS in | What it does not do | Class |
| --- | --- | --- | --- | --- |
| KiwiFS (github.com/kiwifs/kiwifs, BSL-1.1, 2026) | Go server; REST, MCP, NFS, WebDAV, FUSE | Schema-checked metadata, an index rebuilt from the files, `WHERE`/`SORT`/`GROUP BY` queries | Metadata is markdown front matter, not xattrs; access by tokens and spaces, not POSIX; not in the kernel | proven (README, `internal/dataview/autoindex.go`, `internal/fuse/fuse.go`) |
| OpenViking (github.com/volcengine/OpenViking, AGPL-3.0, 2026) | Python/Rust service with a vector index | Retrieval filtered by the caller's identity, with ACL fields matched inside the query | Principals of the application, not POSIX; ACL off by default and only eventually consistent with the index | proven for its documentation (`docs/en/concepts/15-acl.md`); code not read |
| LSFS, "From Commands to Prompts" (ICLR 2025), with AIOS | Python in user space | A "semantic file system" layer for agents with keyword retrieval under AND/OR conditions | Untyped metadata in a vector store; no multi-user or credential filtering | proven (paper) |
| Turso AgentFS, LayerFS, ContextFS (Tsinghua), Redis Agent Filesystem | FUSE or NFS over SQLite, Redis or a content store | Filesystems for agents: snapshots, branches, audit | No attribute index or query; in the three whose code was read, xattr calls are not implemented | proven for the code read |
| macOS `searchfs()` (HFS+, also offered on APFS) | Kernel, own filesystem | The only user-callable kernel metadata query found that checks the caller on every result (`vnode_authorize` on parent directories, for non-root callers) | Scans fixed catalog fields, no index, no xattrs | proven for `hfs_search.c`; APFS behavior probable |
| btrfs `TREE_SEARCH_V2` | Kernel ioctl, btrfs only | Returns tree items, xattr items among them, by key range | Requires `CAP_SYS_ADMIN`; no typed values, no value predicates, no per-file check | proven (`fs/btrfs/ioctl.c`) |
| CMU 15-712 tagfs (2011) | Patched 2.6 kernel, filesystem derived from ext2 | Tag queries in the kernel through new system calls | Not stacked, untyped tags, course project | proven for its API; query semantics not verified |
| US20090240737A1 (abandoned application, priority 2004) | Patent | Stacked VFS layers, an attribute index and queries in the path | Kernel placement and result filtering not stated | probable (description read, claims not read; no legal opinion, as in §8) |
| Permission-filtered agent memory: Collaborative Memory (arXiv 2505.18279), MAP-Graph (2608.10509), Governed Shared Memory (2606.24535) | Frameworks in user space | Filtering what an agent retrieves by permission | Their own principals (graphs, tenants, roles); no filesystem | probable (abstracts) |

**Conclusion.** Nothing found anticipates the combination of §5: an index of typed xattrs with
structured queries, in a kernel module stacked over unmodified ext4, btrfs or XFS, with results
filtered by the caller's POSIX credentials (probable, given the coverage limits above). Each part
exists elsewhere on its own. Structured metadata queries (KiwiFS, LSFS) and permission-filtered
retrieval (OpenViking, the papers above) are not novel as features; what remains is where they
run and whose credentials decide. KiwiFS and OpenViking are the natural comparison for "why
not a service?" (`docs/faq.md` §21).

## Sources consulted

Accessed on 2026-10-10:

- https://yolofs.github.io/
- https://github.com/YoloFS/YoloFS (clone; `README.md`, `docs/architecture.md`, `kmod/`)
- https://smfs.ai/research/memory-as-a-filesystem/
- https://github.com/supermemoryai/smfs (clone; `Cargo.toml`, `crates/`)
- https://github.com/lspecian/vexfs (clone; `README.md`, `FUSE_STATUS.md`, `kernel_module/`)
- https://github.com/Factory-AI/vfs (clone; `README.md`, `docs/SPEC.md`, `Cargo.toml`)
- https://github.com/desplega-ai/agent-fs (clone; `README.md`, `packages/`)
- https://github.com/Venere-Labs/ragfs (clone; `README.md`, `crates/`)
- https://pages.cs.wisc.edu/~remzi/Classes/838/Fall2001/Papers/sfs-sosp91.pdf (scanned PDF, pages 1-3 and 5-6 read)
- https://arxiv.org/abs/2607.26637
- https://arxiv.org/abs/2604.13536 (referenced by the YoloFS page; not read; note of 2026-10-28: read, see §10)
- https://arxiv.org/abs/2609.10144 (consulted; not a filesystem; not used in the matrix)
- https://patents.google.com/patent/US7617250B2/en
- https://patents.google.com/patent/US20230401171A1/en
- https://en.wikipedia.org/wiki/Be_File_System
- https://docs.kernel.org/filesystems/befs.html
- https://docs.kernel.org/filesystems/overlayfs.html
- https://github.com/haiku/haiku (`src/add-ons/kernel/file_systems/bfs/Query.cpp`, `headers/private/file_systems/QueryParser.h`)
- https://git.haiku-os.org/haiku.git/tree/headers/private/fs_shell/fssh_fs_query.h
- https://haiku-os.org/legacy-docs/BeBook/IndexFunctions_Overview.html
- https://www.bibsonomy.org/bibtex/ab2f49f3779bb2ff505135917eae8975 (TagFS)
- https://tmsu.org/
- https://en.wikipedia.org/wiki/Semantic_file_system
- https://www.crss.ucsc.edu/pub/ames-msst06.html (LiFS)
- MoobFS repository: `README.md`, `ROADMAP.md`, `docs/decisions/0001-0007`, `docs/spec-m2.md`, `docs/spec-m3.md`, `docs/ecosystem.md`, `docs/research-ai-memory.md`, `docs/benchmark-plan.md`, `docs/history/0011-0020`, `layer/main.c`, `layer/index.c`, `include/moobfs_ioctl.h`

Accessed on 2026-10-27 and 2026-10-28 (§10):

- https://arxiv.org/abs/2604.13536 (YoloFS, HTML version v5)
- https://github.com/gianlucamazza/ragfs (`docs/adr/012-native-filesystem-vs-semantic-volume.md`)
- https://github.com/kiwifs/kiwifs (clone)
- https://github.com/volcengine/OpenViking (`docs/en/concepts/15-acl.md`, `07-retrieval.md`)
- https://arxiv.org/abs/2410.11843 (LSFS) and https://github.com/agiresearch/AIOS-LSFS
- https://github.com/tursodatabase/agentfs, https://github.com/Ephemeral-AI-Lab/layerfs,
  https://github.com/thustorage/ContextFS, https://github.com/redis/agent-filesystem (clones)
- https://github.com/apple-oss-distributions/hfs (`core/hfs_search.c`)
- Linux v6.12.112 `fs/btrfs/ioctl.c`
- https://github.com/15-712/tag_utils
- https://patents.google.com/patent/US20090240737A1/en
- https://arxiv.org/abs/2505.18279, https://arxiv.org/abs/2608.10509, https://arxiv.org/abs/2606.24535 (abstracts)
