# 0231 — Python scan fallback walked by directory descriptor

Date: 2026-10-29. Status: merged into batch 6 with journal 0229 (C6), whose
evidence lists the batch's full `make vm-test` and KASAN runs. Work started: about 2026-10-29 09:00 UTC; ended about 09:45 UTC. Wall-clock time,
with the test-host runs, not an engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

After journal 0227 the external reviewer pointed out that the scan fallback of the Python
library (`python/src/moobfs/scan.py`) still walked the domain by path: it opened the domain
once, then called `os.scandir()` on joined path strings, `os.lstat()` on each entry's path and
`get_attrs()` (`listxattr`/`getxattr`, which follow links) on the same path. That is the class of
TOCTOU fixed in `moobfs.mcp` and `publish()` by batch 4: a shared domain is `2770` without a
sticky bit (`docs/operations.md`), so another member can swap a directory or a file for a
symbolic link between the check and the use. The walk then listed a directory outside the domain
and reported its files under the domain's names, or reported the attributes of a file outside
the domain under the name of one inside it, with the caller's rights (an existence and
attribute oracle on files the member cannot read, when the caller can). User space only;
ADR-0022 does not apply.

## Decisions

- **Walk by descriptor, as `moobfs.mcp` does (journal 0227).** Below the domain no component is
  resolved by path. Each directory is listed with `os.scandir(fd)`; each entry that `d_type`
  (or `fstatat` relative to the directory, `follow_symlinks=False`) says is a directory or a
  regular file is opened with `openat(dir_fd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK |
  O_NOCTTY | O_CLOEXEC)`; its type, mode, owner, link count and inode come from `fstat` on that
  descriptor, its access ACL and typed attributes are read through it. So the inode the rule
  judges is the inode whose attributes are reported. Links are never opened (`d_type` filter,
  then `O_NOFOLLOW` gives `ELOOP` if one was swapped in), and a FIFO swapped in does not block.
- **Descending reopens the directory by name and checks its identity.** A directory to descend
  into is closed after it is judged and opened again with `O_DIRECTORY | O_NOFOLLOW` when its
  turn comes, so the walk holds one descriptor per level, not one per pending directory (a wide
  tree would otherwise exhaust descriptors). The reopened descriptor must have the `(st_dev,
  st_ino)` that was judged and must still pass the rule (`_reach_ok` on its `fstat` and ACL);
  otherwise the directory is left out.
- **The domain itself is still opened by the caller's path**, following a link to it, as before
  (`test_scan_through_a_link_to_the_domain`): that is the caller's name for the directory
  `QUERY2` would be issued on, as the MCP server's root is the administrator's. Its read and
  search check now uses `faccessat(fd, ".", R_OK | X_OK, AT_EACCESS)` on the opened descriptor
  (`os.access(..., dir_fd=)`), where `os.access` supports `dir_fd`; the path is the fallback.
- **Same answers for a tree that does not change.** An entry the caller cannot open for reading
  is left out. The walk by path left it out too: `os.scandir()` of a directory needs read
  permission, and reading a `user.` attribute needs `MAY_READ` on the inode
  (`xattr_permission()`, references §29), so such an entry contributed nothing before either.
  The authorization emulation (`_reach_ok`, `_ext_acl`) is unchanged and gets the same inputs
  (an `fstat` of the entry instead of an `lstat`, the same ACL bytes read through a descriptor).
  `run()` sorts by inode or by value, so the order of the walk does not matter.

## Alternatives

- **`openat2()` with `RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS`.** Not taken, as in journal 0227:
  Python's `os` module does not offer it, and a ctypes call would add an ABI to maintain for the
  same guarantee the per-component `O_NOFOLLOW` already gives.
- **Keep every pending directory's descriptor open** (no reopen). Rejected: descriptors grow with
  the width of the tree, not its depth; the reopen with an identity check gives the same
  guarantee.
- **`O_PATH` descriptors**, which need no read permission. Rejected: `fgetxattr` on an `O_PATH`
  descriptor fails with `EBADF`, and the entries the caller cannot read have no readable typed
  attribute anyway (above).
- **Recheck by path after each step** (`lstat` before and after). Rejected: it narrows the
  window without closing it.

## Changes

- `python/src/moobfs/scan.py`: `candidates()` rewritten around `_visit()` and `_open_entry()`;
  module docstring states the guarantee.
- `python/tests/test_linux.py`: four tests. `test_scan_keeps_a_directory_swapped_for_a_link`
  (a hook on `os.scandir` swaps a subdirectory for a link to a private directory just before it
  is listed), `test_scan_reads_the_file_it_judged` (a hook on `scan.get_attrs` swaps the file for
  a link to a file outside before its attributes are read), `test_scan_under_a_racing_swap` (a
  thread exchanges a directory and a link with `renameat2(RENAME_EXCHANGE)` during 2000 walks,
  as the MCP test of journal 0227 does) and `test_scan_never_follows_links` (planted links to a
  directory and a file outside, and a FIFO; this one passes on the old walk too and guards the
  static case).
- `docs/decisions/0023-python-client-library.md`: dated notes on decision 3 and on the cost
  consequence. `python/README.md` (cost and guarantee), `docs/testing.md` (`make pylib-test`
  row), `docs/security-roadmap.md` (S1), `docs/status.md` and `docs/status.pt-BR.md`,
  `CHANGELOG.md`, `docs/references.md` §29, `docs/retired-claims.txt` (the old cost wording).

## Evidence

On the dedicated host (Debian 13, Python 3.13.5), in a copy of the branch under `~/scan`, the
old walk being `python/src/moobfs/scan.py` of `1d7376f` with the new tests:

- `python3 -B -m unittest discover -s python/tests` (system Python, no optional SDKs), three
  runs each: new walk `Ran 43 tests ... OK (skipped=14)` three times; old walk
  `FAILED (failures=3, skipped=14)` three times, the three failures being the hook, file and race
  tests (`'sub/secret' unexpectedly found in ['sub/secret']`, `{'f': {'k': 2}} != {'f': {'k':
  1}}`, `{'notes.link/secret'} is not false`). The skips are the MCP and LangGraph tests (SDKs not
  installed), the root-only test and the installed-package test.
- The race test alone, `-p test_linux.py -k racing`, ten runs each: with 400 walks the old walk
  failed 7 of 10, so the test was raised to 2000 walks; then the old walk failed 10 of 10 and the
  new one passed 10 of 10.
- In a venv with `mcp==2.3.0` and `langgraph==1.2.15` and the library installed with pip from
  the branch: `MOOBFS_TEST_INSTALLED=1 venv/bin/python -B -m unittest discover -s python/tests`
  three times, `Ran 43 tests ... OK (skipped=1)` (the root-only test), and once as root with
  `sudo`, `Ran 43 tests ... OK`. The old walk from source in the same venv, three runs:
  `FAILED`, the same three tests each time.
- As root, `test_linux.py` only: new `Ran 11 tests ... OK`; old `FAILED (failures=3)`.
- Equality with `QUERY2`, on the module in the disposable KASAN guest (the guest has
  `python3`; the `vm-test` initramfs does not): `MOOB_ONLY="pylib minacl" sh vm/kasan-local.sh`
  from a git copy of the branch: `MOOBFS_PYLIB_PASS: 400 requests (255 with answers in alice) x 2
  domains x pages 7 and 4096, 60 batches: kernel = scan through the mount = scan of the lower;
  owner and team member agree; another uid refused by both; ...`, then `MOOBFS_KASAN_ONLY_PASS:
  steps pylib minacl under KASAN, UBSAN and lockdep with no kernel diagnostics`. The log showed no
  `MINACL_PASS` line among the lines filtered for `_PASS`, and it was removed with the copy before
  that was looked into, so the `minacl` comparison of the Python scan is not claimed here.
- On the workstation (macOS), `make pylib-test`: `Ran 43 tests ... OK (skipped=24)`; every
  Linux test skips there (no `os.listxattr` on macOS), so this run says nothing about the walk.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.

CI's `make pylib-test` runs `test_linux.py` on a Linux runner, so the four new tests run there
when its temporary directory supports user extended attributes (they skip otherwise); they have
not run in CI yet. The MCP and LangGraph tests still run only where those SDKs are installed.

## Limitations

- The domain is opened by path; a link to it is followed, as before. Whoever controls the path
  of the domain's parent controls which directory is walked.
- A directory moved elsewhere after it was opened is still walked (the same limitation as journal
  0227): its entries are then reported under the old name, but they are entries the walk reached
  inside the domain and judged with the domain's rule.
- A chmod between the judgement and the read is not detected; the kernel's index is also built
  from the state it sees. No tree that changes during the walk has a defined answer.
- Each entry is now opened for reading. Opening does not change access time; an `fanotify`
  permission watcher on the domain would see the opens.
- `v0.5.0-experimental` has the defect; earlier tags do not have the scan fallback (it arrived in
  d96e48d, 2026-10-26). *Note, 2026-10-29, by the coordinator of the batch:* the maintainer asked
  for an advisory; [GHSA-f769-xjf5-54wc](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-f769-xjf5-54wc), severity low, credits the external reviewer and is published
  after the batch is merged.

## Next step

The coordinator merges the branch into the batch; the batch's full `make vm-test` and KASAN
suite (with `pylib`) run once before publication.
