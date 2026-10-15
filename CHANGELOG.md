# Changelog

## Unreleased

- syzkaller over the layer (journal 0109, security roadmap S3, in progress): kernel configuration,
  syzlang descriptions of the mount, ioctls and typed attributes, seed programs and a runner
  (`vm/syzkaller/`, `vm/syzkaller-local.sh`). First runs: no report; a six-hour run in progress.
- Fixed (journal 0112): a mount that failed on an allocation kept a reference on the lower root
  (finding F8, found by syzkaller); out of memory while resolving `lower=` was reported as
  `EINVAL`; the SQLite extension could drop a condition it had declared handled, on an older
  SQLite. The fault-injection test now covers the mount and checks the log after the lower is
  unmounted.
- **SQL over a domain, without a database** (prototype, ADR-0013, journal 0108): `sqlite/`, a
  SQLite virtual table that turns conditions, IN lists and ordering on a required column into
  `QUERY2` calls and leaves OR, sorting, counts and grouping to SQLite (`make sqlite-ext`). New
  suite step `sqlext` (30 statements equal to an ordinary table). Measured: the newest 20 rows in
  0.24 ms against 2.5 ms on an ordinary SQLite table; a getxattr per row (about 10 µs) where a
  column of every row is read.
- Remount (journal 0106): an option change through the new mount API (`fsconfig`) is refused
  with `EINVAL`, as overlayfs does; through `mount(2)` options are still ignored. The suite now
  tests `remount,ro` and `remount,rw` of the layer.
- **Queries no longer copy results to user space with the query semaphore held** (journal 0105,
  finding F7 of ADR-0011): a caller whose results buffer was slow to fault could hold up the
  queries of every user of the mount. Results are gathered in a kernel buffer and copied after
  the semaphore is released; `QUERY2_BATCH` releases it between requests. v1 `QUERY` checks its
  request before building anything. Stale texts removed (`modinfo` no longer says "phase 2a").
- **SELinux** (journal 0104, security roadmap S12): first run under SELinux enforcing, in a guest
  of Debian 6.12.111 with the reference policy (`vm/selinux-local.sh`). Two defects fixed:
  `QUERY2` named files whose label hid them from the caller (finding F6 of ADR-0011; every
  result is now checked for the caller when an LSM labels inodes), and labels were read with the
  caller's credentials, which a confined first caller was refused (`security.*` is now read with
  the mounter's, as overlayfs does). The layer needs the policy rule `fs_use_xattr moobfs_layer`
  (`vm/selinux/moobfs_layer.cil`). An unused include is removed (correction to journal 0100).
- Releases measured (journal 0103, `bench/release.sh`, `bench/results/2026-10-14-macmini-release/`):
  a released domain of 7 500 files is rebuilt in about 0.14 s from the page cache and 2.3 s from
  disk; under a budget smaller than the working set, a loop over the domains rebuilds on every
  query. Steady-state `QUERY2` with releases equals the module without them; against the module
  built at mount, three shapes are 8 to 15 % slower at the median, inside the spread. The suite
  step `release` now checks that a cursor resumes across a release.
- Tracepoints for the index (journal 0102, security roadmap S13): `moobfs_domain_build`,
  `moobfs_domain_release`, `moobfs_scan_reread`, `moobfs_index_incomplete`, readable by root
  through tracefs. The test-only module parameter `scan_retries` is removed.
- **Built domains are released** (journal 0101, ADR-0012 items 3 and 4): when the build of a
  domain does not fit `index_budget`, the domains queried least recently are released until it
  does; a shrinker releases built domains under memory pressure, except those queried in the
  last `index_idle` seconds (new mount option, default 60). `GET_INFO` gains
  `domain_releases`; `domain_builds` becomes 32-bit (neither was published before). The
  mount-wide `QUERY` does not release and fails with `ENOMEM` when the domains do not all fit.
  New suite step `release`.
- A btrfs subvolume below the lower root is now recognised by the device `getattr` reports
  (`st_dev`) differing from the lower root's, as advised on linux-btrfs, instead of by inode
  number 256 (journal 0100). Behaviour is unchanged for btrfs: such a directory is not entered
  (`EXDEV`) and is left out of the index; any other directory that reports another device is
  now treated the same way.
- Correction (journal 0099): ADR-0011 and `docs/research-stacked-fs-kernel-rules.md` called
  MoobFS's credential split (name operations with the caller's credentials, data operations
  with the mounter's) "the overlayfs model". It is not: overlayfs checks every access against
  both the caller and the mounter and runs the real operation, name operations included, with
  the mounter's credentials. Dated notes added; the model itself and the code are unchanged.
- **Index built on demand** (journal 0098, ADR-0012 items 1, 2, 5 and 6): nothing is scanned
  at mount; the first query on a domain builds its part of the index while the domain stays
  writable; the mount-wide `QUERY` builds the rest. On 60 000 memories in eight domains a cold
  mount went from about 20 s to 0.3 s and the index memory with one domain in use from 36.4 MB
  to 4.5 MB; the first query on a domain of 7 459 files costs 2.7 s cold and 0.1 s warm.
  `GET_INFO` gains `MOOB_FLAG_ON_DEMAND`, `domains_built` and `domain_builds`. A directory
  renamed inside a built domain now makes the next query build that domain again, which
  replaces "names stay stale until the next mount". New suite step `buildrace`; fault
  injection covers the build. Fixed on the way: a rename racing with a write could leave the
  write out of the index (in `main` since journal 0096), and a write to a file unlinked a
  moment before left a record for ever. Releasing domains under memory pressure is not
  implemented.
- Decisions after the first answers from outside (journal 0097): nested btrfs subvolumes are
  to be detected by comparing `st_dev`, as advised on linux-btrfs, instead of by inode number
  (not implemented); two items added to the security roadmap, a run under SELinux (S12) and
  tracepoints for the index (S13); `docs/references.md` gains a section of thanks.
- Fault injection (journal 0096): `tests/failnth.c` and a block in `vm/kasan-guest.sh` fail every
  allocation made on behalf of the module in turn, with the kernel's `fail-nth`, across 17
  operations, under KASAN and lockdep. Fixed what it found: an allocation failure in the walk
  from a file to its domain was taken for a path that is too deep, so a query returned `ENOTTY`
  and a write could leave a file out of its domain without declaring the index incomplete. The
  walk no longer allocates.
- Direction (journal 0095, ADR-0012 proposed): the index stays in the kernel module; its mount
  and memory costs are to be removed by building each domain's partition on demand and releasing
  partitions under memory pressure. Not implemented.
- User-space baseline measured (journal 0094, `bench/userspace/compare.sh`): on 60 000 memories
  the layer answered five query shapes 2 to 150 times faster than the generic per-attribute
  SQLite index, writes are a tie for the writer, and the baseline's follower fell behind and
  resynchronized under the bulk load. Most of the query gap is the generic schema: a SQLite
  index written for the application's schema (journal 0047) is within 1.1 to 6 times of the layer.
- User-space baseline (journal 0092): `bench/userspace/` builds the same per-domain index with
  no kernel module (plain `user.*` attributes, one SQLite database per domain directory, an
  inotify follower) and a query tool whose output equals the oracle's on 37 query shapes. The
  journal records what it does not give (validation at write time, read-your-writes, a freshness
  a reader can verify, the layer's sharing boundary) and what it gives that the layer does not.
  No latency measured yet.
- README and landing page (journal 0093) open with the problem, the three parts, four numbers
  (one where the layer loses, one that is a cost) and a demonstration transcript taken from the
  VM test; caveats moved down, none removed.
- **M4 report** (journal 0088): the four questions of the benchmark plan answered with what was
  measured, pilot-grade, including where the layer loses. New campaigns for it: latency per
  operation on the current module (2 to 5 µs more per read, 6 to 13 µs more per attribute write,
  nothing measurable on a create with `fsync`) and `bench/synccost.c`, which shows that keeping a
  side SQLite in step costs almost nothing per write in its usual mode, so write cost is not an
  argument for the layer.
- S2 (journal 0091): with `vm/fstests/exclude-quick.txt`, the fstests `quick` group ends with no
  failure over ext4, btrfs and XFS (631 tests each; 186, 280 and 307 pass, the rest are skipped
  by fstests). No kernel diagnostic.
- Tools (journal 0090): `moobfs-check --domain-depth N` lists the files that carry typed
  attributes and that `QUERY2` leaves out, each with the reason (a directory more private than
  the domain, another owner, an ACL, a hard link, a file above every domain). The specification
  promised this list and the tool did not produce it.
- Second client (journal 0089): `bench/client/langgraph_store.py`, a LangGraph `BaseStore` on a
  layer directory, gives the same answers as LangGraph's in-memory store in five runs of 3 000 to
  6 000 random operations at domain depths 1, 2 and 3, with no change to the layer. It also
  showed how easily the homogeneity rule hides every file (one `os.makedirs` call).

## v0.3.0-experimental — 2026-10-13

Third source pre-release, and the one to use instead of `v0.2.0-experimental`: it fixes the
defects listed below, which that tag carries, all found by this project's own tests and reviews in
two days (corrected on 2026-10-13: the tagged text said three). No
binaries, no packaging, no support; nothing here has been reviewed by an outside party. Journal
0087.

Fixed, present in `v0.2.0-experimental`:

- **Security:** a write to a set-user-ID file crashed the kernel (0069); set-user-ID,
  set-group-ID and file capabilities survived `copy_file_range` and clone (0074); `GET_INFO`
  showed every user's index totals to any user (0061); delegated writes could ignore quota limits
  (0069).
- **Durability:** `syncfs` on the layer did not sync the lower filesystem (0078).
- **Availability:** an attribute write under memory pressure could hang every query and
  attribute change of the mount (0082).
- **Wrong answers:** files in a btrfs subvolume inside the tree shared index records with files
  outside it (0083); a rename over an indexed file left it in the index (0083); long names
  vanished from answers (0083); a typed attribute replaced by a raw one kept answering (0083); an
  ACL read back could be the earlier one (0076).

Changes of behaviour to know before upgrading: a btrfs subvolume inside the lower tree is not
entered (`EXDEV`); an empty or over-long `user.moob.format` marker refuses the mount; `GET_INFO`
counters are zero for callers without `CAP_SYS_ADMIN` (**UAPI change**, `MOOB_FLAG_COUNTERS`);
unmounting the layer waits for a sync of the lower; `lsattr` and `chattr` now work through the
layer.

Still open and documented: on ext4 with the default reserve owner, ordinary users writing
through the layer can use root's reserved blocks (ADR-0011 F4); one index budget is shared by
all users of a mount (F2); the layer's `atime` mount options have no effect.

- Documentation (journal 0086): `docs/security-roadmap.md` and `docs/benchmark-harness.md` are in
  English, from the translations of an outside contributor (pull requests 46 and 47), put in
  place of the originals. Nothing in the repository is in Portuguese now.
- M4 question 3 (journal 0085, `bench/q3-space.sh`): on the 60 000-file data set, whole-file
  dedupe through the layer frees 6.7 % on btrfs and 13.6 % on XFS (15 % of the files are
  copies), and a reflink copy of the tree costs 47 % of a plain copy on btrfs and 9.5 % on XFS.
  The saving is the lower filesystem's; small files limit it.
- Harness (journal 0084): `MOOB_ONLY="btrfs reserve" make vm-test` runs only the named suite
  steps (101 s against 261 s) and ends in `MOOBFS_ONLY_PASS`, which no check accepts as a
  complete run. The KASAN workflow now starts on every pull request and skips its heavy steps
  when nothing the module is built or tested from changed, so it can be a required check.
- **Fixes** (journal 0083; from an outside read-only inspection of `layer/index.c`):
  - a rename over a file with typed attributes left the replaced file in the index until
    unmount;
  - a result whose name is 256 bytes or longer was dropped when results are checked one by one,
    and `MOOB_RES_PATH_TRUNCATED` was never set; it is now returned cut and marked;
  - a typed attribute overwritten by a raw one kept answering queries with the old value;
  - a failed allocation while noting a permission change of a directory left the domain
    answering without per-result checks; it now turns the checks on for every domain;
  - a dentry reference is no longer dropped with the index lock held.
- **Behaviour change** (journal 0083): a btrfs subvolume or snapshot inside the lower tree is
  not entered (`EXDEV`) and not indexed. Its files reuse inode numbers, and two files with one
  number shared an index record: setting an attribute on one removed the other from answers.
- **Fix** (journal 0082; found from an outside inspection report): allocations made with the
  index lock held could recurse into memory reclaim, which evicts inodes and takes the same lock.
  An ordinary user setting a typed attribute under memory pressure could hang every query and
  attribute change of the mount. Sections under the lock now run in a no-filesystem-reclaim
  scope, the mount scan takes the lock per attribute, and lockdep is told about the dependency.
  Not reproduced as a hang; proved with lockdep. The layer module now has a `MODULE_VERSION`.
- Decisions (journal 0081): the second real client is the LangGraph store (`BaseStore`), through
  an adapter; outside review is requested now (community action 15).
- S6 (journal 0080): `docs/compatibility.md` proposes the policy of supported kernels and of
  compatibility for the attribute encoding, the ioctl interface, mount options and tools.
  Accepted by the maintainer on 2026-10-13.
- CodeQL (journal 0080): `cpp/path-injection` is turned off in `.github/codeql/codeql-config.yml`.
  It accounted for 21 of the 22 alerts dismissed so far and describes the interface of the
  command-line tools; the file says when to turn it back on.
- File flags (journal 0079): `lsattr`, `chattr`, `FS_IOC_FSGETXATTR` and `FS_IOC_FSSETXATTR` work
  through the layer, with the caller's credentials; `statx` reports the lower's attributes and
  creation time. fstests `generic/169` and `generic/420` pass. Changing a directory's casefold
  flag through the layer is refused.
- Review of every place where the layer uses the mounter's credentials (journal 0079): no new
  defect. The layer's inodes now carry only the four lower inode flags the VFS enforces.
- Decision (journal 0079): the layer's `atime` mount options stay without effect; documented
  as a limitation.
- **Fix** (journal 0078; found with fstests `generic/674` over btrfs): `syncfs` on the layer, and
  the sync of its unmount, returned success without syncing the lower filesystem. `fsync` and
  `sync` were not affected. The layer now delegates `sync_fs` to the lower, as overlayfs does.
- S2 (journal 0077): fstests group `quick` over btrfs (271 pass, 360 not run, 16 fail) and XFS
  (298 pass, 333 not run, 16 fail), no kernel diagnostic. The reflink and dedupe tests run and
  pass. The same 15 tests fail on the three lowers (10 by design, 1 not applicable, 4 gaps);
  `generic/674` on btrfs exposed that `syncfs` on the layer did not reach the lower.
- **Fix** (journal 0076; found with fstests `generic/318`): an ACL read through the layer right
  after it was changed could be the earlier one, until a remount. Access decisions were not
  affected; they are made on the lower.
- S2, first run (journal 0075): fstests v2026.09.22 runs over the layer through a small port
  (`vm/fstests/`, `vm/fstests-local.sh`). Group `quick` over ext4: 647 tests, 178 pass, 451 not
  run, 18 fail, of which 10 by design (no execution, no device nodes), 4 gaps, 1 fixed, 1 open.
- **Security fix** (journal 0074, ADR-0011 F5; found with fstests): `copy_file_range` and a clone
  through the layer left set-user-ID, set-group-ID and file capabilities on the target, so an
  ordinary user with write access to such a file could change its content and keep them. Also
  fixed: ordinary writes to a file with capabilities failed with `EIO`.
- S4 (journal 0073): adversarial lower trees (`tests/hostile.c`) in the VM suite on the three lower
  filesystems, under KASAN, and through the tools under sanitizers in CI (`make hostile-test`).
  **Fix:** an empty or over-long `user.moob.format` marker on the lower root was accepted as "no
  marker"; it now refuses the mount. `moobfs-check` no longer passes a null array to `qsort`.
- M3 closed (journal 0072): a batch read of content is dropped and the change cursor stays in the
  client, both with measurements. `bench/readcost.c` shows that reading the attributes of a
  query's results costs far more than the query (16 µs per file against 0.9 µs per result);
  returning chosen values with the results is proposed, not built.
- Stage 2 closed with a live mirror (journal 0071): a process that only watches files keeps the
  attributes of 2 000 pages equal to the client's database as they are written, without slowing
  its writes or waking its watcher; the scale of 20 000 pages repeated on the module with the
  fixes of journals 0061 and 0069 gives the same numbers.
- `docs/client-contract.md` (M3 phase 3a, journal 0070): what any agent-memory system must follow
  to use the layer, with no client named.
- **Security fix** (journal 0069; review of journal 0066): any user writing to or truncating a
  set-user-ID or set-group-ID file through the layer, and any `chown` of one, crashed the kernel
  (`BUG()` in `notify_change`). Regression test in `tests/layer.c`.
- **Behaviour change** (journal 0069): data operations no longer carry `CAP_SYS_RESOURCE`, so quota
  limits apply to writes through the layer. ext4's reserve with the default owner (uid 0) is still
  reachable; see the README's limitations and ADR-0011, finding F4.
- `docs/research-stacked-fs-kernel-rules.md` (journal 0066): the rules a stacked filesystem must
  follow, from kernel v6.12 sources, each with whether the layer complies.
- `docs/adoption-risks.md` (proposal; journal 0067): fifteen risks any agent-memory system takes by
  adopting the layer, each with its evidence, what exists and an answer (fix, build, contain or
  accept).
- Real-client test, stage 2 (journals 0063 to 0065): ai-memory's structured queries against `QUERY2`
  with projects as domains. Tools in `bench/client/` (workload through its MCP, mirror, comparator)
  and `bench-aimem-latency`, which runs the six shapes on SQLite and on `QUERY2` in one process,
  warm and cold. At 150 and 1 905 pages every answer is equal; warm, `QUERY2` is 1.6 to 5.8 times
  faster on the filtered shapes, equal on two and slower on the incremental traversal (0.74 to
  0.86). Small scales, one run each.
- Stage 2 at scale (journal 0068): equal answers up to 60 000 pages in ten projects. Warm,
  `QUERY2` is 5 to 6.5 times faster on a set of kinds and 1.6 to 3.8 times on a family filter,
  equal on plain ordered scans, and at 0.51 to 0.71 of SQLite's speed on long ordered traversals.
  Index memory is 530 bytes per page. The large scales are loaded by `bench/client/aimem-bulk.py`
  and the client's `reindex`, because its MCP write cost grows with the store on any filesystem.
- **UAPI change** (journal 0061, ADR-0011 F1): `GET_INFO` fills the index counters only for
  `CAP_SYS_ADMIN`, flagged by the new `MOOB_FLAG_COUNTERS`; other callers get zeros, because the
  counters sum every user's memories.
- ADR-0011 (accepted; journal 0060): threat and credential model of the layer (security roadmap S1),
  with two findings: `GET_INFO` counters cross users (F1) and one index budget for all users (F2).
- `ROADMAP.md` brought up to date, planned items in the agreed order (journal 0059).

## v0.2.0-experimental — 2026-10-12

Second source pre-release: the per-domain `QUERY2` of ADR-0009 (phases Q2a to Q2e) and its
measurements on a dedicated host, the first test with a real, unmodified memory client (and the
layer bug it found), the repository in English (ADR-0010) and verified references. No binaries, no
packaging, no support; the UAPI is experimental and changed in this release (see the entries
marked **UAPI change**). Journal 0062.

- First real-client test (journal 0054, `docs/client-test-plan.md`): ai-memory v2.6.3 unchanged on
  the layer. Stage 1a, its own test suites: 1 147 pass on plain ext4 and on the layer once the
  rename fix (journal 0057) is in. Stage 1b, LongMemEval-S retrieval (50 questions): identical
  results per question; `memory_query` p50 14 vs 15 ms in one run.
- Measurement gaps closed (journal 0055): cold cache, mount time at 60 000 files and write cost,
  `bench/q2-gaps.sh`.
- **Fix** (journal 0057): renaming a file or directory over a name whose previous file was just
  deleted failed with `EINVAL` (the atomic "write a temporary file, rename it over" pattern). Found
  by running ai-memory's own tests on the layer; regression test in `tests/layer.c`.
- `vm/kasan-local.sh` (journal 0058): the KASAN/lockdep job on a local KVM host, with the kernel
  built once and cached; 17.5 minutes per run on the benchmark host against about an hour in CI.
- `docs/references.md` (journal 0053): verified list of the papers, projects and tools MoobFS builds
  on, includes, compares against or used to decide; five citation and license errors corrected in
  the research documents, the fsx notice and `docs/ecosystem.md`.
- Phase Q2e (journal 0052): `QUERY2_RACE_PASS` races mode changes, renames, unlink and
  recreate, moves across domains and `chmod` of the domain directory against paged `QUERY2` over
  the plain, equality and ordered plans; test client `query race2`.
- Everything in the repository is in English (ADR-0010, journal 0051): 82 documents, ADRs and
  journals written in Portuguese were translated in place; `README.pt-BR.md` stays as a mirror.
- **UAPI change:** with `noindex`, `MOOB_IOC_QUERY` and `MOOB_IOC_QUERY2` return `EOPNOTSUPP`
  instead of `ENOSYS` (journal 0050). checkpatch is now clean and required in CI; the oracle no
  longer calls `qsort` on an empty answer; `bench/equiv.sh` checks the oracle's own status;
  `bench-smoke` runs under ASan and UBSan in CI; `kasan.yml` fails explicitly on failure
  markers.
- QUERY2 "N most recent" (journal 0048): without per-result checks a scan chunk gathers only the
  results still missing plus one, instead of 256 candidates; attributes are matched by name node
  inside a domain. On the Mac mini the top-20 shapes got 7 to 17 times faster and now beat the
  SQLite baseline (journal 0047).
- Phase Q2d (journal 0038): term flag `MOOB_TERM_MISSING_OK` ("missing or ...", the byte
  formerly `reserved[0]` is now `flags`) and `MOOB_IOC_QUERY2_BATCH` (up to 8 QUERY2 requests on
  one domain, one permission check, all validated before any runs). Operators ending in `?` in
  `query`, `moobfs-index` and the bench query files; `query q2batch`.
- Phase Q2c (journals 0037 and 0044): `QUERY2` without `order_by` and with two or more
  equality terms finds the shortest of their inode-ordered runs by walking them in lockstep
  and scans only that one; a first version with leapfrog seeks was measured slower whenever
  the first term's run was short and was replaced. Test client `--raw` prints the kernel's
  order. Measurements on the Mac mini host (`bench/q2-campaign.sh`, `bench/q2c-compare.sh`,
  `bench/results/2026-10-12-macmini-q2/`): QUERY2 answers v1 Q3 24 to 30 times faster and Q4
  about 7 times faster, with the same inodes.
- Phase Q2b (journal 0036): `QUERY2` orders by an attribute (`order_by`, `order`,
  `order_type`, ties by inode) and pages with an authenticated 128-byte cursor (`ESTALE` for
  a cursor from another query or mount, corrupt, or issued before a fallback). Oracle
  `--order`/`--limit`, test client paging, `bench-load --order/--limit`.
- KASAN workflow: `virtme-ng` 1.41 and its dependencies are installed from
  `vm/kasan-requirements.txt` with `pip --require-hashes` instead of a version-only pin
  (journal 0046; OpenSSF Scorecard Pinned-Dependencies).
- CodeQL triage (journal 0045): every one of the 30 open alerts has a decision. Fixed in
  `bench/`: manifest fields are copied with `memcpy` of the length already checked against the
  destination (the three `cpp/unbounded-write` alerts were false positives), and record files
  and the `bench-gen` manifest are created with mode 0644 through `bench_create_out()` instead
  of `fopen`'s 0666 (unchanged under umask 022); W6 opens `drop_caches` without `O_CREAT`.
  The 20 `cpp/path-injection` alerts and the `cpp/sql-injection` alert describe the intended
  use of command-line tools and of the benchmark harness; dismissal commands for the
  maintainer are in the journal.
- Contributor experience, batch 3 (plan `docs/dx-plan.md`, journal 0041): `README.md` and
  `README.pt-BR.md` open with one sentence, a real demonstration (domain-scoped `QUERY2` as a
  regular user, results filtered by permission, v1 refused) captured from `make vm-test`, and a
  four-command quickstart, followed by the experimental notice, now compact and dated
  2026-10-11 (UAPI v2, pilot-grade measurements only); the demonstration is a new `vm/suite.sh`
  block with assertions and the `DEMO_PASS` marker. `docs/README.md` indexes every document
  with its language. `docs/assets/social-preview.svg`/`.png` for the GitHub social preview.
  Benchmark raw archives move to one release, `bench-evidence`: `bench/evidence-release.md`
  (release text and manifest), `scripts/bench-evidence-release.sh` (dry run by default;
  `--apply` uploads, verifies size and SHA-256 through the API and the public URL, then deletes
  the per-campaign pre-releases and keeps their tags; `add` for new campaigns),
  `bench/results/README.md`, links in `bench/results/*/README.md` and
  `docs/benchmark-harness.md` §7.1. The release move itself is pending for the maintainer.
- Contributor tooling, batch 2 (plan `docs/dx-plan.md`, journal 0039): `.devcontainer/`
  (Ubuntu 24.04, docker-in-docker, host tools; not yet advertised, the Codespaces measurement
  of `make vm-test` is pending); OpenSSF Scorecard workflow and README badge; CodeQL for the
  user-space C (`tools/`, `common/`, `bench/`); `make checkpatch` (`checkpatch.pl --no-tree`
  on `layer/` and `kernel/`, script pinned to `v6.12.112` and verified by SHA-256), advisory
  in the CI `Lint` job; `area/*` labels on pull requests by path (`.github/labeler.yml`) and a
  greeting on a person's first issue and first pull request; Dependabot for the devcontainer.
- Phase Q2a of the domain-scoped QUERY (`docs/spec-query-v2.md`, journal 0035): mount option
  `domain_depth` (default 1, 0 keeps the previous behaviour); the RAM index is partitioned by
  domain and by the homogeneity rule; `MOOB_IOC_QUERY2` on a domain directory answers from its
  partition with one permission check per call; `GET_INFO` reports `domain_depth`,
  `heterogeneous` and `fallback_domains` (flag `MOOB_FLAG_DOMAINS`) in former reserved space;
  the v1 `QUERY` needs `CAP_SYS_ADMIN` while domains are on; a directory renamed across
  domains or depths gets `EXDEV`. `moobfs-index --domain`, `query q2|q2errno|q2invalid|rename`,
  `bench-load --query2 1` and `--mount-opts`, and `bench/timing-v2.sh`. **Default change:** a
  mount without `domain_depth=0` restricts the v1 QUERY to administrators.
- Contributor experience (plan `docs/dx-plan.md`, journal 0032): `make help` and `make lint`
  (the CI `Lint` job now runs `make lint`, which also covers `scripts/*.sh`); SPDX check moved
  to `scripts/check-spdx.sh`; `.github/CODEOWNERS`; `CITATION.cff`; `good first issue` and
  `help wanted` documented alongside the project's own labels.
- ADR-0009 accepted (alternative E): the attribute index stays in the kernel, partitioned by
  permission domain; `docs/spec-query-v2.md` proposes the domain-scoped QUERY (UAPI v2). Research
  behind the decision: `docs/research-metadata-search.md` and addendum,
  `docs/research-academic-survey.md`, `docs/research-client-query-patterns.md`.
- `QUERY`: results are copied to the caller one at a time instead of through a limit-sized
  kernel array, and the scan of the first term starts at the first candidate and stops after
  the last one (journal 0027). UAPI unchanged. New suite block with one attribute holding
  several types.
- Dentry revalidation now answers in RCU-walk for the plain case instead of always forcing
  ref-walk, so path walks through the layer stay lockless (journal 0028).

## v0.1.0-experimental — 2026-10-10

First pre-release tag: a source snapshot of M1, M2 (phases 2a–2e), M3 phase 3b and the M4
harness, with the community and SEO files. No binaries, no packaging, no support, no
performance numbers. Everything below was previously listed as unreleased.

- `bench/`: first slice of the M4 harness: deterministic manifest generator, loader for W1-W4
  with per-call latency records, report with percentiles and dispersion, `make bench-smoke` in
  CI. Correctness only; no performance numbers. `bench/sqlbase.sh` (SQLite baseline schema,
  composite indexes, plan gate) and `bench/equiv.sh` (kernel, SQLite and oracle must agree)
  run in the VM suite over the layer. W5, W6/W7, W8, `bench/run.sh` (campaign orchestrator)
  and `bench/sqlclient.c` (W3 through libsqlite3 for the baselines) complete the harness.
- SEO/GEO files (plan `docs/seo-geo-plan.md`, journal 0024): `README.md` reorganized into the
  planned section order (banner and "At a glance" unchanged; new "How it works", "What is
  implemented, with evidence", "Limitations", comparison, "Documentation map", "Prior work and
  positioning"), `README.pt-BR.md` mirrored; new English pages `docs/architecture-overview.md`
  (layer internals with source citations), `docs/when-not-to-use.md` (comparison with plain
  files, SQLite, FUSE systems and memory frameworks; what M4 must show) and `docs/faq.md`
  (twenty sourced answers); site updated to the current state (M1, M2 phases 2a–2e and M3
  phase 3b done with evidence, M4 pending, nothing released) with the new title and
  description, the experimental banner, `rel=canonical` and Open Graph tags on every page,
  `site/faq.html`, `site/robots.txt` and `site/sitemap.xml`; stale statements fixed (`plan.html`
  said M2 was not implemented and that `QUERY` returned file handles; `guide.html` said the QEMU
  timeout was 180 s). Documentation only; no code, test or CI changes; no release or tag.
- Community readiness (plan `docs/community-plan.md`, actions 1–14, journal 0024): Contributor
  Covenant 2.1 as `CODE_OF_CONDUCT.md`; `SECURITY.md` with scope, what to include, response
  targets (acknowledge in 7 days, assessment in 30, updates every 30), coordinated disclosure,
  CVE via GitHub, credit, no bounty; `CONTRIBUTING.md` rewritten with the experimental stage,
  language policy, DCO (`Signed-off-by` required), commit style, history and scope rules, testing
  requirements, review rule (one maintainer plus one recorded external review for `layer/`),
  no performance claims without evidence; PR template checklist; issue templates revised
  (`bug_report`, `proposal`, `config`) and added (`test_failure`, `review_request`);
  `docs/labels.md`; `docs/development.md` (new) and `docs/testing.md` restructured with a marker
  table; experimental-status banner and "At a glance" in both READMEs; `ROADMAP.md` in three
  evidence states; CI `Lint` job (shellcheck, SPDX headers, DCO check on pull requests); KASAN
  workflow now also runs on pull requests touching `layer/` or `vm/`. Documentation and
  configuration only; no code or test changes.
- `docs/project-review-2026-10-10.md`: implementation and roadmap review, static QUERY
  correctness findings, configurations requiring Linux reproduction, and a proposed
  focused benchmark and agent-memory demonstration. Analysis only; no code fixes.
- `docs/research-originality.md`: prior-art and originality audit (SFS, BFS, YoloFS, SMFS and
  others; patents described, no legal opinion); claims classified as proven, probable or
  unverified, with a list of statements to avoid in public texts.
- `docs/distribution-strategy.md`: proposed deployment model (Core, Workspace, Integration),
  DKMS packaging and kernel matrix, container bind-mount limits, incremental release plan;
  nothing implemented.
- `docs/value-assessment.md`: critical assessment of real usefulness, value proposition and
  maturity as an adoption criterion; today's recommendation for production stays SQLite or
  plain files; code findings on index term selection and budget accounting recorded.
- `docs/spec-m3.md`: M3 specification proposal (identity, isolation, provenance, change cursor,
  batch reads).
- `docs/benchmark-plan.md`: M4 benchmark methodology, baselines and the real clients used as
  workloads; M3 scope gains agent/namespace isolation, batch reads and a change cursor.
- `docs/security-roadmap.md`: proposed list of security and robustness techniques for the layer
  (threat model, fstests, syzkaller, hostile lower, static analysis, disclosure policy,
  hardening, resource limits, module signing), with current state and sources; linked from
  the roadmap. Nothing implemented yet.
- Phase 2e: acceptance with git, SQLite (WAL, concurrent processes) and an own memory client;
  power-loss replay with dm-log-writes and I/O error injection with dm-flakey. Two layer bugs
  found and fixed: stale lower dentry after unlink+recreate (lost SQLite WAL frames) and a
  concurrent `set_nlink` race on unlocked attribute copies (kernel warning on CI).
- Phase 2d: in-memory attribute index built at mount and kept current on writes; `QUERY` and
  `GET_INFO` ioctls on the mount root (UAPI v1); `moobfs-index` user-space oracle; kernel answers
  verified identical to the oracle over ext4, btrfs and XFS.
- Phase 2c: typed `user.moob.*` attributes validated in the kernel (shared encoding in
  `common/attr.c`), `moobfs-attr` and `moobfs-check` tools, host unit tests and a libFuzzer target.
- Phase 2b: rename, links, symlinks (`nolinks` option), opaque xattr pass-through, delegated
  reflink/dedupe/copy_file_range and FIEMAP; validated over ext4, btrfs and XFS in the VM.
- Phase 2a of the memory layer: `layer/moobfs_layer.ko`, a native stacked pass-through
  filesystem over a lower directory, validated in the QEMU VM over ext4 with the pass-through
  acceptance test and 23,000 `fsx` operations (vendored from xfstests).
- ADR-0007 supersedes ADR-0006: MoobFS becomes a native stacked memory layer over a mature
  filesystem; `docs/spec-m2.md` rewritten accordingly, previous version preserved. Proposal only.
- Journal 0009: differentiation analysis and the "build on mature software, innovate only in
  LLM-memory features" principle; open question whether M2 becomes a stacked filesystem.
- ADR-0006 and `docs/spec-m2.md`: M2 specified as a writable POSIX volume on jbd2 + iomap with
  shared extents via `remap_file_range`; `docs/kernel-infrastructure.md` lists the reused kernel
  infrastructure verified against v6.12. Proposal only; no code.
- `docs/research-ai-memory.md`: code-level reading of akitaonrails/ai-memory and its
  relation to MoobFS; M2 now needs POSIX writes to host a real client.
- Site: interactive plan page (`site/plan.html`) showing inputs, processing and outputs per milestone.
- `docs/ecosystem.md`: survey of free software relevant to testing, crash consistency,
  kernel data structures, BFS tooling and agent memory layers.
- ADR-0005: agent memory model (typed attributes, kernel-side attribute indexes,
  ioctl queries) with the Be File System as design reference; no code changes.
- Clarified LLM memory/context as the project goal in both READMEs and the site,
  separating future agent workflows from the implemented read-only M1.
- Native Linux read-only filesystem module, offline image creator and verifier.
- Independent file inodes can share one immutable payload; FIEMAP exposes sharing.
- Versioned M1 disk format with bounded parsing, CRC32C metadata and SHA-256 content.
- Tool tests and isolated QEMU mount tests, including malformed images and read-only enforcement.
- Project history, architecture decisions and shared Codex, Claude and Cursor rules.
- GPL-2.0-only license for the entire project.
- English/Portuguese README, contribution, governance, security and conduct policies.
- Issue/PR templates, pinned CI actions and a static GitHub Pages site with a reproduction guide.

This is an experimental milestone, not a stable release or the complete transactional MVP.
