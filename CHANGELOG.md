# Changelog

## Unreleased

- Tool output (journal 0213): `moobfs-check` escapes every string of its JSON document, not only
  paths; an invalid attribute name or a directory name inside a `left_out` reason could forge
  problem entries or break the document. `moobfs-index --uid` refuses a non-numeric uid and, as
  root, drops supplementary groups (so does the test client `tests/query.c`). `moobfs query` and
  the SQLite table refuse a result count above the request's limit and treat a path without its
  terminator as unknown.

- Python library input checks (journal 0212): a `Term` validates any `Value`, including one
  built by hand, before its payload is copied into the request (a wrong-sized payload wrote past
  the term's 1024-byte field); integers and times outside 64 bits raise `ValueError` instead of
  `struct.error`; `publish()` and `layer_mounted()` no longer start `ldconfig` or a compiler to
  find the C library; the scan judges the directory itself, not a symbolic link to it.

- Harness hygiene (journal 0211): the manual KCSAN and arm64 KASAN runs refuse a `steps` input
  that is not a list of suite step names, since it is quoted inside the guest's command line;
  `make uninstall` removes each installed path as one word, so a `PREFIX` or `DESTDIR` with a
  space no longer removes other files; the syzkaller image is mounted with `nosymfollow` while
  root writes into it.

- Security batch 1 (journal 0210). Issue #113 fixed: a domain record and a fallback mark are
  dropped when their directory is removed (reference-counted records freed with RCU, removal
  with tried locks), and `QUERY2` or a chmod through a removed directory's descriptor records
  nothing (`ENOENT` for the query). New suite step `domainrecs`; its regression test fails
  without the fix (200 records after 200 domains on btrfs). Issue #118 closed: over a lower
  slowed by `dm-delay` the build completes up to 200 ms per I/O and stops at the deadline with
  `ETIMEDOUT` and no partial answer at 400 ms (`bench/client/issue118_slow_lower.py`).

- Faster check cycle (journal 0209): a kernel correction runs `checkpatch`, the suite steps it
  touches and its negative round, plus KASAN on those steps when it touches locking or lifetime;
  the full `make vm-test` and full KASAN run once per batch (`AGENTS.md`). The KASAN job now runs
  only for kernel-side paths and caches the built kernel.

- MCP adapter (roadmap C7, ADR-0024, journal 0208): `python -m moobfs.mcp --root <dir>` serves
  `info`, `query`, `read`, `write` and `delete` over stdio, as the agent's own uid; it refuses
  root and setuid, has no network transport, and refuses absolute paths, `..` and symbolic
  links. On the dedicated host, through stdio servers started as two users, its answers equal
  the library's for 143 random requests per domain, each user is refused on the other's domain,
  a shared domain works both ways, and a start as root is refused.

- Python client library, phase 2 (roadmap C5 done, journal 0207): `moobfs.langgraph.MoobStore`,
  a LangGraph store on the library with the files of the G1 adapter (either reads the other's).
  `bench/client/g1_mounted.py --store package` runs G1's mounted gate on it; it passed three
  times on the dedicated host. By a protocol amendment accepted before the run, the no-module
  case expects `salary.json` absent for uid 1002 as well, as `QUERY2` does. `Domain.stats` counts
  `QUERY2` calls and walks.

- Security operations (roadmap S22, journal 0205), from the OSTIF best-practices guide:
  a safe-harbor section in `SECURITY.md`; `security.txt` under the project site, checked by
  `scripts/check-site.py` (fails once it expires) and published with hidden files on; and
  `docs/security-operations.md` with access read from the GitHub API, an incident procedure,
  advisory sources, third-party components and the SBOM export. Dependabot
  security-update pull requests enabled for this repository. Nothing changed at
  organization level.

- Debian 6.12.113 supported (issue 142, journal 0206): fstests `quick` with the exclusion list
  passed on ext4, btrfs and XFS (631 tests each, 0 failures, no kernel diagnostic) with that
  kernel booted in the guest by the new `MOOB_KVER` of `vm/fstests-local.sh`.
  `scripts/supported-kernels` lists it, so `make install` accepts a fresh Debian 13 install
  from the 2026-10-21 cloud image as it is; `make install-test` now installs on the image's own
  kernel and checks the refusal with an unlisted release.

- Python client library, phase 1 (roadmap C5, ADR-0023, journal 0204): package `moobfs` in
  `python/`, LGPL-2.1-or-later, standard library only, installable with `pip`, not on PyPI.
  Typed attributes with the layer's validation, publishing by the contract's write sequence
  (an existing name refused unless `replace=True`), and `Domain.query`/`batch` through `QUERY2`
  or, without the module, a walk that applies the kernel's matching and homogeneity rules. In
  the suite's new `pylib` step (KASAN job) the two backends gave equal answers for 400 random
  requests and 60 batches on the dedicated host; `make pylib-test` runs in CI. The LangGraph
  store and G1's gate move onto it in phase 2.

- CI: every `github/codeql-action` step moves to v4.39.1 together (replaces
  Dependabot PRs 131 to 133, which bumped one step each and so failed CodeQL's
  same-version check). Dependabot now groups those actions in one PR.

- G5 hybrid-retrieval package (journal 0201): `bench/client/g5_retrieval.py`
  and `g5_mounted.py` filter with `QUERY2`, then rank with exact
  `all-MiniLM-L6-v2`, SQLite FTS5 BM25 and reciprocal rank fusion, against
  the same engines over plain files with a metadata table and an oracle that
  reads no index. The mounted gate passed on the dedicated host: identical
  top 10 on both paths and the oracle, 0 results outside the eligible set, 0
  revoked turns returned, fusion `recall_all@10` 0.845 against lexical's
  0.809, Q1 p95 at most 0.135 s and Q3 p95 at most 1.525 s in 3 runs. A
  25,000-file domain needs `index_build_work=65536`. No change to the layer,
  the UAPI or the G1 adapter.

- G5 protocol amendment (journal 0200, protocol section 13), accepted as
  written: LongMemEval labels (110 instances, one file per turn), exact
  `all-MiniLM-L6-v2`, SQLite FTS5 BM25 and reciprocal rank fusion, the
  MoobFS path against the same engines over plain files with a metadata
  table, and bounds for identical answers, authorization, a relative
  relevance floor and latency.

- Debian 6.12.113 test infrastructure (issue 142, journal 0203): `make vm-test`
  accepts `MOOB_DEBIAN_KERNEL=6.12.113`, with image and headers pinned to
  `6.12.113-1`. A separate CI job runs the same suite while the existing
  `Native Linux VM` check keeps 6.12.111 as its default. The suite passed on
  6.12.113 in PR 148's CI; the fstests runs are pending, so 6.12.113 is not yet
  supported or accepted by `make install`.

- Tool JSON schema (issue #7, journal 0202): `moobfs-index --json` emits one
  document with `dir`, `count` and ordered `{ino, path}` results; its default
  tab-separated output is unchanged. `docs/tools.md` records the fields and
  experimental stability of this document and the existing `moobfs-check` JSON.
  A Linux host test validates both documents and their required types.

- Container recipe (roadmap C3, journal 0199): `docs/container-integration.md`
  gives each Docker or Podman container a bind mount of its agent's domain
  directory under the owner's uid; `QUERY2` answers through it, `GET_INFO`
  does not. A read-only bind of the whole root, a remapped user namespace,
  Podman's `:idmap` (the kernel refuses the idmapping; Podman falls back to a
  plain bind with no translation) and rootless Podman with `--userns=keep-id`
  were tested with `make container-test`, a new target that reuses the
  install-test VM; `MOOB_HOST_QEMU=1` runs either test under KVM on a Linux
  host. The not-implemented lists no longer name a CLI (C4 shipped one).

- `moobfs` command-line tool (roadmap C4, journal 0198): `moobfs info`,
  `moobfs query` (QUERY2 with the cursor followed, or the mount-wide QUERY
  without domains; text or JSON; paths sorted like `moobfs-index` without
  `--order`) and `moobfs check`, with an explicit message and a sysexits(3)
  status for each refusal. Installed by `make install`; tested in the VM
  suite as two users against the oracle (`CLI_PASS`, `CLI_E2BIG_PASS`) and
  in `make install-test` (`CLI_QUERY`). User space only; no new ioctl.

- First contact and installation (roadmap C1 and C2, journal 0197): the
  READMEs open with a short warning, "Is it for you?" and "Ways to try it";
  the status detail moved to `docs/status.md` (and its Portuguese mirror).
  New `docs/operations.md`, `sudo make install` / `make uninstall` (refuse
  kernels outside `scripts/supported-kernels`, never overwrite without
  `FORCE=1`) and `make install-test`, which followed the guide in a
  disposable Debian 13 VM and ended in `MOOBFS_INSTALL_PASS`. Finding: a
  fresh Debian 13 runs 6.12.113, outside the kernel list (issue 142). The site
  guide gains "Install on a dedicated test host"; "never load the modules on
  your host" became "on a machine used for anything else". Before merge,
  `install.sh` was corrected: it replaced a dangling link, wrote through a
  pre-existing fixed temporary name and could replace an entry created after
  its check. It now refuses any existing entry without `FORCE=1`, copies to a
  `mktemp` name and links into place; new `make install-unit` test in CI.
  No module change.

- Roadmap: a user-space "Consumable path" (items C1–C7: first contact,
  install and mount, container recipe, `moobfs` CLI, client library, DKMS
  and packages prepared in VMs, optional agent adapter) and ADR-0022, which
  keeps the kernel, UAPI and format freeze and lets these items proceed one
  by one. Packages are not published before the stage 2 gates (journal
  0196). Documentation only.

- G4 protocol amendment proposed (journal 0194, protocol section 12): a
  per-uid derived index (one vector per memory, SHA-256 content version,
  deterministic stand-in model) in a `0700` cache outside the data
  directory, kept by a state-based inotify watcher with full passes at
  start and on queue overflow. Eleven scenarios and proposed bounds:
  inventory equal to a fresh scan, nothing obsolete served, revocation and
  cache protection, live freshness p99 at most 1 s, recovery within 60 s.
  Accepted by the maintainer the same day.

- G4 derived-index package (journal 0195): `bench/client/g4_derived.py`
  (watcher, reconciler, exact derived search) and `bench/client/g4_mounted.py`.
  On the dedicated Debian host, ext4, three runs passed every bound of
  protocol section 12.6: inventories equal to a fresh scan after each of
  E1–E11, nothing obsolete served, revocation and cache protection held,
  live freshness p99 0.098–0.444 s (max 0.454 s), recovery after restart,
  queue overflow, model change and lower-tree writes in 0.77–1.08 s. No
  change to the layer, the UAPI or the G1 adapter. G5 not started.

- Merged per-mount `index_build_work` policy (PR 138, ADR-0020,
  journals 0187 and 0193):
  retain the 32,768-unit default; let a mounter choose 1,024–65,536
  cumulative work units. A dedicated VM opt-in test admitted a valid
  17,000-file attributed domain at the upper bound while the default
  refused it. The complete native VM and KASAN/UBSAN/lockdep suites
  passed locally; all twelve PR checks passed, including Native Linux VM
  and KASAN/lockdep. The deadline and admission limits are unchanged;
  issue 118 stays open for broader evidence.

- Budget release (journal 0192, ADR-0021): a build that does not fit
  `index_budget` may release only a domain last built by the same filesystem
  uid. On the dedicated Debian host, ext4 and `index_budget=1M`, uid 1002's
  search of a second 2 000-file domain returned `ENOMEM` and left uid 1001's
  domain built (`domain_builds` stayed 1, `domain_releases` stayed 0).
  Uid 1001's following search did not rebuild. Two domains of the same uid
  still released one another. The same script exited 2 on the previous
  module, which did release across uids. A first filter keyed on the real
  uid did too, because the harness keeps real uid 0. The opt-in native-KVM
  step `buildcostevict` then passed on the dedicated host
  (`BUILD_COST_CROSS_USER_HELD`, budget 1762216) and failed on the
  pre-filter module. The steps `release`, `overbudget` and `buildcostevict`
  then passed under KASAN, UBSAN and lockdep (`MOOBFS_KASAN_ONLY_PASS`).
  The complete `vm/kasan-local.sh` run of this change then exited 0
  (`MOOBFS_KASAN_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`,
  `FAULT_INJECTION_PASS`), with `RELEASE_PASS` and `OVERBUDGET_PASS` on
  ext4, btrfs and XFS. The complete native-KVM suite was not run. No reserved per-user fraction and
  no latency ceiling. G2 stays open. The shrinker can still drop any idle
  domain.

- Baseline (journal 0191): on the module recorded that day, ext4 and `index_budget=1M`,
  a second domain of 2 000 files released the first. The same release happened
  between uid 1001 and uid 1002 and between two domains of one uid. Each search
  still returned its files, and only one domain stayed built. This does not
  implement fair shares and does not pass G2. No latency ceiling was set.

- Restore drill (journal 0190): `bench/client/g3_restore.py` copies a lower
  tree with `rsync -aHAX --numeric-ids` and refuses a destination that already
  has entries. On the dedicated Debian host the G1 ext4 corpus restored with
  matching bytes, typed attributes and numeric identity. `moobfs-check` exited
  0, `QUERY2` answers matched, an illegal type byte made `moobfs-check` exit 2,
  and `user.moob.format` set to 2 refused the mount. A second run did not write.
  No new archive format and no kernel change. btrfs, XFS and a non-identity
  UID map were not part of this run.

- Adapter (journal 0189): the LangGraph store package publishes a file the
  homogeneity rule can index. A publisher who is not the domain owner cannot
  chown the file to that owner, so the mode gains other-read (`0664` on the
  root-owned `2770` domain). The first mounted run returned zero `team` hits
  with `0660` and was not a pass. The second run, on the dedicated Debian host
  (kernel 6.12.111, ext4), printed `G1_MOUNTED_PASS`: twelve shapes against
  `InMemoryStore`, two `QUERY2` pages on 5 000 files, client pages, confined
  search, `salary.json` omitted for both uids and root, plain-ext4 fallback,
  and `ENOMEM` from a separate `index_budget=1M` mount of 5 000 files. After
  the searches, `GET_INFO` reported `index_used=2883508` and `index_inodes=7000`
  inside the default 128 MiB budget. No latency claim, no VM and no KASAN in
  this run. No kernel or UAPI change. G3 is not started.

- Documentation (journal 0188): write the G6 evaluation protocol and the G1
  scope before any client package. The client is the existing LangGraph store
  adapter, with a fixed 7 000-file corpus, two OS identities, pass/fail counts
  for answer equality and integration burden, and registered bounds for the
  later G3 restore drill. No adapter, kernel, VM, benchmark or restore run was
  part of this batch. Latency, memory, freshness and relevance ceilings stay
  unset, so G2, G4, G5 and the G6 final evaluation cannot pass yet.

- Investigation (journal 0186, PR 136 merged; issue 118 open): a real
  ai-memory MCP workload wrote 2,000 requested pages in ten projects
  through the guarded layer inside a dedicated VM, with zero refused
  writes. The client created one
  additional profile page; the mirror covered all 2,001 pages and 66
  SQLite-versus-`QUERY2` comparisons passed. This bounded client shape
  does not decide the larger-domain limit; issue 118 stays open.

- Investigation (journal 0185, issue 118): dedicated native-KVM probes
  measured larger domain builds and synthetic delayed lower-device reads.
  Ten matched, alternating module pairs on cloned 17,000-file images with
  six attributes per file and a 20 ms read delay showed `E2BIG` under the
  current guard versus a full answer before the guard. The denial saved a
  paired median 17.945 ms wall and 8 KiB task reads, while using a paired
  median 13.262 ms more process CPU. The issue remains open for the
  compatibility/policy decision; no production limit changed.

- Documentation (journal 0184): reconcile the experimental issue-118 guard
  merged by PR 135 after eleven green checks, including Native Linux VM and
  KASAN/lockdep. Issue 118 remains open for the 17,000-file compatibility
  denial and residual availability limits; no additional module change.

- Documentation (journal 0183): PR 134 integrated the completed client-roadmap
  research after nine green checks, including VM and KASAN/lockdep. Its
  G-items remain proposals without implementation.

- Investigation (journal 0182, issue 118): a bounded native-KVM probe of
  unprivileged `QUERY2` on a 1,000-file ext4 domain observed a first build
  and seven post-rename rebuilds, with no build on paired warm queries.
  The first build took 14.181 ms and post-rename queries 5.818–8.868 ms
  wall time on that host; an opt-in 7,500-file probe observed 156.833 ms
  for the first build and 50.992–70.107 ms after renames. Process and
  device read counters stayed at zero until a remount-based cold-cache
  control observed 160 KiB and 1.90 MiB read on first builds of 1,000 and
  7,500 files. A two-uid probe demonstrated shared-budget eviction and
  rebuilding across private domains. The maintainer selected a resource-limit
  design; ADR-0019 adds an experimental 32,768-work-unit and ten-second
  per-build guard, plus 16 attempts per unprivileged UID and 64 per mount in
  60-second windows. Over-limit queries now return `E2BIG`, `ETIMEDOUT` or
  `EDQUOT` rather than a partial successful answer. Targeted native-KVM
  tests passed, including a negative control against the pre-limit module;
  the complete native-KVM suite and the opt-in deadline regression passed.
  The first full KASAN run exposed that the existing unprivileged fault sweep
  exceeds the new admission window; its independent fresh-domain trials now
  remount in groups of eight. The corrected complete KASAN/UBSAN/lockdep run
  passed. An opt-in fixture denied oversized builds on ext4, btrfs and XFS,
  and a synchronized directory move verified that the rescan shares the
  original work allowance. A 30-pair, order-alternated native-KVM comparison
  on a 7,500-file ext4 domain detected a +0.9% cold-query wall-median cost
  (95% bootstrap interval +0.3% to +1.8%) and unchanged median measured
  read bytes. PR 135 has since merged; issue closure remains pending. These
  are not strict CPU-time or physical-I/O-byte caps, and the
  shared index budget still has no fair per-user shares.
  A later 30-pair, fixed-frequency comparison of a cold 17,000-file query
  against the pre-limit module measured a paired median saving of 3.490 ms
  wall time, 3.370 ms process CPU and 20,480 task-attributed read bytes when
  the guarded module returned `E2BIG`; the pre-limit module completed the
  answer. This near-threshold saving is small and the legitimate denial
  remains a policy tradeoff. An opt-in VM control also completed a cold
  `QUERY2` on a 6,000-file domain with six typed attributes per file, matching
  the per-project scale of the previously measured 60,000-page client layout.
  This is a one-domain synthetic control, not a fresh end-to-end client run.
  The existing opt-in limit regression passed again after the shared fixture
  helper gained the six-attribute plant mode.
  A later bulk stage-2 client run in a KVM guest on the Mac mini completed
  60,000 pages across ten domains with the guarded module; all 60
  SQLite-versus-QUERY2 answer checks agreed. It skipped MCP writes, live
  mirroring and latency measurement. The 17,000-file denial still needs a
  compatibility decision.
  The numerical limits are still provisional.

- Documentation (journal 0183): integrate completed roadmap research from
  journals 0164–0166 after PR 130, preserving current-state records and leaving
  the proposed client packages unimplemented.

- Documentation (journal 0181): reconcile the merged PR 129, its eleven
  successful checks and closed issue 116 across current-state documentation;
  preserve the earlier publication checkpoints and the unmeasured performance
  overhead.

- Integration (journal 0179): PR 127 passed all eleven GitHub checks and
  merged the unprivileged allocation-fault test/fix and concurrent budget
  release correction. Issue 114's final T9 item was checked and the issue
  closed. Issue 116 was handled separately in journal 0180 and PR 129.

- Fix (journal 0180, issue 116): charge user-triggered index, scan and query
  allocations, including the retained `QUERY2` result buffer, to the current
  memory cgroup. Add a bounded guest regression with separate buffer and index
  assertions. The integrated full native-KVM and KASAN/UBSAN/lockdep suites
  passed locally; [PR 129](https://github.com/studiomoob/llmoobfs/pull/129)
  replaced PR 128 after its documentation commits failed the DCO sign-off
  gate. The replacement passed all eleven reported checks and merged as
  `40645e2` on 2026-10-23, closing issue 116. Controlled before/after
  performance comparison remains pending. The per-mount budget and S9 fair
  sharing remain distinct.

- Fix (journal 0179): when the least-recently-used domain is being built,
  try another eligible built domain before refusing an index-budget build.
  The native KVM concurrency test on the dedicated host failed before this
  change and passed on ext4, btrfs and XFS after it. The full reference Docker
  VM suite, full native KVM run and full local KASAN/UBSAN/lockdep run passed.
  The correction was subsequently integrated by PR 127.

- Fix/tests (journal 0178, issue 114 T9): propagate `ENOMEM` when a per-result
  permission lookup cannot decide under an injected allocation failure,
  instead of returning a successful answer that silently omits a readable
  file. Exercise unprivileged plain, ordered and batched `QUERY2` over built
  and fresh domains in the KASAN fault step. The full local KASAN and
  `make vm-test` suites passed; the correction was integrated by PR 127. An additional
  dedicated-host KVM run reproduced a separate issue 111 concurrent-budget
  failure, recorded in journal 0178 for follow-up.

- Documentation (journal 0177): reconcile the merged PR 126 and PR 75,
  closed issues 110/111 and successful pull-request checks in current-state
  roadmaps and both READMEs; preserve the dated pre-merge records.

- Documentation (journal 0176): record the locally validated issue 110/111
  correction batch in PR 126, its publication gate and the paused follow-up order in
  the roadmap and security roadmap; reconcile issue 112 as completed and keep
  issue 114's remaining T9 gap visible.

- Fix (journal 0175, issue 111, R2-7 of journal 0155): concurrent domain
  builds can no longer erase the budget-refusal signal before the failing build
  tries to release an older domain. The new `overbudget` suite step checks this
  on ext4, btrfs and XFS. Raise the emulated guest's hung-run guard to 2400 s
  after the expanded full suite reached the previous 1200 s guard.

- Fix (journal 0174, issue 110, R2-5 of journal 0155): a domain directory
  renamed between the moment a build took its name and the moment the build
  recorded the rename generation no longer leaves the domain fresh under the
  old name, which made the v1 QUERY omit its uncached files until another
  rebuild. The build records the generation sampled before the name, and
  QUERY2 reads the name from the lower dentry. New suite step `rootpath`.
- Contract/tests (journal 0173, issue 112): specify that all QUERY2_BATCH output
  must be discarded on error; there is no partial-success or rollback protocol.
  Add dedicated-VM controls for validation-before-execution, a later result-copy
  fault, a final header-copy fault and successful reuse, without module/UAPI changes.

- Tests (journal 0172, issue 114 T5/T6/T8/T10 and part of T9): make the QUERY2
  execute check decisive after a successful directory open; assert filtered
  counts and truncation; compare attributed directories with lower permissions;
  add fixed encoding expectations independent of the shared validator, and
  unprivileged checks before index release and after rebuilding. Unprivileged
  allocation-fault coverage remains deferred.

- Documentation (journal 0171, issue 114 T11): clarify QEMU verdict markers,
  explicit coverage skips, the 1200-second guard and compiled-only test programs.

- Harness (journal 0171, issue 114 T7): preserve failing producer statuses in
  logged race, buildrace and oracle checks; print race2 statistics only after
  validating the forked child. Add controlled failure and success regressions.

- Validation (journal 0170): reconcile PR 109 with current documentation and add
  VM regressions for failed rename-path allocation and removal during an index scan.
  Record its completed merge and successful published-head CI in a dated note.

- Documentation (journal 0169): add the confirmed TeleAI Awesome Agent Memory listing
  to both READMEs' external records, preserving all five existing badges.

- Documentation (journal 0168): add the Zenodo version DOI badge and full archived-snapshot
  citation to both READMEs, distinguishing commit `02381fba` from the earlier source tag.

- Documentation (journal 0167): add verified external project records to both READMEs,
  with the scope and current limits of each record; preserve the four existing badges.

- Research (journal 0166): assess established query, cache and retrieval
  algorithms against source and historical measurements; document hypotheses
  and validation plans. No algorithm adopted or implemented.

- Planning (journal 0165): add G1–G6 client integration and retrieval packages to
  ROADMAP with priorities, dependencies, effort estimates and validation gates.
  All remain planned; implementation is explicitly deferred.

- Assessment (journal 0164): evaluate client integration, derived search-index
  lifecycle, portable recovery and end-to-end validation. Recommendations only;
  native stacked architecture and the accepted roadmap sequence are unchanged.

- Coverage (journal 0154): `moobl_xattr_get` at 0 of 4 PCs in the fifth syzkaller run is explained
  by its corpus, which kept almost no attribute read inside a mounted layer. Replayed with
  per-call coverage, every such read reaches 3 of the 4 PCs. Three new seeds
  (`vm/syzkaller/seeds/xattr-get-*`) end in that read.
- Integration (journal 0153): the correction batch of review 0144 (R1, R2) and the fix of
  review 0151 (journal 0152) in one tree; every public surface states the three findings. The
  complete `make vm-test` passes. Its QEMU guard is now 1200 s; one complete run had reached
  the old 600 s under TCG on an arm64 host.
- Fix (0149): synchronize mutable file status flags with the backing open
  before delegated I/O. The clear-append regression fails without the fix and
  passes with it; clear- and set-append assertions pass in the integrated VM
  suite on ext4, btrfs and XFS.
- Fix (0150): propagate operational lower `getxattr` read errors while building
  or updating the index. Injected `EIO`/`ENOMEM` tests and the full VM suite
  pass on the correction branch; the negative branch confirms the old silent
  omission. The integrated full VM suite and local KASAN/UBSAN/lockdep run pass
  at `49098aa`. That local checkpoint preceded merge; the review batch later
  merged after its required checks passed. `v0.4.1-experimental` still
  contains both defects.
- Documentation: correct stale `MOOB_ONLY` instructions in `docs/testing.md`
  and `docs/development.md` with dated notes (journal 0149).

- Process (journal 0148): add the agreed batch cadence, issue ownership,
  client-neutral scope and reference practice to `AGENTS.md`. Keep the full
  continuity and outreach rules in the local MoobFS rules file.

- Review (0144): two correctness findings in the reviewed source. Clearing
  `O_APPEND` with `F_SETFL` still appended through the layer (reproduced in an
  ext4 VM); operational `getxattr` errors could silently omit an index entry
  (confirmed in source). Corrections are recorded in 0149 and 0150; the latter
  uses injected errors, not a physical lower-filesystem I/O failure.
- Coverage (0145): the fifth run's 88.7% aggregate meets the numeric S3 threshold,
  but `moobl_xattr_get` at 0/4 leaves its per-function criterion unresolved.
- Documentation (0146): reconcile current public surfaces with implementation and
  recorded evidence, distinguish historical pilots from repeated measurements,
  and document the review's open limitations in English and Portuguese.
- Measurement analysis (0147): the existing 1–3% p50 residual survives first-cycle
  removal; much of Q1's p99 difference is associated with the first cycle.
  Reproducible analysis of existing data, not a new benchmark or a causal attribution.

- Independent review of 3627172, round 2 (journal 0155): twelve findings, two of them
  reproduced in the VM with `tests/review-3627172-vm.sh`: a directory 65 or more components
  below the mount root is placed nowhere by the write paths, so its chmod sets no fallback and
  a value change below it is not indexed (R2-1); a file renamed inside its directory while the
  scan reads it keeps the scan's listing name (R2-2). Ten more findings from reading the code
  (partial records carried across domains, a failed upper-inode creation, and lesser ones) and
  eleven test gaps, two of them high (no test exercises an ACL on the domain directory, nor a
  write under a private intermediate directory).
- syzkaller (journal 0143): a fifth six-hour run, no report; the ioctl and attribute handlers at
  88.7 % of their PCs, above the 85 % threshold of security roadmap S3. The fix of journal 0139
  confirmed with 40 repetitions: within 1 to 3 % of module 0102 on filtered queries.
- syzkaller descriptions for the weak handlers (journal 0138; first run in journal 0143): the `QUERY2` cursor
  as resources with a continuing call `ioctl$MOOB_IOC_QUERY2_NEXT`, typed query terms and
  attribute values from short lists, and six new seeds (cursor pages, long keys, value types,
  the equality plan, per-result checks, attribute flags).
- **Review finding (journal 0151, exact target `d110d4e`):** a chmod during a domain's first
  index build can leave a stale homogeneous entry, allowing later `QUERY2` results for a file
  whose read permission has been revoked. Reproduced in the disposable VM; no fix is included.
- **Fixed (journal 0152):** the finding above. The file's own homogeneity is now checked inside
  the index load, after the loader is registered and again at every repeated reading. New suite
  step in `buildrace` (`tests/chmodrace.c`) on ext4, btrfs and XFS; the negative round fails
  without the fix.

## v0.4.1-experimental — 2026-10-20

Fifth source pre-release, the one to use instead of `v0.4.0-experimental`, whose queries are
about twice as slow as they should be (journal 0139). Same findings status as v0.4.0: F1 to F12
fixed except the fair-shares part of F2. No binaries, no packaging, no support; no outside human
review. Journal 0142.

- **Fixed:** the query slowdown of v0.4.0, found by the measurement method of journal 0134 and
  bisected to one change (a buffer for the requested limit allocated on every query); a
  casefolded lower directory is now refused (issue #95).
- **Testing:** interoperability with getfattr, fselect, cshatag, Recoll and KFileMetaData;
  every refusal of a typed attribute write and the 64-attribute limit; arm64 under emulation
  (ext4, btrfs, XFS and fault injection under KASAN); a fourth six-hour syzkaller run with no
  report and richer descriptions for the weakly covered handlers.
  *Correction, 2026-10-20:* the richer descriptions were not in this tag; they were merged after
  it, with their first run (journal 0143).
- **Measured:** the on-demand index costs 7 to 12 % on filtered queries against the index built
  at mount (40 repetitions, CPU frequency held, bootstrap intervals).

The entries below are the detail.

- Tests (journal 0141): new suite marker `TYPED_LIMITS_PASS` (`tests/layer.c typed`, once per
  lower filesystem): every refusal of a typed attribute write with its errno and an unchanged
  lower, and the limit of 64 per file (the 65th gives `ENOSPC`, replacing and removing at the
  limit, what does not count). `q2invalid` and `invalid` now check each term and header rule of
  `QUERY2` and `QUERY` next to a valid request. Paths syzkaller reaches poorly (journal 0138).
- Fixed (journal 0139): since pull request 71 every `QUERY`/`QUERY2` allocated a buffer for the
  requested limit (about 1.1 MB at the maximum), which made filtered queries 1.3 to 2.5 times
  slower; the buffer is now kept with the open file and reused. Measured with the method of
  journal 0134 (40 repetitions, frequency held, bootstrap interval): the on-demand index costs
  7 to 12 % against the index built at mount, which the 2026-10-14 report could not show.
- CI, arm64 (exploratory, journal 0137): the guest boots under emulation and the suite step ext4
  passed under KASAN twice, btrfs and XFS once, and the fault-injection step once. The boot failure of journal 0124 came from virtme-ng's x86-64-only
  `virtme-ng-init`; the job now uses the bash init, a virtio-fs root, the disks in reverse order,
  and runs the test programs from a guest tmpfs (from virtio-fs they crashed at random).
- syzkaller (journal 0138): a fourth six-hour run with F2 and F4, no report; handler coverage
  78.6 % (threshold 85 %). The attribute calls now take a file opened in the layer and
  `getxattr` by path is fuzzed (`moobl_xattr_get` had never been reached); the per-function
  coverage is fetched 20 minutes before the end, since the page takes minutes to answer.
- Fixed (issue #95, journal 0136): a lower directory already casefolded (ext4 `-O casefold`) was
  not refused, though the layer's names are case-sensitive; it is now refused, with every name in
  it and as the lower root, as overlayfs refuses layers whose casefolding differs. New suite step
  `casefold`.
- Testing (journal 0140): an interoperability test, `vm/interop-guest.sh` with the on-demand
  workflow `interop.yml` (not a required check), runs getfattr/setfattr, fselect, cshatag,
  Recoll and KDE KFileMetaData through a layer mount and ends in `INTEROP_PASS` or
  `INTEROP_FAIL: <steps>`. Passed locally (arm64) and in CI on amd64 (run 39352397079, all 14
  steps).

## v0.4.0-experimental — 2026-10-19

Fourth source pre-release, the one to use instead of `v0.3.0-experimental`, which predates the
fixes below. Every finding of ADR-0011 (F1 to F12) is fixed in it except the fair-shares part of
F2 (security roadmap S9, not started). No binaries, no packaging, no support; no outside human
review. Journal 0135.

- **Security fixes** (private advisories, journals 0116 and 0117): F9, a query could return a
  file to members of its group when only "others" could read it; F10, a query waiting on a FUSE
  mount could stall every other user's queries; F11, changes through the layer ignored a
  read-only or frozen lower; F12, an idmapped lower mount was accepted and its mapping ignored.
- **F2 (ADR-0016):** a failed index update now rebuilds its domain on the next query instead of
  leaving answers silently incomplete until remount.
- **F4 completed:** writes carry the writer's fsuid and groups, so ext4's reserved blocks go only
  to whom ext4 would grant them directly, with the default reserve owner too.
- **Index on demand (ADR-0012):** built per domain by its first query, released under the budget
  and memory pressure. UAPI: `struct moob_info` keeps its size; three counters take reserved
  space and `MOOB_FLAG_ON_DEMAND` is new; older binaries keep working.
- Six medium and low items of the second source review (journal 0120).
- **Testing:** KASAN, UBSAN and lockdep on every module change, with KVM; KCSAN weekly and on
  demand; three six-hour syzkaller runs; negative rounds showing each fix's test fails without
  the fix (journal 0131); a measurement method with tests and intervals (journal 0134).
- Still open: fair shares of the index budget (S9); issue #95 (a lower directory already
  casefolded is not refused; not reproduced yet); arm64 is exploratory (its guest panics in
  the kernel's 9p read path before the module loads); one kernel series (6.12), x86-64, in a VM.

The entries below are the detail.

- Measurement method (journal 0134, after an outside review): a difference is reported only with
  a test and a confidence interval (`bench/compare.py`: bootstrap interval, Mann-Whitney U, ECDF),
  30 repetitions, the CPU frequency held still (`bench/cpu-freq.sh`), SQLite's missing permission
  check and its cache settings stated. The 8 to 15 % of 2026-10-14 is not claimed.
- syzkaller: a third six-hour run on `main` at bf1a34b (the layer code of `main` before F2), 720 747
  executions, no report (journal 0133).
- Fixed (journal 0132, finding F4 completed): an ordinary user writing through the layer could spend
  ext4's reserved blocks with the default reserve owner; writes now run with the writer's fsuid
  and groups.
- Fixed (journal 0130, ADR-0016): an index update that failed after the lower
  write (budget full) left queries answering without the file until remount, flagged only in
  `GET_INFO`; now only that domain is built again by its next query.
- Negative rounds (journal 0131): the tests of journal 0120's items 9, 7 and 5 and the KCSAN filter
  each fail with their fix undone (or, for the filter, with a race added to the module).
- `vm/build-tests.sh` is the one list of test programs for every VM harness and workflow; the
  arm64 workflow had drifted and missed two programs (issue #72, journal 0129).
- Shell scripts in `vm/` and `scripts/` pass shellcheck at every severity, which `make lint` and CI
  now enforce (issue #5, journal 0128).
- CI (journal 0127): KCSAN over the stress steps runs on GitHub with KVM (`kcsan.yml`, on demand and
  weekly), so it no longer needs the benchmark host.
- Issues #6, #8, #9, #12, #13, #14 and #15 closed (journal 0125): tool --help and exit status
  (docs/tools.md), manual pages, fuzz seeds, site accessibility checks, compiler matrix, pin
  checker, SPDX on every source.
- Documentation audit (journal 0126): every current-state document brought up to date; the FAQ page
  generated from `docs/faq.md`; claims that stopped being true are listed in
  `docs/retired-claims.txt` and checked in CI.
- Published (journals 0111 and 0114): the two independent source reviews of commit 5582e06 that led to
  the security fixes F9 to F12 and to journal 0120, with the verification of each finding. README
  (both languages) states the review status.
- Fixed (journal 0120, written, not yet run): six medium and low items of the source review of
  journal 0114. SQL conditions under a collation other than BINARY stay SQLite's (rows were lost
  with COLLATE NOCASE); a typed attribute can be set on a write-only file; the walks that free a
  domain or the index yield the CPU; directory reads run with the mounter's credentials, as
  ADR-0011 says; ref-walk revalidation reads the lower dentry under RCU; a build reads directories
  in batches of 1024 names. New suite step `bigdir`.
- KCSAN (journal 0115, security roadmap S8): the stress steps `buildrace` and `release` ran under
  the data-race detector with no race in the module; the 35 reports were the kernel's own
  (timers, virtio, the dcache, the ext4 xattr cache). The KASAN harness now judges KCSAN reports
  apart (`vm/kdiag.sh`): one fails the run only when one of its racing accesses is in
  `moobfs_layer`.
- syzkaller over the layer (journal 0109, security roadmap S3, in progress): kernel configuration,
  syzlang descriptions of the mount, ioctls and typed attributes, seed programs and a runner
  (`vm/syzkaller/`, `vm/syzkaller-local.sh`). First runs: no report; a six-hour run in progress.
- **Security fixes** (journal 0116, findings F9 and F10 of ADR-0011, found by an independent source
  review): `QUERY2` answered members of a file's group, or its owner, a file only others could
  read (mode `0604`), without a per-result check; the homogeneity rule now asks the bit of every
  class, in the kernel and in `moobfs-index` and `moobfs-check`. A query with per-result checks
  that waited on a FUSE mount inside the domain held up every query of the mount; the checks now
  run with `query_sem` released and do not leave the mount. New suite steps `permmatrix` (QUERY2
  against the lower's own permission decision) and `stall` (a FUSE daemon that never answers).
- Fixed (journal 0117, findings F11 and F12 of ADR-0011): changes through the layer now take
  write access on the lower mount, so a read-only lower mount refuses them with `EROFS` and a
  frozen lower filesystem holds them until the thaw (ext4 used to warn); an idmapped mount is
  refused as `lower=` (compatibility decision in ADR-0011). New suite step `lowerro`.
- Fixed (journal 0118): `mount -o remount,ro` of the layer failed with util-linux 2.41, which
  sends every option of the mount again through the new mount API; the layer refused them all.
  An option repeated with its current value is now accepted, and a real change is still refused
  with `EINVAL` (journal 0106). `tests/reconf.c` covers both.
- CI (journal 0124): the big-endian run passes (60 encoding checks, 9 image tests); GitHub's arm64
  runners have no KVM, so the arm64 job runs under emulation and, by default, only the step `ext4`.
- CI (journal 0123): a big-endian (s390x, QEMU user mode) run of the attribute encoding and image
  tests; an exploratory KASAN job on arm64 runners (`kasan-arm64.yml`). Research on test
  infrastructure in `docs/research-test-infrastructure.md`.
- Project rule (journal 0122): every public surface is updated together with the project's state;
  inventory in `docs/public-surfaces.md`.
- Site (ADR-0015, journal 0119): the project's journey, its status against what the
  kernel asks of a new filesystem, open research topics and how the project is made, in English
  and Portuguese (`site/pt/`), generated by `scripts/site-journey.py`.
- CI: the KASAN workflow opens `/dev/kvm` to the runner user (the udev rule of GitHub's
  2024-04-02 changelog), so QEMU can use KVM instead of emulation; aimed at the boot reports of
  issue #18 and the job's length. Confirmed on the first run of pull request 74: `KVM usable`,
  the debug kernel boots in under a second. With KVM the guest sees the host CPU and prints
  `Speculative Return Stack Overflow: WARNING: See ...` at boot, which the harnesses took for a
  kernel warning; they now match `WARNING:` only where a kernel warning puts it, at the start of
  the message.
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
