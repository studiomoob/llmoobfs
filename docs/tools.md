# Command-line tools: synopsis, diagnostics and exit status

The Linux tools that query, read and check typed attributes (`make attr-tools`; they use the
Linux xattr API and do not build on macOS). All print their synopsis with `--help` (exit 0) and,
when called wrongly, on standard error (exit 64). Errors name the tool and the file:
`moobfs-attr: <file>: <reason>`, `moobfs-check: <directory>: <reason>`. Experimental, like the
rest of the project.

## moobfs

```
moobfs info [--json] <path>
moobfs query [--json] [--ino] [--order NAME TYPE asc|desc] [--limit N] [--page N] <dir> [terms...]
moobfs check [moobfs-check arguments]
moobfs diagnose [--json] <path>
moobfs --help
```

One command for a mounted layer (roadmap item C4, journal 0198), over the existing ioctls; it
adds no kernel interface. Errors start with `moobfs:` and say what to do.

- `info` finds the layer mount that holds `<path>` and prints its `GET_INFO`: lower filesystem,
  interface versions, whether the index is on, the domain depth, the index budget and build
  work, attribute limits, and for an administrator the index counters. `--json` prints one
  object. Through a bind mount of a subdirectory the mount root is not reachable, and `info`
  says so (client contract, section 7).
- `query` issues `QUERY2` on a domain directory and follows the cursor until the answer is
  complete or `--limit` results were printed; `--page` sets the results per call (default
  4096). On the root of a mount without domains it issues the mount-wide `QUERY`, which has no
  cursor. Terms are those of `moobfs-index`: `name op type value`, up to four, an operator
  ending in `?` also matching files without the attribute. Paths are relative to `<dir>`, one
  per line (`--ino` adds the inode number and a tab; a `?` marks a path the kernel did not
  know). Without `--order` the results are sorted by inode number, as `moobfs-index` prints
  them. A cursor made stale by the domain changing restarts the answer, at most three times;
  `EAGAIN` is retried three times.
- `check` runs `moobfs-check` from the same directory, else from `PATH`, with its arguments
  and exit status.
- `diagnose` (roadmap item G2, journal 0243) reports, without changing anything, what bounds a
  query on `<path>` by this caller: whether `<path>` is a domain directory, above the domains
  or inside one; the index (on or off, budget, build work); the memory cgroup of the process
  and the smallest `memory.max` above it, with its usage and how often it was reached
  (`memory.events.local`: `max`, `oom`; `memory.events`: `oom_kill`); `RLIMIT_NOFILE`; the
  test-only module parameters (`scan_delay_us`, `scan_hold_us`, `scan_getxattr_errno_once`) when
  set; and for root the index's use of its budget and whether the `moobfs` tracepoints are on.
  Each finding is a `warning` or a `note` with a stable `code`: `no-memory-limit`,
  `oom-killed`, `near-limit`, `test-parameter` and `budget-near-full` are warnings;
  `limit-reached`, `index-off`, `not-a-domain`, `info-unknown`, `cgroup-unknown`,
  `tracepoints` and `administrator-only` are notes. `--json` prints one object (`path`,
  `mount`, `role`, `uid`, `index`, `memory_cgroup` with `limit` and `levels`, `nofile`,
  `module_parameters`, `tracepoints`, `warnings`, `findings`). Exit 0, or 2 when a warning was
  printed. Run it as the user and in the session that queries: the memory cgroup is the
  process's own, and `sudo -u` keeps the caller's.
- When a query fails with `ENOMEM`, `query` reads the caller's memory cgroups again and says
  which bound refused it: a cgroup whose own limit was reached during the query (its local
  `max` or `oom` count grew; on a systemd host usually `user-<uid>.slice`, above the session
  scope), or else the index budget or this uid's share of it, with the budget's size. Another
  process of the same cgroup reaching the limit at the same moment would be counted too. The
  exit status stays 75.

What it reads (issue #167, journal 0230): `<path>` or `<dir>` itself (`realpath`, `stat`,
`statfs`, and `open` of the directory for the ioctl), and `/proc/self/mountinfo` for the mount
record; `/proc/filesystems` only to say that the module is not loaded; `/proc/self/cgroup` and
the `memory.*` files of the caller's cgroups under `/sys/fs/cgroup` (`query` after an `ENOMEM`,
and `diagnose`); `/sys/module/moobfs_layer/parameters/` and, as root,
`/sys/kernel/tracing/events/moobfs/enable` (`diagnose`). A denied read makes that part
unknown, never an error. Under a sandbox that
denies reading `/proc` (for example Landlock without `READ_FILE` there), the commands go on
without the mount record: the layer is recognised by `statfs` (type `0x4d4f4f42`), `query`
issues `QUERY2` as before and, when the directory is the mount root, learns the domain depth
from `GET_INFO`, so the mount-wide `QUERY` of a mount without domains still answers; `info`
takes the highest directory above `<path>` on the same device as the mount root. What is then
lost: `info` cannot show the build work and idle options (`index_build_work` and `index_idle`
are `null` in `--json`) and cannot tell a bind mount of a subdirectory from the mount root
except by `GET_INFO` refusing it; an `ENOTTY` from a directory that is not a domain does not
say the domain depth; a missing module is reported as a path not on a layer. `query` needs
directory listing (`READ_DIR` under Landlock) on `<dir>`, `info` on the mount root.

| Exit | Meaning |
| --- | --- |
| 0 | success |
| 1 | a system call failed (the reason is printed) |
| 2 | `diagnose`: at least one warning was printed |
| 3 | `query`: the mount-wide `QUERY` had more than 4096 results and cannot return the rest; those returned were printed |
| 64 | usage |
| 65 | invalid terms, or a query the kernel refused as invalid (`EINVAL`) |
| 69 | no layer there: the module is not loaded, `<path>` is on another filesystem, `<dir>` is not a domain directory (`ENOTTY`), or the index is off (`noindex`) |
| 75 | temporary: the domain does not fit `index_budget`, or the caller's memory cgroup reached its `memory.max` (`ENOMEM`; the message says which), too many first queries in a minute (`EDQUOT`), the build needs more than `index_build_work` (`E2BIG`) or took longer than its deadline (`ETIMEDOUT`), or the domain kept changing (`EAGAIN`) |
| 77 | permission denied (`EACCES`, or `EPERM` for the mount-wide `QUERY` of a user when domains are on) |

## moobfs-attr

```
moobfs-attr get <file> <name>
moobfs-attr set <file> <name> <string|int64|double|bool|time|raw> <value>
moobfs-attr rm <file> <name>
moobfs-attr list <file>
moobfs-attr rawset <file> <name> <hex>   (test helper: writes bytes unchecked)
moobfs-attr --help
```

`<name>` is written without the `user.moob.` prefix. On a layer mount the kernel validates every
write; on a plain directory the tool validates before writing (`rawset` does not, by design).
`get` and `list` print `<name>\t<type>\t<value>`, or `<name>\tinvalid\t<reason>` for a value that
does not decode.

| Exit | Meaning |
| --- | --- |
| 0 | success |
| 1 | a system call failed (the reason is printed: file missing, permission, not supported, an invalid value refused by the layer) |
| 2 | `get`: the attribute exists but its value is invalid (printed as `invalid`) |
| 3 | `get`: no such attribute |
| 64 | usage: wrong arguments, a name that is not valid, a bad hex string for `rawset` |
| 65 | `set`: the value cannot be encoded as the type, or is invalid for it (for example a NaN double or a string over 1024 bytes) |

## moobfs-check

```
moobfs-check [--domain-depth N] <directory>
moobfs-check --help
```

Validates every typed attribute under `<directory>` and the format marker on its root; never
repairs. Prints one JSON object (`format`, `problems`, and with `--domain-depth` the files left
out of `QUERY2` answers, `left_out`, which are not problems).

| Exit | Meaning |
| --- | --- |
| 0 | clean |
| 1 | the tree could not be walked (the reason is printed) |
| 2 | problems found (listed in `problems`) |
| 64 | usage, or `--domain-depth` outside 1..32 |

### JSON document

`moobfs-check` always emits one JSON object on a completed walk. The required members are:

| Member | Type | Meaning |
| --- | --- | --- |
| `format` | object | Format marker: `state` string (`missing`, `unreadable`, `invalid`, `newer`, or `ok`), `version` integer (0 when unknown), `supported` integer. |
| `problems` | array of objects | Each has `path`, `attribute`, and `problem` strings. Paths are relative to the checked directory; an empty `attribute` means the problem is not tied to one typed attribute. |
| `files`, `typed_attributes`, `identities`, `identity_conflicts`, `same_id_other_namespace`, `problem_count` | integers | Walk and identity counts. `problem_count` equals the length of `problems`. |
| `domain_depth`, `left_out_count`, `left_out` | integer, integer, array of objects | Present only with `--domain-depth`. Each `left_out` object has a relative `path` string and a `reason` string. These entries do not count as problems. |

For example, a clean one-file tree with a version-1 marker and one typed attribute can report:

```json
{"format":{"state":"ok","version":1,"supported":1},"problems":[],"files":2,"typed_attributes":1,"identities":0,"identity_conflicts":0,"same_id_other_namespace":0,"problem_count":0}
```

## moobfs-index

```
moobfs-index [--uid N] [--domain] [--json] [--order NAME TYPE asc|desc] [--limit N] <dir> [terms...]
```

`--uid N` switches to uid and gid N before the walk; run as root, it also drops every
supplementary group (journal 0213), so the walk sees only what uid and gid N reach. A value that
is not a decimal uid is a usage error.

By default, each result remains an `inode<TAB>path` line, sorted by inode unless `--order`
selects value order. `--json` emits one object after the walk and after `--limit` is applied:

| Member | Type | Meaning |
| --- | --- | --- |
| `dir` | string | Directory argument, as supplied. |
| `count` | integer | Number of emitted results; equals the length of `results`. |
| `results` | array of objects | Each has an `ino` integer and a `path` string relative to `dir`, in the same order as text output. The root entry, when included, has an empty path. |

For example (the inode number depends on the lower filesystem):

```json
{"dir":"/srv/memories/alice","count":1,"results":[{"ino":1234,"path":"note.md"}]}
```

These tool JSON shapes are **experimental and may change** in later experimental releases,
consistent with [the compatibility policy](compatibility.md). Existing tab-separated text
output and exit statuses retain that policy's compatibility promise. `dir`, `count`, `results`,
`ino`, and `path` use the same names and types as `moobfs query --json` where they represent
the same values. The oracle has no kernel path-unknown or path-truncated flags.

Strings carry the bytes of the name as stored: `"`, `\` and control characters are escaped in
every string, attribute names and `left_out` reasons included (before journal 0213 only paths
were), and nothing else is converted. Linux names need not be UTF-8, so a tree with such a name
produces a document that strict JSON parsers reject; `moobfs query --json` behaves the same.

| Exit | Meaning |
| --- | --- |
| 0 | success |
| 1 | privilege change or walk failed |
| 64 | usage error |
| 65 | invalid query terms |

## Walking a tree other users can write

`moobfs-check` and `moobfs-index` can be run by root over a lower directory that other users
write, such as a shared domain (`2770`, no sticky bit, `docs/operations.md`). Since journal 0232
they walk by directory descriptor (`tools/walk.h`), as the Python scan (journal 0231) and the MCP
server (journal 0227) do:

- Below the directory argument no name is resolved through a path. Each entry is looked at with
  `fstatat(..., AT_SYMLINK_NOFOLLOW)` relative to its directory's descriptor and opened relative
  to it with `O_NOFOLLOW`; its mode, owner, link count, inode number, access ACL and typed
  attributes come from that descriptor (`fstat`, `fgetxattr`, `flistxattr`), and a directory is
  listed through the descriptor that was judged. A directory or file swapped for a symbolic link
  between two steps (`renameat2(RENAME_EXCHANGE)`) is then left out, never followed: the tools
  do not report, under a name inside the tree, an inode outside it. Before, `nftw()` and the
  reads by path could (`tests/tools-walk.sh` shows it on the old tools).
- Symbolic links are not reported, as before; a symbolic link given as the directory argument,
  with or without a trailing slash, is not walked, and entries on other filesystems are skipped.
- A FIFO, socket or device, and a regular file the caller may not open for reading (a run as a
  user other than root, or `--uid`), get an `O_PATH` descriptor, which opens nothing; their
  attributes are read through `/proc/self/fd/N`, which reaches the same inode with the same
  permission check as a read by path. Without `/proc` those reads fail and are reported as
  unreadable.
- The walk holds at most 32 directory descriptors, as `nftw(..., 32, ...)` did: deeper, it closes
  the shallowest one and reopens it by name from the root down when it is needed again, checking
  at each level that it is still the directory it judged. One that is not ends its listing with
  a line on standard error.

For a tree that does not change, the output (text, JSON and exit status) is the one the
`nftw()` walk gave. Limits:

- The directory argument itself is opened by its path; whoever controls the directories above it
  controls which tree is walked. `moobfs-check` also reads the format marker by that path.
- A tree that changes during the walk has no defined answer: a directory moved elsewhere after it
  was opened is still walked under its old name, and a `chmod` between the judgement and the
  read is not seen (the kernel's index is also built from the state it sees).
- Each directory and regular file is opened for reading, which an `fanotify` permission watcher
  sees. A regular file replaced by a device node between `fstatat` and `open` is opened
  (`O_NONBLOCK | O_NOCTTY`) and closed at once by the identity check; placing a device node in the
  tree needs `CAP_MKNOD`, or a hard link that `fs.protected_hardlinks` refuses to other users.
