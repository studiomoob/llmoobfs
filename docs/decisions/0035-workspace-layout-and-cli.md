# ADR-0035 — Workspace layout and the `moobfs workspace` commands

Date: 2026-11-08. Status: **accepted** by the maintainer on 2026-11-09 (journal 0273), after the
local implementation and its VM runs. Proposed on 2026-11-08, awaiting the maintainer's review,
with a local implementation so that the review could read working code. Amends
`docs/distribution-strategy.md` §2.2 (layout) and §3 (the `workspace`, `mount` and `status`
rows); their text is kept with a dated note. Estimated effort: M (one to three days). Estimate,
not labor time.

*Correction, 2026-11-08 (journal 0273):* the first text of this ADR said that files failing the
homogeneity rule "are answered with per-result checks" (and that §2.2's layout "puts almost
every file on the slow path of per-result checks"). That is wrong: `docs/spec-query-v2.md`
§3 says such a file "does not appear in v2 queries", and `QUERY2` reads only the domain's
homogeneous partition (`layer/index.c`, `query_run()`, the `name_find(..., false)` lookups;
fallback mode adds per-result checks to that partition, it does not add heterogeneous files).
The VM run of `make workspace-test` found it: `a2`, a member of `moob-proj`, could read
`shared/s1.md` (`a1:moob-proj 0660`, no ACL), and `moobfs query` of `shared` as `a2` exited 0
with no result. The default ACL of `shared` and `grant ro` are changed below so that shared
files pass the rule. The sentences that were wrong, as first written:

> Answers stay correct either way (per-result checks), but the index then saves nothing.

> Shared files are owned by the agent that made them and the group `moob-<ns>`, `0660`; the
> domain directory is owned by root, so they fail `reach_ok()`'s same-owner branch and are
> answered with per-result checks: correct, and the documented cost of sharing.

> `ro` adds named ACL entries `u:<user>:r-x` to `<ns>` and `<ns>/shared` and to the default ACL
> of `shared`, which makes shared files extended (per-result checks, as above). `revoke` removes
> both.

> Agent-private memories, the common case, are answered from the index; shared ones are
> correct at the cost of per-result checks, stated in `docs/operations.md`.

The first default ACL of `shared` was `u::rwx,g::rwx,o::---`.

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
leaves almost every file out of query answers:

- With `domain_depth=1` the namespace `/<ns>` (`root:moob-<ns>` `0750`) is the domain, and
  `agents/<a>` (`<a>:<a>` `0700`) is an intermediate directory that fails the rule for
  everything below it.
- `setfacl -d -m g:moob-<ns>:rwX` on `shared` gives every new file a named group entry, an
  extended ACL, which fails the rule by itself.

Such files are heterogeneous: they are left out of every `QUERY2` answer (spec §3), so a client
that relies on the index does not find them at all.

Linux 6.12.116 `fs/posix_acl.c` `posix_acl_create()`: when a directory's default ACL has only
the three base entries, `posix_acl_create_masq()` returns 0, no access ACL is stored on the new
file, and the default ACL's bits replace the umask. So a base-only default ACL gives a group
write access without making files extended. `reach_ok()` (`layer/index.c:659–679`) returns true
at its first test when the file grants the needed bits to all three classes and has no
extended ACL, before it looks at the domain directory at all; only its second branch (same
owner and group) reads the domain directory's mode and ACL.

## Decision

1. **Layout.** A workspace is a namespace directory one level below the mount root, with its
   areas one level below it, and the layer is mounted with `domain_depth=2`, so that each area is
   its own domain:

   | Path | Owner:group | Mode / ACL |
   | --- | --- | --- |
   | `<ns>` | `root:moob-<ns>` | `0750` |
   | `<ns>/shared` | `root:moob-<ns>` | `2770`, default ACL `u::rwx,g::rwx,o::r-x` (base entries only) |
   | `<ns>/<agent>` | `<agent>:<agent's group>` | `0700` |

   An agent's files (its own owner and group) pass the rule in its area and are answered from
   the partition: `0644` by the first branch of `reach_ok()`; `0600` by the second, since file
   and domain directory are both `<a>:<a>`, neither has an extended ACL, and the area (`0700`)
   lets only its owner class traverse, to which the file grants read (spec §3, condition 2).
   Subdirectories the agent makes (`0700` or `0755`) pass the same way with read and search.

   Shared files are owned by the agent that made them and the group `moob-<ns>`. The default
   ACL's bits replace the umask, so a file created with the usual mode `0666` is `0664` and a
   subdirectory `2775` (setgid inherited), with no access ACL: every class can read, so they
   pass `reach_ok()`'s first branch and are answered from the partition. The read bit for
   others exposes nothing: a user outside `moob-<ns>` cannot traverse `<ns>` (`0750`), nor
   `shared` itself, whose own mode stays `2770` (the default ACL governs only what is created
   inside), so there are two barriers before any shared file. A file created with an explicit
   narrower mode (for example `open(..., 0600)`, or a later `chmod 0640`) fails the rule: it is
   left out of `QUERY2` answers and listed by `moobfs-check --domain-depth 2`. That is the
   documented cost; a writer that wants its shared files found keeps them readable by all
   classes. The agent name `shared` is refused.
2. **`workspace create <mount> <ns> [--agents a,b] [--create-users] [--dry-run] [--json]`**:
   refuses a mount whose domain depth (from `GET_INFO`) is not 2, a namespace that exists, and
   names that are not `[a-z_][a-z0-9_-]{0,31}`; creates the directories with `mkdir`, `chown`,
   `chmod` and the default ACL written as `system.posix_acl_default` directly (no `setfacl`
   dependency); adds each agent to `moob-<ns>`. Users and groups must exist, unless
   `--create-users` runs `groupadd --system moob-<ns>` and `useradd --system --user-group
   --no-create-home --shell /usr/sbin/nologin <agent>` (shadow-utils). Nothing existing is
   overwritten; `--dry-run` prints each step without doing it.
3. **`workspace grant <mount> <ns> <user> rw|ro`** and **`revoke`**: `rw` adds the user to
   `moob-<ns>` (`gpasswd -a`); `ro` adds the named entry `u:<user>:r-x` to the access ACL of
   `<ns>` and of the directory `<ns>/shared` only, not to the default ACL, so files created
   during a grant are not extended and stay in the partition; the user reads them as "other"
   (`r--`), which the two directory entries now let through. The access ACL on `shared` does not
   change its files' homogeneity, because they pass the first branch of `reach_ok()`, which does
   not look at the domain directory. `revoke` removes the entries; an ACL left without named
   entries is removed and the mode set back (`0750`, `2770`). A group change takes effect at the user's next
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

- Agent-private memories and shared memories with the default mode are answered from the
  index; a shared file made narrower than `0644` is left out of queries, stated in
  `docs/operations.md`.
- A change of mode, owner, group or access ACL of a domain directory puts that domain in
  fallback mode until the next mount (`moobl_index_perm_changed()`, `layer/index.c:3106–3122`;
  spec §3, Q2a): its answers then add a permission check per result and `GET_INFO` counts it in
  `fallback_domains`. `workspace create` (the `chown`/`chmod` of each area) and `grant`/`revoke
  ro` (the access ACL of `shared`) do this; answers stay correct and complete for homogeneous
  files, and a remount ends it.
- §2.2 and §3 of the distribution strategy, `docs/operations.md` and `docs/tools.md` change with
  it; the stage 2 acceptance step 1 runs `workspace create` with this layout.
- Creating users is optional and explicit; the CLI never deletes users, groups or data.
