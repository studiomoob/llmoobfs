# Frequently asked questions

Short, factual answers about MoobFS as of 2026-10-25. Each answer names the file it is taken
from; when a file and this page disagree, the file wins and this page needs a correction.
Rendered copy: [studiomoob.github.io/llmoobfs/faq.html](https://studiomoob.github.io/llmoobfs/faq.html).

## 1. What is MoobFS?
MoobFS is an experimental GPL-2.0 Linux kernel module, stacked over ext4, btrfs or XFS, that
passes POSIX operations to the lower filesystem and adds typed `user.moob.*` attributes, an
in-memory attribute index and query ioctls. It is a research project: the tags are source
pre-releases, and the last one, `v0.4.1-experimental` (2026-10-20), is a source
snapshot before the corrections in review 0144.
[`README.md`; `docs/research-originality.md` §9]

## 2. What problem does it target?

It explores storing LLM-agent memories as ordinary files while letting a program ask which
files have given attribute values, with the operating system's permissions deciding what each
caller sees. The narrow hypothesis is attribute queries with per-result POSIX authorization,
no separate database server and no second authorization policy. That hypothesis is not yet
shown to cost less than SQLite plus application-level checks.
[`docs/value-assessment.md`, "Conclusão fundamentada"; `docs/when-not-to-use.md`]

## 3. How does the stacking work?

The module mounts with `mount -t moobfs_layer -o lower=<dir> none <mount>` and delegates
file I/O to the lower directory through the kernel's backing-file helpers
(`fs/backing-file.c`), in the same family of designs as overlayfs and ecryptfs. Attributes
are real xattrs on the lower filesystem, validated by the layer on write. Layer inodes reuse
the lower inode numbers.
[`docs/decisions/0007-stacked-memory-layer.md`; `layer/main.c`; `docs/architecture-overview.md` §1-2]

## 4. What does `QUERY` do?
`QUERY2` is an ioctl on an open descriptor of a domain directory (a directory at a fixed depth
of the mount, `domain_depth=1` by default). It returns the inode number and the path relative
to that directory of files matching up to four attribute predicates joined by AND (`=`, `<`,
`<=`, `>`, `>=`, string prefix, "exists", and "missing or ..."), optionally ordered by one
attribute and paged by an authenticated cursor; `QUERY2_BATCH` sends up to eight requests in
one call. The first `QUERY` on the mount root is kept for administrators. Answers are checked
against a user-space oracle (`moobfs-index`) on ext4, btrfs and XFS.
[`include/moobfs_ioctl.h`; `docs/spec-query-v2.md`; journals 0035 to 0038]

## 5. What does `QUERY` not do?
In the kernel: no OR, no aggregation, no full-text, vector or semantic search, and at most one
ordering attribute. OR, `IN` lists, counts and grouping are available in user space through the
optional SQLite virtual table over `QUERY2` (a prototype, ADR-0013). The agent application
selects relevant memories and builds the model's context; the filesystem does not extend a
context window.
[`docs/spec-query-v2.md`; `docs/decisions/0013-user-space-query-library.md`; `docs/research-originality.md` §7]

## 6. How is `QUERY` authorized?
`QUERY2` first requires read and search permission on the domain directory. A file enters its
domain's partition of the index only if everyone who can traverse the directory can also read
it (the homogeneity rule, judged from mode bits, owner, group and ACLs); such a partition is
answered without a check per result. When a permission changes inside the domain, or when a
security module labels the files (SELinux, Smack), every result is checked for the caller
instead: the path is resolved with the caller's credentials and `inode_permission(MAY_READ)` is
required, of the file and of every directory between the queried directory and it (a
search-only directory hides its names, journal 0215). Results the caller cannot read are omitted silently, and `count`/`truncated` refer only
to visible results. A test compares answers with the lower kernel's own decision for random
modes, owners and groups (journal 0116). Path-based LSMs (AppArmor, Landlock) do not filter
results; no outside human review of the kernel code has taken place.
[`docs/spec-query-v2.md` §3; `docs/decisions/0011-threat-and-credential-model.md`; journals 0104, 0116]

## 7. What is the TOCTOU contract?

A result is not a grant of access. Authorization is checked when the query runs; permissions
or names may change afterwards, so a client must call `open()` normally and accept `EACCES`
or `ENOENT`. There is no guarantee of the absence of a time-of-check/time-of-use window. A directory
rename through the layer marks its domain stale; the next query rebuilds it, and may fail if
concurrent changes keep preventing a stable build or if its work, time or
unprivileged rebuild-frequency limit is reached (ADR-0019). Direct writes to the lower tree bypass
this tracking and require remount for guaranteed resynchronization.
[`docs/spec-m3.md` §3.1 and its dated note; `docs/decisions/0019-bound-query-build-work-and-frequency.md`; `layer/index.c`: `moobl_index_renamed`, `query2_pin`]

## 8. What is implemented today?
A stacked pass-through layer over ext4, btrfs and XFS (`fsx`, fstests' `quick` group), typed
attributes validated in the kernel, an index built per domain by the first query and released
under memory pressure, experimental per-build work and unprivileged frequency
limits, `QUERY2` with ordering, cursors and batches, the first `QUERY` for
administrators, `GET_INFO`, delegated reflink and dedupe, tracepoints, and acceptance runs with
git, concurrent WAL-mode SQLite, `dm-log-writes` power-loss replay and `dm-flakey` error
injection. A SQLite virtual table over `QUERY2` exists as a prototype. Everything is tested in a
VM. The KASAN, UBSAN and lockdep suite runs for pull requests that change module or test
inputs; documentation-only changes pass the required job without executing the suite. Fault
injection and syzkaller provide additional coverage.
[`.github/workflows/kasan.yml`]
[`README.md` "What is implemented"; `ROADMAP.md`; `CHANGELOG.md`]

## 9. What is not implemented?
A persistent index (it lives in RAM and each domain's part is built by its first query),
user-namespace and idmapped mounts (both refused, as is an idmapped mount as `lower=`), batch
reads of results, a change cursor, revisions, values returned with the results (specified by
ADR-0014, not implemented), packaging (DKMS, `.deb`), a stable UAPI, and the full
xfstests suite.
[`docs/spec-m3.md`; `docs/distribution-strategy.md`; `ROADMAP.md`]

## 10. How does it differ from SQLite next to plain files?

SQLite offers full SQL, composite indexes and decades of maturity; MoobFS offers a single
store of files plus xattrs and authorization by the OS per result. Writes to SQLite and to
the files are two operations; MoobFS writes a file and an xattr, but file/metadata atomicity
is the client's convention. Today SQLite or plain files are the recommendation for anything
outside a lab.
[`docs/value-assessment.md` "Comparação honesta"; `docs/when-not-to-use.md` §1]

## 11. How does it differ from plain files and from FUSE-based systems?
Plain files need `find` plus `getfattr`, a linear scan, with no query call. FUSE systems run
filesystem logic in a user-space process; MoobFS is a native kernel module, which gives
different trade-offs (a kernel bug can crash the host). No comparison with FUSE has been
measured; the comparison with SQLite is in question 13.
[`AGENTS.md`; `docs/value-assessment.md`; `docs/benchmark-plan.md`]

## 12. Is it a competitor to mem0, Letta or ai-memory?

No. They are memory frameworks and potential clients; MoobFS is a possible storage layer
under them. The M4 benchmark plan treats real clients (ai-memory, mem0, Letta) as workloads.
Prior art in agent filesystems such as YoloFS (2026) is acknowledged.
[`docs/benchmark-plan.md`; `docs/research-originality.md` §3, §9]

## 13. What is proven and what is still a hypothesis?
Shown in the author's VM runs: answers equal to the oracle, isolation between users and domains
on three lower filesystems, git and SQLite acceptance, and the suite under KASAN, UBSAN and
lockdep on kernel 6.12.112. Measured, pilot-grade, on one host: against SQLite with composite
indexes, `QUERY2` was 1.1 to 6 times faster on the five synthetic shapes after the optimization
of journal 0048 (the low end is a tie). Earlier, SQLite was 4 to 6 times faster on "N most
recent"; that result predates the optimization. The real client's long ordered traversal still
lost, while answers matched from 150 to 60 000 memories. A later 40-repetition module comparison
with confidence intervals found and fixed a roughly 2x regression; a residual 1 to 3 % in p50
against module 0102 remains unexplained, with larger differences in some p99 results. It did not repeat the SQLite comparison. Small-budget recovery
is tested, but default-budget saturation at scale, long-running operation and performance on
other hardware remain open. There is no independent reproduction yet (issue #68 asks for one).
[`README.md` "What was measured"; journals 0047, 0048, 0088, 0130, 0139, 0143; `docs/benchmark-plan.md`]

## 14. Which kernels are supported?
One series: Debian kernels 6.12.111 and 6.12.113 in a QEMU VM (both accepted by `make install`;
fstests `quick` passed on both with the three lower filesystems), and upstream 6.12.112 with KASAN, UBSAN and
lockdep on every pull request that touches the module and weekly. x86-64 for the module, plus an
exploratory arm64 job under emulation where the ext4, btrfs and XFS steps pass under KASAN
(journal 0137); the attribute encoding's user-space tests also run on big-endian s390x under
emulation. Kernel
APIs change between versions; compatibility with other kernels is not promised.
[`README.md`; `docs/testing.md` §1; journal 0124]

## 15. Is it production-ready? Is there a release?
It is not production-ready. It is a research project with source pre-release tags only, the last of which,
`v0.4.1-experimental` (2026-10-20), is a source snapshot before the corrections in review 0144. A module bug can cause a
kernel panic or data loss; the project's tests and two source reviews by AI assistants found
kernel-side defects, security ones among them (findings F1 to F14 of ADR-0011, all but the fairness
part of F2 fixed). Run it only in a disposable VM with synthetic data.

The [review of 2026-10-20](https://github.com/studiomoob/llmoobfs/blob/main/docs/history/0144-independent-review.md)
found stale backing-file flags after `F_SETFL` and silently skipped lower `getxattr` errors.
Both defects remain in `v0.4.1-experimental`. Corrections are on `main`.
R1 synchronizes mutable flags before delegated I/O, and R2 propagates operational read errors.
The full integrated VM suite at `49098aa` passed: clearing and setting append on ext4, btrfs
and XFS, R2 injected-error regression, and acceptance/layer suites. Negative rounds detected
both original defects. The local batch KASAN/UBSAN/lockdep suite also passed on Linux 6.12.112,
and the required pull-request checks passed before that batch merged. An injected value-read error does
not establish behavior under a real lower-filesystem I/O fault.
A later review ([0151](https://github.com/studiomoob/llmoobfs/blob/main/docs/history/0151-review-d110d4e.md))
found that a chmod during the first build of a domain could leave the file in `QUERY2` answers to
users who can no longer read it; it is fixed in
[0152](https://github.com/studiomoob/llmoobfs/blob/main/docs/history/0152-chmod-during-build-fixed.md),
with a suite test that fails without the fix.
Since that tag, `main` also bounds an unprivileged domain build.
`index_build_work` defaults to 32,768 cumulative units, and a mounter may choose
1,024–65,536 ([ADR-0020](https://github.com/studiomoob/llmoobfs/blob/main/docs/decisions/0020-configurable-query-build-work-per-mount.md),
[PR 138](https://github.com/studiomoob/llmoobfs/pull/138)). The default denies the measured
17,000-file domain. Issue 118 stays open; this is not a strict CPU or I/O cap.
See [R1 validation](https://github.com/studiomoob/llmoobfs/blob/main/docs/history/0149-sync-backing-file-flags.md)
and [R2 validation](https://github.com/studiomoob/llmoobfs/blob/main/docs/history/0150-propagate-index-xattr-read-errors.md).
[`SECURITY.md`; `docs/decisions/0011-threat-and-credential-model.md`; `CHANGELOG.md`; journal 0144]

## 16. How can I try it?

With Docker, `make vm-test` builds the tools and module and boots a disposable QEMU VM with
the pinned kernel; no host module loading is needed. Expect passing markers such as
`MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS` in `build/vm/console.log`. Do not load the
module on a machine you care about.
[`docs/development.md` §3; `docs/testing.md` §3]

## 17. How is data read without the module ("nothing trapped")?

Memories are ordinary files and xattrs on the lower filesystem. After unmounting, `grep`,
`getfattr`, `git` and `rsync` read them directly. This follows from the design and was
exercised with git and `fsx`; reading the lower directory after a specific unmount sequence
is rated "probable" in the originality audit.
[`docs/decisions/0007-stacked-memory-layer.md`; `docs/research-originality.md` §5]

## 18. What license applies?

GPL-2.0-only for the whole project.
[`LICENSE`; `README.md`]

## 19. How do I contribute or ask a question?

Start with `CONTRIBUTING.md`: reproducible bug reports, kernel or format review and
reproducibility improvements are welcome, with a `Signed-off-by` on each commit (DCO) and a
KASAN run required for changes under `layer/` or `vm/`. Questions go to GitHub Discussions
or an issue; review requests with bounded questions are listed in the community plan.
[`CONTRIBUTING.md`; `docs/community-plan.md` §8 and §13]

## 20. How do I report a security issue?

Do not open a public issue. Use the private GitHub Security Advisory channel or the contact
in `SECURITY.md`; the stated response targets are acknowledgement in 7 days and an assessment
in 30.
[`SECURITY.md`; `docs/community-plan.md` §13]
