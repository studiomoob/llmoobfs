# ADR-0025 — An intermediate directory must be listable, not only searchable

Date: 2026-10-27. Status: **accepted by the maintainer on 2026-10-27** (fix option 1 of the
private report, journal 0215). Estimated effort: S (hours). Amends ADR-0011 (promise 3 and
non-promise 3) and `docs/spec-query-v2.md` §3; neither text is replaced, both carry a dated
note pointing here.

## Context

A private report found that `QUERY2` returned the names, inode numbers and, through probes,
the attribute values of files under a search-only directory (for example `0711`) inside a
domain to every user who could enter the domain. Those users cannot list that directory on
the lower or through the layer. The homogeneity rule asked of an intermediate directory only
the execute bit of each class (spec-query-v2 §3, Q2a implementation; `reach_ok()` with
`want_read = false` in `layer/index.c`), and the per-result check of fallback mode
(`caller_may_read()`) resolved the path, which needs search on each directory, then asked
`MAY_READ` of the file alone. The user-space mirrors (`moobfs-index`, `moobfs-check`, the
Python scan) carried the same rule, and `tests/permmatrix.c` judged a file by `getxattr`, which a
`0711` directory lets through, so no test saw it.

ADR-0011's non-promise 3 argued that a confined process "already sees [the name] by listing the
directory". That holds for path-based LSMs but not for a directory whose mode forbids the
listing. A search-only directory is the usual Unix way to keep names private while letting
those who know a name open it (`~/.ssh`-style or upload directories).

Prior art:

- The kernel's path walk asks only `MAY_EXEC` of each directory (`may_lookup()` in `fs/namei.c`).
  Listing a directory needs it opened for reading, which `may_open()` checks as `MAY_READ`
  through `inode_permission()`. Knowing a name and learning it are different permissions.
- plocate, which answers from a database built as root, shows a path only when the caller passes
  `access(dir, R_OK | X_OK)` on every directory above it (`access_rx_cache.cpp`). This decision
  is the same rule for the same problem.

## Decision

A file is answered only if every directory strictly between the domain directory and the file
is listable and searchable by everyone who can traverse the domain:

- **Homogeneity rule.** An intermediate directory satisfies condition 1 when it grants read and
  search to owner, group and others (`a+rx`) and has no extended ACL. It satisfies condition 2
  when it has the same owner and group as `D`, neither has an extended ACL, and it grants read
  and search to every class `D` lets traverse. A file below a directory that fails both is
  heterogeneous: left out, counted in `GET_INFO`, listed by `moobfs-check`.
- **Per-result checks** (fallback mode, `domain_depth=0`, labelled files): after the lookup, the
  caller must also have `MAY_READ` on every directory strictly between the queried directory and
  the result (`caller_may_list_above()` in `layer/index.c`). A result whose ancestors no longer
  lead to the queried directory is refused. The lookup and the walk run under the `rename_lock`
  sequence counter: a positive answer counts only if no rename happened between them, so the
  directories checked are the ones the recorded path went through. Otherwise it is decided
  again, and after four tries the result is left out, as a stale result is.
- The three mirrors of the rule (`tools/moobfs-index.c`, `tools/moobfs-check.c`,
  `python/src/moobfs/scan.py`) follow, so they keep giving the kernel's answers.

## Alternatives considered

- **State it as a non-promise** (option 2 of the report). Rejected: it would leave a common Unix
  arrangement unsafe to put inside a domain, and the cost of the fix is small.
- **Per-result `MAY_READ` checks only, rule unchanged.** Rejected: a homogeneous partition is
  answered without any per-result check; the rule itself must exclude these files.
- **Judge condition 2 against the classes that can query `D` (read and search on it, which
  `QUERY2` requires) instead of those that can traverse it.** It would admit more files, for
  files as well as directories, so it is a change of the rule beyond this fix. Left for a later
  decision with its own tests. It matters most for an owner or group working in their own `0751`-style
  subdirectories of a more open domain: they lose those files from `QUERY2` unless the
  directory is made `o+rx`, an incentive to open directories rather than close them. To be raised
  with the maintainer (independent review of journal 0215).
- **Leave the window between lookup and walk** (a rename there checks another chain). Rejected:
  it would reveal only that a file used to be under a directory and needs write access to
  those directories, but closing it costs one sequence-counter read, the pattern `where_is()`
  already uses. Checking `MAY_READ` per component during resolution was not chosen: it needs a
  component-by-component walk of our own instead of `vfs_path_lookup()`.

## Compatibility

No on-disk format or UAPI change. A file the old rule admitted and the new one does not becomes
heterogeneous, as happened with F9 (journal 0116). The recommended layout of the client contract
(§3: directories `0700` with files `0600`, or the same mode bits as the domain directory) already
grants read and search together, so it is unaffected. Answers can only shrink: a domain whose
intermediate directories are `0711`, `0751`, `0701` or `0710` under a more open domain directory
loses the files below them from `QUERY2`, and `moobfs-check` lists them with "more private than
the domain directory". This also holds when `D` is itself search-only for some class (a `0711`
domain with `0711` directories), although only the classes that can read `D` can query it: the
rule stays judged against every class that can traverse `D` (see the alternatives). With `domain_depth=0`, v1 `QUERY` no longer returns a file under a
directory the caller can search but not read (suite step `ext4`, ACL `u:1001:x`).

## Consequences

- Kernel, tools and library change in one unit with tests: `tests/permmatrix.c` judges by
  `getxattr` of the file and `opendir()` of the directory between, counts the cases the rule
  before this decision answered wrongly, and runs again in fallback mode; the suite's ACL case
  separates search alone from read and search; the Python scan has a test of its own.
- ADR-0011 records this as finding F15.
