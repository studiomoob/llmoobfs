# ADR-0023 — A Python client library under LGPL, with a scan that applies the kernel's rules

Date: 2026-10-26. Status: **accepted by the maintainer on 2026-10-26** for the licence and the
package name and distribution (journal 0204); the rest is the implementation of roadmap item
C5 under ADR-0022 and is proposed with it. Estimated effort of C5: M–L (roadmap); this first
phase, the library and its tests, M.

## Context

ROADMAP item C5 asks for "an installable, versioned Python package with capability detection
and the scan fallback", built from G1's adapter (`bench/client/langgraph_store.py`). Until now a
program reached the layer through `setxattr` and a hand-built ioctl (README: "no library
yet"); every client in `bench/client/` mirrors the UAPI with its own ctypes structures and its
own fallback. The client contract section 7 proposes detection and fallback but nothing
implements it outside a test client.

The repository is GPL-2.0-only. The UAPI header carries the Linux syscall note so that a
program that only talks to the kernel is not bound by the GPL. A Python library is imported
into the client's process, so its licence decides who can use it.

## Decision

1. **A package `moobfs` in `python/`**, standard library only, Python 3.9 or later, version
   0.x and experimental: the API may change between minor versions. Installable with `pip`
   from the repository; not published on PyPI until the distribution gates of
   `docs/distribution-strategy.md` §8 (maintainer's choice).
2. **Licence LGPL-2.1-or-later for `python/` only** (maintainer's choice). A memory system
   under any licence can import it; changes to the library itself stay open. The rest of the
   repository stays GPL-2.0-only.
3. **Two backends, one answer.** `Domain.query` asks the kernel (`QUERY2`, every page followed;
   `QUERY2_BATCH` for up to eight requests) when the directory is on a mounted layer, and
   otherwise walks the directory. The walk applies the kernel's rules, not the caller's:
   matching and ordering as `layer/index.c` (`term_check`, `entry_matches`, `value_cmp`, ties
   by inode), and a file is a candidate only where the homogeneity rule (`reach_ok`,
   `target_ok`) would index it. So the fallback never returns a file that the domain does not
   share with everyone who can enter it, even when the caller could open it, and its answers
   equal the kernel's. The caller must be able to read and search the domain directory, as for
   `QUERY2`.
   *Note, 2026-10-29 (journal 0231):* the walk listed, stat'ed and read every entry by path
   after opening the domain, so a member of a shared domain who swapped a directory or a file
   for a symbolic link between two steps could make it list a directory outside the domain or
   report a file outside it with its attributes (the external reviewer, after journal 0227).
   Below the domain the walk now goes through directory descriptors only: each entry is opened
   relative to its directory with `O_NOFOLLOW` (`O_NONBLOCK` for files), judged with `fstat`
   and its ACL and read through that descriptor, and a directory reopened to descend must still
   be the inode that was judged. The domain itself is still opened by the caller's path (a link
   to it is followed, as before). The answers for a tree that does not change during the walk
   are unchanged.
4. **A resource refusal is not hidden.** `ENOMEM`, `E2BIG`, `ETIMEDOUT` and `EDQUOT` raise
   `QueryRefused` with no partial list; `on_refusal="scan"` answers from the walk instead, as
   contract section 7 step 2 suggests for `ENOMEM`. `ENOTTY`, `ENODEV` and `EOPNOTSUPP` mean the
   kernel does not serve the query, and the automatic backend walks.
5. **Publishing refuses an existing name by default** (`replace=True` to replace it), with
   `renameat2(RENAME_NOREPLACE)`. Not `link` then `unlink`: a second name, even briefly, makes a
   regular file heterogeneous until its next change (spec-query-v2 section 3, Q2a), so it would
   drop out of `QUERY2`. The file gets the domain's owner and group when the publisher may set
   them, which condition 2 of the rule needs even when the domain grants its group nothing.
6. **The UAPI is mirrored by hand** with ctypes, and a test compiles a C program against
   `include/moobfs_ioctl.h` to compare every size, field offset and ioctl number.
7. **The LangGraph store moves onto the library in a second phase**, which carries G1's gate
   over (the roadmap's first acceptance criterion). `bench/client/` stays as it is, since the
   recorded gates G1 to G5 ran with it.
   *Note, 2026-10-27 (journal 0207):* done as `moobfs.langgraph` (extra `langgraph`), with the
   adapter's files; G1's mounted gate passed three times on it with `--store package`. Its
   fallback case follows a protocol amendment the maintainer accepted before the run: without
   the module `salary.json` is absent for uid 1002 too, as for `QUERY2`.
   *Note, 2026-10-28 (journal 0227, GHSA-3wx4-fh9w-2v66):* the store's `batch()` did not apply
   LangGraph's namespace label rule, which `BaseStore` applies only in its public methods, and a
   label is a directory name here: a batched `PutOp` with `..` wrote outside the root. `batch()`
   now checks every operation before any runs.

## Prior art this builds on

- The adapter of journal 0089 (`bench/client/langgraph_store.py`): the ctypes structures, the
  write sequence and the publisher's identity rules, rewritten here without LangGraph.
- Python's `os` extended-attribute functions (`os.setxattr`, `os.listxattr`, since 3.3) and
  `fcntl.ioctl`; the `pyxattr` and `xattr` packages do the same through a C extension, which
  the standard library makes unnecessary here.
- `ctypeslib`/`h2xml` generate ctypes from headers; not used, for a dependency on clang and
  generated code to review, against a frozen UAPI of five structures checked by a test.
- PEP 621 (project metadata in `pyproject.toml`), PEP 639 (SPDX licence expression) and
  setuptools as the build backend, pinned by hash for the test install.
- `renameat2(2)` with `RENAME_NOREPLACE` (Linux 3.15), which the layer passes to the lower
  (`layer/main.c`).

## Alternatives considered

- **GPL-2.0-only, like the repository.** Rejected by the maintainer: it would bind every
  client that imports the library, against the client-neutral goal.
- **MIT or Apache-2.0.** Not chosen: the maintainer preferred that changes to the library stay
  open.
- **A fallback that returns every file the caller can open.** Rejected: it gives more answers
  than `QUERY2` for heterogeneous files (a `0600` file in a group domain, a hard link), so the
  two backends would disagree and a client would see results appear when the module is loaded
  or not. The G1 adapter's walk does this; the library does not.
- **Promote `bench/client/langgraph_store.py` as the package.** Rejected: it depends on
  LangGraph, and the gates already recorded must stay reproducible with the code that ran.
- **A C extension or cffi.** Rejected for now: a build step and a compiler at install time for
  five ioctl structures; ctypes needs neither.

## Consequences

- One place mirrors the UAPI for Python clients; the bench clients keep their own copies.
- The walk costs one `stat`, one `listxattr` and the `getxattr` calls of every file in the
  domain per query; it is the fallback, not a replacement for `QUERY2`.
  *Note, 2026-10-29 (journal 0231):* since the walk goes through descriptors, an `open` and an
  `fstat` per entry in place of the `stat`, and a second `open` per directory it descends into.
- Edges where the walk and the kernel can differ are listed in `moobfs/scan.py`: an attribute
  list over 64 KiB (the kernel leaves the file out), more than 64 levels below the mount root,
  and a domain the kernel answers in fallback mode after a directory's permissions changed.
