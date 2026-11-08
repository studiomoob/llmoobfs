# ADR-0035 — Workspace layout and the `moobfs workspace` commands

Date: 2026-11-08. Status: **proposed**, awaiting the maintainer's review (journal 0273); a local
implementation follows it so that the review can read working code. Amends, once accepted,
`docs/distribution-strategy.md` §2.2 (layout) and §3 (the `workspace`, `mount` and `status`
rows); their text is kept with a dated note. Estimated effort: M (one to three days). Estimate,
not labor time.

## Context

Stage 2 (`docs/distribution-strategy.md` §8) lists a minimal `moobfs` CLI (§3) among its
deliverables, and its first acceptance step runs `workspace create`. `tools/moobfs.c` (item C4,
journal 0198) has `info`, `query`, `check` and `diagnose`; `mount`, `status` and `workspace` do
not exist. The maintainer chose on 2026-11-08 to implement them before stage 2. They are user
space only (ADR-0022).

§2.2 was written before per-domain queries (ADR-0009, `domain_depth`) and the homogeneity rule
(ADR-0011, ADR-0017, ADR-0025): a file is answered from its domain's partition only if everyone
who can traverse the domain directory can read it, which `reach_ok()` decides with a sufficient
condition (all classes can read, or the same owner and group as the domain directory with the
bits of every class the domain lets traverse; no extended ACL). Under that rule §2.2's layout
puts almost every file on the slow path of per-result checks:

- With `domain_depth=1` the namespace `/<ns>` (`root:moob-<ns>` `0750`) is the domain, and
  `agents/<a>` (`<a>:<a>` `0700`) is an intermediate directory that fails the rule for
  everything below it.
- `setfacl -d -m g:moob-<ns>:rwX` on `shared` gives every new file a named group entry, an
  extended ACL, which fails the rule by itself.

Answers stay correct either way (per-result checks), but the index then saves nothing.

Linux 6.12.116 `fs/posix_acl.c` `posix_acl_create()`: when a directory's default ACL has only
the three base entries, `posix_acl_create_masq()` returns 0, no access ACL is stored on the new
file, and the default ACL's bits replace the umask. So a base-only default ACL gives a group
write access without making files extended.

## Decision (proposed)

1. **Layout.** A workspace is a namespace directory one level below the mount root, with its
   areas one level below it, and the layer is mounted with `domain_depth=2`, so that each area is
   its own domain:

   | Path | Owner:group | Mode / ACL |
   | --- | --- | --- |
   | `<ns>` | `root:moob-<ns>` | `0750` |
   | `<ns>/shared` | `root:moob-<ns>` | `2770`, default ACL `u::rwx,g::rwx,o::---` (base entries only) |
   | `<ns>/<agent>` | `<agent>:<agent's group>` | `0700` |

   An agent's files (`0600` under the usual umask, its own owner and group) pass the rule in
   its area and are answered from the partition. Shared files are owned by the agent that made
   them and the group `moob-<ns>`, `0660`; the domain directory is owned by root, so they fail
   `reach_ok()`'s same-owner branch and are answered with per-result checks: correct, and the
   documented cost of sharing. The agent name `shared` is refused.
2. **`workspace create <mount> <ns> [--agents a,b] [--create-users] [--dry-run] [--json]`**:
   refuses a mount whose domain depth (from `GET_INFO`) is not 2, a namespace that exists, and
   names that are not `[a-z_][a-z0-9_-]{0,31}`; creates the directories with `mkdir`, `chown`,
   `chmod` and the default ACL written as `system.posix_acl_default` directly (no `setfacl`
   dependency); adds each agent to `moob-<ns>`. Users and groups must exist, unless
   `--create-users` runs `groupadd --system moob-<ns>` and `useradd --system --user-group
   --no-create-home --shell /usr/sbin/nologin <agent>` (shadow-utils). Nothing existing is
   overwritten; `--dry-run` prints each step without doing it.
3. **`workspace grant <mount> <ns> <user> rw|ro`** and **`revoke`**: `rw` adds the user to
   `moob-<ns>` (`gpasswd -a`); `ro` adds named ACL entries `u:<user>:r-x` to `<ns>` and
   `<ns>/shared` and to the default ACL of `shared`, which makes shared files extended (per-result
   checks, as above). `revoke` removes both. A group change takes effect at the user's next
   login; the command says so. No agent area is ever opened to another user.
4. **`workspace list <mount> [--count NAME] [--json]`**: the namespaces (directories at depth 1
   whose group is `moob-<name>`), their areas, owners, modes and whether they carry ACLs; with
   `--count NAME`, the number of files in each area that carry the typed attribute `NAME`
   (`QUERY2` `NAME exists`, as the caller). No attribute name is built in: the CLI stays
   client-neutral.
5. **`mount <lower> <mountpoint> [--domain-depth N] [--nolinks] [--noindex] [--index-budget B]`**:
   checks the running kernel against the installed `supported-kernels` list and refuses another
   (override `--unsupported`), refuses a lower that is itself a layer or whose path others can
   traverse (§2.3, item 1; override `--allow-open-lower`), then `mount(2)`. Default
   `--domain-depth 2`, matching the workspace layout. **`status [mountpoint]`**: `info` plus the
   loaded module's version and `srcversion`.
6. Exit statuses follow `docs/tools.md` (0, 1, 64, 69, 77; 65 for an invalid name); output is
   text or `--json`; no network.

## Alternatives

- **§2.2 as written** (`agents/<a>` below the namespace, a named-group default ACL): simplest
  to explain, but the index answers almost nothing from partitions.
- **`domain_depth=3` with `<ns>/agents/<a>`**: agent areas are domains, but `shared` (depth 2)
  is then an intermediate directory and shared files fall outside every domain.
- **A dedicated group-owned `shared` with a service owner**: would let shared files pass the
  rule only if every writer were that owner; it needs a daemon or setuid helper. Rejected (no
  daemon, §3).

## Consequences

- Agent-private memories, the common case, are answered from the index; shared ones are
  correct at the cost of per-result checks, stated in `docs/operations.md`.
- §2.2 and §3 of the distribution strategy, `docs/operations.md` and `docs/tools.md` change with
  it; the stage 2 acceptance step 1 runs `workspace create` with this layout.
- Creating users is optional and explicit; the CLI never deletes users, groups or data.
