# Adoption risks and what answers each one

*Note, 2026-10-18: an analysis as of 2026-10-12, kept as written. The project's current status is in the README ("Status" and "Limitations") and `docs/security-roadmap.md`; several gaps named below have been closed since (syzkaller, fstests, the threat model, the index built on demand).*

Date: 2026-10-12. Status: **proposal**, written for the maintainer's review (journal
[0067](history/0067-adoption-risks.md)). It lists the risks any agent-memory system takes by
putting MoobFS under its data, the evidence for each, what already exists, and a proposed answer.
MoobFS is meant for every system of that kind, not for one client: ai-memory appears below only
because it is the one client tested so far, and risk 15 is about exactly that.
It orders work; it does not change the roadmap by itself. [when-not-to-use.md](when-not-to-use.md)
says who should not adopt the layer today; this document says what would have to be true for that
list to shrink.

"Answer" has four possible kinds, and each risk says which one applies:

- **fix**: a defect or gap with a known change;
- **build**: a planned or proposed piece of work that removes or bounds the risk;
- **contain**: the risk stays, and a deployment rule or a client convention limits its effect;
- **accept**: a consequence of the design (ADR-0001, ADR-0007) that will not be removed.

## Summary

| # | Risk | Kind | Answer in one line | Tracked under |
| --- | --- | --- | --- | --- |
| 1 | A module defect takes the host down | fix + build + contain | Fix the known crash; fstests, syzkaller, outside review; run in a dedicated VM | S2, S3, S8, action 15 |
| 2 | A defect in pass-through damages data | build + contain | fstests and fsx on every lower; snapshots of the lower | S2, S4 |
| 3 | The lower tree as hostile input | build | Test and fuzz the domain scan and the lower's attributes | S4 |
| 4 | Credential-model gaps found by review | fix | Drop the capability; state the inode-number limit | ADR-0011 |
| 5 | Linux only, a module and root are required | accept + contain | Clients detect the layer and keep their own path | ADR-0001 |
| 6 | Out-of-tree module against a moving kernel | build | DKMS, a kernel matrix in CI, a stated support window | S10 |
| 7 | Index in RAM, rebuilt after every mount | build (conditional) | Per-domain build on first query (done, journal 0098); release under memory pressure next | ADR-0012 |
| 8 | One index budget shared by all users | build | Per-domain accounting and a per-domain "incomplete" state | S9, F2 |
| 9 | Experimental UAPI | build | A version policy and a frozen UAPI at a stated milestone | S6 |
| 10 | The query language is small; SQLite stays | accept + build | Keep it small; a client library for unions and paging | ADR-0009 |
| 11 | The homogeneity rule excludes mixed-mode files | contain + build | A deployment recipe and a check tool that names the files | spec-query-v2 |
| 12 | Content and attributes are separate writes | contain | The temporary-file, attributes, rename convention | proposed here |
| 13 | Slower than SQLite on some operations | build + accept | Profile the per-call floor; publish where it loses | M4 |
| 14 | One maintainer, no outside review | build | Review requests, the KASAN check enforced, a second reviewer | actions 15 and 18 |
| 15 | The design is fitted to one client | build + contain | A client contract, a second and a third client, client quirks kept out of the layer | proposed here |

## 1. A module defect takes the host down

**Risk.** The layer is kernel code. A defect is an oops or a panic for every process on the
machine, where a defect in SQLite or in a FUSE daemon ends one process.

**Evidence.** Three kernel-side defects were found by the project's own tests before v0.2.0, and a
fourth by the client's test suite (journal 0057). A review on 2026-10-12
(`docs/research-stacked-fs-kernel-rules.md`, finding R-CRED-3, journal 0066) reports a crash an
unprivileged user can cause: writing to a set-user-ID file through the layer reaches a `BUG()` in
the VFS, because `moobl_setattr` forwards `ATTR_MODE` together with the kill-suid flags. That
finding is reported as reproduced in a VM and **not fixed**; it was not re-verified for this
document. *Note, 2026-10-12:* verified against the code and fixed in journal 0069, with the test
item 1 below asks for.

**Exists.** KASAN and lockdep on every change to `layer/` (CI and `vm/kasan-local.sh`), sparse and
`W=1`, checkpatch, a race test for `QUERY2` (journal 0052), ai-memory's 1 147 tests on the layer.

**Answer.**

1. *Fix* R-CRED-3 first, with a regression test in `tests/layer.c` (a write to a set-user-ID file
   as an ordinary user). It contradicts promise 1 of ADR-0011 and is the only known crash.
2. *Build*: S2 (fstests on the layer), S3 (syzkaller on the ioctls and `setxattr`), S8 (runtime
   hardening), and an audit that no user-reachable path can reach `BUG()` or `WARN()`.
3. *Build*: community action 15, review requests to people who maintain stacked filesystems.
4. *Contain*: until 2 and 3 are done, the supported deployment is a dedicated VM or host whose
   crash costs one service, never a shared machine. Say so in `SECURITY.md`.

**Residual.** Kernel code can always crash the host. Testing lowers the rate; it does not change
the failure domain. This is the price of ADR-0001.

## 2. A defect in pass-through damages data

**Risk.** Every read and write of the client's files goes through the layer. A wrong offset, a lost
`fsync` or a wrong rename is data loss that the lower filesystem cannot prevent.

**Evidence.** The rename defect of journal 0057 broke the atomic "write a temporary file, rename it
over" pattern (it failed with `EINVAL`; nothing was lost). No fstests run exists yet.

**Exists.** `fsx` and the pass-through tests on ext4, btrfs and XFS in `make vm-test`; unmounting
leaves a plain tree with ordinary extended attributes, so nothing is locked into a format.

**Answer.** *Build* S2 with the generic fstests groups on all three lowers and record the excluded
tests with reasons. *Contain*: document a snapshot schedule of the lower (btrfs or XFS reflink
copies) as part of any deployment recipe; the layer adds no backup of its own.

**Residual.** Bounded by what fstests covers; power-failure behaviour is the lower's.

## 3. The lower tree as hostile input

**Risk.** The mount scan reads every extended attribute of the lower tree, and a process with
access to the lower can write there behind the layer (adversaries A2 and A3 of ADR-0011).

**Evidence.** ADR-0011 states the case; a file made private directly on the lower stays visible by
name in its domain's `QUERY2` results until remount.

**Exists.** Attribute decoding is fuzzed in user space (`make fuzz`); invalid encodings are
skipped by the scan.

**Answer.** *Build* S4: crafted lower trees (oversized, malformed and hostile attribute sets,
directory loops through bind mounts, attributes changed between scan and query) in the VM suite,
and the scan under KASAN with those trees. Recommend in the deployment recipe that the lower
directory be mode 0700 and owned by the mounter, which removes A2 for ordinary users.

**Residual.** Root on the host is trusted by definition.

## 4. Credential-model gaps found by review

**Risk.** The layer performs data operations with the mounter's credentials. Two findings of the
2026-10-12 review say this leaks privilege or identity: the mounter credentials keep
`CAP_SYS_RESOURCE`, so a user's write can use space reserved for root and pass quota limits
(R-CRED-2); and `st_dev`/`st_ino` are not unique when the lower spans btrfs subvolumes (R-INODE-4),
which tools such as `tar`, `rsync` and `du` use to detect hard links.

**Evidence.** The review's reading of the code against v6.12; neither was re-verified here.

**Answer.** *Fix* R-CRED-2 the way overlayfs does: drop `CAP_SYS_RESOURCE` from the credentials
captured at mount, with a test that fills a filesystem as an ordinary user and checks `ENOSPC` at
the reserved margin. For R-INODE-4, the smallest answer is to *contain*: refuse at mount a lower
that crosses a subvolume or mount boundary, or document the limit and add it to `moobfs-check`.
A mapped inode space (overlayfs `xino`) is the full answer and is not proposed now.

**Residual.** None for R-CRED-2 once fixed. Each finding that changes a promise needs a dated
revision of ADR-0011.

*Correction, 2026-10-12 (journal 0069).* "None once fixed" was wrong. The capability is lowered
now, which closes the quota case, but ext4 also grants its reserve to uid 0, the fsuid of the
mounter credentials (`fs/ext4/balloc.c:650`). With the default reserve owner an ordinary user
still spends the reserve through the layer. Closing it needs delegated writes to run with the
caller's fsuid for the allocation, or a deployment rule (`resuid` other than 0); recorded as F4 in
ADR-0011 and in the README's limitations.

*Note, 2026-10-19 (journal 0132).* F4 is completed: delegated writes now carry the writer's fsuid,
fsgid and groups, so the reserve follows the writer with the default reserve owner too, and no
deployment rule is needed.

## 5. Linux only, a module and root are required

**Risk.** A client that depends on the layer stops working on macOS and Windows, in rootless
containers, in hosted CI and on any host where modules cannot be loaded. ai-memory runs in all of
them today.

**Evidence.** Mounting needs `CAP_SYS_ADMIN` in the initial user namespace (ADR-0011).

**Answer.** *Accept* the platform limit: it follows from ADR-0001. *Contain* it in the client's
design: the layer must be an optional backend. A client asks `MOOB_IOC_GET_INFO` on its data
directory and, when the call fails, uses its own index exactly as before. Stage 3 of
`client-test-plan.md` should be written that way, so the fork never requires the layer. On macOS
and Windows the supported route stays a Linux VM (journal 0017).

**Residual.** Where the layer is absent the client gains nothing and keeps both code paths.

## 6. Out-of-tree module against a moving kernel

**Risk.** The kernel has no stable internal interface. A distribution update can make the module
fail to build or, worse, build and misbehave. Only Debian 6.12.111 and upstream 6.12.112 are tested.

**Answer.** *Build* S10 as a matrix, not as packaging alone: a list of supported kernels (the
long-term series the tested distributions ship), the full VM suite and the KASAN job on each in
CI, DKMS that refuses to build outside the list unless forced, and a rule that a kernel enters the
list only with a green run. State the support window in `SECURITY.md`.

**Residual.** Each new long-term series costs porting work; with one maintainer that work has a
ceiling (risk 14). Upstream submission would remove this risk and is not planned.

## 7. Index in RAM, rebuilt at every mount

**Risk.** After a reboot or a remount, queries wait for a scan of every extended attribute, and the
index is unpageable kernel memory.

**Evidence.** 60 000 memories: 18.5 to 19.3 s with a cold cache, 0.73 s warm (journal 0055); about
1 MiB of index for 2 000 ai-memory pages with six attributes each (journal 0065), so the order of
30 MiB at 60 000.

**Exists.** `index_budget` bounds the memory; `MOOB_FLAG_INDEX_INCOMPLETE` reports a partial index.

**Answer.** *Build*, in order of cost, and only as far as measurements justify:

1. Report rebuild progress in `GET_INFO` and let files be read and written while it runs, so the
   client can fall back to its own index for those seconds instead of blocking.
2. Build each domain's partition on its first query instead of scanning everything at mount. A
   client that touches three projects after a reboot then pays for three.
3. An on-disk index only if M4 shows 1 and 2 are not enough; it is a format change and needs an
   ADR, a specification, invalid-input tests and a compatibility decision.

**Residual.** With 1 and 2 the first query on a domain after mount stays slow. Memory stays
unpageable by design.

*Note, 2026-10-14 (journal 0098).* Answer 2 is implemented (ADR-0012): nothing is scanned at
mount, and the first query on a domain builds its partition while the domain stays writable.
On 60 000 memories in eight domains, a cold mount went from about 20 s to 0.31 s and the index
memory with one domain in use from 36.4 MB to 4.5 MB; the first query on a domain of 7 459
files costs 2.7 s cold and 0.10 s warm, and the mount-wide `QUERY` still costs a full scan.
Answer 1 is not needed for the domain path; releasing built domains under memory pressure is
the second half of ADR-0012 and is not implemented.

## 8. One index budget shared by all users

**Risk.** Any writer can fill the budget and leave the index incomplete for every user until
remount (ADR-0011, finding F2). A second finding of the review: index memory is not charged to the
writer's memory cgroup.

**Answer.** *Build* S9: account entries per domain, give each domain a share of the budget (a
default fraction, a mount option to change it), and make "incomplete" a state of the domain that
filled its share rather than of the mount. Charge the allocations to the writer's cgroup
(`__GFP_ACCOUNT`) so a container's limit applies. Add the test: one user fills the share, another
user's domain still answers completely.

**Residual.** Inside one domain, writers still share one limit; that matches the homogeneity rule,
where a domain has one owner.

## 9. Experimental UAPI

**Risk.** A client written against today's ioctls can break at the next release. `GET_INFO`
changed on 2026-10-12 (journal 0061).

**Exists.** Version fields in every request, reserved fields checked for zero, capability and flag
bits in `GET_INFO`.

**Answer.** *Build* S6: a written version policy (what may change inside a UAPI version, what needs
a new one, how long the previous one keeps working), a compatibility test that runs the previous
release's client binary against the new module, and a named milestone at which UAPI 2 is declared
stable. Until then every release note lists UAPI changes first, as the changelog already does.

**Residual.** Before the freeze, clients must pin a module version.

## 10. The query language is small; SQLite stays

**Risk.** `QUERY2` is a conjunction of at most four terms with one ordering attribute. There is no
OR, join, aggregate, full-text or vector search. A client keeps its database for those, so it runs
two mechanisms instead of one.

**Evidence.** In stage 2, "kind is rule or decision" needed a `QUERY2_BATCH` of two requests, and
ai-memory's handoffs, sessions, full-text and vectors stay in SQLite.

**Answer.** *Accept* the limit: ADR-0009 keeps the kernel's part to what must be authorized by the
kernel. *Build* a small user-space library (C, with a documented ABI other languages can bind)
that turns "attribute in a set", paging and unions into batches, so every client does not rewrite
them. A kernel-side extension is considered only when a client shows a measured need that the
library cannot meet.

**Residual.** Two mechanisms remain. The gain claimed is removing the duplicated structured
columns and their synchronization, not removing the database.

## 11. The homogeneity rule excludes mixed-mode files

**Risk.** A file whose owner or mode differs from its domain's is left out of `QUERY2` answers.
A client that creates files with default modes silently gets empty or partial answers.

**Evidence.** ai-memory's pages were all heterogeneous until the server ran with `umask 077`
(journal 0064).

**Exists.** `heterogeneous` and `fallback_domains` in `GET_INFO` (administrators only since journal
0061); a fallback mode that checks each result.

**Answer.** *Contain* with a deployment recipe per client (owner, `umask`, directory modes) kept
next to the client test plan. *Build* into `moobfs-check` a report that names the files failing the
rule and the reason, runnable by the domain's owner, so "why is my page missing" has a one-command
answer. An unprivileged way to learn that one's own domain is heterogeneous is needed, since the
counters are now hidden; a per-domain flag in the `QUERY2` reply is the proposal.

Note of 2026-10-13: the second client met this on its first run (journal 0089), and
`moobfs-check --domain-depth N` now lists the files left out with the reason (journal 0090). It
runs on the lower tree, so it serves the administrator; a client confined to its domain still
has no way to ask.

**Residual.** The rule is what makes one permission check per query sound; it stays.

## 12. Content and attributes are separate writes

**Risk.** A crash between writing a file's content and setting its attributes leaves a file that
queries do not find, or find with old attributes. SQLite updates row and index in one transaction.

**Answer.** *Contain* with the convention the benchmark's W1 already uses: write a temporary file,
set every attribute on it, `fsync`, rename it over the final name, `fsync` the directory. The page
then appears with all its attributes or not at all. Document it as the required write sequence,
give the library of risk 10 a function for it, and add a crash-consistency test (kill between each
step, remount, compare the index with the oracle).

**Residual.** Updates that change attributes without rewriting the file are not atomic as a group;
clients that need that must rewrite the file.

## 13. Slower than SQLite on some operations

**Risk.** A client may get slower by adopting the layer.

**Evidence.** Incremental traversal at 0.74 to 0.86 of SQLite's speed (journal 0065); attribute set
24.5 µs against 10.1 µs and remove 43.6 against 16.0 µs without the layer; open by path 12.5 against
9.3 µs (journals 0027 and 0028). ai-memory's own write is about 10 % slower on the layer (2 000
pages, one run; journal 0068).

**Answer.** *Build*: profile the per-call floor of `QUERY2` (about 15 µs; the size of the request
structure is the suspect) and the 272-byte result record, which dominates long traversals; both
have candidate changes that belong in a UAPI revision. *Accept* and publish the rest: the M4 report
must list the operations where the layer loses, as the benchmark plan requires.

*Note, 2026-10-13 (journal 0072).* A larger cost than those was measured afterwards: a result is
a path, so a client that needs fields of the results reads them file by file, 2 µs for one
attribute and 16 µs for all, against 0.9 µs per result for the query. Returning chosen values with
the results is proposed there.

**Residual.** Per-file overhead on open and on attribute writes is inherent to stacking.

## 14. One maintainer, no outside review

**Risk.** Every item above depends on one person, and no one outside the project has reviewed the
kernel code. A client cannot rely on fixes arriving.

**Answer.** *Build* community action 15 (review requests, possible now that ADR-0011 exists) and
action 18 (the KASAN check enforced by branch protection). Proposed here, not yet in any plan: a
rule in `GOVERNANCE.md` that changes under `layer/` need a second reviewer once one exists, and a
statement in `SECURITY.md` of the response time the project actually offers.

**Residual.** Until a second person can review and release kernel changes, this risk is open, and
it caps how far risks 1 and 6 can be reduced.

## 15. The design is fitted to one client

**Risk.** MoobFS is meant for any memory system for agents, but every real-client result so far
comes from ai-memory. Choices made to make that client work (projects as domains at depth 2, the
six query shapes, `umask 077`, the attribute names of the mirror) can become the design, and a
second system may then not fit. A related mistake is to read a client's own behaviour as a
property of the layer, or to change the layer to suit it.

**Evidence.** `research-client-query-patterns.md` read the source of six systems (ai-memory, mem0,
two generations of Letta, Graphiti, the LangGraph store, OpenAI Agents SDK sessions) and `QUERY2`
was designed from their common needs: scope by equality, a time range or ordering with a small
limit, "missing or greater than", and lists handled by batches. Only ai-memory has been run. Two
findings of stage 2 are that client's own and say nothing about the layer: its write cost grows
with the store, on plain ext4 as on the layer, because every write checkpoints the whole tree in
git, and its scheduled maintenance rewrites the workload (journals 0065 and 0068). Not known: how
many of the other five keep each memory as a file, which the layer requires.

**Answer.**

1. *Build* a client contract, one document that any system can be checked against without reading
   ai-memory's test plan: memories are files; scope maps to a directory that is a domain;
   structured fields are typed attributes with a naming convention; the write sequence of risk 12;
   detection and fallback of risk 5; what stays in the client's own store.
2. *Build* stage 2 for a second and a third system from the survey, chosen to differ from
   ai-memory (a different scope depth, a different store). For a system that keeps memories in a
   database and not in files, first decide and record whether it is in scope at all.
3. *Contain*: a change to the layer or to the UAPI needs a reason that holds for more than one
   surveyed system. Client-specific handling goes in that client's adapter or in the library of
   risk 10, never in `layer/`.
4. In every journal of a client test, separate what was learnt about the layer from what was learnt
   about the client.

**Residual.** Until a second client runs, generality is a claim supported by reading code, not by
a test.

Note of 2026-10-13: the second client is chosen, the LangGraph store (journal 0081). Not run yet.

## Proposed order

1. Risk 1, item 1 and risk 4: the two fixes from the review (small, both against stated promises).
2. Risk 12 and risk 11: conventions and the check tool, cheap and needed by stage 3.
3. S4 and S2 (risks 2 and 3), already next in the roadmap after stage 2.
4. Risk 8 (S9) and risk 7 items 1 and 2.
5. Risks 6, 9 and 10 with the distribution stage; risk 14 in parallel throughout.
6. Risk 15: the client contract before stage 3, so the fork is written against it; a second client
   after stage 2 closes.

This order is a proposal; the agreed order of journal 0059 stands until the maintainer changes it.
