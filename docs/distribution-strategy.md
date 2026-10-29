# MoobFS distribution and operation strategy

Status: **proposal** (branch `m3/3b-query-authz`). Nothing marked FUTURE or DEPENDS ON M4 is
implemented. It does not change ADRs, format, UAPI or code. Basis: ADR-0001, ADR-0007, M2/M3 specs,
`docs/security-roadmap.md`, journals `docs/history/0015` to `0020`.

*Note, 2026-10-25.* This proposal is not the current contract. The index is built by the
first query on a domain, not at every mount (ADR-0012). A directory rename marks the
domain stale and the next query rebuilds it; `MOOB_FLAG_PATHS_STALE` does not mean the
answers stay incomplete until remount. User-triggered index and query allocations are
charged to the memory cgroup where that controller exists (ADR-0018). The current
description is [architecture-overview.md](architecture-overview.md) and
[spec-query-v2.md](spec-query-v2.md).

*Note, 2026-10-25 (journal 0197).* Installation from source on a dedicated test host, with
`make install` refusing kernels outside `scripts/supported-kernels`, and the `fstab` and mount-unit
recipes are now [operations.md](operations.md). DKMS, packages, signing and the `moobfs` CLI
remain FUTURE (roadmap items C4 and C6).

*Note, 2026-10-29 (journal 0229, [ADR-0028](decisions/0028-dkms-and-debian-packages.md)).* Item C6
prepared the DKMS tree and the `moobfs-dkms` and `moobfs-tools` packages and tested them in a
disposable Debian 13 VM under Secure Boot (`make package-test`): stage 2 acceptance 1, 2 and 4 of
section 8 pass there. They are not published. Signing differs from the proposal of section 6: the
host's own DKMS key signs the module, and there is no project key (ADR-0028, decision 3). The rows
below marked FUTURE for DKMS, packages and signing are now EXISTS in a VM; the `moobfs` CLI is
item C4 (journal 0198).

*Note, 2026-10-29 ([journal 0236](history/0236-reproducible-module-build.md)).* The unsigned
module is built reproducibly: `make repro-test` builds it twice per listed kernel in clean
containers and gets byte-identical files, and DKMS's `MAKE` line gives the same file; the two
`.deb` packages are reproducible too. A row in section 6 records it.

Legend:

- **EXISTS**: it is in the repository and was exercised by the suite described in `docs/testing.md`.
- **FUTURE**: proposal; depends only on engineering work, with no new measurement.
- **DEPENDS ON M4**: only decided after the numbers from `docs/benchmark-plan.md`.

## 0. Facts that constrain the design

| Fact | Source | Operational consequence |
| --- | --- | --- |
| Native module stacked over ext4, btrfs or XFS; filesystem type `moobfs_layer` | `layer/main.c`, ADR-0007 | The administrator mounts with `mount -t moobfs_layer -o lower=<dir> none <mountpoint>` |
| Tested only with the Debian kernels 6.12.111+deb13-amd64 and 6.12.113+deb13-amd64 (suite in QEMU, fstests `quick` on the dedicated host) and the upstream kernel 6.12.112 with KASAN/UBSAN/lockdep (on-demand and weekly workflow) | `vm/Dockerfile`, `.github/workflows/kasan.yml` | No other kernel version is supported |
| User-space tools: `moobfs-attr`, `moobfs-check`, `moobfs-index` (plus test binaries); `mkfs.moobfs` and `fsck.moobfs` belong to M1, frozen | `tools/`, `Makefile` | There is no tool that issues `QUERY` for general use; `tests/query.c` is a test binary |
| `QUERY` and `GET_INFO` require an open descriptor of the mount's **root** (`file->f_path.dentry == sb->s_root`, otherwise `ENOTTY`) | `layer/index.c` (`moobl_root_ioctl`) | Applies to any client, including in a container |
| Phase 3b: `QUERY` resolves each path with the caller's credentials and applies `inode_permission(MAY_READ)`; silently omits what is inaccessible; `truncated` counts only what is visible | `layer/index.c`, `docs/spec-m3.md` §3.1, journal 0020 | Isolation between agents is the lower's POSIX isolation, including ACLs |
| `FS_USERNS_MOUNT` and `FS_ALLOW_IDMAP` not defined | `layer/main.c`, journal 0020 | Mounting requires `CAP_SYS_ADMIN` in the initial user namespace; an idmapped mount is refused by the VFS with `EINVAL` |
| No lock-in (unmounted, the lower is an ordinary tree with xattrs); the lower cannot be modified with the layer mounted (overlayfs rule); identity is the client convention `(namespace, memory_id)` | ADR-0007, `docs/spec-m3.md` §2 | Backup, recovery and provisioning rely on this; the kernel does not validate uniqueness |
| Index in RAM rebuilt at every mount within `index_budget`; no persistence (M4 decision) | `layer/index.c`, `docs/spec-m2.md` | Mount time grows with the number of typed inodes |

## 1. Recommended deployment architecture

Three tiers. Each can be adopted without the next; none imposes a daemon.

```text
Integration  agent: writes files + sets typed attributes + QUERY (ioctl) + GET_INFO
Workspace    user space: directories, logical namespaces, owners, modes, ACLs
Core         moobfs_layer module: installation, kernel compatibility, mount, diagnostics, update
-----------  ext4 / btrfs / XFS (lower) on disk
```

### 1.1 Core

| Item | Status | Detail |
| --- | --- | --- |
| Compile (`make layer KDIR=...`), `insmod`/`mount -t moobfs_layer` (only in a test VM; `MODULE_ALIAS_FS` allows `modprobe`) | EXISTS | |
| DKMS, module signing and Secure Boot | EXISTS in a VM, not published (ADR-0028) | section 6 (S10) |
| Diagnostics via `GET_INFO` (caps, flags, index budget and usage, lower type) | EXISTS | ioctl; no general-purpose command-line tool |
| Offline diagnostics of the lower (`moobfs-check`, `moobfs-index` as oracle) | EXISTS | `moobfs-check` exits with 0 clean, 2 problems, 1 error |

Core rule (FUTURE): load only on hosts whose `uname -r` is in the tested matrix; outside it,
DKMS and `moobfs mount` refuse.

### 1.2 Workspace

User-space tier that the module deliberately does **not** have (`docs/spec-m3.md` §3).

| Responsibility | Mechanism | Status |
| --- | --- | --- |
| Create the namespace directory and subdirectories (`shared/`, `agents/<agent>/`) | `mkdir`, `chown`, `chmod`, `setfacl` through the layer | EXISTS as POSIX commands; ACLs work through the layer since `d0c8911`; no tool wraps them |
| Grant and revoke access | `setfacl -m`/`-x` and `usermod -aG`, wrapped | FUTURE |
| Audit permissions and identities | `moobfs-check` (identity and attributes) plus a scan of modes | EXISTS (identity/attributes); FUTURE (modes) |

### 1.3 Integration (minimum contract for agent clients)

The contract is what `tests/` and the memory client convention already exercise. No server.

1. **Write** the content as an ordinary file inside the agent's directory (`write`, `fsync`,
   atomic `rename` from a temporary file in the same directory). EXISTS.
2. **Set typed attributes** `user.moob.*` through the xattr API (`setxattr`, or
   `moobfs-attr set`). The kernel validates type and size and refuses invalid encoding. Minimum
   attributes of the M3 convention: `namespace`, `memory_id` (UUID v7 recommended), `agent`; optional
   provenance `origin_namespace`, `origin_id`. EXISTS (validation); convention documented in
   `docs/spec-m3.md` §2.
3. **Query** with `open(<mount root>, O_RDONLY|O_DIRECTORY)` and `ioctl(MOOB_IOC_QUERY)`
   (up to 4 terms, operators `EQ/LT/LE/GT/GE/PREFIX/EXISTS`, `limit` up to 4 096). The result is a
   path relative to the root; the client opens the file normally and accepts `EACCES`/`ENOENT`
   (TOCTOU contract, `docs/spec-m3.md` §3.1). EXISTS.
4. **Discover** capabilities with `MOOB_IOC_GET_INFO` before depending on anything (section 2.8). EXISTS.
5. Without the ioctl the client remains functional (files, xattr, scan): no lock-in. A client
   library that packages steps 2 to 4: FUTURE; `include/moobfs_ioctl.h` and `moobfs_attr.h` are already
   shared with the module (fixed-width fields).

## 2. Operational questions

### 2.1 One mount per host or one mount per agent

| Criterion | One mount per host (per lower) | One mount per agent |
| --- | --- | --- |
| Isolation | POSIX per directory; `QUERY` filtered by the caller (phase 3b, `ISOLATION_PASS` on ext4/btrfs/XFS) | adds path isolation: the agent only sees its own `lower=` |
| Index memory | one index, one `index_budget` (default 128 MiB) | `N` budgets added up; nothing is shared between mounts |
| Rebuild | one scan of the whole lower per mount | smaller, independent scans |
| `QUERY` | scans candidates from everyone and discards the inaccessible ones (batches of 256, one path resolution per candidate) | only the agent's candidates |
| Administration | one `fstab`, one `moobfs-check` | `N` of each |
| Containers | `QUERY` requires the root (2.5) | each container receives the root of its superblock |

**Recommendation (EXISTS as configuration):** one mount per host per lower filesystem,
namespaces as directories (`/<ns>/shared/`, `/<ns>/agents/<agent>/`), one uid/group per agent
and `nolinks`. Less memory, one point of administration, sharing by permission. The cost
of scanning other agents' candidates in `QUERY` has not been measured: **DEPENDS ON M4** (W3).
**A mount per agent makes sense** when: the agent runs in a container and needs `QUERY`;
agents of very different trust (an ACL mistake must not expose the other); it is necessary to
remount, restore or delete one agent in isolation; there are distinct `index_budget` or `noindex`
settings; or the lowers differ (btrfs with snapshots versus ext4). There are no tests with
dozens of mounts and no scale data.

### 2.2 Private and shared directories

Create with the layer mounted (or unmounted). The POSIX commands EXIST; automation is FUTURE.

| Need | Owner:group | Mode / ACL |
| --- | --- | --- |
| Mount root | `root:root` | `0755` (the ioctl requires opening the root; `0711`/`0700` block the agent, finding from journal 0020) |
| Namespace directory `/<ns>` | `root:moob-<ns>` | `0750` |
| Agent-private `/<ns>/agents/<a>` | `<a>:<a>` | `0700`, files `0600` |
| Private with auditor, or read-only shared (`2750`) | same | `0700` + `setfacl -m u:auditor:rX` and `-d -m u:auditor:rX` |
| Shared with write access `/<ns>/shared` | `root:moob-<ns>` | `2770` (setgid: new files inherit the group) + `setfacl -d -m g:moob-<ns>:rwX` |

Facts: every directory in the path needs `x` for the caller, otherwise the `QUERY` result
is omitted; changing `user.moob.namespace`/`agent` does not grant access; `chmod`/`setfacl` take effect on the
next `QUERY`; POSIX ACLs pass through the layer since `d0c8911`; renaming a directory leaves
stale paths until remount (section 7); `nolinks` prevents hard links between agents.

### 2.3 Preventing agents from bypassing the layer through the lower directory

Whoever writes to the lower from outside skips attribute validation and the `QUERY` filter and
violates the rule "lower immutable with the layer mounted". Measures, from simplest to strongest:

1. `lower=` at `/var/lib/moobfs/<name>` with `root:root 0700` (all components without `x` for others).
2. Mount at `/srv/memory`, lower at a path the agent cannot reach.
3. Mount namespace separation: agents start in another namespace (`unshare -m` or a container)
   where only the layer's mount point exists (via bind mount), with `private`/`slave` propagation so the
   lower does not leak.
4. Agents without `CAP_DAC_OVERRIDE`, `CAP_DAC_READ_SEARCH`, `CAP_SYS_ADMIN` (`--cap-drop=ALL`, non-root uid);
   an AppArmor/SELinux profile for the lower's path is optional (FUTURE).

**Honest limit:** root, `CAP_SYS_ADMIN`, `CAP_DAC_OVERRIDE` or access to the device always
bypass it. The layer is not a boundary against root and does not promise confidentiality beyond the
lower (S1); for that, dm-crypt/fscrypt on the lower. Improper modification of the lower is only
detected at remount or by `moobfs-check`, never in real time.

### 2.4 Identity and provenance when sharing or cloning

Key `(namespace, memory_id)`; equal content is not equal identity; reflink is
physical sharing (`docs/spec-m3.md` §2).

| Operation | Effect | Client action |
| --- | --- | --- |
| Share within the same namespace | no copy | grant an ACL on the directory |
| `cp --reflink=auto` or `FICLONE` (reflink only on btrfs/XFS; ext4 copies) | new inode; xattrs only if the tool copies them (`cp -a`) | new `memory_id`, destination `namespace`, `origin_namespace`, `origin_id` of the original |
| `ln` (hard link) | same inode and attributes | do not use for distinct identities; `nolinks` prevents it |
| `rename` | identity preserved | nothing |

Manual recipe (writes under a temporary name and only then does the `rename`, because between `cp` and the change
of `memory_id` there are two inodes with the same key, which is exactly the `moobfs-check` error):

```sh
cp --reflink=auto -p ns-a/shared/x.md ns-b/agents/b/.x.tmp
moobfs-attr set ns-b/agents/b/.x.tmp origin_namespace string ns-a
moobfs-attr set ns-b/agents/b/.x.tmp origin_id string "<memory_id do original>"
moobfs-attr set ns-b/agents/b/.x.tmp memory_id string "$(uuidgen)"
moobfs-attr set ns-b/agents/b/.x.tmp namespace string ns-b
mv ns-b/agents/b/.x.tmp ns-b/agents/b/x.md
```

`moobfs-check` classifies: normal (same id in different namespaces), warning (same id and
identical content without provenance), error (two inodes with the same key). EXISTS (check);
`moobfs memory clone` is FUTURE.

### 2.5 Subdirectories for containers with bind mounts

Constraints in the code: no idmapped mount (`mount_setattr` with idmap gives `EINVAL`; no
`FS_ALLOW_IDMAP`) and no mounting inside a user namespace (no `FS_USERNS_MOUNT`; requires
`CAP_SYS_ADMIN` in the initial namespace). The container never mounts the layer; it receives a bind mount of
something mounted on the host.

Pattern: (1) mount the layer on the host; (2) bind-mount, read-write, only the agent's
directory (`/srv/memory/ns-a/agents/a1`); (3) **align uids**: the process runs with the same
numeric uid/gid that owns the files (`--user 20001:20001`, without the runtime's userns or with
1:1 mapping), since there is no idmap to correct it; (4) read, write and `setxattr` work and
the kernel validates the encoding.

**Limitation, stated plainly:** `QUERY` and `GET_INFO` only accept a descriptor whose dentry is the
root of the superblock (`ENOTTY` otherwise). A bind mount of a subdirectory **cannot issue
`QUERY`**. Alternatives (none implemented):

| Alternative | Cost / risk | Status |
| --- | --- | --- |
| An additional read-only bind mount of the **entire root**, next to the writable subdirectory | results remain filtered by the agent's credentials, but the root becomes browsable according to POSIX; the ioctl does not check `MNT_READONLY`, but it **has not been tested** on an `ro` bind | FUTURE, to be validated |
| One mount per agent, with its root mounted in the container | more memory and administration | EXISTS as configuration |
| A helper outside the container that opens the root and issues `QUERY`, delivering the result over a local Unix socket or an inherited descriptor (no network, no HTTP daemon) | must assume the agent's uid before the ioctl, otherwise it queries with other credentials (confused deputy) | FUTURE, design not detailed |
| Client scans the tree and xattrs (`find` + `moobfs-attr list`) | loses the index; linear cost | EXISTS |

Container in a mapped userns: not tested.

> **Note of 2026-10-25 (roadmap C3, journal 0199).** Tested in a disposable Debian 13 VM with
> Docker 26 and Podman 5: the limitation above is about `QUERY` and `GET_INFO`. `QUERY2`, added
> later, judges a directory by its depth in the layer, not by the mount point, so a bind mount of
> the agent's **domain directory** answers `QUERY2` inside the container. The read-only bind of the
> whole root was also tested: `QUERY2` and `GET_INFO` work through it and writes fail. In a
> mapped user namespace the files keep their host owners, so a remapped uid is refused; Podman's
> `:idmap` falls back to a plain bind without translation; rootless Podman with
> `--userns=keep-id` works, because it keeps the user's uid. The recipe and its limits are in [container-integration.md](container-integration.md). The table
> above is kept as written.

### 2.6 Installation on distributions

| Aspect | Proposal | Status |
| --- | --- | --- |
| DKMS | sources of `layer/`, `include/`, `common/attr.c` in `/usr/src/moobfs-<v>/`; `dkms.conf` with module `moobfs_layer`, `AUTOINSTALL=yes` | EXISTS in a VM (ADR-0028) |
| Kernel policy | install only if `uname -r` is in the matrix (section 6); outside it, `BUILD_EXCLUSIVE_KERNEL` and a message. The VFS API changes between series (the module uses `backing_file_*`, `fs_context`, `mnt_idmap` from 6.12) | EXISTS in a VM: `BUILD_EXCLUSIVE_KERNEL` from `scripts/supported-kernels` (ADR-0028) |

### 2.7 Upgrading, unmounting, backing up and recovering without loss

The lower is the source of truth; the only format of our own is the versioned xattr encoding.

- **Unmount:** `umount`; nothing of our own is written, the index disappears. Durability is that of the lower
  and of the client's `fsync` (validated with `dm-log-writes` and `dm-flakey`). EXISTS.
- **Upgrade the module:** stop agents, `sync`, `umount`, `rmmod moobfs_layer`, install the
  new one, `modprobe`, `moobfs-check <lower>`, mount, check `GET_INFO`. Rolling back is the reverse
  path; the module refuses to mount if `user.moob.format` is of a higher version than the supported one.
  A format change requires a specification, tests of invalid inputs and a compatibility decision.
- **Backup:** `rsync -aHAX --numeric-ids <lower>/ target/` (`-X` is mandatory: without it the
  `user.moob.*` attributes are lost; `-A` preserves ACLs). `git` versions content, **not** arbitrary
  xattrs; use it with an export of the attributes (FUTURE). A read-only btrfs snapshot of the
  lower does not modify it and is consistent per subvolume. Device image only when unmounted.
- **Restore:** `bench/client/g3_restore.py` copies to a new directory with
  `rsync -aHAX --numeric-ids` and exits without writing when the destination is
  not empty. Journal 0190 ran that script on the G1 ext4 corpus: bytes, typed
  attributes and numeric identity matched, `moobfs-check` exited 0, `QUERY2`
  answers matched, an illegal type byte made `moobfs-check` exit 2, and
  `user.moob.format` set to 2 refused the mount. A non-identity UID map, and a
  comparison against the `moobfs-index` oracle, remain FUTURE.
- **Recover:** after a crash, `fsck` of the lower, `moobfs-check`, remount (the index is
  rebuilt). Invalid attribute: `moobfs-check` lists path and reason (exit 2), manual
  fix with `moobfs-attr`; it does not repair. Without the module: mount the lower directly
  (`grep`, `getfattr`, `git`, `rsync` work); direct writes do not go through validation,
  so run `moobfs-check` before remounting. Index overflowed: queries of a
  domain that does not fit fail with `ENOMEM` (before ADR-0016, `MOOB_FLAG_INDEX_INCOMPLETE`);
  increase `index_budget` or use `noindex`.

### 2.8 Capability discovery via `GET_INFO`

A client's first call (ioctl on the root, EXISTS; no command-line tool).
| Field | Meaning / use |
| --- | --- |
| `uapi_version`, `format_version`, `attr_*_max` | refuse an unknown version; write attributes in this version and validate limits in the client |
| `caps` REFLINK / DATA_CHECKSUMS / SNAPSHOTS | announced by the lower's **type** (btrfs: all three; xfs, ocfs2, bcachefs: reflink), not by probing; XFS without `reflink=1` may announce what it does not have, so handle `EOPNOTSUPP` |
| `caps` XATTR | the lower accepts xattrs; without it typed attributes do not work |
| `flags` NOLINKS / INDEX_OFF | do not attempt `ln`; with `noindex`, scan instead of `QUERY` |
| `flags` PATHS_STALE | expect false negatives in `QUERY` until remount |
| `flags` INDEX_INCOMPLETE | modules before ADR-0016 only: budget reached, `QUERY` may omit |
| `index_budget`, `index_used`, `index_entries`, `index_inodes`, `lower_fs` | monitor occupancy (alert near the budget), record the lower |

### 2.9 What exists, what is missing, what depends on M4

| Topic | EXISTS | FUTURE | DEPENDS ON M4 |
| --- | --- | --- | --- |
| POSIX passthrough, typed attributes, ACLs | yes | client library | — |
| `QUERY` authorized by the caller | yes (Debian VM) | KASAN on the branch; concurrent `chmod`/`rename` | per-candidate cost (W3) |
| `moobfs-check` with identity | yes | check modes and ACLs | — |
| DKMS, packages, signing, `moobfs` CLI | yes, in a VM (C4, C6; not published) | publishing, other distributions | — |
| `QUERY` in a container with a subdirectory; index persistence | no | `ro` root or helper | persistence: M4 |
| `QUERY_BATCH`, `READ_BATCH`, cursor; supported scale | no; no numbers | — | M4 (decision) |

## 3. Proposed CLI experience (FUTURE)

A single `moobfs` executable, no daemon, no state beyond a text manifest
(`/etc/moobfs/moobfs.conf`); each subcommand is a thin *wrapper* around existing tools or syscalls
(`moobfs attr ...` forwards to `moobfs-attr`). Nothing is implemented today.

| Subcommand | Does | Built on | Status |
| --- | --- | --- | --- |
| `moobfs mount <lower> <mountpoint> [--nolinks] [--noindex] [--index-budget 256M]` | checks the kernel against the matrix, checks `lower` (mode, owner, not another layer), `mount -t moobfs_layer` | `mount(8)`, `uname`, matrix | FUTURE |
| `moobfs status [mountpoint]` | prints `GET_INFO` (caps, flags, budget and usage) and the loaded module/version | `MOOB_IOC_GET_INFO` | FUTURE |
| `moobfs workspace create <ns> [--agents a1,a2] [--shared]` | creates `/<ns>/shared`, `/<ns>/agents/<a>`, groups, modes, default ACLs | `mkdir`, `chown`, `chmod`, `setfacl`, `groupadd` | FUTURE |
| `moobfs check <lower\|mountpoint>` (wrapper of `moobfs-check`) | invokes checks | `moobfs-check` | FUTURE |
| `moobfs workspace grant\|revoke <ns> <agent\|user> [ro\|rw]`; `workspace list` | `setfacl -m`/`-x` (with `-d`); lists namespaces, owners, ACLs and memory count (`QUERY` `EXISTS memory_id`) | POSIX ACL, `getfacl`, `MOOB_IOC_QUERY` | FUTURE |
| `moobfs query <mountpoint> --where namespace=ns-a --where tags~prefix ...` | opens the root, issues `QUERY` with the credentials of whoever runs it, prints paths (JSON line by line) | `MOOB_IOC_QUERY`, `tests/query.c` as a base | FUTURE |
| `moobfs memory clone <source> <target-ns>` | section 2.4, with atomic swap | `cp --reflink`, `moobfs-attr`, `rename` | FUTURE |

Principles: `--json` output, exit codes like those of `moobfs-check` (0 clean, 2 problems,
1 error), nothing is overwritten, nothing destructive without `--yes`, no network.

## 4. Complete flow (target; today only steps 2 and 3, manual in a VM, are EXISTS)

Scenario: Debian host with a kernel from the matrix, ext4 lower on `/dev/vdb`.
1. **Install (prepared in a VM, not published; ADR-0028):** `apt install moobfs-dkms moobfs-tools`; DKMS compiles for the running kernel
   and refuses a kernel outside the matrix; with Secure Boot, sign with MOK. Today: `make layer KDIR=...`, `insmod`.
2. **Lower (EXISTS, POSIX):** `mkfs.ext4 /dev/vdb`, mount at `/var/lib/moobfs/store`, `chmod 0700`.
3. **Mount (EXISTS; `moobfs mount` FUTURE):**
   `mount -t moobfs_layer -o lower=/var/lib/moobfs/store,nolinks,index_budget=256M none /srv/memory`;
   `chmod 0755 /srv/memory`; check `/proc/mounts` and `GET_INFO`.
4. **Workspace (FUTURE; manual today):** `moobfs workspace create project-x --agents planner,coder --shared`
   creates the group, users, `2770`/`0700` directories and default ACLs (2.2).
5. **Integrate the agent:** it runs with its own uid; receives the path of its directory and that of the root;
   follows the contract in section 1.3 (write with `fsync` + `rename`, set `namespace`, `agent`,
   `memory_id`, `QUERY` on the root, `open()` handling `EACCES`/`ENOENT`). In a container: bind mount
   of the agent's directory and, for `QUERY`, one of the alternatives in 2.5.
6. **Operate:** `moobfs status`, periodic `moobfs check` on an unmounted lower or a snapshot,
   backup with `rsync -aHAX`; share between namespaces by cloning with a new identity (2.4).
7. **Remove an agent:** `moobfs workspace revoke`, archive `agents/<a>` preserving xattrs,
   `userdel`. **Remove a workspace:** archive and delete `/<ns>` through the layer (never from outside with
   it mounted), `groupdel`.
8. **Uninstall:** `umount`, `rmmod moobfs_layer`, remove packages. The lower remains as an
   ordinary tree with `user.moob.*` readable by `grep`, `getfattr`, `git`, `rsync`.

## 5. Permission and isolation model

POSIX only (bits, groups, ACLs, setgid). Typed attributes neither grant nor deny access.
`QUERY` authorization: path resolution with the caller's credentials plus
`inode_permission(MAY_READ)`, the same as an `open()` (journal 0020).

| Actor | Can | Cannot | Note |
| --- | --- | --- | --- |
| Administrator (root, `CAP_SYS_ADMIN`) | mount/unmount, load the module, read and write the lower, bypass every permission, create workspaces | — | the only boundary the layer does not protect |
| Agent (own uid, no capabilities) | read and write its directory; read `shared/` according to group/ACL; set `user.moob.*` on files it can write; `QUERY` over what it can see | see results from inaccessible directories (silently omitted); mount; access the lower | changing `namespace` or `agent` in an attribute does not widen access |
| Process in a container (aligned uid) | same as the agent, limited to the mounts it receives | `QUERY` if it only has a subdirectory (2.5); mount the layer | mount in a userns and idmapped mount refused |
| Whoever has access to the disk/lower | everything (outside the layer) | — | mitigation: dm-crypt/fscrypt on the lower, outside the layer's scope |

Verified in the phase 3b suite (ext4, btrfs, XFS): no leak between two uids and namespaces;
attribute reassignment does not grant access; `chmod` takes effect immediately; isolation after
remount; 600 private candidates and 6 authorized ones across more than one batch. Also verified on 2026-10-10 (journal 0020): POSIX ACLs through the layer, identity with
`moobfs-check`, `mount_setattr` idmap refused with `EINVAL` and mount in a user namespace refused
with `EPERM`. Pending: KASAN on the branch and `chmod`/`rename`/`unlink` concurrency with queries.

## 6. Packaging and compatibility

| Topic | Proposal | Status |
| --- | --- | --- |
| DKMS | `dkms.conf` with name `moobfs`, repository version, `PRE_BUILD` that refuses a kernel outside the matrix, `POST_INSTALL` that runs `depmod`; source tree: `layer/*.c`, `layer/moobl.h`, `include/*.h`, `common/attr.c` | EXISTS in a VM: `BUILD_EXCLUSIVE_KERNEL` instead of `PRE_BUILD`; DKMS runs `depmod` itself (ADR-0028) |
| Packages | `moobfs-dkms` (sources + `dkms.conf`), `moobfs-tools` (`moobfs-attr`, `moobfs-check`, `moobfs-index`, future `moobfs`). Do not package `mkfs.moobfs`/`fsck.moobfs` (M1 frozen) by default | EXISTS in a VM, not published (ADR-0028) |
| Kernel matrix | see table below | EXISTS (two rows) |
| Signing | generate a project key for release builds; users with Secure Boot sign the DKMS `.ko` with their own MOK; `CONFIG_MODULE_SIG_FORCE` rejects unsigned modules | EXISTS in a VM with the host's own DKMS key, no project key (ADR-0028, decision 3; S10) |
| Reproducible build | the same commit and kernel headers give the same unsigned `.ko`, whose hash is recorded; signing is applied afterwards | EXISTS in containers: `make repro-test`, two builds per listed kernel byte-identical, DKMS's `MAKE` line gives the same module, and the two `.deb` packages reproducible (journal 0236, S10) |

Kernel matrix (what was actually run):

| Kernel | Functional suite (`make vm-test`) | KASAN/UBSAN/lockdep | Status |
| --- | --- | --- | --- |
| Debian 6.12.111+deb13-amd64 | yes, in QEMU TCG | no (distro kernel without these detectors) | EXISTS |
| Debian 6.12.113+deb13-amd64 | yes, in QEMU TCG (since journal 0203) | no (distro kernel without these detectors) | EXISTS, supported since journal 0206 |
| Upstream 6.12.112 (virtme-ng, `vm/kasan.config`) | full `vm/suite.sh` | yes | EXISTS; for the branch `m3/3b-query-authz`, journal 0020 lists KASAN as pending |
| Any other kernel, series or architecture (6.1/6.6 LTS, 6.13+, ARM64, other distro 6.12.x) | no | no | not supported; requires a port and the full suite plus KASAN |
| macOS / Windows | — | — | only the portable M1 tools compile; the module is Linux |

## 7. Operational and security risks

| Risk | Effect | Mitigation | Status |
| --- | --- | --- | --- |
| The module is ring 0 code | a bug becomes a failure of the whole kernel, including DoS by an ordinary user | disposable VM for development; S1 to S9; do not install on critical production hosts before the technical preview and stages S1 to S4 | EXISTS (VM); S1-S9 FUTURE |
| No KASAN/lockdep on distro kernels | memory corruption can go undiagnosed | suite under KASAN before each release; debug kernel in staging | EXISTS (workflow); no continuous coverage of the current branch |
| Secure Boot/lockdown, production hosts, absence of syzkaller and fstests | module does not load without a signature; ioctl and xattr surface not fuzzed in the kernel | MOK signing; S2, S3, S10; do not use in production before the preview | MOK signing by the host's DKMS key EXISTS in a VM (ADR-0028); the rest FUTURE |
| Root bypasses the layer | unrestricted read and write on the lower | document; dm-crypt/fscrypt; auditing | structural limit |
| Lower modified with the layer mounted | stale index and paths; undefined behavior (overlayfs rule) | 0700 permissions; do not back up by writing; `moobfs-check` afterwards | EXISTS (rule); real-time detection does not exist |
| Index memory | `index_budget` of up to 4 GiB per mount; several mounts add up; index and query buffers are charged to the caller's memory cgroup (ADR-0018) | per-mount budget and per-uid shares (ADR-0027); `GET_INFO` monitored; `memory.max` on the users' cgroups as the per-user ceiling, tested when hit (journal 0235) | EXISTS; S9 done with ADR-0029 (accepted 2026-10-29) |
| Stale paths after renaming a directory | `QUERY` with false negatives until remount (`MOOB_FLAG_PATHS_STALE`) | avoid renaming namespace directories in production; remount afterwards; dentry alias covers cached entries | EXISTS (documented, measurement pending) |
| TOCTOU between `QUERY` and `open()` | result listed and then refused | client handles `EACCES`/`ENOENT` | v1 contract |
| Mount/userns/idmap | overlayfs CVE class avoided by flags turned off | flags turned off; `EINVAL` and `EPERM` tests in the suite (journal 0020) | EXISTS (code and test) |

## 8. Incremental plan

### Stage 1 — Lab (now; VM only)

Scope: what exists, in disposable QEMU (Debian 6.12.111; KASAN 6.12.112). Missing to close
phase 3b (journal 0020): KASAN over the branch and `chmod`/`rename`/`unlink` concurrent with
queries; the `unshare -Ur`+mount (`EPERM`) and `mount_setattr` idmap (`EINVAL`) tests are already
in `vm/suite.sh` and passed.
Acceptance: `make vm-test` ends with `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
`ISOLATION_PASS` on the three lowers; the KASAN workflow ends with `MOOBFS_KASAN_PASS`.
Done when: both green at the tagged revision, with the journal and `CHANGELOG.md` updated.

### Stage 2 — Technical preview (DKMS on pinned kernels, documented limits)

Scope: technical users in a VM or on a dedicated host with a kernel from the matrix.
Deliverables: `dkms.conf` blocking kernels outside the matrix; experimental `.deb` package
(`moobfs-dkms`, `moobfs-tools`); minimal `moobfs` CLI (section 3); operations and
container documents; S1 (threat ADR), S4 (hostile lower), S5 (lint), S6 (`SECURITY.md`, versioning
policy) completed; S2 (fstests) with recorded results; signing documented and tested
in a VM with lockdown (S10).
Acceptance:

1. install on a clean Debian from the matrix, mount, `workspace create`, run the integration
   contract as two uids; no leak between them;
2. upgrade and roll back the module without loss (`moobfs-check` and `QUERY` equal to the oracle before and after);
3. backup and restore with `rsync -aHAX`: `getfattr -R` and `cmp` identical;
4. kernel outside the matrix: DKMS refuses; a signed module loads with lockdown, an unsigned one is refused;
5. container with a bind mount of the agent's directory: read, write and xattr work with an
   aligned uid; `QUERY` on the subdirectory returns `ENOTTY` as documented.

Done when: a third party reproduces everything, the matrix and the limitations fit on one page and no leak is known.

### Stage 3 — Public release

Preconditions: **M4 completed and published** (W3 with per-candidate authorization, mount
and rebuild time, index memory, p50/p95/p99, write amplification) and decisions
on `QUERY_BATCH`, `READ_BATCH` and persistence recorded (DEPENDS ON M4);
**security roadmap** with S1 to S6 and S8 to S10 completed (S2 fstests, S3 syzkaller with
defined coverage, S9 limits, S10 signing/DKMS), S7 scheduled and S11 reviewed; KASAN,
lockdep, KFENCE and S8 options green on the full matrix.
Deliverables: packages for the distributions in the matrix, support policy, disclosure and
UAPI compatibility policy, README without promises beyond what was measured.
Acceptance: stage 2 repeated on each distribution/kernel in the matrix; syzkaller with no open
reports for a defined period; kernel upgrade with DKMS; scale above what was measured documented
as not validated.
Done when: everything is green and the journals carry the numbers.

## 9. Repository files that would need to change

This document creates only `docs/distribution-strategy.md`.
| File | Change | Stage |
| --- | --- | --- |
| `dkms.conf` or `packaging/dkms/` (new) | DKMS definition | 2 |
| `packaging/debian/`, `packaging/rpm/moobfs.spec` (new) | `.deb` (2) and `.rpm` (3) packages | 2-3 |
| `tools/moobfs.c` (new), `tools/query-common.h`, `Makefile` | `moobfs` CLI; `install`, `dist`, `dkms-test` targets | 2 |
| `vm/suite.sh`, `vm/Dockerfile`, `vm/in-container.sh` | userns, idmap, bind mount (`ENOTTY`), upgrade/rollback tests; `dkms`, `rsync` packages | 1-2 |
| `.github/workflows/ci.yml`, `kasan.yml` | DKMS job, kernel matrix, signing, scheduled jobs | 2-3 |
| `docs/operations.md`, `docs/container-integration.md`, `docs/compatibility.md` (new) | operations, containers, matrix and UAPI policy | 2 |
| `docs/testing.md`, `docs/spec-m3.md`, `docs/security-roadmap.md` | packages and installation; cross-reference for cloning and for the root in containers (§2, §3.1); status of S1-S10 | 2-3 |
| `docs/decisions/` | new threat model ADR (S1) with a sequential ID; no existing ADR is changed | 2 |
| `docs/history/`, `CHANGELOG.md` | journal per delivery with reproducible commands | all |
| `README.md`, `README.pt-BR.md`, `ROADMAP.md`, `SECURITY.md`, `site/` | link to this document, kernel matrix, warnings, disclosure, installation page | 2-3 |
| `include/moobfs_ioctl.h`, `layer/index.c`, `layer/main.c` | no change planned; any adjustment (for example, `QUERY` on a subdirectory bind) would require a specification, tests of invalid inputs and a UAPI compatibility decision | — |
