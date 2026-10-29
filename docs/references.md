# References

Works, projects and tools that MoobFS builds on, includes, compares against, or used to decide.
Every entry was checked against its primary source on 2026-10-12 (DOI metadata, publisher or
USENIX page, official repository or kernel documentation); "Used for" points to where it mattered
in this repository. Code that ships in the repository keeps its own license and provenance file.

## Thanks

MoobFS exists because other people published their work. To the authors of the papers below, to
the maintainers of the Linux kernel and of its documentation, of overlayfs, eCryptfs, ext4, btrfs
and XFS, of fstests, virtme-ng, KASAN, lockdep and the fault-injection framework, of SQLite, and
to the authors of the agent-memory projects that were read and used as test clients: thank you.
To the people on the kernel mailing lists who took the time to answer this project's questions
(the first answer is recorded in journal 0097): thank you.
Errors in how this project understood or used any of it are this project's own. If your work is
used here and is missing from this list, or is described wrongly, please open an issue.

## 1. Code included or pinned in this repository

| Project | Where it comes from | License | Pinned | Used for |
| --- | --- | --- | --- | --- |
| fsx | [xfstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) (`ltp/fsx.c`, `src/statx.h`) | GPL-2.0 (xfstests project license; the file header carries only the original NeXT copyright) | commit `57d71a8`, 2026-03-17 | random I/O exerciser on each lower filesystem (`tests/third_party/fsx/`, `vm/suite.sh`) |
| replay-log / log-writes | [xfstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) (`src/log-writes/`) | GPL-2.0 | commit `9b56b94`, 2019-10-24 | power-loss replay test (`tests/third_party/log-writes/`, `POWER_LOSS_REPLAY_PASS`) |
| checkpatch.pl | [Linux stable](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git) `scripts/` ([docs](https://docs.kernel.org/dev-tools/checkpatch.html)) | GPL-2.0 | tag `v6.12.112`, SHA-256 per file | style check of `layer/` and `kernel/` (`scripts/checkpatch.sh`) |
| virtme-ng | [arighi/virtme-ng](https://github.com/arighi/virtme-ng) | GPL-2.0-only | `1.41`, hashes in `vm/kasan-requirements.txt` | KASAN/UBSAN/lockdep kernel runs (`.github/workflows/kasan.yml`) |
| Linux 6.12.112 | [kernel.org](https://cdn.kernel.org/pub/linux/kernel/v6.x/) | GPL-2.0 | tarball SHA-256 | the KASAN kernel and the benchmark kernel (`bench/kernel-build.sh`) |
| fselect | [jhspetersson/fselect](https://github.com/jhspetersson/fselect), release asset `fselect-x86_64-linux-musl.gz` | Apache-2.0 (per its repository) | `0.10.3`, SHA-256 as listed by GitHub for the asset | SQL-like file search with `has_xattr`/`xattr()` through the layer (`vm/interop-guest.sh`, `.github/workflows/interop.yml`, journal 0140); not vendored |
| cshatag | [rfjakob/cshatag](https://github.com/rfjakob/cshatag), release asset `cshatag_v2.2.1_linux-static_amd64.tar.gz` | MIT | `v2.2.1`, SHA-256 computed from the official tarball (the project publishes a GPG signature only) | checksums in `user.shatag.*`, outdated and corrupt files detected through the layer (journal 0140); not vendored |
| Recoll | [recoll.org](https://www.recoll.org/), Ubuntu 24.04 package `recollcmd` | GPL-2.0-or-later | `1.36.1-1build2` (CI); 1.43.0 in the local run | `[xattrtofields]` indexing of `user.moob.*` and `user.xdg.*` through the layer (journal 0140); not vendored |
| KDE KFileMetaData | [KDE/kfilemetadata](https://invent.kde.org/frameworks/kfilemetadata), Ubuntu 24.04 package `libkf5filemetadata-dev`; `KFileMetaData::UserMetaData` | LGPL-2.1-or-later | `5.115.0-0ubuntu4` (CI); 5.116.0 in the local run | the tag, rating and comment attributes Dolphin and Baloo use, through `vm/interop-kfmd.cpp` (journal 0140); not vendored |

## 2. Research that shaped decisions

### Design basis

- David K. Gifford, Pierre Jouvelot, Mark A. Sheldon, James W. O'Toole. "Semantic File Systems".
  ACM SOSP 1991, 16-25. https://doi.org/10.1145/121132.121138 — prior art for attribute queries
  over an existing filesystem (README, `docs/research-originality.md`).
- Dominic Giampaolo. *Practical File System Design with the Be File System*. Morgan Kaufmann, 1999.
  https://shop.elsevier.com/books/practical-file-system-design/giampaolo/978-1-55860-497-1 — the
  reference design of typed attributes and per-attribute indexes (ADR-0005; ADR-0009 alternatives
  C' and D).
- Shawn Wanxiang Zhong, Junxuan Liao, Jing Liu, et al. "Don't Let AI Agents YOLO Your Files:
  Information and Control in Agent-Native Filesystems" (YoloFS). ACM SOSP 2026, 353-378.
  https://doi.org/10.1145/3830418.3843858 — the closest prior art in form, a stacked kernel module
  for agents; it bounds what MoobFS may claim (README, `docs/research-originality.md`).

### The domain-scoped query (ADR-0009, `docs/spec-query-v2.md`)

- Stefan Büttcher, Charles L. A. Clarke. "A Security Model for Full-Text File System Search in
  Multi-User Environments". USENIX FAST 2005.
  https://www.usenix.org/conference/fast-05/security-model-full-text-file-system-search-multi-user-environments
  — restrict before computing; the analog of the measured timing channel.
- Aameek Singh, Mudhakar Srivatsa, Ling Liu. "Efficient and Secure Search of Enterprise File
  Systems". IEEE ICWS 2007, 18-25. https://doi.org/10.1109/ICWS.2007.80 — the partition must be
  the reader set: origin of the homogeneity rule.
- Eirini C. Micheli, Giorgos Margaritis, Stergios V. Anastasiadis. "Lethe: Cluster-Based Indexing
  for Secure Multi-user Search". IEEE BigData Congress 2014, 323-330.
  https://doi.org/10.1109/BigData.Congress.2014.54 (journal version: "Permission-based Index
  Clustering for Secure Multi-User Search", Transactions on Data Privacy 8(1), 2015, 29-53) — how
  few permission sets real collections have.
- Liang Wang, Paul Grubbs, Jiahui Lu, et al. "Side-Channel Attacks on Shared Search Indexes". IEEE
  S&P 2017, 673-692. https://doi.org/10.1109/SP.2017.50 — count leakage under post-filtering is
  exploited in practice; only partitioning closes it.
- Peter Bailey, David Hawking, Brett Matson. "Secure Search in Enterprise Webs: Tradeoffs in
  Efficient Implementation for Document Level Security". ACM CIKM 2006.
  https://doi.org/10.1145/1183614.1183686 — the cost of per-document checks.
- Alon Halevy, Flip Korn, Natalya F. Noy, et al. "Goods: Organizing Google's Datasets". ACM SIGMOD
  2016, 795-806. https://doi.org/10.1145/2882903.2903730 — a separate "readable by everyone"
  domain.
- Ruoming Pang, Ramon Caceres, Mike Burrows, et al. "Zanzibar: Google's Consistent, Global
  Authorization System". USENIX ATC 2019, 33-46.
  https://www.usenix.org/conference/atc19/presentation/pang — the "new enemy" risk of a lagging
  external index.
- Yiduo Wang, Linghang Meng, Liang Li, Jie Wu. "Accelerating Metadata Management of DFS via
  Speculative Permission Checking" (SPMeta). IEEE ICDE 2026, 3039-3052.
  https://doi.org/10.1109/ICDE65706.2026.00226 — per-directory reachability bits.
- Brenden Kokoszka, Patrick Donnelly, Douglas Thain. "Search Should Be a System Call". University of
  Notre Dame TR-2013-03, 2013. http://ccl.cse.nd.edu/research/papers/search-tr.pdf — precedent for
  an in-kernel search call issued on a directory.
- Dominic Manno, Jason Lee, Prajwal Challa, et al. "GUFI: Fast, Secure File System Metadata Search
  for Both Privileged and Unprivileged Users". SC22, 2022. https://doi.org/10.1109/SC41404.2022.00062
  — one index per permission domain, queried with the user's own credentials.
- Aleatha Parker-Wood, Christina Strong, Ethan L. Miller, Darrell D. E. Long. "Security Aware
  Partitioning for Efficient File System Search". IEEE MSST 2010.
  https://doi.org/10.1109/MSST.2010.5496990 — one index per domain can mean many small indexes.
- Andrew W. Leung, Minglong Shao, Timothy Bisson, et al. "Spyglass: Fast, Scalable Metadata Search
  for Large-Scale Storage Systems". USENIX FAST 2009.
  https://www.usenix.org/conference/fast-09/spyglass-fast-scalable-metadata-search-large-scale-storage-systems
  — namespace partitioning as both optimization and permission boundary.

### Query plans (phase Q2c, journals 0037 and 0044)

- J. Shane Culpepper, Alistair Moffat. "Efficient Set Intersection for Inverted Indexing". ACM TOIS
  29(1), 2010. https://doi.org/10.1145/1877766.1877767
- Daniel Lemire, Leonid Boytsov, Nathan Kurz. "SIMD Compression and the Intersection of Sorted
  Integers". Software: Practice and Experience 46(6), 2016, 723-749. https://doi.org/10.1002/spe.2326

  These grounded the leapfrog (galloping) intersection of Q2c; measurement then replaced it with a
  scan of the shortest run (journal 0044).

### The index built on demand (ADR-0012)

- C. Mohan, Inderpal Narang. "Algorithms for Creating Indexes for Very Large Tables Without
  Quiescing Updates". ACM SIGMOD 1992, 361-370. https://doi.org/10.1145/130283.130337

  The method of ADR-0012 for building a domain's partition while writes continue, implemented
  in journal 0098: without a side file for attribute writes, with a side list for directory
  renames.
  Checked on 2026-10-13 (Crossref metadata; the paper itself was not read in full).

- Theodore Johnson, Dennis Shasha. "2Q: A Low Overhead High Performance Buffer Management
  Replacement Algorithm". VLDB 1994, 439-450. https://www.vldb.org/conf/1994/P439.PDF
- Song Jiang, Xiaodong Zhang. "LIRS: An Efficient Low Inter-reference Recency Set Replacement
  Policy to Improve Buffer Cache Performance". ACM SIGMETRICS 2002; Performance Evaluation
  Review 30(1), 31-42.
- Nimrod Megiddo, Dharmendra S. Modha. "ARC: A Self-Tuning, Low Overhead Replacement Cache".
  USENIX FAST 2003.
  https://www.usenix.org/conference/fast-03/arc-self-tuning-low-overhead-replacement-cache

  Replacement policies designed against the failure of least-recently-used order on a loop
  larger than the cache, which journal 0103 measured on the release of built domains under
  `index_budget`. Not adopted. Checked on 2026-10-14: the first page of the 2Q paper and the
  USENIX page read; for LIRS only the venue was confirmed from secondary sources, and the DOI
  was left out because two different ones are given for it.

### Fault injection (journal 0096)

- Linux kernel documentation, "Fault injection capabilities infrastructure"
  (`Documentation/fault-injection/fault-injection.rst`, Linux 6.12.112): `failslab`,
  `fail_page_alloc`, the stack-trace filter that confines faults to one module, and the per-task
  `fail-nth` for systematic injection.

  The technique of journal 0096 (`tests/failnth.c`, `vm/kasan-guest.sh`), implemented. Read in the
  source tree of the kernel the tests are built with.

### Projection of values (ADR-0014)

- PostgreSQL documentation, "Index-Only Scans and Covering Indexes" (current version, read on
  2026-10-14). https://www.postgresql.org/docs/current/indexes-index-only-scans.html
- Linux 6.12.112, `fs/xfs/libxfs/xfs_fs.h`: `XFS_IOC_BULKSTAT`, `XFS_IOC_ATTRLIST_BY_HANDLE`,
  `XFS_IOC_ATTRMULTI_BY_HANDLE`.

  Returning values from the index instead of reading each file, and the caveat about trusting the
  index's copy; kernel ioctls that return metadata of many files in one call.

### Query front-ends (ADR-0013, proposed; `docs/research-query-frontends.md`)

The sources of the study are listed in its last section: SQLite's virtual table mechanism and
optimizer documentation, DuckDB's C API table functions, Apache DataFusion's `TableProvider`,
Polars' `register_io_source`, osquery's table and SDK documentation, Chroma's and Qdrant's
filtering documentation, CEL, and in Linux 6.12.112 the ftrace event filter and nftables' PIPAPO
set. Read on 2026-10-14.

### Disjunctions over index scans (ADR-0013, proposed)

- PostgreSQL documentation, "Combining Multiple Indexes" (current version, read on 2026-10-14).
  https://www.postgresql.org/docs/current/indexes-bitmap-scans.html
- MySQL 8.4 Reference Manual, "Index Merge Optimization" (read on 2026-10-14).
  https://dev.mysql.com/doc/refman/8.4/en/index-merge-optimization.html
- Oracle Database 18c SQL Tuning Guide, "Query Transformations" (OR expansion), and the Oracle
  optimizer team's article "Optimizer transformations: OR expansion".
  https://docs.oracle.com/en/database/oracle/oracle-database/18/tgsql/query-transformations.html
  https://blogs.oracle.com/optimizer/post/optimizer-transformations-or-expansion

  How databases answer OR with several index scans, and how OR expansion keeps a row from being
  returned twice. Checked on 2026-10-14: the PostgreSQL and MySQL pages read; for Oracle, the
  search results and summaries of the two pages, not the pages in full.

### syzkaller (security roadmap S3, journal 0109)

- syzkaller, https://github.com/google/syzkaller, main branch at `8fb0ae89890497b4` (2026-10-14):
  `docs/linux/setup_ubuntu-host_qemu-vm_x86-64-kernel.md`, `docs/linux/kernel_configs.md`,
  `docs/syscall_descriptions.md`, `sys/linux/filesystem.txt` (the `fs_options` types and
  `mount$tmpfs`, followed for the layer's mount), `tools/create-image.sh`.

  The fuzzer and its documentation, used as written; the layer's descriptions follow the existing
  filesystem descriptions.

### Copies to user space under a lock (journal 0105)

- Linux kernel, commit `a48b73eca4ce`, "btrfs: fix potential deadlock in the search ioctl"
  (2020), and the follow-up that probes at sub-page granularity (`fault_in_subpage_writeable()`,
  discussed on linux-kernel in 2021-2022); `search_ioctl()` and `copy_to_sk()` in
  `fs/btrfs/ioctl.c`, Linux 6.12.112.
  https://android-kvm.googlesource.com/linux/+/a48b73eca4ceb9b8a4b97f290a065335dbcd8a04
- `Documentation/admin-guide/sysctl/vm.rst`, `unprivileged_userfaultfd`, Linux 6.12.112.

  Precedent and reason for copying query results to user space only after `query_sem` is
  released. Checked on 2026-10-14: the btrfs source and the documentation read at the tag; the
  commit's description read in a mirror of the repository.

### SELinux (security roadmap S12, journal 0104)

- SELinux reference policy, release 2_20250213, `policy/modules/kernel/filesystem.te`
  (`fs_use_xattr overlay`, line 40).
  https://github.com/SELinuxProject/refpolicy/blob/RELEASE_2_20250213/policy/modules/kernel/filesystem.te
- SELinux test suite, `tests/overlay/` and `policy/test_overlayfs.te` (main branch, read on
  2026-10-14). https://github.com/SELinuxProject/selinux-testsuite

  The labelling rule of `vm/selinux/moobfs_layer.cil` follows the reference policy's rule for
  overlay; the test domain and file types of `vm/selinux/moobtest.te` follow the test suite's
  overlay policy. Also read: `security_fs_use()` (`security/selinux/ss/services.c`),
  `selinux_inode_listsecurity()` (`security/selinux/hooks.c`), `security_inode_listsecurity()`
  (`security/security.c`) and `ovl_xattr_get()` (`fs/overlayfs/xattrs.c`), Linux 6.12.112.

### Comparing two measurements (journal 0134)

- Bradley Efron. "Bootstrap Methods: Another Look at the Jackknife". The Annals of Statistics
  7(1), 1979, 1-26. https://doi.org/10.1214/aos/1176344552
- H. B. Mann, D. R. Whitney. "On a Test of Whether one of Two Random Variables is Stochastically
  Larger than the Other". The Annals of Mathematical Statistics 18(1), 1947, 50-60.
  https://doi.org/10.1214/aoms/1177730491
- Linux kernel documentation, "CPU Performance Scaling" (`Documentation/admin-guide/pm/cpufreq.rst`)
  and "intel_pstate CPU Performance Scaling Driver" (`Documentation/admin-guide/pm/intel_pstate.rst`):
  `scaling_governor`, `no_turbo`, `boost`.

  `bench/compare.py` (percentile bootstrap interval of the difference of medians; the U test with
  the normal approximation and tie correction, checked against SciPy's `mannwhitneyu` on three
  samples) and `bench/cpu-freq.sh`, implemented. Journal 0188 and
  `docs/g6-evaluation-protocol.md` reuse this method for any later G1 latency claim
  and add no source.

### Casefolded lower directories (issue #95, journal 0136)

- overlayfs, Linux v7.3-rc7: one casefold state for every layer, checked when a layer is added
  (`fs/overlayfs/params.c`, `ovl_dentry_casefolded()`) and on lookup for parent and child
  (`fs/overlayfs/namei.c`, "parent wrong casefold", "child wrong casefold").

  The rule `moobl_casefolded()` follows (refuse, do not propagate), implemented.

### Security roadmap (proposal)

- Wen Xu, Hyungon Moon, Sanidhya Kashyap, et al. "Fuzzing File Systems via Two-Dimensional Input
  Space Exploration" (Janus). IEEE S&P 2019, 818-834. https://doi.org/10.1109/SP.2019.00035
- Seulbae Kim, Meng Xu, Sanidhya Kashyap, et al. "Finding Semantic Bugs in File Systems with an
  Extensible Fuzzing Framework" (Hydra). ACM SOSP 2019, 147-161.
  https://doi.org/10.1145/3341301.3359662

  Precedents for item S3 of `docs/security-roadmap.md` (journal 0017).

## 3. Systems and kernel facilities

| Name | Reference | Used for |
| --- | --- | --- |
| overlayfs | https://docs.kernel.org/filesystems/overlayfs.html | model of the stacked layer, `override_creds`, the rule against touching the lower filesystem (ADR-0007) |
| eCryptfs | https://docs.kernel.org/filesystems/ecryptfs.html | model of a 1:1 stacked filesystem (ADR-0007) |
| eCryptfs create, mkdir, symlink and link error paths | [`fs/ecryptfs/inode.c` at v6.12.112](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/fs/ecryptfs/inode.c?h=v6.12.112): `ecryptfs_do_create()` unlinks the lower file when the upper inode cannot be made; `ecryptfs_mkdir()`, `ecryptfs_symlink()` drop a negative upper dentry | the undo and the `d_drop()` after a failed upper inode in `layer/main.c` (R2-4, journal 0156) |
| Linux fault-injection stack filter and function tracing | [`lib/fault-inject.c`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/lib/fault-inject.c?h=v6.12.112), [`Documentation/trace/ftrace.rst`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/Documentation/trace/ftrace.rst?h=v6.12.112), Linux v6.12.112, GPL-2.0 | Exact pinned sources consulted for the function-scoped fail-nth allocation and PID-filtered scan synchronization in the R2-6/R2-10 VM regressions (journal 0170); the issue 118 moved-scan regression reuses that synchronization technique. |
| Linux inode-cache lookup before allocation | [`fs/inode.c`, `iget5_locked()`, Linux v6.12.112](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/fs/inode.c?h=v6.12.112), GPL-2.0 | Explains why the positive source inode in the hard-link path is reused rather than allocated; bounds the R2-4 negative evidence without adding a forced production failure hook (journal 0170). |
| backing_file helpers | [`fs/backing-file.c` at v6.12.112](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/fs/backing-file.c?h=v6.12.112) | I/O delegation in `layer/main.c` |
| overlayfs `ovl_want_write()` | [`fs/overlayfs/util.c` at v6.12.112](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/fs/overlayfs/util.c?h=v6.12.112), called from `ovl_create_object()` in `fs/overlayfs/dir.c` | write access on the lower mount around every change (`moobl_want_write()`, journal 0117) |
| Filesystem freezing, `mnt_want_write()`, idmapped mounts | [`fs/namespace.c` at v6.12.112](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/fs/namespace.c?h=v6.12.112), `ext4_journal_check_start()` in `fs/ext4/ext4_jbd2.c`, https://docs.kernel.org/filesystems/idmappings.html, `fsfreeze(8)` and `X-mount.idmap` of `mount(8)` (util-linux) | the freeze and read-only lower tests and the idmapped-lower refusal (journal 0117) |
| FUSE and FUSE passthrough | https://docs.kernel.org/filesystems/fuse/index.html | the alternative rejected by ADR-0001 |
| xarray, rbtree | https://docs.kernel.org/core-api/xarray.html, https://docs.kernel.org/core-api/rbtree.html | the in-kernel index (`layer/index.c`) |
| iomap | https://docs.kernel.org/filesystems/iomap/index.html | the read-only M1 module (`kernel/main.c`) |
| dm-log-writes, dm-flakey | https://docs.kernel.org/admin-guide/device-mapper/log-writes.html, https://docs.kernel.org/admin-guide/device-mapper/dm-flakey.html | power-loss and I/O-error tests |
| Shrinker interface | `include/linux/shrinker.h` (`shrinker_alloc`, `shrinker_register`, `shrinker_free`), `mm/shrinker.c`, `Documentation/admin-guide/mm/shrinker_debugfs.rst`, Linux 6.12.112 | releases built domains under memory pressure (ADR-0012, implemented in journal 0101) |
| Memory-cgroup allocation flags | [`Documentation/core-api/memory-allocation.rst`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/Documentation/core-api/memory-allocation.rst?h=v6.12.112), `include/linux/gfp_types.h`, `mm/util.c` and `mm/vmalloc.c`, Linux 6.12.112, GPL-2.0 | `__GFP_ACCOUNT` guidance and the kvmalloc/vmalloc charge path investigated in journal 0180 |
| Memory-cgroup charge stays in the cgroup that hit the limit | [`mm/memcontrol.c` `try_charge_memcg()`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/mm/memcontrol.c?h=v6.12.112) calls `try_to_free_mem_cgroup_pages()` on `mem_over_limit` and fails the charge when that reclaim does not make room; [`memory.max`](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/Documentation/admin-guide/cgroup-v2.rst?h=v6.12.112) is that cgroup's hard limit. Linux 6.12.112, GPL-2.0 | Prior art for ADR-0021: a budget release reclaims only the builder's own domains, or the build returns `ENOMEM`. It does not take a sibling's domain. Checked against the v6.12.112 sources on 2026-10-25 |
| Kernel build-limit primitives | `include/linux/jiffies.h` (`time_after_eq`), `include/linux/uidgid.h` (`uid_eq`), `include/linux/timekeeping.h` (`ktime_get_ns`) and `include/linux/capability.h` (`capable`), exact Debian 6.12.111 build headers checked on the dedicated host, GPL-2.0 | Fixed-window admission and cooperative deadline in ADR-0019 and journal 0182; the algorithm is local to MoobFS, not claimed as a kernel-provided rate limiter |
| Device-mapper delay target | [Linux 6.12 `dm-delay` documentation](https://docs.kernel.org/6.12/admin-guide/device-mapper/delay.html), verified against `CONFIG_DM_DELAY=m` and the `dm-delay.ko.xz` file of the exact Debian 6.12.111 guest kernel | The six-argument table delays reads while leaving writes and flushes at zero delay in the issue-118 lower-I/O sensitivity probe (journal 0185); it emulates a delay, not a particular storage device. |
| Tracepoints | `Documentation/trace/tracepoints.rst`, `Documentation/trace/events.rst`, `samples/trace_events/` (the pattern for a module's own events), Linux 6.12.112 | the index's events (journal 0102) |
| `drop_caches` | `Documentation/admin-guide/sysctl/vm.rst` (`drop_caches`: writing 2 frees reclaimable slab objects through every shrinker), Linux 6.12.112 | how the suite asks the shrinker for everything (journal 0101) |
| `list_lru` | `include/linux/list_lru.h`, as used in `fs/dcache.c` and `fs/inode.c`, Linux 6.12.112 | considered for the least-recently-used order of built partitions (ADR-0012); not used, a walk of the domains was enough (journal 0101) |
| btrfs extent-map shrinker | `fs/btrfs/extent_map.c` (`btrfs_extent_map_shrinker_worker`), Linux 6.12.112 | precedent for a filesystem dropping a map of its own and rebuilding it on demand (ADR-0012) |
| Sequence counters | `include/linux/seqlock.h`; `rename_lock` in `fs/dcache.c`, Linux 6.12.112 | reading where a file is (its depth and domain directory) consistently against renames, as path lookup does (`where_is()` in `layer/index.c`, journal 0098); the same sample-then-read order for the rename generation a build records (`rename_gen()`, issue 110, journal 0174) |
| POSIX permission check | `acl_permission_check()` and `generic_permission()` in `fs/namei.c`, Linux 6.12.112 | the owner judged by the owner bits and a member of the group by the group bits, never by those of others: condition 1 of the homogeneity rule (journal 0116, finding F9) |
| Reader-writer semaphores | `rwsem_read_trylock()` and `rwsem_down_read_slowpath()` in `kernel/locking/rwsem.c`, Linux 6.12.112 | a reader queues behind a waiting writer, so one stalled reader of `query_sem` stops every query once a writer waits (journals 0105 and 0116) |
| FUSE request wait | `request_wait_answer()` in `fs/fuse/dev.c`, Linux 6.12.112 | a request the daemon has read is waited for without interruption: the stall of finding F10, and the daemon of `tests/fusestall.c` (journal 0116) |
| Path lookup flags | `LOOKUP_NO_XDEV`, `LOOKUP_NO_SYMLINKS` (`include/linux/namei.h`, `handle_mounts()` in `fs/namei.c`), the kernel side of `openat2(2)`'s `RESOLVE_NO_XDEV` and `RESOLVE_NO_SYMLINKS`, Linux 6.12.112 | the per-result check does not leave the mount nor follow links (journal 0116) |
| SQLite virtual-table collations | [`sqlite3_vtab_collation()`](https://www.sqlite.org/c3ref/vtab_collation.html); `allocateIndexInfo()` in `where.c` (SQLite 3.50.4 amalgamation) | a condition under a collation other than BINARY is not handed to the kernel; an ORDER BY with a different COLLATE never reaches `xBestIndex` (journal 0120) |
| overlayfs directory reads | `ovl_iterate()` in `fs/overlayfs/readdir.c`, Linux 6.12.112 | directory reads under the mounter's credentials (journal 0120) |
| fstests | https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git, GPL-2.0, `v2026.09.22` | the `generic` `quick` group over the layer (`vm/fstests/`, journals 0074 and 0091); not vendored |
| KASAN, UBSAN, lockdep | https://docs.kernel.org/dev-tools/kasan.html, https://docs.kernel.org/dev-tools/ubsan.html, https://docs.kernel.org/locking/lockdep-design.html | the instrumented test kernel |
| KCSAN | https://docs.kernel.org/dev-tools/kcsan.html (`Documentation/dev-tools/kcsan.rst`, Linux 6.12.112) | the data-race runs (`vm/kcsan.config`); reading a report: plain against marked accesses, the top frame of each access, `data_race()` (journal 0115) |
| fanotify, inotify | https://man7.org/linux/man-pages/man7/fanotify.7.html | change-feed experiment (journal 0030); inotify follows changes in the user-space baseline (`bench/userspace/udx-watch.c`, journal 0092) |
| Landlock | https://docs.kernel.org/userspace-api/landlock.html | confinement experiment (journal 0030), ADR-0008 and ADR-0009 |
| idmapped mounts | https://docs.kernel.org/filesystems/idmappings.html | refused by the layer and tested |
| libmount (util-linux) | [`libmount/src/hook_mount.c`](https://github.com/util-linux/util-linux/blob/v2.41.2/libmount/src/hook_mount.c) (`hook_reconfigure_mount`, `configure_superblock`) and `libmount/src/context.c` (`mnt_context_apply_fstab`), v2.39.3 and v2.41.2 | what `mount -o remount` sends through the new mount API: every option of the mount's mountinfo line again (journal 0118) |
| NFS remount comparison | `nfs_compare_remount_data()` and `nfs_reconfigure()` in `fs/nfs/super.c`, Linux 6.12.112 | precedent for accepting an option repeated on remount with its current value and refusing a change (journal 0118) |
| SQLite | https://www.sqlite.org (public domain) | the benchmark baseline (`bench/sqlite-baseline.sh`, journal 0047), the WAL acceptance tests, the cost of a side index at write time (`bench/synccost.c`, journal 0088) and the user-space baseline of the index (`bench/userspace/`, journals 0092 and 0094; 3.46.1 on the benchmark host) |
| Be File System query engine (Haiku) | [`Query.cpp`](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/file_systems/bfs/Query.cpp), MIT | how BFS chooses an index (ADR-0009 C') |
| GUFI | https://github.com/mar-file-system/GUFI, BSD-3-Clause | per-permission-domain index model (ADR-0009) |
| YoloFS | https://yolofs.github.io/, https://github.com/YoloFS/YoloFS, GPL-2.0 | closest prior art (see §2) |
| Elasticsearch document-level security | [Elastic docs](https://www.elastic.co/docs/deploy-manage/users-roles/cluster-or-deployment-auth/controlling-access-at-document-field-level) | precedent for count leakage (ADR-0009); the current page states that restricted documents still count in the scoring statistics |
| LMDB, RocksDB, LevelDB | https://www.symas.com/mdb, https://rocksdb.org/, https://github.com/google/leveldb | engines weighed for a user-space index (ADR-0009) |

Review 0144 and correction 0149 also consult Linux 6.12.112's `fs/fcntl.c`
(GPL-2.0), `fs/overlayfs/file.c` (GPL-2.0-only)
(`ovl_real_fdget`, `ovl_change_flags`), `fs/backing-file.c`,
`fs/read_write.c` and `include/linux/fs.h` for synchronization of mutable
file flags and initialization of the lower synchronous I/O request. These are
the exact test-kernel sources, not a newer API substituted for the build.

Correction 0150 consults Linux 6.12.112's `fs/xattr.c` (GPL-2.0-only; `vfs_getxattr()` and
`__vfs_getxattr()`). A value read can return a negative error from permission,
security or the lower filesystem handler. `-ENODATA` means a name listed earlier
may have been removed; `-ERANGE` means the value exceeded the scan buffer.
Other failures must abort an index build or adoption.

## 4. Agent-memory clients analyzed

| Client | Repository | License | Analyzed |
| --- | --- | --- | --- |
| ai-memory | https://github.com/akitaonrails/ai-memory | MIT | source at `385b81e` and `88f8804` (`docs/research-ai-memory.md`, `docs/research-client-query-patterns.md`) |
| mem0 | https://github.com/mem0ai/mem0 | Apache-2.0 | query patterns at `b7ad69a` |
| Letta | https://github.com/letta-ai/letta | Apache-2.0 | V1 server, branch `archive` at `56ba9c2` |
| Letta Code | https://github.com/letta-ai/letta-code | Apache-2.0 | `271f119` |
| Graphiti | https://github.com/getzep/graphiti | Apache-2.0 | `964bb11` |
| LangGraph store | https://github.com/langchain-ai/langgraph | MIT | `799c2f0`; adapter and equality test against `InMemoryStore` with langgraph 1.2.15 and langgraph-checkpoint 4.3.0 from PyPI (`bench/client/langgraph_store.py`, journal 0089). Journal 0189 reuses those pins for the G1 host checks and for the mounted gate, which passed the same evening on the dedicated Debian host. |
| LangMem | https://github.com/langchain-ai/langmem | MIT | `48e3c11` |
| OpenAI Agents SDK sessions | https://github.com/openai/openai-agents-python | MIT | `125efa0` |

## 5. Public project metadata

Sources for the external-records section of both READMEs, checked on 2026-10-20
(journal 0167). These records describe release tracking and source discovery, not a
kernel review or a compatibility test.

| Source | Used for |
| --- | --- |
| [MoobFS on Anitya](https://release-monitoring.org/project/392843/) | Experimental pre-release tracking and the absence of a distribution package mapping. |
| [MoobFS on Open Hub](https://openhub.net/p/moobfs) | Public project metadata; code analysis is not complete. |
| [MoobFS candidate corpus on XERJ](https://hub.xerj.org/corpus/studiomoob--llmoobfs) | Selection of a source snapshot; license review is pending and the corpus is not installable yet. |
| [MoobFS experimental source snapshot on Zenodo](https://doi.org/10.5281/zenodo.23514721) | Citation metadata and the official version DOI badge for commit `02381fba`, archive version `0.4.1-experimental+git.02381fba` (journal 0168). |
| [TeleAI — Awesome Agent Memory](https://github.com/TeleAI-UAGI/Awesome-Agent-Memory/blob/749a08cfe25ad23dda05565d1a11b444608b7786/README.md) | Verified MoobFS entry under **Emerging projects**, described as experimental agent-memory storage; source for the discovery link in both READMEs (journal 0169, checked 2026-10-20). This listing is not a kernel review or a compatibility test. |
## Product-gap assessment sources (2026-10-20)

- Qdrant, [Filtering in Vector Search](https://qdrant.tech/documentation/search-patterns/vector-search-filtering/)
  and [Indexing](https://qdrant.tech/documentation/manage-data/indexing/).
  Official documentation consulted through the site and Context7
  `/websites/qdrant_tech`; unversioned documentation, accessed 2026-10-20.
  Used in journal 0164 for filtered vector-retrieval trade-offs, not as a selected
  dependency or a claim of inherited authorization. Thanks to its authors.

## Algorithm-fit assessment (2026-10-20, journal 0166)

Sources used to prioritize research in [research-algorithm-fit.md](research-algorithm-fit.md),
not adopted dependencies. That document records the inspected sections and limitations.
The existing entries above already credit 2Q, ARC and equality intersection.

- Linux 6.12.112: [`rbtree_augmented.h`](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/include/linux/rbtree_augmented.h),
  [`shrinker.h`](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/include/linux/shrinker.h),
  [`list_lru.h`](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/include/linux/list_lru.h).
  Exact KASAN version; augmentation and reclaim contracts, read-only inspection.
- Yang et al., [S3-FIFO](https://www.cs.cmu.edu/~rvinayak/papers/s3-fifo-sosp-2023-fifo-queues-are-all-you-need-for-cache-eviction.pdf),
  SOSP 2023, DOI 10.1145/3600006.3613147. Candidate for domain eviction comparison.
- [SQLite FTS5 BM25](https://www.sqlite.org/fts5.html#the_bm25_function), official
  documentation accessed 2026-10-20, also via Context7 `/websites/sqlite_docs`.
  Lexical baseline; no runtime dependency/version selected.
- Cormack, Clarke and Buettcher, [Reciprocal Rank Fusion](https://cormack.uwaterloo.ca/cormacksigir09-rrf.pdf),
  SIGIR 2009. Candidate user-space ranking fusion.
- Carbonell and Goldstein, [The Use of MMR](https://www.cs.cmu.edu/~jgc/publication/The_Use_MMR_Diversity_Based_LTMIR_1998.pdf),
  SIGIR 1998. Candidate client-side diversity reranking.
- Malkov and Yashunin, [HNSW](https://arxiv.org/abs/1603.09320), arXiv 1603.09320,
  revised 2018. Conditional approximate-vector candidate; introductory reading only.
- Lemire, Ssi-Yan-Kai and Kaser, [Roaring](https://arxiv.org/html/1603.06549v4),
  arXiv 1603.06549v4, 2018 revision. Conditional set-representation candidate.

Thanks to these authors and the maintainers of the underlying infrastructure.

## Not included

Works surveyed but not used in a decision (most of `docs/research-academic-survey.md` and the
originality matrix), and tools only planned (syzkaller, LTP, sqlite-vec, USearch). They stay in the
research documents. Two page ranges could not be confirmed and are omitted: Büttcher and Clarke
(FAST 2005) and the SC22 article number of GUFI.

## 6. Harness verdict regression (journal 0171)

- Linux man-pages [`wait(2)`](https://man7.org/linux/man-pages/man2/waitpid.2.html):
  child waiting and `WIFEXITED`/`WEXITSTATUS`, used by `tests/query.c` before
  printing its race2 summary.
- Linux man-pages [`ld.so(8)`](https://man7.org/linux/man-pages/man8/ld.so.8.html):
  `LD_PRELOAD` symbol interposition, used only in the disposable Linux container's
  `tests/test_harness_verdicts.py` to control ioctl replies and child exit status.
  It exercises the real fork/wait path without a module; it does not emulate
  the filesystem or prove query correctness. Thanks to the Linux man-pages maintainers.

## 7. Permission and independent-oracle regressions (journal 0172)

- Linux v6.12.112, [`fs/proc/base.c`](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/fs/proc/base.c),
  GPL-2.0: exact `proc_fail_nth_write`/read handlers consulted for the child-owned
  control descriptor in the deferred unprivileged-fault candidate, preserved
  locally (journal 0172). It is not active coverage of this review batch.
- Existing specifications [spec-m2.md](spec-m2.md) §3/§8 and
  [spec-query-v2.md](spec-query-v2.md) §3/§4 define the expected fixture bytes,
  directory authorization and filtered results. `tests/permission-gaps.c` uses
  fixed expectations and lower-kernel access, without linking `common/attr.c`.

## 8. Domain renamed while a build waits to start (issue 110, journal 0174)

- Linux v6.12.112, `fs/namei.c` `vfs_rename()`, GPL-2.0: the filesystem's
  `->rename()` runs before `d_move()`/`d_exchange()` of the VFS dentries. The
  layer's rename hook therefore raises the generation after the lower rename
  and before its own dentries move, which is why the fix reads the domain's
  name from the lower dentry. Read in the source tree the KASAN harness builds.
- Linux v6.12.112, `include/linux/sched.h` (`TASK_KILLABLE`) and
  `fs/proc/array.c` (state letters), GPL-2.0: `mutex_lock_killable()` sleeps in
  `TASK_KILLABLE`, shown as `D` in `/proc/<pid>/stat`, which `tests/rootpath-race.c`
  uses to see the QUERY2 waiting; `/proc/<pid>/stack` and `wchan` gave no
  function names in the guest kernel.

## 9. G4 derived-index protocol (journal 0194, proposed)

- Linux v6.12.112, `fs/notify/notification.c` `fsnotify_insert_event()` and
  `fs/notify/inotify/inotify_user.c` (`max_queued_events`, default 16 384,
  read by `inotify_new_group()`), GPL-2.0: once a group's queue holds
  `max_events` events, one `IN_Q_OVERFLOW` event replaces further events. E8
  of protocol section 12 lowers the sysctl to cause that. Read from the
  stable tree at the tag, not built.
- [inotify(7)](https://man7.org/linux/man-pages/man7/inotify.7.html):
  overflow and the need for a rescan; with the fanotify entry above.
- Weinberger et al., [Feature Hashing for Large Scale Multitask
  Learning](https://arxiv.org/abs/0902.2206), ICML 2009. The stand-in model
  `hash64-v1` hashes tokens into a fixed number of signed components.
- [FIPS 180-4, Secure Hash Standard](https://csrc.nist.gov/pubs/fips/180-4/upd1/final),
  NIST 2015: SHA-256 as the content version, through Python's `hashlib`.
- Git, [racy-git](https://git-scm.com/docs/racy-git): why a stat comparison
  alone can miss a change made within one timestamp tick; reason the content
  version is a hash, not mtime and size.

## 10. Consumable path (ADR-0022, journal 0196)

- [OpenZFS](https://github.com/openzfs/zfs), CDDL: an out-of-tree Linux filesystem module
  distributed through DKMS (Debian's `zfs-dkms`), whose build checks a stated range of
  supported kernels. Precedent for the stage 2 shape of item C6; no code used.
- [DKMS](https://github.com/dkms-project/dkms), GPL-2.0: `dkms.conf`, including
  `BUILD_EXCLUSIVE_KERNEL`, for building a module against each installed kernel and refusing
  kernels outside a range. Proposed for C6; not used yet.
- Linux v6.12.112, `Documentation/admin-guide/module-signing.rst`, GPL-2.0: module signing,
  `CONFIG_MODULE_SIG_FORCE` and the constraints Secure Boot puts on an out-of-tree module.
  Basis of C6's signing criterion.
- [Debian official cloud images](https://cloud.debian.org/images/cloud/), trixie generic amd64
  image `20261021-2638`, checked against its `SHA512SUMS`: the clean system of
  `make install-test` (journal 0197). Configured through
  [cloud-init NoCloud](https://cloudinit.readthedocs.io/en/latest/reference/datasources/nocloud.html)
  served over HTTP.
- [Debian trixie-security package index](https://deb.debian.org/debian-security/dists/trixie-security/main/binary-amd64/Packages.xz):
  `apt-cache policy` inside the pinned `debian:trixie-slim` image reported
  `linux-image-6.12.113+deb13-amd64` and `linux-headers-6.12.113+deb13-amd64`
  at exact version `6.12.113-1` on 2026-10-26; the CI image pins that pair
  (journal 0203).
- systemd [`systemd.mount(5)`](https://www.freedesktop.org/software/systemd/man/latest/systemd.mount.html):
  `x-systemd.requires-mounts-for=` in `fstab`, `RequiresMountsFor=` and unit naming after the
  mount point, used by `docs/operations.md` section 4.
- FreeBSD [sysexits(3)](https://man.freebsd.org/cgi/man.cgi?query=sysexits&sektion=3), also
  shipped by glibc as `<sysexits.h>`: the exit codes 64, 65, 69, 75 and 77 of the `moobfs` tool
  (journal 0198), as the other tools already use 64 and 65. Defined in the tool, not included.
- Podman, [podman-run(1)](https://docs.podman.io/en/latest/markdown/podman-run.1.html): `--user`,
  `--uidmap`, `--userns=keep-id` and the `idmap` bind option, which needs rootful Podman and a
  filesystem that allows idmapped mounts. Used by `vm/container-test/guest.sh` (journal 0199).
- Docker, [docker run](https://docs.docker.com/reference/cli/docker/container/run/) `--user`
  and bind mounts, Debian's `docker.io` 26.1.5: the same recipe as Podman's.
- Linux v6.12.112, `fs/namespace.c` (`can_idmap_mount()`, which refuses a superblock whose type
  lacks `FS_ALLOW_IDMAP`) and `layer/index.c` `query2_target()` (domain from the dentry's depth,
  not the mount): why a bind of a domain directory answers `QUERY2` and an idmapped bind is
  refused (journal 0199).

## 11. G5 retrieval protocol (journal 0200)

- Wu, Wang, Yu, Zhang, Chang and Yu, [LongMemEval: Benchmarking Chat
  Assistants on Long-Term Interactive Memory](https://arxiv.org/abs/2410.10813),
  ICLR 2025; code [xiaowu0162/LongMemEval](https://github.com/xiaowu0162/LongMemEval)
  (MIT) and data
  [xiaowu0162/longmemeval-cleaned](https://huggingface.co/datasets/xiaowu0162/longmemeval-cleaned)
  (MIT). The labeled questions of protocol section 13; fields as in the
  repository's README, read 2026-10-25.
- [sentence-transformers/all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2),
  Apache-2.0, 384 dimensions, truncation at 256 word pieces (model card read
  2026-10-25); Reimers and Gurevych, [Sentence-BERT](https://arxiv.org/abs/1908.10084),
  EMNLP 2019. The vector branch; not yet downloaded.
- Järvelin and Kekäläinen, [Cumulated gain-based evaluation of IR
  techniques](https://doi.org/10.1145/582415.582418), ACM TOIS 2002. nDCG@10.
- SQLite FTS5 `bm25()` and reciprocal rank fusion are the entries of the
  algorithm-fit assessment above.

## 12. Python client library (ADR-0023, journal 0204)

- Python standard library: [`os` extended-attribute functions](https://docs.python.org/3/library/os.html#linux-extended-attributes)
  (`setxattr`, `listxattr`, `getxattr`, since 3.3), [`fcntl.ioctl`](https://docs.python.org/3/library/fcntl.html#fcntl.ioctl)
  and [`ctypes`](https://docs.python.org/3/library/ctypes.html). The UAPI mirror and the
  attribute calls; no third-party package.
- [PEP 621](https://peps.python.org/pep-0621/) (project metadata in `pyproject.toml`) and
  [PEP 639](https://peps.python.org/pep-0639/) (SPDX licence expression), with
  [setuptools](https://setuptools.pypa.io/) as the build backend, pinned by version and hash in
  `python/build-requirements.txt`.
- [GNU Lesser General Public License, version 2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html),
  the licence of `python/` (text from Debian's `common-licenses`).
- `renameat2(2)` with `RENAME_NOREPLACE` (Linux 3.15, man-pages `rename(2)`): publishing
  without replacing an existing name.
- Considered, not used: [pyxattr](https://pypi.org/project/pyxattr/) and
  [xattr](https://pypi.org/project/xattr/) (C extensions for what `os` now offers) and
  [ctypeslib](https://github.com/trolldbois/ctypeslib) (ctypes generated from headers).

## 13. Security operations (journal 0205)

- OSTIF, [Security Best Practices Guide](https://github.com/ostif-org/best-practices-guide),
  chapters 1 to 7, read 2026-10-26 (sections on the reporting pipeline and response policies
  contributed by Least Authority). The checklist of `docs/security-operations.md` and roadmap
  item S22.
- E. Foudil and Y. Shafranovich, [RFC 9116: A File Format to Aid in Security Vulnerability
  Disclosure](https://www.rfc-editor.org/rfc/rfc9116), 2022. `site/.well-known/security.txt`:
  `Contact` and one `Expires` required, location at the domain root.
- disclose.io, [safe-harbor terms](https://github.com/disclose/dioterms/blob/master/terms/safe-harbor/en-US.md),
  CC0-1.0, read 2026-10-26. Adapted for the safe-harbor section of `SECURITY.md`.
- GitHub Docs, [Exporting a software bill of materials for your repository](https://docs.github.com/en/code-security/supply-chain-security/understanding-your-software-supply-chain/exporting-a-software-bill-of-materials-for-your-repository),
  read 2026-10-26: SPDX export of the dependency graph.
- `actions/upload-pages-artifact` v5.0.0, `action.yml` at
  `fc324d3547104276b827a68afc52ff2a11cc49c9`: files and directories starting with a dot are
  excluded unless `include-hidden-files` is `true` (`.git` and `.github` always are).
- [linux-cve-announce](https://lore.kernel.org/linux-cve-announce/), the
  [Debian security tracker for `linux`](https://security-tracker.debian.org/tracker/source-package/linux)
  and the [SQLite CVE page](https://www.sqlite.org/cves.html): the advisory sources of
  `docs/security-operations.md` §3.

## 14. MCP adapter (ADR-0024, journal 0208)

- [Model Context Protocol specification](https://modelcontextprotocol.io/specification) and the
  official [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk) 2.3.0 (`MCPServer`,
  the stdio transport, `Client` with `StdioServerParameters` for the tests), read through
  Context7 on 2026-10-27.
- The MCP project's reference [filesystem server](https://github.com/modelcontextprotocol/servers/tree/main/src/filesystem),
  which also confines its tools to allowed directories.

## 15. Branch updates and merges (journal 0217)

- Linux kernel documentation, [Rebasing and merging](https://docs.kernel.org/maintainer/rebasing-and-merging.html)
  (`Documentation/maintainer/rebasing-and-merging.rst`): rebase a private series before it is
  published, do not rewrite published history, and avoid merging the mainline into a topic
  branch without a reason.
- Git, [`git push --force-with-lease`](https://git-scm.com/docs/git-push#Documentation/git-push.txt---force-with-leaseltrefnamegt):
  the push is refused when the remote branch is no longer at the expected commit.

## 16. Search-only directories (ADR-0025, journal 0215)

- `may_lookup()` and `may_open()` in `fs/namei.c`, Linux 6.12.111
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/namei.c)): a path walk asks only
  `MAY_EXEC` of each directory, while opening a directory to list it is checked through
  `inode_permission()` with the access mode of the open, `MAY_READ` for a read. Knowing a name
  and learning it are separate permissions; the homogeneity rule and the per-result check now ask
  both of every directory on the way.
- plocate's `access_rx_cache.cpp` ([source](https://git.sesse.net/?p=plocate;a=blob;f=access_rx_cache.cpp)),
  read on 2026-10-27: a path from a database built as root is shown only when the caller passes
  `access(dir, R_OK | X_OK)` on every directory above it. Prior art for ADR-0025.
- `rename_lock` and `read_seqbegin()`/`read_seqretry()` (`include/linux/seqlock.h`, `fs/dcache.c`),
  already used by `where_is()`: the per-result check takes a positive answer only when no rename
  happened between its lookup and its walk of the parent directories.

## 17. Inode reuse and domain records (GHSA-756j-cgwq-gvwm, journal 0216)

- `generic_encode_ino32_fh()`, `fs/libfs.c`, Linux 6.12.112: an NFS file handle pairs the inode
  number with `i_generation` (`FILEID_INO32_GEN`), the model for `domain_fresh_for()`.
- `ovl_encode_real_fh()` (`fs/overlayfs/copy_up.c`) and `ovl_verify_fh()`
  (`fs/overlayfs/namei.c`), Linux 6.12.112: overlayfs origin and index records name a lower inode
  by its file handle, not its number.
- `exportfs_encode_fid()` in `fs/notify/fanotify/fanotify.c`, Linux 6.12.112: fanotify FIDs.
- Generation of a new inode, Linux 6.12.112: ext4 `__ext4_new_inode()` (`fs/ext4/ialloc.c`,
  random); XFS `xfs_iget_cache_miss()` (`fs/xfs/xfs_icache.c`, random on v5 without `ikeep`) and
  `xfs_inode_uninit()` (`fs/xfs/libxfs/xfs_inode_util.c`, bumped on free); btrfs
  `btrfs_create_new_inode()` (`fs/btrfs/inode.c`, the creating transaction).

## 18. Write credentials and CAP_FSETID (GHSA-6xf4-r246-92r6, journal 0218)

- `setattr_should_drop_suidgid()` and `setattr_should_drop_sgid()`, `fs/attr.c:63` and `:33`,
  Linux 6.12.111 ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/attr.c)): a regular
  file loses set-user-ID on a write unless the task has `CAP_FSETID` in the initial user
  namespace (`capable()`, `:75`); set-group-ID goes with group execute unconditionally, otherwise
  unless the task is in the group or has the capability for the inode (`in_group_or_capable()`,
  `:42`). The write credentials now carry the capability only when the writer has it, so this
  test on the lower decides as for the writer.
- `dentry_needs_remove_privs()`, `file_remove_privs_flags()` and `file_modified()`,
  `fs/inode.c:2140`, `:2172` and `:2330`, same tree
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/inode.c)): `IS_NOSEC()` returns
  before any check (`:2147`, `:2179`), and `inode_has_no_xattr()` (`include/linux/fs.h:3742`)
  sets `S_NOSEC` after a check that removed nothing on an `SB_NOSEC` superblock (`:2194`). ext4
  reaches them from `ext4_write_checks()` through `file_modified()` (`fs/ext4/file.c:279`). Why
  the layer's own call cannot be relied on for a mode changed behind it.
- `backing_file_write_iter()`, `fs/backing-file.c:212`
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/backing-file.c)): the lower write
  runs under `override_creds(ctx->cred)` (`:239`) and `vfs_iter_write()` (`:243`), so the lower's
  capability tests see the credentials the layer passes.
- `ns_capable_noaudit()` and `capable()`, `kernel/capability.c:399` and `:434`
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/kernel/capability.c)): the same test
  on `init_user_ns`, the first without an audit record; used to decide whether the writer keeps
  the capability.
- `ovl_setup_cred_for_create()`, `fs/overlayfs/dir.c:556`
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/overlayfs/dir.c)): overlayfs
  prepares per-operation credentials from the mounter's (`prepare_creds()`, `:562`) and adjusts
  them for what the lower will decide; the model of the layer's write credentials (F4).

## 19. Prior art and Landlock, second search (journal 0220)

Read on 2026-10-27 and 2026-10-28 for the update of `docs/research-originality.md` (§10) and
question 21 of `docs/faq.md`. Licenses as stated by each repository.

- Shawn Wanxiang Zhong et al., YoloFS, ACM SOSP 2026
  ([arXiv 2604.13536v5](https://arxiv.org/abs/2604.13536)), already listed in §2: now read; no
  attributes, index or query, per-path access rules.
- [KiwiFS](https://github.com/kiwifs/kiwifs) (BSL-1.1): a user-space knowledge filesystem with a
  rebuildable metadata index and queries; the closest service in function.
- [OpenViking](https://github.com/volcengine/OpenViking) (AGPL-3.0), `docs/en/concepts/15-acl.md`:
  retrieval filtered by the caller's application identity; the closest service in authorization.
- Shi, Mei et al., "From Commands to Prompts: LLM-based Semantic File System for AIOS"
  (LSFS), ICLR 2025 ([arXiv 2410.11843](https://arxiv.org/abs/2410.11843)): agent file operations
  with keyword retrieval in user space.
- [RAGFS ADR-012](https://github.com/gianlucamazza/ragfs/blob/main/docs/adr/012-native-filesystem-vs-semantic-volume.md):
  a FUSE project's recorded rejection of a kernel driver.
- `searchfs()` in Apple's [HFS sources](https://github.com/apple-oss-distributions/hfs)
  (`core/hfs_search.c`, APSL): a kernel metadata query that checks the caller per result.
- `BTRFS_IOC_TREE_SEARCH_V2`, `fs/btrfs/ioctl.c`
  ([v6.12.112](https://github.com/gregkh/linux/blob/v6.12.112/fs/btrfs/ioctl.c)): a privileged
  search of filesystem trees by key.
- [Landlock documentation](https://docs.kernel.org/userspace-api/landlock.html) (warning on
  actions it cannot restrict yet, among them `stat` and xattr calls) and
  [landlock-lsm/linux issue 11](https://github.com/landlock-lsm/linux/issues/11), "File metadata
  control", which tracks the patch series for a metadata access right; `security/landlock/fs.c`
  of Linux 7.3 registers no `inode_permission` or xattr hook. Why a query under Landlock follows
  `getxattr`, and what the layer should honor when that right lands (journal 0220).

## 20. Lookup errnos, getattr, flush and build checks (issue #119 M4 and M8, journal 0221)

Read on 2026-10-28 in the upstream 6.12.112 tree the KASAN kernel is built from, unless stated.

- `Documentation/filesystems/vfs.rst`, `lookup`: an error only on a real error, since a negative
  dentry is what lets `create(2)` and `mkdir(2)` proceed; a name that exists but cannot be
  represented is such an error.
- `ovl_lookup_single()`, `fs/overlayfs/namei.c:249`, and `ovl_dentry_weird()`,
  `fs/overlayfs/util.c:201`: a lower object overlayfs does not support (automount, hashed or
  compared names) fails the lookup with `EREMOTE`, without a log line. The same function in
  Linux 7.3 ([source](https://github.com/torvalds/linux/blob/master/fs/overlayfs/namei.c),
  `:254` and `:269`): "parent wrong casefold" is `ESTALE`, "child wrong casefold" is `EREMOTE`,
  both with `pr_warn_ratelimited()`. The errnos the layer now returns; the log line, once.
- `ovl_mount_dir_check()`, `fs/overlayfs/params.c:307`: overlayfs 6.12 refuses any layer whose
  superblock has an encoding (`sb_has_encoding()`); not followed, as Linux 7.3 relaxed it to one
  casefold state per layer (§ "Casefolded lower directories" above) and the layer serves the
  ordinary directories of such a filesystem.
- `fs/namei.c:1586` (`handle_mounts()` with `LOOKUP_NO_XDEV`, `openat2(RESOLVE_NO_XDEV)`): a
  lookup that may not cross into another filesystem fails with `EXDEV`; the errno the layer keeps
  for a btrfs subvolume.
- `vfs_getattr()` and `vfs_getattr_nosec()`, `fs/stat.c:196` and `:136`; `ovl_do_getattr()`,
  `fs/overlayfs/overlayfs.h:421`; `ecryptfs_do_getattr()`, `fs/ecryptfs/inode.c:1011`: the lower
  security hook unless the call carries `AT_GETATTR_NOSEC`, which `vfs_getattr_nosec()` adds to
  every `->getattr` call (`:166`).
- `ovl_flush()`, `fs/overlayfs/file.c:582`, with `ovl_override_creds()`; `ecryptfs_flush()`,
  `fs/ecryptfs/file.c:309`, without.
- `vfs_fileattr_set()`, `fs/ioctl.c:668`, which takes `inode_lock()` on the inode it is given
  (`:681`); `ecryptfs_fileattr_set()`, `fs/ecryptfs/inode.c:1132`, and `ovl_real_fileattr_set()`,
  `fs/overlayfs/inode.c:640`, which call it on the lower dentry as the layer does.
- `include/linux/backing-file.h:22` (`const struct file *user_file` in 6.12.112) and the
  stable series that changed it,
  "[PATCH v3 stable/linux-6.12.y 2/3] lsm: add backing_file LSM hooks"
  ([LKML](https://lkml.iu.edu/2606.3/07613.html)); the v6.12 tag takes `const struct path *`
  ([source](https://github.com/torvalds/linux/blob/v6.12/include/linux/backing-file.h)).
  `static_assert()` and `__same_type()`, `include/linux/build_bug.h` and
  `include/linux/compiler.h`.
- `path_noexec()`, `fs/exec.c:112`, used by `do_open_execat()` (`:912`), `do_mmap()`
  (`mm/mmap.c:417`) and `do_faccessat()` (`fs/open.c:503`); `may_open_dev()`, `fs/namei.c:3385`;
  `SB_I_NOEXEC | SB_I_NODEV` set by binfmt_misc (`fs/binfmt_misc.c:990`) and pidfs
  (`fs/pidfs.c:404`).
- `pr_info_once()` and `pr_warn_once()`, `include/linux/printk.h`: a message a caller can cause
  at will is logged once per module load; each occurrence stays visible through the errno and the
  index's tracepoints (`moobfs_index_incomplete`, `moobfs_domain_build`, journal 0102).

## 21. Term padding, the index's privilege test and extended ACLs (ADR-0026, journal 0222)

All in the Linux 6.12.112 tree used by the KASAN build
([source](https://github.com/gregkh/linux/tree/v6.12.112)).

- `Documentation/userspace-api/ioctl/ioctl-number.rst:173-177`: magic `'M'` registered by
  `linux/soundcard.h` (all numbers), `mtd/mtd-abi.h` 01–16, `megaraid_sas.h` 01–03 and
  `fsl-diu-fb.h` 00–0F; why the magic is shared, and kept (ADR-0026 decision 1).
- `ns_capable_common()`, `ns_capable_noaudit()` and `capable()`, `kernel/capability.c:351`,
  `:399` and `:434`: the same test on the initial user namespace, `PF_SUPERPRIV` set on a grant,
  the first without an audit record on denial (`CAP_OPT_NOAUDIT`).
- `kallsyms_show_value()`, `kernel/ksyms_common.c:28`, and `cap_vm_enough_memory()`,
  `security/commoncap.c:1401`: the kernel's unaudited tests of a privilege that only widens what
  an ordinary caller gets; the precedent of `moobl_index_admin()`.
- `posix_acl_equiv_mode()`, `fs/posix_acl.c:313`: 0 when an ACL is only the three entries the
  mode bits express, 1 with a named or mask entry, `-EINVAL` for an unknown tag; its bits are
  compared with `i_mode` by the new `ext_acl()` and by `tools/ext-acl.h` and the Python scan.
- `posix_acl_permission()`, `fs/posix_acl.c:382`, and `acl_permission_check()`,
  `fs/namei.c:350`: for anyone but the owner, an inode with an ACL and group bits is judged by
  the ACL's entries, not the bits; why a minimal ACL that differs from the bits is extended.
- `posix_acl_update_mode()`, `fs/posix_acl.c:713`, called by `ext4_set_acl()`
  (`fs/ext4/acl.c:252`): an ACL equivalent to the mode is dropped on a write through the VFS, so
  a stored minimal ACL comes only from outside it.
- `struct posix_acl_xattr_header` and `struct posix_acl_xattr_entry`,
  `include/uapi/linux/posix_acl_xattr.h:29-37`, and the tags of `include/uapi/linux/posix_acl.h`:
  the form `getxattr("system.posix_acl_access")` returns, parsed by the user-space mirrors.
- e2fsprogs 1.47.2 `debugfs` (`ea_set -f`, `ea_list`; [debugfs(8)](https://man7.org/linux/man-pages/man8/debugfs.8.html)),
  which converts `system.posix_acl_access` into ext4's on-disk form: plants the minimal ACLs of
  the `minacl` suite step on an unmounted filesystem.

## 22. Index lock on instantiation and eviction (issue #117, journal 0223)

- `iget5_locked()`, `inode_insert5()`, `ilookup5_nowait()` and `iput()`, `fs/inode.c:1328`,
  `:1255`, `:1553` and `:1889`, Linux 6.12.112
  ([source](https://github.com/gregkh/linux/blob/v6.12.112/fs/inode.c)): the insertion of a new
  inode (`:1263`) and the lookup (`:1559`) both run under `inode_hash_lock`; the final reference
  is dropped by `atomic_dec_and_lock()` (`:1895`). The ordering behind the lockless test on
  instantiation, and why a link stored by a reference holder is visible at eviction.
- `xa_load()`, `lib/xarray.c:1466`
  ([source](https://github.com/gregkh/linux/blob/v6.12.112/lib/xarray.c)), and
  `Documentation/core-api/xarray.rst` ("The XArray uses RCU and an internal spinlock",
  "Takes RCU read lock: xa_load()"), same tree: a lookup in any context without the user's lock;
  the layer only tests its result for `NULL`.
- `Documentation/atomic_t.txt:169` (value-returning RMW operations are fully ordered) and
  `Documentation/memory-barriers.txt:474` and `:489` (ACQUIRE and RELEASE operations, among them
  LOCK and UNLOCK), same tree: the two orderings the argument uses.
- `ecryptfs_evict_inode()`, `fs/ecryptfs/super.c:119`; `ovl_destroy_inode()`,
  `fs/overlayfs/super.c:177`; `ovl_lookup_inode()`, `fs/overlayfs/inode.c:1102`, same tree
  ([source](https://github.com/gregkh/linux/tree/v6.12.112/fs)): two stacked filesystems that take
  no filesystem-wide lock on eviction, and overlayfs finding its inodes by key with `ilookup5()`
  instead of keeping pointers to them, the model of the proposal not taken in journal 0223.

## 23. Passthrough gaps of issue #115 (journal 0224)

Read on 2026-10-28 in the Linux 6.12.112 tree the KASAN harness builds
([source](https://github.com/gregkh/linux/tree/v6.12.112)).

- `ovl_fadvise()`, `fs/overlayfs/file.c:462`: overlayfs forwards `vfs_fadvise()` to the real
  file; the model of `moobl_fadvise()`. `vfs_fadvise()`, `mm/fadvise.c:180`, exported at `:187`.
  `ksys_readahead()`, `mm/readahead.c:677`, ends in `vfs_fadvise(..., POSIX_FADV_WILLNEED)`
  (`:694`), so `readahead(2)` follows the same hook; `cachestat(2)`, `mm/filemap.c:4462`, reads
  the descriptor's own `f_mapping` (`:4501`) and has no hook.
- `ovl_getattr()`, `fs/overlayfs/inode.c:156`: overlayfs starts from the real file's kstat
  (`:173`) and overwrites what the overlay owns; `struct kstat`, `include/linux/stat.h`, for the
  `dio_*`, `subvol` and `atomic_write_*` fields; `vfs_getattr_nosec()`, `fs/stat.c:136`, which
  sets `STATX_ATTR_DAX` in `attributes_mask` itself (`:159`, `:162`).
- `iput_final()`, `fs/inode.c:1837`, calls `->drop_inode` under `i_lock` (`:1846`);
  `generic_drop_inode()`, `include/linux/fs.h:3188`. overlayfs (`fs/overlayfs/super.c:265`) and
  FUSE (`fs/fuse/inode.c:1192`) use `generic_delete_inode`; ecryptfs (`ecryptfs_sops`,
  `fs/ecryptfs/super.c:177`) keeps the default.
- `OVL_SETFL_MASK` and `ovl_change_flags()`, `fs/overlayfs/file.c:62`; `ovl_open_realfile()`
  drops `O_NOATIME` when the mounter does not own the real inode (`:49`). `SETFL_MASK` and the
  `O_NOATIME` ownership check of `setfl()`, `fs/fcntl.c:37` and `:51`.

## 24. Paths on directory descriptors in the MCP server (GHSA-4gr9-rvgr-44qj, journal 0227)

- `may_open()` and `do_open()`, `fs/namei.c:3391` and `:3793`, Linux 6.12.111
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/namei.c)): without
  `LOOKUP_FOLLOW` the last component is not followed; a link then fails with `ENOTDIR` when
  `O_DIRECTORY` asks for a directory (`:3820`) and with `ELOOP` otherwise (`:3402`). The errors
  the walk of `moobfs.mcp` turns into refusals.
- `do_renameat2()`, `fs/namei.c:5119`, same tree: `RENAME_EXCHANGE` swaps two names atomically;
  the test uses it to swap a directory of the path and a link while the agent works.
- `fifo_open()`, `fs/pipe.c:1107`, same tree
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/pipe.c)): a read-only open of a
  FIFO without a writer waits for one (`wait_for_partner()`, `:1162`) unless `O_NONBLOCK` is set
  (`:1157`). Why `read` opens with `O_NONBLOCK`.
- `task_cap()`, `fs/proc/array.c:311`, same tree
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/proc/array.c)): the `CapEff` line
  (`:328`) that `check_identity()` reads.
- `build_open_flags()`, `fs/open.c:1221`, same tree
  ([source](https://github.com/gregkh/linux/blob/v6.12.111/fs/open.c)): `openat2()`'s
  `RESOLVE_NO_SYMLINKS` and `RESOLVE_BENEATH` (`:1331`, `:1333`), the one-call form of the same
  walk, not taken because Python's `os` module does not offer `openat2()`.
- Python's `os` module, [paths relative to directory descriptors](https://docs.python.org/3/library/os.html#dir-fd):
  `dir_fd` on `open`, `mkdir`, `stat`, `rename` and `unlink`, which the walk and `publish()` use.

## 25. Namespace labels of batched LangGraph operations (GHSA-3wx4-fh9w-2v66, journal 0227)

- `langgraph/store/base/__init__.py` of `langgraph-checkpoint` 4.3.0
  ([PyPI](https://pypi.org/project/langgraph-checkpoint/4.3.0/)): `_validate_namespace_labels()`
  (line 1325) is the rule (strings, not empty, no period); `BaseStore.put()` (`:866`) and the
  other public methods apply it before batching, and `validate_op_namespace()` (`:1342`) asks
  stores that serialize namespaces to apply it to every operation they execute. The store's
  check repeats that rule.
- The same file in `langgraph-checkpoint` 4.1.0 and 4.2.0 has no `validate_op_namespace()`, and
  `langgraph` 1.2.0, the extra's minimum, requires `langgraph-checkpoint>=4.1.0,<5.0.0`
  ([PyPI JSON](https://pypi.org/pypi/langgraph/1.2.0/json)); why the store does not call it.
- Python's [`urllib.parse.quote()`](https://docs.python.org/3/library/urllib.parse.html#urllib.parse.quote)
  never quotes `.` (an unreserved character), so `_enc()` leaves `.` and `..` as directory names.

## 26. Fair shares of the index budget (ADR-0027, journal 0228)

- `effective_protection()`, `mm/page_counter.c:335`, Linux 6.12.112 (the tree the KASAN harness
  builds; [source](https://github.com/gregkh/linux/blob/v6.12.112/mm/page_counter.c)): memcg's
  `memory.low` protection is `min(usage, setting)`, reclaim takes only usage above it, and
  protection a group does not use stays available to its siblings ("work-conserving"). ADR-0027
  applies the same rule with an equal setting per uid and a build's release in place of reclaim.
- D. Bertsekas and R. Gallager, *Data Networks*, 2nd ed., Prentice Hall, 1992, §6.5.2 (max-min
  fairness): no user's allocation grows at the expense of a user with a smaller or equal one.
- A. Cidon, D. Rushton, S. M. Rumble and R. Stutsman, "Memshare: a Dynamic Multi-tenant
  Key-value Cache", USENIX ATC 2017
  ([paper](https://www.usenix.org/conference/atc17/technical-sessions/presentation/cidon)):
  memory a tenant does not need is given to others and taken back on demand.
- Q. Pu, H. Li, M. Zaharia, A. Ghodsi and I. Stoica, "FairRide: Near-Optimal, Fair Cache
  Sharing", NSDI 2016 ([paper](https://www.usenix.org/conference/nsdi16/technical-sessions/presentation/pu)):
  the isolation guarantee of a shared cache. Both evict per item; the index releases whole
  domains, so ADR-0027's guarantee is per domain.

## 27. DKMS, Debian packages and module signing (ADR-0028, journal 0229)

- `dkms` 3.2.2-1~deb13u1 (Debian 13; [upstream](https://github.com/dkms-project/dkms)):
  `read_conf()` in `/usr/sbin/dkms` (line 815) sets `build_exclude` when `uname -r` does not match
  `BUILD_EXCLUSIVE_KERNEL`, and the build exits 77 before any header is looked for;
  `/usr/lib/dkms/common.postinst` (line 248) treats 77 as "not built". `prepare_mok()` (line 1185)
  signs with `/var/lib/dkms/mok.key` and `mok.pub` and generates the pair once when missing.
- `dh_dkms` from `dh-dkms` 3.2.2 and `debhelper` 13.24.2: installs `debian/<package>.dkms` as
  `dkms.conf`, replaces `#MODULE_VERSION#` (`-V`), and adds the `postinst-dkms` and `prerm-dkms`
  autoscripts of `/usr/share/debhelper/autoscripts/`.
- `Documentation/admin-guide/module-signing.rst`, Linux 6.12: signature verification of modules,
  `CONFIG_MODULE_SIG`, and the refusal (`EKEYREJECTED`, "Key was rejected by service") of a module
  whose signature is missing or made by a key the kernel does not trust under enforcement.
- `Documentation/admin-guide/LSM/lockdown.rst` (kernel_lockdown(7)), Linux 6.12: the integrity
  mode, which a Debian kernel enables when it boots under UEFI Secure Boot, requires signed
  modules.
- shim 16.1 (`shim-signed` 1.51~1+deb13u1; [upstream](https://github.com/rhboot/shim)): MOK
  enrolment through `mokutil --import` and MokManager; enrolled keys live in the `MokList`
  variable with owner `SHIM_LOCK_GUID` 605dab50-e046-4300-abb6-3dd810dd8b23, and are passed to
  the kernel's machine keyring.
- `virt-fw-vars` from `python3-virt-firmware` 24.11-2
  ([upstream](https://gitlab.com/kraxel/virt-firmware)): `--add-mok GUID FILE` adds a certificate
  to the `MokList` of an edk2 variable store; how `make package-test` enrols the guest's key
  without MokManager's console.
- OVMF 2025.02-8+deb13u1 (`ovmf`): `OVMF_CODE_4M.secboot.fd` with `OVMF_VARS_4M.ms.fd`, the
  variable store with the Microsoft keys enrolled, which accept Debian's signed shim.

## 28. The moobfs CLI without the mount record (issue #167, journal 0230)

- `find_mount_point()`, `src/find-mount-point.c`, GNU coreutils v9.5
  ([source](https://github.com/coreutils/coreutils/blob/v9.5/src/find-mount-point.c), GPL-3.0):
  the mount point of a file is found without the mount table by walking up `..` while `st_dev`
  stays the same; `moobfs info` does the same on the real path when `/proc/self/mountinfo`
  cannot be read.
- [Landlock documentation](https://docs.kernel.org/userspace-api/landlock.html), already listed
  in §19: `stat` is among the actions Landlock cannot restrict yet, and
  `LANDLOCK_ACCESS_FS_IOCTL_DEV` (ABI 5) covers ioctls on device files only; `statfs` was seen
  to work under the sandbox of `tests/landlock_run.c` (journal 0230). A sandbox that may list a
  domain directory can therefore still recognise the layer and issue `QUERY2` on it.
