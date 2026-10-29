# 0232 — moobfs-check and moobfs-index walk by directory descriptor; udx follows ADR-0025

Date: 2026-10-29. Status: implemented on branch `claudiofp/b7-tools-walk`, not merged; for the
coordinator of batch 7. Work started: 2026-10-29 12:27 UTC; ended 14:50 UTC. Wall-clock time,
with the test-host and VM runs, not an engineering-effort measurement. Estimated effort: M
(one to three days); actual: M (one long session, about 2.5 hours, most of it waiting for test
runs on a shared host).

## Goal

Two items of issue #168:

- **Journal 0213.** `tools/moobfs-check.c` and `tools/moobfs-index.c` walked with
  `nftw(FTW_PHYS | FTW_MOUNT)` and then read typed attributes, ACLs and the directories between
  the domain and an entry by path (`llistxattr`, `lgetxattr`, `getxattr`, `lstat`). Root running
  them over a tree other users can write (a shared domain is `2770` without a sticky bit,
  `docs/operations.md`) could be led outside the tree: a directory swapped for a symbolic link
  between `nftw()`'s `lstat` and its `opendir()` by full path, or between the walk and a read of
  `dir/sub/f`, made the tools list a directory outside the root and report its files, with their
  attributes, under names inside it. The issue asked to document the limit or move to
  `openat`-relative walks; this journal does the second.
- **Journal 0215, `udx.h` sub-item.** `bench/userspace/udx.h`, the user-space baseline index,
  still applied the homogeneity rule as it was before ADR-0025 (and before journal 0222): it
  asked only the execute bit of an intermediate directory, accepted owner and group on the bits
  of others, and counted any access ACL as extended.

User space only; ADR-0022 does not apply. No change to `layer/`, `include/` or the format.

## Decisions

- **One walker for both tools, `tools/walk.h`.** Below the root no name is resolved through a
  path. Each directory's names are read once through its own descriptor (`fdopendir` on a
  `dup`); each entry is looked at with `fstatat(dir_fd, name, AT_SYMLINK_NOFOLLOW)` and opened
  with `openat(dir_fd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC)`
  (`O_DIRECTORY` for a directory); the descriptor must have the `(st_dev, st_ino)` and type the
  `fstatat` saw, otherwise the entry changed and is left out. The callback gets that descriptor
  and its `fstat`; the tools read attributes and the ACL with `fgetxattr` and `flistxattr`, and a
  directory is listed through the descriptor that was judged. So the inode judged is the inode
  reported, as in the Python scan (journal 0231) and the MCP server (journal 0227).
- **The directories between are the walk's frames.** The homogeneity rule of `moobfs-index
  --domain` and the left-out reasons of `moobfs-check --domain-depth` used to `lstat` every
  prefix of the entry's path again; they now use the `fstat` and ACL of the directories the walk
  holds (frames 1 .. level-1, the domain being frame 0 or frame `domain_depth`). The rule itself
  (`reach_ok`, `reach_why`) is unchanged; only its inputs come from descriptors.
- **Bounded descriptors.** A frame keeps its directory open for the `openat` of its entries,
  but at most 32 frames are open (`MOOB_WALK_OPEN`, the `nopenfd` the tools gave `nftw()`).
  Deeper, the shallowest open frame below the root is closed; when the walk comes back to it, it
  is reopened from the deepest open frame above, by name, `O_DIRECTORY | O_NOFOLLOW`, checking at
  each level that the `(st_dev, st_ino)` are the ones judged. A frame that fails this ends its
  listing with one line on standard error. Names are read in full when a directory is entered,
  so a closed frame needs no open stream. The walk needs about 36 descriptors at any depth.
- **Entries that cannot be opened for reading.** A FIFO, socket or device is never opened: it
  gets an `O_PATH | O_NOFOLLOW` descriptor (which opens nothing). So does a regular file the
  caller may not read, which happens when the tools run as a user other than root or with
  `--uid`: the walk by path listed its attribute names and reported "cannot read value", and that
  must stay. `fgetxattr` on an `O_PATH` descriptor fails with `EBADF` (`fdget()` refuses
  `FMODE_PATH`, references §30), so these are read through `/proc/self/fd/N`, a magic link to the
  same inode that applies the same permission check as a read by path. A process may always reach
  its own descriptors there, also after `setuid()` (checked on the host).
- **Same output for a tree that does not change.** The walker keeps what the tools got from
  glibc's `nftw()` (references §30): readdir order, a directory before its entries, the root at
  level 0; symbolic links, entries `fstatat` refuses with `EACCES` or `ENOENT`, entries on another
  device and directories that cannot be opened for `EACCES` are not passed (the tools ignored
  `FTW_SL`, `FTW_NS` and `FTW_DNR`); other `fstatat` errors stop the walk (exit 1, as before);
  trailing slashes of the root are stripped first, so a symbolic link given as `link/` is not
  walked, as `nftw()` reported it as `FTW_SL`. Paths are built relative to the root by the walker
  instead of cutting `strlen(argv[1])` bytes off `nftw()`'s path, which gave wrong paths for a
  root written with two or more trailing slashes; that is the one output change, and it was a
  defect.
- **The root is still the caller's path.** It is opened once (`O_NOFOLLOW` on the last
  component); `moobfs-check` still reads the format marker with `lgetxattr` on the argument and
  `moobfs-index --domain` still `stat`s it first for its error message. Whoever controls the
  directories above the root controls which tree is walked, as for the Python scan.
- **`udx.h` follows the oracle.** `udx_reach()` is now `reach_ok()` of `tools/moobfs-index.c`:
  read for a file, read and search for a directory, for every class the domain lets traverse,
  every class judged, and the extended-ACL decision of `tools/ext-acl.h` on the bytes of
  `lgetxattr` for entries and of `getxattr` for the domain (which `udx_open()` follows with
  `stat`). `ext-acl.h` now exposes that decision as `moob_ext_acl_bytes()`, on bytes already
  read, so the tools (descriptor) and udx (path) share it. One difference is kept on purpose: when
  the ACL read fails with `ENOENT` or `ENOTDIR`, udx judges on the `lstat` it already has, as its
  old "any ACL" test did. `udx-watch` refreshes paths while the tree is changing; a path renamed
  between its `lstat` and the ACL read would otherwise be stored as restrictive, and the first
  version of this change did exactly that (evidence). The oracle never sees a vanished path, and
  the events of the rename refresh it. The udx walk itself stays by path: it is a benchmark
  baseline, not a tool run as root over shared trees.

## Alternatives

- **Document the limit only** (the other option of issue #168). Rejected: the fix is small and
  the same class was fixed in the Python library and the MCP server.
- **`openat2(RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS)` on full paths.** It would keep the path
  reads but needs Linux 5.6 and a raw system call (glibc 2.41 has no wrapper), and a `chmod` or
  ACL read per prefix would still be per path. The per-component `O_NOFOLLOW` gives the same
  guarantee with what the tools already use.
- **Keep every pending directory's descriptor** (no reopen) or **one descriptor per level with
  no bound**. Rejected: on a 150-level tree with a 64-descriptor limit the unbounded variant
  failed with `EMFILE` (evidence); `nftw()` was bounded too.
- **Python's reopen-on-descend** (journal 0231: collect subdirectories, close, reopen by name
  with an identity check when their turn comes). Not taken here: it changes the order of the
  walk, and `moobfs-check` prints its problems in walk order, so the output would change. The
  walker descends at once, as `nftw()` did, and reopens only beyond 32 levels.
- **Open every entry `O_PATH` and read all attributes through `/proc/self/fd`.** Rejected as the
  general case: it depends on `/proc`, which the regular path does not need.
- **Skip regular files the caller cannot open.** Rejected: `moobfs-check` as a user reported
  "cannot read value" for them, and the files count would change.

## Changes

- `tools/walk.h` (new): the walker, `struct moob_node` and its `getxattr`/`listxattr`/ACL helpers.
- `tools/ext-acl.h`: `moob_ext_acl_bytes()` replaces `moob_ext_acl()`.
- `tools/moobfs-check.c`, `tools/moobfs-index.c`: walk with `moob_walk()`; reads through the
  node; `reach_why()`/`reach_ok()` take the ACL decision instead of a path.
- `tests/hostile.c`: `hostile swap <dir> <a> <b> <seconds>`, `renameat2(RENAME_EXCHANGE)` in a
  loop. `tests/tools-walk.sh` (new, in `make hostile-test`): planted links to a directory and a
  file outside and a FIFO are not followed; a race of `TOOLS_WALK_SECONDS` (default 4) during
  which `dom/sub` and a link `dom/alt -> ../../out` trade names while `moobfs-index` (plain and
  `--domain`) and `moobfs-check` (plain and `--domain-depth 1`) run in a loop, none of which may
  answer `out/f`'s attributes; a 150-level tree (two files per level) walked under
  `RLIMIT_NOFILE` 64; as a user other than root, a `0000` file still reported "cannot read
  value".
- `bench/userspace/udx.h`: `udx_reach()` and `dacl` as above. `bench/userspace/test.sh`: a `0711`
  and a `0705` directory with a matching file under domain `ns0`.
- `Makefile`: dependencies on `tools/walk.h` and `tools/ext-acl.h`; `tests/tools-walk.sh` in
  `hostile-test`.
- `docs/tools.md` (new section "Walking a tree other users can write"), `man/moobfs-check.1`,
  `man/moobfs-index.1`, `docs/testing.md` (`make hostile-test` row), `CHANGELOG.md`,
  `docs/references.md` §30, `docs/history/README.md`.

## Evidence

All on the dedicated host (Debian 13, kernel 6.12.111+deb13-amd64, glibc 2.41, gcc 14, clang),
in a copy of the branch under `~/b7a`; the old tools are `git archive 7ac52cd` (main) built the
same way. `/tmp` there is tmpfs with user extended attributes. The host was shared with other
agents' VMs, kernel builds and fuzzers during the runs (load average 12 to 20).

- **Builds.** `make attr-tools` and `make userspace-tools` with gcc and with `CC=clang` (both
  `-Werror`): no warning. `make lint`: passes (after `git init` in the copy). `shellcheck
  --shell=sh tests/tools-walk.sh`: clean. `mandoc -Tlint -Wwarning man/*` (`make man-lint`, on
  the workstation): no output. `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- **`make hostile-test`** (which now includes `tests/tools-walk.sh`), on the final tree
  (`1ac39f5`): with gcc `HOSTILE_USER_PASS: 20 trees`, `TOOLS_JSON_PASS ... (xattr fixture=1)`,
  `TOOLS_WALK_PASS: static links and FIFO, 467 rounds of 4 tool runs during a directory/link
  exchange, 150 levels under 64 descriptors and an unreadable file`; with clang the same (361
  rounds); with clang under ASan and UBSan, the CI flags and `HOSTILE_SEEDS=50`: exit 0,
  `HOSTILE_USER_PASS: 50 trees`, `TOOLS_JSON_PASS`, `TOOLS_WALK_PASS` (54 rounds), 0 lines
  matching `runtime error|ERROR: AddressSanitizer`. `make json-test`: `TOOLS_JSON_PASS`. As root
  (`sudo sh tests/tools-walk.sh build/gcc`): `TOOLS_WALK_PASS` (340 rounds; the unreadable-file
  case is skipped as root).
- `a02b448` (udx) also removed an unused helper from `tools/ext-acl.h`; `make attr-tools
  userspace-tools` with gcc and clang was run again on that tree: no warning. The runs above of
  the tools are on `1ac39f5`, whose tool code is the same.
- **Negative round.** `tests/tools-walk.sh` against the old tools (with the new `hostile` for
  `swap`), three runs as the user: `TOOLS_WALK_FAIL: 870 of 1133 rounds answered from outside the
  root ...`, `910 of 1174`, `904 of 1155`; once as root: `354 of 436`. The first leak printed
  each time is the outside file reported inside, for example `46859	dom/sub/f` from
  `moobfs-index` and `{"path": "dom/alt/f", "attribute": "Leak", ...}` from `moobfs-check`. The
  static part (planted links, FIFO) passes on the old tools too; it guards the static case.
- **Bounded descriptors, negative.** The new tools rebuilt with `MOOB_WALK_OPEN` set to 100000
  (one descriptor per level, no bound): `walk: Too many open files`, `TOOLS_WALK_FAIL: deep tree
  under 64 descriptors: 0 of 300 files`.
- **Same output as `nftw()`.** A script runs each command on the old and the new binaries and
  compares standard output and exit status byte for byte, over `hostile plant` trees (120 files)
  with a `0711`, a `0750` and a `0705` directory, a `0600` and a `0000` file, a named-user ACL on
  a file and a `g::rx` ACL on a directory, a link to a directory and one to a file, a FIFO, a
  deeper chain, every third seed owned by uid 1001; each tree before and after `hostile mutate`;
  per tree `moobfs-check` (also with a trailing slash and with `--domain-depth` 1 and 2),
  `moobfs-index` on seven names plain and `--json` with `exists?`, a conjunction, `--order` with
  and without `--limit`, `--domain` on every directory of the first two levels (with and without
  a trailing slash), and as root `--uid 1001` plain and `--domain`; plus a root that is a link, a
  regular file and a missing path. 30 seeds as the user: `CMP: 5229 comparisons, 0 differ (uid
  1000)`; 30 seeds as root: `CMP: 7869 comparisons, 0 differ (uid 0)`. A first run found one
  difference, a link given as `link/` (the old tools did not walk it, the new walk did), fixed
  by stripping trailing slashes as `nftw()` does. A 400-level tree with two side directories per
  level (so the walk reopens closed frames): `moobfs-index` plain and `--domain`, `moobfs-check`
  plain and `--domain-depth 3`, all equal (1200, 1200, 10 and 20 lines).
- **The oracle against the kernel** in the disposable VM, KVM, on the tree of `1ac39f5`'s parent
  (`4602360`'s code; `1ac39f5` only keeps an `errno` and fixes two comments):
  `MOOB_VM_KVM=1 MOOB_ONLY="ext4 btrfs xfs minacl" make vm-test` →
  `CLI_PASS`, `QUERY2_PASS`, `ISOLATION_PASS`, `HOSTILE_LOWER_PASS` and `LAYER_LOWER_PASS` for
  ext4, btrfs and XFS, `MINACL_PASS: ... moobfs-index and moobfs-check equal; python scan:
  not-run`, `MOOBFS_ONLY_PASS: steps ext4 btrfs xfs minacl (not a complete run)`, no `FAIL`
  line, exit 0. These steps compare `moobfs-index` (`--domain`, `--uid`) with `QUERY` and
  `QUERY2` and run `moobfs-check` over the lowers.
- **udx against the oracle**, `sh bench/userspace/test.sh` (privileged `ubuntu:24.04` container,
  ext4 on a loop device, 3000 memories; the test copies the sources, so each run below used the
  `udx.h` named):
  - *Old rule* (`udx.h` of `7ac52cd`, the new `test.sh`): `UDX_FAIL: after udx-index: 6 of 296
    comparisons differ`, the lines udx adds being `extra/searchonly/g`, `extra/othersonly/g` and
    the directory `extra/othersonly`; the same 6 differences at every later stage (W4 burst, both
    rounds of 400 random changes, domain rename and removal, overflow). The run was stopped after
    the overflow stage (exit 137); its differences were all the rule's.
  - *New rule, first version* (ACL read failure always restrictive), twice: `UDX_COMPARE_OK`
    after `udx-index`, the W4 burst and the first 400 changes, then `UDX_FAIL: after 400 more
    random changes: 4 of 444` and, in the second run, `2 of 444` (the same seed 8 and the same
    mutation counts both times; udx listed old names of renamed files, for example
    `1408 a21/m1133.md` instead of `1408 r2/ren8_398.md`). The old rule did not show this at that
    stage, so it was this change: a path renamed between `udx-watch`'s `lstat` and its ACL read
    gave `ENOENT`, which the new decision counted as a restrictive ACL. Fixed as above.
  - *New rule, final* (`a02b448`): every stage `UDX_COMPARE_OK` up to and including both rounds of
    400 random changes and the domain moves, then `UDX_FAIL: after an inotify queue overflow and
    the full pass it forces: 13 of 518 comparisons differ`, then `UDX_COMPARE_OK` after the
    watcher restart, the outside-link pass and the per-user watcher; `UDX_TEST_FAIL: 1 check(s)
    failed`. At that check `udx-query --status` printed `watcher=absent state=resync` (the
    earlier runs printed `watcher=running`) and the watcher's final counters equal those of the
    run where the stage passed (`rows_written=2223 overflows=2 resyncs=2`): the comparison ran
    after the first forced full pass and before the second. A second run of the final version,
    at load average about 5, gave the same picture (`22 of 518` at that stage only,
    `UDX_TEST_FAIL: 1 check(s) failed`).
  - *main* (`7ac52cd`, its own `udx.h` and `test.sh`, without the two new directories), run right
    after on the same host: the same single failure, `after the overflow: watcher=absent
    state=resync ... overflows=1`, `UDX_FAIL: after an inotify queue overflow and the full pass
    it forces: 24 of 518 comparisons differ`, every other stage `UDX_COMPARE_OK`,
    `UDX_TEST_FAIL: 1 check(s) failed`. So that stage fails on main too on this host now: the
    `settle` marker can become visible before the second resync, a timing weakness of
    `test.sh` that predates this change (the mutations only use modes `600`, `644`, `700` and
    `755` and no ACL, on which the old and the new rule agree). It is left as an open point, not
    counted as a pass: no run of `bench/userspace/test.sh` ended in `UDX_TEST_PASS` here.

## Limitations

- The root is opened by the caller's path (above), and the format marker of `moobfs-check` is
  still read by that path.
- A tree that changes during the walk has no defined answer: a directory moved elsewhere after it
  was opened is still walked under its old name, a `chmod` between the judgement and the read is
  not seen. Only the swap of a name for a link or for another inode is caught.
- Each directory and regular file is opened for reading: an `fanotify` permission watcher sees
  the opens. A regular file replaced by a device node between `fstatat` and `openat` is opened
  (`O_NONBLOCK | O_NOCTTY`) before the identity check closes it; placing a device node there needs
  `CAP_MKNOD` or a hard link that `fs.protected_hardlinks` refuses to other users.
- The `O_PATH` cases need `/proc`; without it their reads fail and `moobfs-check` reports them as
  unreadable.
- The race test is probabilistic: it shows that the old tools leak and that the new ones did not
  in the rounds run; the guarantee comes from the construction, not from the test.
- The udx walk is still by path (above). `bench/userspace/test.sh` did not end in
  `UDX_TEST_PASS` on the host, on main either: its overflow stage compares before the second
  forced resync (evidence). Not fixed here; it needs a settle condition on `state = 'synced'`
  for every domain, not on one marker.

## Next step

The coordinator merges the branch into batch 7; the batch's full `make vm-test` and KASAN suite
run once before publication. The `udx.h` sub-item and the journal 0213 item of issue #168 can be
ticked when the batch is merged.
