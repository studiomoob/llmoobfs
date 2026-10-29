# ADR-0011 — Threat and credential model of the stacked layer

Date: 2026-10-12. Status: **accepted by the maintainer on 2026-10-12**. Item S1 of
`docs/security-roadmap.md`; community action 16. Describes the module as implemented on `main`
(ADR-0007, ADR-0009, phases Q2a to Q2e); where the code and this text disagree, the findings
below say so and a later step fixes the code, not the text.

## Context

Reviewers cannot judge a stacked filesystem's credential scheme without a written model; the
overlayfs `override_creds=off` discussion is the usual example. `SECURITY.md` already says what
counts as a vulnerability, and journals 0028 and 0030 left two questions for this ADR (path-based
LSMs and the `QUERY` timing channel). This ADR fixes the adversary, the promises, the non-promises
and the unprivileged surfaces, so that S2, S4, S9 and outside reviews test against a stated contract.

## Assets

- The contents, names and `user.moob.*` attributes of each user's memories.
- The integrity and availability of the running kernel.
- The data on the lower filesystem.

## Adversaries

- **A1 — Unprivileged local user** with ordinary access to the mount point, including an agent
  running under a user's own uid without `CAP_SYS_ADMIN`, `CAP_DAC_OVERRIDE` or
  `CAP_DAC_READ_SEARCH`. This is the main adversary.
- **A2 — A process writing to the lower tree directly** while the layer is mounted, bypassing it.
- **A3 — The lower tree's content on disk** at mount time, prepared by whoever could write to it
  (A2 earlier, another system, a restored backup).

Trusted: root and holders of `CAP_SYS_ADMIN` or `CAP_DAC_OVERRIDE` in the initial user namespace,
the mounter, the kernel and the lower filesystem's code, the hardware and the block device.

## Credentials (as implemented)

- **Mounting** needs `CAP_SYS_ADMIN` in the initial user namespace: the filesystem type sets no
  `FS_USERNS_MOUNT` and no `FS_ALLOW_IDMAP`, so user-namespace mounts fail with `EPERM` and
  idmapped mounts with `EINVAL` (journal 0020). This removes the class of overlayfs CVEs that
  needed unprivileged mounts (CVE-2015-1328, CVE-2016-1576, CVE-2020-16120). It is a decision, not
  an accident: enabling either flag needs a new ADR.
- **Name operations** (lookup, create, mkdir, unlink, rmdir, rename, link, symlink, setattr,
  `setxattr`/`removexattr`, ACLs) run with the **caller's** credentials, and `permission()` is the
  lower inode's own check with the caller's credentials. New files belong to the caller.
- **Data operations on an open file** (read, write, mmap, fsync, fallocate, FIEMAP, reflink,
  dedupe, `copy_file_range`, directory reads) run with the **mounter's** credentials captured at
  mount time, through `backing_file` or `override_creds`, after the VFS checked the caller's
  permission on open. This is the overlayfs model; it is safe only because the open was authorized
  for the caller first.
- `user.moob.format` on the lower root is reserved to the tools (`EPERM` through the layer).

*Note, 2026-10-18 (journal 0120).* Until journal 0120, directory reads did not do what the bullet
above says: `moobl_iterate()` called `iterate_dir()` with the caller's credentials. They now run
under `override_creds()` with the mounter's, as the bullet states.

*Note, 2026-10-14 (journal 0099).* "This is the overlayfs model" is wrong, as an overlayfs
maintainer pointed out on linux-unionfs. Overlayfs does not split name operations from data
operations: every access is checked twice, (a) the caller against the overlay inode, DAC and
MAC, and (b) the mounter against the real inode, DAC and MAC, and the real operation runs with
the mounter's credentials, name operations included (`Documentation/filesystems/overlayfs.rst`,
"Permission model"; `ovl_override_creds()` in `fs/overlayfs/dir.c` around create, rename and
unlink, v6.12.112). The two bullets above describe what MoobFS does, and that stays true; what
was wrong is the claim that it is what overlayfs does. In MoobFS a name operation is checked
only against the caller, on the layer and on the lower, and nothing checks the mounter; a data
operation is checked against the caller when the file is opened and against the mounter on the
backing file, where the lower's LSM hooks (`security_file_permission()` from
`rw_verify_area()`) see the mounter.

## Promises

Against A1:

1. **No crash, hang, memory corruption or kernel warning**, denial of service included.
2. **No privilege escalation**: the layer never lets the caller do on the lower what the lower
   would refuse to the same caller directly.
3. **No exposure beyond the lower through the query interface.** `QUERY2` is issued on a domain
   directory, checks `MAY_EXEC | MAY_READ` on it with the caller's credentials, and answers only
   from that domain's partition, which holds only files every traverser can read (homogeneity
   rule, `docs/spec-query-v2.md` §3; fallback to per-result checks after a permission change of a
   directory). It does not walk other domains, so its time does not depend on them (journal 0043).
   With domains on, v1 `QUERY` is limited to `CAP_SYS_ADMIN`.
4. **Typed attributes are validated** in the kernel on every write through the layer, with the
   same code as the tools (`common/attr.c`).

Against A2 and A3:

5. **No crash, hang or memory corruption**, whatever the lower contains or however it changes.
   Attributes read from the lower are untrusted: the mount scan skips invalid names and encodings
   (they are not indexed; `moobfs-check` reports them) and never fails the mount for them.

## Non-promises

1. **Confidentiality against trusted parties** (root, `CAP_DAC_OVERRIDE`, access to the device) and
   at rest. For that: dm-crypt or fscrypt on the lower.
2. **Integrity against A2.** Changes made behind the layer reach the index only at the next mount;
   until then `QUERY`/`QUERY2` may return stale or missing results (never results from a domain the
   caller cannot traverse). This includes a file made private directly on the lower (`chmod 600`
   behind the layer): until remount, other traversers of its domain can still see its name and
   attributes in `QUERY2` results, though not open it. Changing permissions through the layer
   takes effect on the next call. `moobfs-check` detects the divergence; nothing does in real time.
   *Note, 2026-10-28 (journal 0216, GHSA-756j-cgwq-gvwm):* a directory or file removed behind the
   layer whose inode number the lower then gave to a new one was answered from the removed one's
   index (a domain) or record (a file): answers that belonged to another directory or file, not
   merely stale ones. Domain and file records now keep the lower inode's `i_generation`;
   a differing generation rebuilds the domain record and drops the file record. Reported privately
   by an external reviewer, credited in the
   [advisory](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-756j-cgwq-gvwm); no
   finding number was given.
3. **Path-based LSMs** (AppArmor, Landlock) do not filter query results. A confined process can see
   in the results a name it could not open; it already sees it by listing the directory and reading
   its attributes, which Landlock does not control (journal 0030). To hide a domain from a
   confined process, deny it the domain directory itself.
   *Note, 2026-10-27 (journal 0215, ADR-0025):* "it already sees it by listing the directory"
   held only for directories the process may read. For a search-only directory it did not, and
   that was finding F15 below; since this date an answer requires read and search of every
   directory on the way, so the argument holds again for mode bits and ACLs.
4. **`domain_depth=0`** keeps v1 open to everyone with per-result checks and its timing channel
   (about 1 µs per hidden match, journal 0030). It is an administrator's choice for single-user
   mounts and is documented as such.
5. **Fair sharing of the index budget** (finding F2 below) and other resource limits (S9).

## Unprivileged surfaces

`getxattr`, `setxattr`, `removexattr` and `listxattr` on `user.moob.*`; `MOOB_IOC_QUERY2` and
`MOOB_IOC_QUERY2_BATCH` on a domain directory; `MOOB_IOC_GET_INFO` on the mount root;
`FICLONE`, `FICLONERANGE`, `FIDEDUPERANGE`, `FIEMAP` and `copy_file_range` (delegated, mounter's
credentials after an authorized open); every ordinary file and directory operation. There are no
export operations, so `open_by_handle_at` does not apply. These are the targets for S3 (syzkaller)
and S7 (fuzzing).

## Findings recorded with this ADR

- **F1 — `GET_INFO` counters cross users.** Any process that can open the mount root gets the
  index totals (`index_entries`, `index_inodes`, `index_used`, `heterogeneous`,
  `fallback_domains`), which sum every user's memories. Polling them shows another user's write
  rate. This contradicts promise 3 and `SECURITY.md` ("including through counts"). Proposed fix:
  the counters only for `CAP_SYS_ADMIN`, zero for others, with the static fields (limits, lower
  type, capabilities, flags) unchanged; a UAPI note and a test. **Fixed on 2026-10-12** (journal
  0061): `MOOB_FLAG_COUNTERS` marks a reply that carries them.
- **F2 — One budget for all users.** `index_budget` (128 MiB by default) is shared by the mount. A1
  with write access can fill it with attributes; the index is then marked incomplete for everyone
  until remount (`MOOB_FLAG_INDEX_INCOMPLETE`, one `pr_warn`). Not a crash, but a cross-user
  degradation of the query service. Kept as a known limitation under S9 (per-uid or per-domain
  accounting is the candidate); `GET_INFO` and `moobfs-check` make it visible.
  *Note, 2026-10-18:* the flag reached only `GET_INFO`, so queries kept answering from the
  stale domain. [ADR-0016](0016-failed-index-update-rebuilds-its-domain.md) confines a failed
  update to its own domain, which is built again by its next query. Fair shares of the budget
  remain under S9.
  *Note, 2026-10-25:* [ADR-0021](0021-budget-release-keeps-the-builders-domain.md)
  stops a budget release from dropping a domain last built by another
  filesystem uid. The query returns `ENOMEM` when the remainder is too small.
  One uid can still fill the whole budget by releasing that uid's own domains.
  A reserved fraction is not implemented, so F2 stays open under S9. Journal
  0192 records the host regression. The shrinker can still drop any idle domain.
  *Note, 2026-10-28 ([ADR-0027](0027-fair-shares-of-the-index-budget.md), journal 0228):* each
  domain counts its own bytes, and a build of a uid below its share may release the domains of a
  uid above it (the share is the budget divided by the uids with a built domain); a uid within
  its share keeps its domains against every other uid's build. F2 is fixed.

Added on 2026-10-12, from the review of journal 0066 (`docs/research-stacked-fs-kernel-rules.md`):

- **F3 — a write to a set-user-ID file crashed the kernel.** `setattr` forwarded `ATTR_MODE`
  together with `ATTR_KILL_SUID`/`ATTR_KILL_SGID` to the lower, a pair `notify_change` refuses
  with `BUG()`. Any user could reach it by writing to or truncating a set-user-ID or set-group-ID
  file, and any `chown` of such a file did too. It broke promise 1. **Fixed on 2026-10-12**
  (journal 0069), with a regression test.
- **F4 — the mounter credentials kept `CAP_SYS_RESOURCE`.** Delegated writes ignored the quota
  hard limit and could use space reserved by the lower, against promise 2. The capability is
  lowered since journal 0069, as overlayfs does. **One case stays open:** ext4 also grants its
  reserve to the reserve owner's uid, 0 by default and the fsuid of the mounter credentials, so
  with the default `resuid` an ordinary user writing through the layer still spends that reserve.
  Promise 2 does not hold for it; the README lists the workaround (another `resuid`).
  *Note, 2026-10-18:* fixed in journal 0132. Writes, `fallocate`, clones and `copy_file_range`
  run with the mounter's credentials but with the writer's fsuid, fsgid and groups, so ext4
  grants its reserve exactly as it would to that writer directly. Promise 2 now holds for
  the reserve as well.
  *Note, 2026-10-27 (journal 0218):* those credentials also carry `CAP_FSETID` only when the
  writer has it in the initial user namespace; with the mounter's, a set-user-ID bit given on the
  lower behind the layer survived the write (F16).

- **F5 — copy and clone kept set-user-ID, set-group-ID and file capabilities** (found on
  2026-10-13 with fstests, journal 0074). `copy_file_range` and a clone ran on the lower with the
  mounter's credentials without first dropping the target's privileges with the caller's, so an
  ordinary user with write access to a set-user-ID file could change its content and keep the bit,
  against promise 2. **Fixed** in journal 0074, with the related failure of ordinary writes to
  files that carry capabilities. The general lesson is recorded there: every operation delegated
  with the mounter's credentials must be checked for decisions the lower takes from the caller's
  capabilities.

Added on 2026-10-13, with the tests of promise 5 (security roadmap S4, journal 0073). The
behaviour on input the layer did not write is now stated in full and tested:

- An attribute on the lower with a name outside the rules, or a value that fails the encoding, is
  not indexed, does not fail the mount and is not logged; `moobfs-check` reports it. `getxattr`
  through the layer returns it unchanged: the layer validates what it writes, and a client that
  reads raw attributes validates what it reads, as the tools do.
- The limit of 64 typed attributes per file applies to writes through the layer. A file that
  already has more on the lower is indexed whole, and further attributes cannot be added to it.
- The format marker `user.moob.format` on the lower root must be a decimal version this module
  knows. Anything else refuses the mount, including an empty marker and one longer than a version
  number, which were accepted as "no marker" until this date.
- What changes on the lower behind a mounted layer reaches the index at the next mount
  (non-promise 2); until then calls return stale or failing answers, never a crash or a hang.

Added on 2026-10-13, from the review of journal 0079 (the follow-up F5 asked for). Every site
that acts on the lower with the mounter's credentials was checked against the kernel source for
decisions taken from the caller; the table is in that journal. No new finding. Two things are now
part of the credential model: the file flags (`lsattr`, `chattr`) are name operations, run with
the caller's credentials; and the layer's inode carries only the lower's `S_SYNC`, `S_NOATIME`,
`S_APPEND` and `S_IMMUTABLE` flags, which are the ones the VFS enforces before the layer is
called. Not covered by the review: behaviour under a Linux security module policy.

Added on 2026-10-14, from the first run under SELinux enforcing (security roadmap S12, journal
0104):

- **F6 — `QUERY2` named files that SELinux hid from the caller.** The homogeneity rule reads
  mode bits and ACLs only, so a domain's partition could hold a file whose label denies the
  caller even `getattr`; a confined process learned its name, inode number and that its
  attributes matched, which it cannot learn on the lower directly. It broke promise 3. **Fixed**
  in journal 0104: when an LSM keeps labels for the layer's inodes, every `QUERY2` result is
  checked for the caller (path walk and `inode_permission(MAY_READ)`), as after a permission
  change. Non-promise 3 (path-based LSMs) is unchanged.
- **Credentials:** `security.*` attributes are now read from the lower with the mounter's
  credentials, as overlayfs reads every attribute. An LSM reads a new inode's label through the
  layer with the credentials of whoever reached the file first; a confined first caller was
  refused and left the inode unlabelled, logging at every try. A caller's own read of such an
  attribute is checked by the VFS against the layer's inode first. Other attributes are read
  with the caller's credentials, as before.
- Under SELinux the layer needs a labelling rule, `fs_use_xattr moobfs_layer`
  (`vm/selinux/moobfs_layer.cil`); without it every file through the layer is unlabelled and
  confined processes are refused everything.

Added on 2026-10-14, from the code review of journal 0105:

- **F7 — a query could hold up the queries of every user of the mount.** Results were copied to
  user space with `query_sem` held; a copy waits on a page fault for as long as whoever backs the
  caller's buffer decides, and a writer of the semaphore (the rebuild of a stale domain, a
  release over the budget, both reachable by any user) and every reader behind it waited too. It
  broke promise 1. Confirmed by a lockdep assertion at the copy site; **fixed** in journal 0105:
  results are gathered in a kernel buffer and copied after the semaphore is released, as btrfs's
  search ioctl avoids faulting under its locks. v1 `QUERY` also reads and checks its request
  before building anything.

Added on 2026-10-14, from syzkaller (journals 0109, 0112):

- **F8 — a failed mount kept a reference on the lower root.** When an allocation failed while
  the root dentry was set up, the reference taken on the lower root was never dropped; the lower
  could not then be unmounted cleanly, a leak of the kind that can end in a use-after-free.
  Reachable only by whoever may mount, but it broke promise 1. **Fixed** in journal 0112, with
  the mount added to the fault-injection test and the kernel log checked after the lower is
  unmounted.

Added on 2026-10-15, from an independent source review of 5582e06 (journal 0114, published
later) and its verification against Linux 6.12.112 (journals 0116 and 0117):

- **F9 — `QUERY2` answered group members a file only "others" could read.** Condition 1 of the
  homogeneity rule (spec-query-v2 §3) admitted a file whose bit for others was set, but the
  kernel judges the owner by the owner bits and a group member by the group bits, never by
  those of others (`acl_permission_check`). A file 0604 of group `staff` was answered, without
  a per-result check, to members of `staff` who cannot read it: its name, inode number and,
  through probes, its values. Same for intermediate directories 0701. It broke promise 3; the
  user-space oracle carried the same rule, so the tests comparing with it could not see it.
  **Fixed** in journal 0116: condition 1 asks the bit of all three classes, in the kernel and in
  both tools, and a new test compares answers with the lower's own decision.
- **F10 — a query waiting on a FUSE mount held up every query of the mount.** Per-result checks
  (a domain after a permission change inside it, every domain under an LSM with labels) walked
  each candidate's path with `query_sem` held, in the caller's mount namespace. A FUSE mount
  over a directory of the domain whose daemon never answers made the walk wait without end
  (`request_wait_answer`); a rebuild then queued for writing and every later query behind it
  (`rwsem_down_read_slowpath`). It broke promise 1, the class of F7. **Fixed** in journal 0116:
  the checks of a chunk run with `query_sem` released and the domain is pinned again before the
  next chunk; the walk does not leave the mount nor follow symbolic links.
- **F11 — changes through the layer ignored the lower mount.** The system call takes write
  access on the layer's mount only, and the layer never took it on the lower's: a lower given as
  a read-only bind mount was written anyway, which breaks promise 2 (the lower refuses the same
  caller directly), and with the lower ext4 frozen any user's create, `chmod` or `setxattr`
  reached ext4 and hit `WARN_ON(... SB_FREEZE_COMPLETE)` in `ext4_journal_check_start()` before
  waiting for the thaw, which breaks promise 1. Data writes were not affected: the backing file
  holds write access on the lower mount and `vfs_iter_write()` takes the freeze protection.
  **Fixed** in journal 0117: every change to the lower takes `mnt_want_write()` on the lower
  mount first, as overlayfs's `ovl_want_write()`; suite step `lowerro`, with a negative round.
- **F12 — an idmapped lower mount was accepted and its mapping ignored.** Every delegated call
  passes `nop_mnt_idmap`, so on an idmapped lower mount the checks used the on-disk ids, not the
  ones that mount shows: a caller could act on files the lower mount gives to someone else.
  Reachable only by an administrator's choice of `lower=`, and already noted as R5 of
  `docs/project-review-2026-10-10.md` without a fix. **Fixed** in journal 0117 by refusing such
  a lower at mount (compatibility decision below).
- **F13 — a chmod during the first build of a domain left the file in its answers.** The scan
  computed a file's homogeneity before its loader registered, and stored it after a re-read
  caused by a chmod that found no record to update. A file made 0600 stayed in the homogeneous
  partition, and `QUERY2` returned its name and confirmed its values to users whose `getxattr`
  fails with `EACCES`. It broke promise 3. Found by the independent review of journal 0151.
  **Fixed** in journal 0152 (pull request 106): the check runs inside the loader, after the
  registration and at every re-read. Advisory GHSA-5vmj-wjqc-7gx6, published.
  *Dated note, 2026-10-20 (journal 0156): recorded here late; at the time it was cited only by
  review and journal.*
- **F14 — a directory more than 64 components deep was placed nowhere by the write paths.**
  `where_is()` stops at `MOOBL_MAX_DEPTH`, so a chmod of such a directory set no fallback and a
  value change below it was not applied, while the scan indexed the files below it in their
  domain. `QUERY2` kept answering files under a directory made private, with their old values.
  It broke promise 3; any user with a writable directory can build such a tree. Found by the
  round-2 review (journal 0155, R2-1) and reproduced in the VM. **Fixed** in journal 0156: with
  domains on, nothing more than 64 components below the mount root is indexed, a limit now
  stated in `README.md` and `docs/when-not-to-use.md`.
- **F15 — `QUERY2` named files under a search-only directory.** The homogeneity rule asked
  only the execute bit of an intermediate directory, and the per-result check resolved the path
  and asked `MAY_READ` of the file alone. A file under a `0711` directory of a `0755` domain was
  answered to every user who could enter the domain: its name, inode number and, through
  probes, its values, although none of them could list that directory. It broke promise 3; the
  user-space mirrors carried the same rule and `tests/permmatrix.c` judged by `getxattr`, which
  such a directory lets through. Reported privately by an external reviewer (2026-10-27),
  credited in the [advisory](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-95h9-j7c2-gcw6). **Fixed** in journal 0215 (ADR-0025): every directory on the way must grant
  read and search, in the kernel, the tools and the library; `permmatrix` lists each directory in
  its oracle and runs in fallback mode too.
- **F16 — a set-user-ID bit given on the lower behind the layer survived a write through it.**
  The write credentials of F4 kept the mounter's `CAP_FSETID`, so `setattr_should_drop_suidgid()`
  on the lower kept the bits for every writer, and the layer's own `file_remove_privs()` judged a
  mode its inode may hold from before a chmod made on the lower, then skipped the check once
  `S_NOSEC` was set. An ordinary user with write access to such a file could change its content
  and keep the bit, against promise 2; it needs the bit set behind the layer (A2), itself a
  fragile configuration. The lesson of F5 applied to the credentials themselves. Reported
  privately by an external reviewer, who also wrote the fix, credited in the
  [advisory](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-6xf4-r246-92r6). **Fixed** in journal 0218: the credentials carry `CAP_FSETID`
  only when the writer has it in the initial user namespace, the test the lower applies.
- **F17 — `QUERY2` answered a file whose minimal access ACL refuses the caller.** The
  homogeneity rule took an access ACL for extended only when it had more than three entries. A
  minimal ACL can still disagree with the mode (`u=rw,g=r,o=-` on a `0644` file refuses others,
  and `acl_permission_check()` follows the ACL), so such a file entered its domain's partition by
  its bits and was answered to users who cannot read it, against promise 3. It needs an ACL
  written outside the VFS (A2): ext4 replaces an ACL equivalent to the mode on every write
  through it. Found by the maintainer's team while aligning the user-space mirrors with the
  kernel ([advisory](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-82w5-ph62-47wf)).
  **Fixed** in journal 0222: an access ACL is extended when `posix_acl_equiv_mode()` finds it not
  equivalent to a mode or its bits differ from the file's; the tools and the library follow.
*Compatibility decision, 2026-10-15 (journal 0117).* An idmapped mount as `lower=` is refused
with `EINVAL`. Supporting it would mean passing `mnt_idmap(lower mount)` to every delegated call
and to the index's permission rule; that is left for a later decision with its own tests. No
released version accepted it on purpose, and no on-disk format changes.

## Decision

Adopt the adversaries, credentials, promises and non-promises above as the contract that
`SECURITY.md`, reviews and the S-items test against. Fix F1 before asking for independent review
(community action 15); track F2 in S9. `moobfs-check` prints the ADR number of the model it was
built against, so a report can say which contract it was judged by.

## Alternatives considered

- **Overlayfs-style `override_creds` for name operations too** (mounter's credentials everywhere):
  rejected; it would make every name operation depend on a correct emulation of the lower's
  permission checks, the source of most overlayfs credential bugs.
  *Note, 2026-10-14 (journal 0099):* this alternative is what overlayfs actually does (see the
  note under "Credentials (as implemented)"). "The source of most overlayfs credential bugs" was
  never checked against the overlayfs fixes and is withdrawn as a reason; the decision stands on
  the other one: a name operation run with the caller's credentials on the lower gets exactly
  the lower's own answer for that caller, with nothing to emulate.
- **Enable `FS_USERNS_MOUNT`** for rootless agents: rejected for now; it reopens the CVE class
  above. Rootless use goes through an administrator-mounted layer.
- **Filter query results by path-based LSMs**: rejected; the kernel has no general hook to ask
  "could this process open this path" without opening it, and the gain over listing is small
  (journal 0030).

## Consequences

S4 tests promise 5 with adversarial lower trees; S2 (fstests) and S3 exercise promises 1 and 2; F1
is the next code step of this series. `SECURITY.md` links this ADR as the model reports are judged
against.
