# Specification: per-domain `QUERY` (UAPI v2)

Status: **accepted specification**, 2026-10-11, following from ADR-0009 (alternative E, accepted). The four
points marked **[accepted decision]** were accepted by the maintainer on the same day, in the recommended forms:
domains by `domain_depth`, extended ACLs outside the partition in this version, v1 restricted to
`CAP_SYS_ADMIN`, and `EXDEV` for directories renamed across domains. Implementation in phases (§8).

## 1. Goal

Answer attribute queries within a permission domain without traversing entries of other
domains, without checking the path of each result and without a database engine. Closes the
timing channel of journal 0030, removes the per-result cost of journal 0028 and bounds the cost
per candidate by the size of the domain (journal 0029).

## 2. Domains

- **Definition.** A domain is a directory at a fixed depth below the root of the mount,
  given by the mount option `domain_depth=N` (default 1: each first-level directory is a
  domain; 0 turns domains off and keeps only the v1 behavior). **[accepted decision]** Alternative
  considered: marking domains with a reserved attribute; discarded because any directory owner
  could create or undo domains without the administrator.
- **Partition.** The in-RAM index now has one partition per domain (tree of attribute names
  and, per name, tree of entries ordered by type, value and inode), plus one partition for what
  lies outside any domain (above depth N). The inode-to-record map
  stays global and stores the domain of each inode.
- **Budget.** The `index_budget` still applies to the whole mount; `GET_INFO` now
  reports the number of domains. (Revision of 2026-10-12, journal 0061: the counters of `GET_INFO`, `heterogeneous` and
  `fallback_domains` included, are filled only for `CAP_SYS_ADMIN`; ADR-0011 finding F1.)

## 3. Homogeneity rule

A file only enters the partition of its domain if every process able to traverse the domain
directory can also read it. Since the kernel does not enumerate processes, the rule is checked by
sufficient conditions, evaluated over the inode of the file `f` and that of the domain directory `D`:

1. `f` grants read to every class (`a+r`: owner, group and others) and has no access ACL beyond the
   minimal one; **or**
2. `f` and `D` have the same owner and the same group, neither has an access ACL beyond the minimal one,
   and, for each class (owner, group, others), if `D` grants execute to that class, `f` grants
   read to it.

*Note, 2026-10-15 (journal 0116, finding F9 of ADR-0011):* condition 1 read "`f` grants read to
others (`o+r`)" until this date, and the kernel and the tools implemented it so. It was not a
sufficient condition: the kernel judges the owner by the owner bits and a member of the group by
the group bits, never by those of others (`acl_permission_check`, fs/namei.c), so a file `0604`
of group `staff` is unreadable by the members of `staff` who could traverse `D`. For
intermediate directories the same holds with execute in place of read. Compatibility: no format
changes; a file the old condition admitted and the new one does not becomes heterogeneous (left
out of `QUERY2`, counted and listed by `moobfs-check`) unless condition 2 admits it, which keeps
the common `0644` and `0640`-in-a-group-domain cases in the partition.

Files with an extended access ACL stay outside the partition in this version, except through condition 1.
**[accepted decision]** Handling ACLs would require comparing named entries; it is left for a revision with its own
test.

A file that does not satisfy the rule is **heterogeneous**: it does not appear in v2 queries, is counted in
`GET_INFO` (`heterogeneous`) and listed by `moobfs-check`. The rule is re-evaluated in the same call
that changes what it uses: `setattr` (mode, owner, group), `set_acl` and creation, under the write lock
of the index. A change of mode, owner, group or ACL of `D` itself marks the partition as "to
re-evaluate"; the next query to that partition re-evaluates all entries before answering
(cost linear in the size of the domain, paid once per change).

**Q2a implementation (2026-10-11, journal 0035), differences from the text above.** The
proposal remains recorded as above; the implemented behavior is this:

- A change of mode, owner, group or ACL of a **directory** at the domain depth or below
  it (`D` itself or an intermediate one) does not re-evaluate the partition: the domain starts answering
  with per-result permission checking (like v1) until the next mount, and `GET_INFO`
  counts these domains in `fallback_domains`. Reason: re-evaluating would require traversing the subtree
  under the index write lock; the fallback mode is correct, simpler and rare in normal
  use (agent directories do not change permission on every write).
- Intermediate directories between `D` and the file enter the rule through the execute bit: each one
  must let through every process that traverses `D` (condition 1 with `o+x`, or condition 2
  with execute in place of read).
- A regular file with more than one name (`nlink > 1`) is heterogeneous, because another name may be
  in another domain. When it goes back to one name, it may stay heterogeneous until the next change of
  mode, owner, group, ACL or rename, or until the next mount (fail-safe: it stays out,
  never appears in excess).
- With `domain_depth=0`, v1 keeps the previous semantics and remains open to everyone; the restriction
  to `CAP_SYS_ADMIN` (§4.5) applies when domains are on.
- `MOOB_IOC_QUERY2` with the index off (`noindex`) returns `ENOSYS`, like v1. (Revision of 2026-10-12, journal 0050: both versions now return `EOPNOTSUPP`.) The fields
  `order_by`, `order`, `cursor`, `flags`, padding, reserved fields and the `MISSING_OK` flag must
  be zero in this phase (`EINVAL`); they come in phases Q2b and Q2d.

## 4. Interface (UAPI v2)

### 4.1 `MOOB_IOC_QUERY2`

Issued on an open descriptor of the **domain directory** (not the root). Any other descriptor
returns `ENOTTY`; domains off (`domain_depth=0`), `EOPNOTSUPP`.

New fields relative to v1:

- `version = 2`.
- Up to 4 terms with the v1 operators, plus the per-term flag `MOOB_TERM_MISSING_OK`: the term
  also matches entries that do not have the attribute (validity and expiry of the "missing or greater
  than" kind). At least one term must be without that flag, except when there is `order_by`.
- Optional `order_by`: name of an attribute of type `int64`, `time`, `double` or `string`, and
  `order` (`ASC` or `DESC`). Without `order_by`, the order is that of the scan (unspecified).
- `limit` from 1 to 4096 and an opaque 128-byte `cursor`: the response returns in `cursor` the next
  position when `truncated = 1`; resending the same query with that `cursor` continues from there. A
  cursor from another query, from another mount or after re-evaluation of the partition returns `ESTALE`.
- Results: `ino`, `flags` and path **relative to the domain directory**.

**Q2b implementation (2026-10-11, journal 0036), additions to the text above.**

- The ordering type is explicit: field `order_type` (`string`, `int64`, `double` or `time`),
  taken from the structure's padding, without changing its size. An attribute may have entries of
  different types; only those of the requested type enter the ordering. This makes "wrong type" a
  verifiable input error (`EINVAL`), while `order_by` with a valid name that no file in the
  domain has returns an empty response, not an error: the absence of an attribute is not a
  malformed request.
- Ties in value are broken by inode number, in the same direction as the order.
- The cursor is the key (type, value, inode) of the last delivered result, plus a digest of the
  query (domain, fallback state, ordering and terms; the `limit` may change between
  pages) and an authentication code, both with a random per-mount key. Corrupted,
  from another query, from another mount or issued before the domain went into fallback:
  `ESTALE`. Values of up to 96 bytes go in the cursor; larger ones are recovered from the current
  entry of the inode, which must still have the same type, size and prefix, otherwise `ESTALE`.
- Pagination is keyset-based, as in databases: entries created, removed or with a changed value
  between pages may appear twice or not appear; those that did not change appear
  exactly once.
- Without `order_by`, the cursor also works, over the scan order of the first term.

### 4.2 Authorization

On receiving the call, the kernel checks `MAY_EXEC | MAY_READ` on the domain directory with the
caller's credentials (the ancestors were checked when the descriptor was opened). If
it passes, the results come from the partition without per-result checking: the homogeneity rule
guarantees that whoever traverses the domain can read each file indexed in it. Timing contract
(TOCTOU): the same as v1; a concurrent permission change takes effect from the next call.
When fallback or an inode-labeling LSM requires a per-result path/permission
check, an `ENOMEM` from that check refuses the call rather than silently
omitting a readable result (correction, 2026-10-22, journal 0178). An ordinary
access denial or stale path still excludes that candidate. The same rule
applies to the per-result checks of v1 `QUERY`.

### 4.3 `MOOB_IOC_QUERY2_BATCH`

Up to 8 v2 queries over the same domain in one call, with concatenated results and one count
per query. Union and duplicate removal are left to the client. Covers "or" across values
(tags, types) with one call.

**Q2d implementation (2026-10-11, journal 0038), differences and additions.**

- The batch carries a vector of complete `moob_query2` requests, each with its own results
  vector, `limit`, `count`, `truncated` and `cursor`, instead of a single vector of concatenated
  results. A client that wants contiguous results points the requests at slices of
  one same vector. Reason: each request stays pageable by its own cursor, without a rule for
  splitting the limit among requests.
- All requests are validated before any execution; an invalid request refuses the whole
  batch with `EINVAL` and nothing is written back. The requests run in order, each under its
  own acquisition of the index lock: the batch is not a single snapshot.
- **Runtime errors (clarification, 2026-10-21, issue 112, journal 0173).** Only
  a zero return makes the batch's output usable. Result arrays are copied as
  requests complete; the output headers are copied after all requests succeed.
  A later execution error can therefore leave earlier results written without
  their output counts or cursors. A fault during the final header copy may also
  leave partially written headers. There is no rollback or partial-success
  protocol. On any error, discard every result, output count, truncation flag
  and cursor from that call; none identifies a usable completed prefix. After
  addressing the cause, reinitialize the output fields and retry as fresh
  requests or from cursors retained from previous successful calls. This
  clarifies existing behavior without changing the UAPI; the no-output promise
  above applies to validation failures before execution.
- `MOOB_TERM_MISSING_OK` lives in the term's flags byte (formerly `reserved[0]`, renamed
  `flags`). v1 refuses any flag (`EINVAL`). `EXISTS` with the flag is refused ("exists or
  missing" would match everything). An attribute present with another type, invalid or raw (`raw`) counts
  as present or missing according to the index: a different type is present and does not match; invalid and
  raw are not indexed and count as missing.
- In the simple scan, the kernel drives by the first term without the flag (on a copy of the terms;
  the cursor digest uses the request order). The intersection uses only `EQ` terms without the flag.

### 4.4 Execution plan in the kernel, without a planner

- With `order_by`: walks the tree of the ordering attribute in the partition, in the requested order, and
  tests the terms on each entry until the `limit` is filled.
- Without `order_by` and with two or more equality terms without `MISSING_OK`: intersection by
  leapfrog over the runs of those terms, which are already ordered by inode within
  each value; the remaining terms are tested on each inode of the intersection.
- In the other cases: scan of the first term without `MISSING_OK`, as in the corrected v1.

**Q2c implementation (2026-10-11, journal 0037).** The intersection applies to `QUERY2` without
`order_by` with two or more `EQ` terms; v1 keeps the scan of the first term, to
serve as a comparison baseline. The first `EQ` term, in request order, drives: the results
come out in inode order and the cursor stores the value of that term with the last inode. Each step is
a logarithmic search in the tree of each term, until all reach the same inode; the remaining
terms (including the `EQ` ones, for simplicity) are tested only on the inodes of the intersection.

**Revision of the equality plan (2026-10-12, journal 0044).** Measured on the host of journal 0042,
the leapfrog only won when the run of the first term was long and was 2.7 to 3.6 times
slower than the simple scan when it was short. The plan became: measure the
runs of the `EQ` terms in parallel until the first one ends and scan only that one, testing all
the terms. Resuming this plan uses only the inode. The original proposal of §4.4 remains recorded
above.

### 4.5 v1

**[accepted decision]** Proposal: `MOOB_IOC_QUERY` (v1) on the root continues to exist, with the current
semantics, but only for those with `CAP_SYS_ADMIN` in the initial user namespace (administration
tools and comparison with the oracle). For everyone else it returns `EPERM`, because it traverses all
domains and keeps the timing channel. Alternative: keep v1 open and document the channel.

## 5. Rename across domains

A file renamed into another domain: its entries change partition in the same call, and the homogeneity
rule is re-evaluated at the destination. A **directory** renamed into another domain: returns
`EXDEV`, as across filesystems; `mv` then copies and deletes. **[accepted decision]** Avoids moving
whole subtrees between partitions inside a `rename`.

## 6. Tools

- `moobfs-index --domain DIR`: oracle that evaluates the same homogeneity rule and answers only
  about the domain, for comparison.
- `moobfs-check --domain-depth N`: lists the files left out of `QUERY2` answers, with the reason
  (implemented in journal 0090; until then the tool did not do it).
- `bench`: W3 shape per domain and the timing-channel experiment of journal 0030 as a test.

## 7. Required tests

1. Invalid inputs: version, terms, `MISSING_OK` flag alone without `order_by`, `order_by` on a
   nonexistent attribute or of the wrong type, `cursor` corrupted, from another query and after
   re-evaluation (`ESTALE`), descriptor that is not a domain (`ENOTTY`), `domain_depth=0`
   (`EOPNOTSUPP`), batch with more than 8 queries.
2. Equivalence with the per-domain oracle, in all W3 shapes, on the three lower filesystems.
3. Homogeneity: a `chmod` that makes a file more restricted removes it from the results on the next
   call; reverting brings it back; an extended ACL excludes it unless readable by everyone; a change in the
   mode of the domain directory triggers re-evaluation.
4. Timing channel: the latency of a query in a domain with 10 files does not vary (within noise)
   when another domain goes from 0 to 20 000 files that match the predicate.
5. Isolation: a user without access to the domain directory cannot open it or query.
6. Rename: a file across domains changes partition; a directory across domains returns `EXDEV`.
7. Ordering and cursor: "N most recent" equal to the ordered oracle; full pagination by cursor
   returns the same set as the query without a limit.
8. Concurrency: the race test of journal 0020 applied to v2, including `chmod` on the domain
   directory during queries.
9. KASAN and lockdep without diagnostics; v1 restricted as in 4.5.

## 8. Phases

| Phase | Content | Done-when criterion |
| --- | --- | --- |
| Q2a | Per-domain partitions, homogeneity rule, `MOOB_IOC_QUERY2` without `order_by`, v1 restriction | Tests 1, 2, 3, 4, 5, 9 |
| Q2b | `order_by`, `cursor` | Test 7 |
| Q2c | Leapfrog intersection | Equivalence and measurement against Q2a in the `namespace AND agent` shape |
| Q2d | Batch and `MISSING_OK` | Tests 1 and 2 for the new fields |
| Q2e | Rename across domains | Test 6 and 8 |

Each phase comes in through its own pull request, with a journal in `docs/history/`, the VM suite and
KASAN, and a measurement on the Azure host compared with journal 0029.

**Status note (2026-10-12, journal 0052).** Test 6 (renames across domains: `EXDEV` for directories,
repartition for files) landed with Q2a. Q2e therefore adds test 8: the `QUERY2_RACE_PASS` block of the
suite races mode changes, renames, unlink and recreate, files moving to another domain and back,
and `chmod` of the domain directory against paged `QUERY2` over the plain, equality and ordered
plans. Q2c and Q2d went in one combined pull request by the maintainer's decision, and the
measurements moved from the Azure host to the Mac mini host (journal 0042).

## 9. Building a domain on demand (ADR-0012, journal 0098)

Status: implemented and tested in the VM (journal 0098) for items 1, 2, 5 and 6 of ADR-0012;
items 3 and 4, releasing built domains, in journal 0101 (last bullets). This section
replaces "the index is built at mount" wherever earlier sections or documents say so; what a
query answers does not change.

- **Mount** reads nothing but whether the lower root answers `listxattr`. No file is scanned.
- **States.** A domain is not built, being built, or built. "Outside" (what lies above depth N,
  and everything when `domain_depth=0`) is one more unit with the same three states.
- **First query.** `MOOB_IOC_QUERY2` and `MOOB_IOC_QUERY2_BATCH` on a domain that is not built
  build it before answering, by scanning that domain's subtree with the mounter's credentials
  and the homogeneity rule of section 3. Other callers of the same domain wait (the wait can be
  interrupted by a fatal signal, `EINTR`). The authorization of section 4.2 comes first: a
  caller who may not query the domain cannot make it be built.
- **Mount-wide `MOOB_IOC_QUERY`** builds "outside" and every domain directory that exists at
  that moment, then answers. Its first use after a mount costs what the mount scan used to.
- **Writes.** A change to a file whose domain is not built does not touch the index: the build
  will read it. A file that enters a built domain from one that is not (rename, new hard link)
  has its attributes read from the lower filesystem at that moment; one that leaves the built
  part is dropped.
- **Build against concurrent writes.** The domain stays writable during the build. The scan
  announces the file it is reading, reads its attributes with no lock held, and replaces that
  file's entries under the index lock only if no write path touched the same file meanwhile,
  reading the file again otherwise. A file is absent from the index or whole in it. This is the
  online index build of Mohan and Narang (SIGMOD 1992) in its variant without a side file for
  attribute writes. Three cases need more than that:
  - a **directory renamed** during the build can carry a subtree from a part not yet scanned
    into one already scanned. The rename records the directory in a side list of the domain,
    and the build scans it again, in the order of the renames, before the domain is declared
    built;
  - a **file renamed** during the build may have a record that holds only the attributes
    written since the build began (the scan had not reached it). The rename reads the file
    whole from the lower filesystem;
  - a **write to a file that has just been unlinked** (the path was resolved before the
    unlink) is not indexed: the file has no name left.
- **Where a file is** (its depth and its domain directory) is read as one consistent view
  under the dcache's `rename_lock` sequence counter, and a write computes it again if a rename
  or a link completed meanwhile. Two separate walks let a rename between them misplace the
  write (found by `tests/buildrace.c`, journal 0098).
- **A directory renamed in a built domain makes the domain stale.** The names recorded below
  it are old, and its files now have other directories between them and the domain, so their
  homogeneity may have changed. The next query on the domain takes it down and builds it
  again. A rename of a domain directory, or of a directory above it, does the same to every
  domain. This replaces "names stay stale until the next mount" (`MOOB_FLAG_PATHS_STALE` still
  reports that a directory was renamed since mount). Cost: the query after such a rename pays
  the build of the whole domain.
- **A running query keeps its domain.** `QUERY2`, `QUERY2_BATCH` and `QUERY` hold a semaphore
  for reading while they run; taking a built domain down takes it for writing.
- **Errors a first query can now return**, besides those of section 4: `ENOMEM` (no memory, or
  the domain does not fit `index_budget`), `EAGAIN` (the domain kept changing: 16 readings of
  one file each overtaken by a write to that file, or directories renamed in it for 64 rounds
  or between 8 attempts to start; the caller may retry), `EINTR`, and any error
  the lower filesystem returned to the scan. After a failed build nothing of the domain is left
  in the index, and the next query tries again. A build never answers with part of a domain: an
  ACL or a name that could not be read fails it. One exception, by decision (journal 0156): a file
  whose attribute list is longer than `XATTR_LIST_MAX` (64 KiB, reachable on btrfs with many
  non-typed `user.*` names) is left out of the index, and `moobfs-check` reports it. Failing the
  build would let any user who can write such a file in a shared domain make the domain
  unanswerable for everyone.
- **`GET_INFO`.** New flag `MOOB_FLAG_ON_DEMAND` (0x40), set whenever the index is on. New
  fields in space that was reserved or padding: `domains_built` (`__u32`, "outside" included),
  `domain_builds` (`__u32`, builds completed since mount) and `domain_releases` (`__u32`, built
  domains released since mount, journal 0101; `domain_builds` was a `__u64` until then, never
  published). Like the other counters they are
  filled for `CAP_SYS_ADMIN` only, and `index_used`, `index_entries`, `index_inodes` and
  `heterogeneous` now cover the built domains only. The structure keeps its size and the
  fields already published keep their offsets; the new ones are output in what was padding and
  reserved space, which `docs/compatibility.md` (section 3) allows while the interface is
  experimental. A program that needs the whole-mount counters must first issue the mount-wide
  `QUERY`.
- **Not changed:** the attribute encoding, the `QUERY2` request and reply, cursors (a cursor is
  bound to the mount and carries a value and an inode number, not a position in memory, so it
  resumes after its domain was released and built again; argued from the code, not tested;
  note of 2026-10-14: now tested by the suite step `release`, journal 0103),
  `fallback_domains`, the budget rule for an update to a domain already built.
- **Release under the budget** (journal 0101). When the build of a domain needs more than
  `index_budget` allows, the built domain queried least recently (other than this one) is
  released, back to "not built", and the build starts again; this repeats until the domain
  fits or nothing is left to release. A single domain larger than the budget fails with
  `ENOMEM` to its own callers, and the others stay as they were. A release needs the other
  domain's build lock, which is only tried, and waits for running queries to finish. The
  mount-wide `QUERY` never releases: it needs every domain at once, and fails with `ENOMEM`
  when they do not all fit. An attribute write to a domain already built that does not fit
  makes that domain be built again by its next query (ADR-0016; before it, the write marked
  the whole index incomplete until remount).
- **Release by the shrinker** (journal 0101). Each mount registers a shrinker. Under memory
  pressure the kernel asks it for memory, and it releases built domains, least recently queried
  first, skipping any used in the last `index_idle` seconds (mount option, 0 to 86400, default
  60; shown in `/proc/mounts` when not the default). It only tries its locks and gives up if
  any is held, and does nothing for an allocation that may not enter the filesystem
  (`__GFP_FS` clear). What it reports as freeable is an estimate: the index does not count its
  memory per domain, so it shares `index_used` evenly among the built domains.
- **Queries and releases.** A released domain is built again by the next query on it, at the
  cost of a first query. The mount-wide `QUERY` returns `EAGAIN` if domains are released eight
  times while it builds the others.
