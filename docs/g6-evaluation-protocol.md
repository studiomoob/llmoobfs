# G6 evaluation protocol and the G1 scope

Date: 2026-10-24. Status: protocol and scope, written before any run of this
batch. The G3 drill is specified here and not started. No number below was
measured for this protocol.

Implementation note, 2026-10-24 (journal [0189](history/0189-g1-adapter-package.md)):
the adapter package is `bench/client/langgraph_store.py` with host checks in
`bench/client/g1_protocol.py`. The mounted gate in sections 5 and 6 passed the
same evening on the dedicated Debian host, ext4, kernel `6.12.111+deb13-amd64`.
The pass, the counters and the limits are in that journal's evening follow-up.
No latency, memory or relevance ceiling was added. The G3 identity-map drill
in section 8 passed later the same evening (journal
[0190](history/0190-g3-restore-drill.md)).

This is the method required by [ROADMAP](../ROADMAP.md) item G6 before a
client package is built. It extends the measurement rules of
[benchmark-plan](benchmark-plan.md) and journal
[0134](history/0134-measurement-method-review.md). It does not replace the
M4 pilot, and those pilot results are not fresh evidence for this batch.
Journal [0188](history/0188-g6-protocol-and-g1-scope.md) records the decision.

The preselected objective for the G1 package is a reduction in integration
burden, counted below. A speed win is not the objective. Latency may be
reported later only under the rules in section 7, and a latency figure cannot
pass or fail G1.

## 1. What is fixed, and what stays unbound

Fixed before any implementation run, and not to be edited after results are
seen:

- the client, the reference store, the corpus, the two OS identities, the
  filter shapes, the selectivity of the timed filter, the repetition rule and
  the G1 pass/fail counts in sections 3 to 6;
- the G3 recovery bounds in section 8.

Left unbound on purpose. A package that needs one of these cannot be declared
passed until a dated amendment sets the number **before** the run that uses it:

- any latency or memory ceiling;
- a G2 contention bound;
- a freshness bound for a derived index (G4);
- a relevance floor, recall@k or nDCG (G5 and the G6 final evaluation).

An amendment is a new dated section of this file plus a journal entry. It does
not rewrite the bounds already fixed.

## 2. What this batch does not change

No kernel interface, disk format, mount option or attribute encoding. No new
query operator. Projection stays the accepted and unimplemented ADR-0014.
Richer SQL stays ADR-0013 and is not this package: the adapter keeps calling
`QUERY2` directly. S9 fair sharing and the issue 118 resource limits stay on
their own track. Algorithm experiments in
[research-algorithm-fit](research-algorithm-fit.md) stay unstarted.

Client-specific behavior stays in the adapter. A need that appears only for
this client does not change `layer/` or the UAPI.

## 3. Client and reference

The one client of this batch is the LangGraph store interface (`BaseStore`),
through the existing adapter `bench/client/langgraph_store.py`. It is the
second client named in the [client contract](client-contract.md) section 9
and journal [0089](history/0089-second-client-langgraph-adapter.md).
ai-memory remains evidence of another client. It is not the design target of
this package.

Pinned reference, the same versions already recorded in
[references](references.md): langgraph 1.2.15 and langgraph-checkpoint 4.3.0.
The oracle for answer equality is LangGraph's `InMemoryStore`, compared as
sets where that store promises no order. The Python of the guest is recorded
in the run's `environment.txt`. If those pins do not install, the run is
blocked. It is not a pass and not a reason to change the pin after the fact.

The lower filesystem for this gate is ext4. btrfs and XFS stay covered by the
module suite and are not required to close G1.

## 4. Workload

Prepared by the test, then used by the adapter. Seed 1. `domain_depth=1`.
Group `moobteam` contains uid 1001 and uid 1002.

| Domain | Owner : group | Directory mode | Files the adapter creates | Count |
| --- | --- | --- | --- | --- |
| `alice` | 1001 : 1001 | `0700` | `0600`, same owner | 1 000 |
| `bob` | 1002 : 1002 | `0700` | `0600`, same owner | 1 000 |
| `team` | root : `moobteam` | `2770` | `0664`, publisher uid, group `moobteam` | 5 000 |

The shared directory is setgid so new files keep `moobteam`. The adapter used
to create every file `0600`. That mode is more private than `team` and the
homogeneity rule then omits the file from every `QUERY2`, which is the failure
journal 0089 already hit. `reach_ok` indexes a file that shares the domain
directory's owner and group, or a file readable by owner, group and other. The
owner of `team` is root. Uid 1001 cannot give a file that owner, so a `0660`
file planted by uid 1001 stays out of the index: the first mounted run returned
zero `team` hits for that reason. The package adds other-read when the
publisher is not the domain owner. The `team` files of this corpus are therefore
`0664`, owned by the publisher, group `moobteam`. A publisher who is already
the domain owner, including both private domains and the host check, still
creates `0600` or `0660` from the directory classes. Modes are set on the
descriptor. The test creates the three domain directories before the adapter
runs, so the adapter does not recreate them with another mode.

One extra file, `team/salary.json`, is planted outside the adapter: owner
1002, mode `0600`. It is not one of the 5 000. The homogeneity rule leaves it
out of `team`'s index for every caller, including uid 1002 and root. Uid 1002
can still read it by path. Uid 1001 cannot.

Each of the 7 000 adapter files is one JSON memory. The value has four
top-level scalars and no other indexed field:

- `kind`: one of `decision`, `note`, `task`, `fact`, `plan`, `event`, `ref`,
  `log`, in exact equal counts (125 in each private domain, 625 in `team`);
- `flag`: bool, exactly half `true`;
- `score`: a double with two decimal digits, assigned independently of `kind`;
- `note`: a short string that is not queried.

`Random(1)` only shuffles which item receives which quota slot. The timed
filter is `kind` equal to `decision`. Its selectivity is 625/5000 = 0.125 on
`team` and 125/1000 = 0.125 on a private domain, by construction. A generator
that does not hit those counts is rejected before any query. That check is a
property of the fixture, not a performance result.

Namespace labels are the domain names, one label deep, so every file sits in
its domain. Keys are `m00000` upward, stable for a seed.

### Filter shapes

The equality run covers the twelve shapes of journal 0089, all pages:

1. string equality on `kind`;
2. bool equality on `flag`;
3. `score` greater than 0.5;
4. `score` greater or equal to 0.5;
5. `score` less than 0.5;
6. `score` less or equal to 0.5;
7. `$ne` on `kind`;
8. `kind` and `flag` together;
9. a list value;
10. a nested object;
11. a key that is not a valid attribute name;
12. a missing field.

Shapes 1 to 6 and 8 are pushed to `QUERY2` when the adapter can express them,
within four terms. Shapes 7 and 9 to 12 stay post-filtered in the adapter.
Both groups must match the oracle. Post-filtering is not kernel support, and
it is not a permission check.

### Paging, concurrency, repetitions

`team` has 5 000 files. One `QUERY2` call returns at most 4 096 results, so a
search that matches the whole domain must issue at least two `QUERY2` calls
and follow the cursor. A client search with limit 100 must return 100 items,
and the next search with offset 100 the following 100, in the adapter's
documented order (newest first). The union of the pages equals the oracle.
Client offset may slice a result set the kernel cursor has already completed.
Pushing offset into the kernel is not required.

Correctness is deterministic for seed 1. One complete run judges the
zero-difference checks. Thirty repetitions apply only when a latency
difference is reported (section 7).

Concurrency, recorded and not a freshness bound: while uid 1002 publishes 30
new files in `team`, uid 1001 runs 30 searches there. Two publishers of the
same key are last-rename-wins. Each final name is one complete file. The
report lists any search that missed an in-flight file. That miss does not
fail G1. There is no second writer inside `alice` or `bob`.

## 5. Baselines

| Question | Oracle | Pass condition |
| --- | --- | --- |
| Do supported searches match the reference store? | `InMemoryStore` at the pinned version | 0 item differences, every shape, every page |
| Is the private file outside the domain index? | `QUERY2` as uid 1001, uid 1002 and root on `team` | `salary.json` absent for all three; uid 1001's `open` fails; uid 1002's `open` by path succeeds |
| Can a caller who cannot see the mount root still search? | the same search with the mount root visible | same result set; detection uses `statfs` on the domain only |
| Does the no-module fallback respect the caller's access? | every matching file that uid can `open` | 0 paths the uid cannot open |

The mount-root case is prepared as root, then run as uid 1001 inside a mount
namespace whose root is a bind mount of `alice`. The process has no path to
the moobfs mount root. `statfs` on the domain returns magic `0x4d4f4f42`.
Opening the mount root is not required and must not be added as a requirement.

Fallback is for a directory whose `statfs` magic is not the layer, and for an
ioctl that fails because the module is absent. It walks only names the caller
can open. It may return `salary.json` to uid 1002, who can read it, even
though `QUERY2` omits that file. That difference is documented. It is not a
failure. Fallback must not return `salary.json` to uid 1001.

*Amendment, 2026-10-27, accepted by the maintainer before the run (journal
[0207](history/0207-python-library-langgraph-store.md)):* for the store of the
Python library (`moobfs.langgraph`, gate option `--store package`), the
fallback applies the kernel's matching and homogeneity rules (ADR-0023), so it
answers what `QUERY2` answers on a mounted copy. For that store the no-module
bound of section 6 reads: `salary.json` absent for uid 1001 **and** uid 1002,
uid 1002's `open` by path still succeeds, and 0 paths the uid cannot open. The
G1 adapter (`langgraph_store`, the default) keeps the wording above, and its
recorded runs stand as they are. Every other bound is unchanged for both.

A resource refusal is not a fallback. `ENOMEM`, `E2BIG`, `ETIMEDOUT` and
`EDQUOT` from `QUERY2` fail the search. The caller receives the error and no
list. A short successful list after one of those errors fails the case.

## 6. Bounds that pass or fail G1

All of the following are required. One miss fails the package.

**Burden.** Counted by reading the adapter and the data directory after the
run. The JSON body may repeat the scalars that are also typed attributes.
That repetition is the file. It is not a second store.

| Id | Count | Bound |
| --- | --- | --- |
| B1 | files created that are not the memory `.json`, a `.tmp.*` publication temporary, or a namespace directory | 0 |
| B2 | functions that, on put, update anything other than that file and its `user.moob.*` attributes | 0 |
| B3 | uses of `access`, mode bits or ACLs to drop a result the kernel already returned | 0 |
| B4 | side databases or side indexes in the data directory | 0 |

Post-filtered value predicates are counted apart from B3 and may be non-zero.

**Answers.** Zero differences against `InMemoryStore` on the twelve shapes,
including both pages of a `team` search wider than 4 096 results, and both
client pages of limit 100.

**Publication.** The adapter keeps the contract order: temporary file in the
same directory, content, typed attributes, `fsync` the file, `rename` over the
final name, `fsync` the directory. A test runs each prefix of that sequence
and then inspects the directory. Before `rename`, the final name is absent or
still the previous complete file. After `rename`, the final name is the new
complete file: the bytes and the typed attributes of that publication, not a
mix of two publications. A crash after `rename` and before the directory
`fsync` may leave either the new complete name or the previous one. It must
not leave a mix. This prefix test is the G1 crash gate. It does not replace
the module's existing power-loss replay and does not claim a new one.

**Sequential freshness.** After a put returns, a search by the same uid in the
same process sees the item. After a delete of ten known keys returns, a search
does not see them. Zero misses, zero leftovers, in that scripted order only.

**Capability and errors.**

- The confined uid 1001 completes a search of `alice` without a descriptor of
  the mount root.
- With the module not mounted, fallback matches the open-oracle of section 5.
- Heterogeneous `salary.json` stays out of every `team` search while the
  module is mounted.
- Budget drill, separate mount, `index_budget=1M` (the minimum the module
  accepts), 5 000 files, eight typed attributes, ext4. The sizing uses the
  historical accounting of about 530 to 607 bytes per file in journal
  [0088](history/0088-m4-report.md), which is not a measurement of the current
  module. If the first search returns `ENOMEM`, `E2BIG`, `ETIMEDOUT` or
  `EDQUOT` and the adapter reports that failure with no list, the case passes.
  If those 5 000 files fit, the case does not pass and the file count is not
  raised or lowered after the fact. A later amendment may choose a larger
  count before another run. The main corpus uses the default budget. If that
  corpus itself returns a resource error, the run is an invalid environment,
  not a product pass or fail.

**Operations the package documents and does not pretend to support as kernel
queries:** `$ne`, lists, nested objects, invalid attribute names, client
offset as a kernel cursor, and concurrent publishers of one key. Disjunctions,
counts and full-text stay outside the adapter, as the client contract already
says.

## 7. What is recorded and cannot pass or fail G1

After the cold build of `team` and after the warm searches, record as root
`index_budget`, `index_used` and `index_inodes` from `GET_INFO`, and the
searching process's `VmHWM`. No ceiling is set. A memory number cannot close
G1 or G6.

Warm p50, p95 and p99 of the selective `kind` search and of put may be
recorded. A comparison against the no-module fallback, or any claim that one
side is faster, uses a KVM guest, CPU frequency held still, at least 30
repetitions, order alternated, and `bench/compare.py` as journal 0134
requires: relative difference of medians, 95% bootstrap interval, Mann-Whitney
U. Fewer than 30 repetitions, or a run under emulation, is exploratory and
claims no difference. The spread rule of the benchmark plan remains a
stability flag, not evidence. SQLite is not the G1 baseline. Where an older
report compares with SQLite, that engine does not perform the POSIX check the
layer does.

No relevance number is reported. There is no labeled question set in this
batch.

## 8. G3 bounds

The identity-map drill passed on 2026-10-24 (journal
[0190](history/0190-g3-restore-drill.md)), on the G1 ext4 corpus. A
non-identity UID map stays out of scope. The bounds below are the ones that
run used.

The recovery unit is the file bytes, every `user.moob.*` attribute
byte-for-byte, and the numeric owner, group, mode and POSIX ACL. The UID/GID
map of this drill is identity. A non-identity map is out of scope.

The drill uses the same three domains and the private file. It does not need
the G1 adapter: a fixture may plant the tree. The copy tool is
`rsync -aHAX --numeric-ids` from the lower tree, which is the command already
named in [distribution strategy](distribution-strategy.md) section 2.7. No new
archive format. The user-facing step is a script that exits without writing
when the destination is not empty. Raw rsync remains the mechanism inside that
script.

Pass, all required:

- content hashes, typed-attribute bytes, owner, group, mode and ACLs match
  under the identity map;
- after mount, `QUERY2` answers and denials for uid 1001 and uid 1002 match
  the pre-restore oracle, and `salary.json` is still absent from `team`
  searches;
- `moobfs-check` exits 0 on the restored tree;
- with the module unmounted, `cat` and `getfattr` still read the restored tree;
- one typed attribute with an illegal type byte is detected (`moobfs-check`
  exit 2), not accepted as a complete memory;
- `user.moob.format` on the lower root set above `MOOB_ATTR_FORMAT_VERSION`
  (1 in the current header) makes the mount fail;
- a second run onto a non-empty destination does not replace an existing file.

An absent optional attribute is not the defect case. The format allows absence.

## 9. G2 fairness property, registered before a mechanism

G2 stays with S9. ADR-0019 bounds how much work one domain build may do and
how often an unprivileged uid may start one. It does not split `index_budget`.
On the module recorded in journal 0191, when a build did not fit,
`domain_release` dropped the least recently queried built domain of any
user. That was the cross-user eviction finding F2 left open.

The property a later implementation must meet, on ext4, with two unprivileged
uids and `domain_depth=1`:

- A build charged to uid 1002 does not release a domain built for uid 1001.
  The following search by uid 1001 returns the same answers, and
  `domain_builds` does not increase because of that search.
- If uid 1002's build does not fit in the budget without releasing uid 1001,
  the search returns `ENOMEM`, `E2BIG`, `ETIMEDOUT` or `EDQUOT`, with no
  result list. Uid 1001's domain stays built.
- One uid may still use the whole `index_budget`, including by releasing
  that same uid's other domains. Fairness does not disable release.
- No latency number is a pass or a fail. p50, p95 and p99 may be recorded
  under the method of section 7. A latency ceiling is not set, so G2 cannot
  pass as a completed roadmap item. This property is necessary and not
  sufficient. The share algorithm is not chosen in the property above.

`bench/client/g2_baseline.py` records the module before the filter. Its
output is evidence of that module. It is not a pass of the property. Journal
[0191](history/0191-g2-eviction-baseline.md) records the 2026-10-24 run:
with `index_budget=1M` and 2 000 files in each domain, the second build
released the first domain, both across the two uids and within one uid.
Each search still returned its 2 000 answers. Only one domain stayed built.

Implementation note, 2026-10-25
([ADR-0021](decisions/0021-budget-release-keeps-the-builders-domain.md),
journal [0192](history/0192-same-fsuid-budget-release.md)): a budget
release may drop only a domain last built by the same filesystem uid. If
the remainder is too small, the search returns `ENOMEM` and leaves the
other uid's domain built. The shrinker is unchanged and may still drop any
idle domain. One uid may still fill the budget. There is no reserved
fraction. `bench/client/g2_share.py` is the regression. On the dedicated
Debian host it exited 2 on the previous module and exited 0 after the
filter. The latency ceiling stays unset, so G2 still cannot pass as a
completed roadmap item. Later the same morning the opt-in native-KVM
step passed on the dedicated host and failed on the pre-filter module.
`release`, `overbudget` and `buildcostevict` then passed under KASAN,
UBSAN and lockdep. The complete KASAN, UBSAN and lockdep suite of this
change then exited 0 (`MOOBFS_KASAN_PASS`). The complete native-KVM suite
of this change was not run.

Amendment, 2026-10-28 ([ADR-0027](decisions/0027-fair-shares-of-the-index-budget.md),
journal [0228](history/0228-security-batch-5-fair-shares.md)), accepted by the maintainer before
the mechanism was built. The first bullet above becomes: a build charged to uid B does not
release a domain of uid A while A's usage, the bytes of A's built domains, is at or below the
share, `index_budget` divided by the number of uids with a built domain (B counted once). When
A is above the share and B below it, B's build may release A's least recently queried domains
until A is not above it; if the build still does not fit, the search returns a resource errno
with no list. The other bullets stand: one uid alone may use the whole budget, and release by
the same uid is not disabled. `bench/client/g2_share.py` checks the amended property; the
latency ceiling is section 14.

## 10. Still unbound

Note, 2026-10-25: section 12 proposes G4's freshness and inventory bounds.
They bind only once the maintainer accepts that section. It was accepted the
same day, and the G4 package met those bounds (journal 0195).

G4's freshness bound and G5's recall floor are not set, so those items and
the G6 final evaluation cannot pass yet. The final evaluation, when those
amendments exist, is still one real client, two OS identities, private and
shared domains, and the same search engine over plain files plus a
user-space metadata index. Negative results are published. Existing M4
pilots are not that evaluation.

## 11. Order

The G1 package is the next implementation unit of this track. G3's drill
follows that scope and does not wait for G1 to run, because a fixture can
plant the tree. This document does not start either one. G2 stays with S9.
G4 and G5 wait on G1 and on their own amendments.

Evening note, 2026-10-24: the G1 mounted gate has passed (journal 0189).
The G3 identity-map drill then passed (journal 0190). G2, G4, G5 and the
final evaluation remain unbound.

Later the same evening, section 9 registered the G2 fairness property.
The latency ceiling stays unset, so G2 still cannot pass as a completed
item. The baseline script records the module of that day; it is not that pass.

On 2026-10-25 the budget-release filter of ADR-0021 met the property on
the dedicated host (journal 0192). The complete KASAN, UBSAN and lockdep
suite of that change then exited 0. The latency ceiling is still unset, so
G2 remains incomplete.

Later on 2026-10-25, section 12 proposed the G4 amendment. G4's
implementation waits for its acceptance. G5 waits on G4.

The maintainer accepted section 12 the same morning. The G4 package then met
every bound of section 12.6 in three runs on the dedicated host (journal
[0195](history/0195-g4-derived-index-package.md)). G5's relevance floor and
the final evaluation stay unbound.

Later the same day, section 13 proposed the G5 amendment. G5's implementation
waits for its acceptance.

The maintainer accepted section 13 as written. On 2026-10-26 the G5 package
met every bound of section 13.6 on the dedicated host (journal
[0201](history/0201-g5-hybrid-retrieval-package.md)). The final evaluation
and the G2 latency ceiling stay unbound.

## 12. G4 amendment, 2026-10-25 — proposed, not accepted

Status: proposed by journal [0194](history/0194-g4-protocol-amendment.md).
The maintainer has not accepted it, nothing below has run, and no number in
this section was measured for it. Once accepted, sections 12.1 to 12.6 are
fixed under the rule of section 1 and are not edited after a result. Until
then G4 stays unstarted.

Acceptance note, 2026-10-25 (journal [0195](history/0195-g4-derived-index-package.md)):
the maintainer accepted this section as written, before any G4 run.
Sections 12.1 to 12.6 are fixed from that point. The heading keeps its
original wording.

### 12.1 What G4 is in this batch, and what it does not change

G4 is the lifecycle of a **derived index** kept by the client in user space:
one vector per memory, computed from the memory's content by a named model.
The batch tests that this index stays equal to the files it was derived from
through update, rename, delete, permission revocation, a stopped or restarted
watcher, lost change events and a model change. It does not test whether the
vectors find useful memories. Relevance is G5.

No change to `layer/`, the UAPI, the disk format, mount options or attribute
encoding. The G1 adapter's `put`, `get` and structured `search` keep their
code and their section 6 counts: B1, B2 and B4 stay 0, because the derived
index is not in the data directory and `put` does not write it. A text query
on the adapter's `BaseStore.search` still raises `NotImplementedError`.
Wiring a vector query into that method is G5. Revisions and change history
stay in the client under [M3 section 5](spec-m3.md). No kernel change cursor
is requested.

### 12.2 Identity, version and model

- **Identity** is the LangGraph pair `(namespace, key)`, which in the adapter
  is the path below the data directory. A rename outside the adapter is, for
  LangGraph, a delete of one key and a new key. The derived index follows
  that. `user.moob.memory_id` is not introduced by this batch.
- **Content version** is the SHA-256 of the file's bytes, read as the
  indexing uid. Modification time and size are not the version: a change in
  the same timestamp tick with the same size is the racy-clean case git
  documents, and a stat comparison would keep an obsolete vector.
- **Model** is a named, deterministic embedding with no dependency:
  `hash64-v1` maps the value's canonical JSON (sorted keys, no spaces) to 64
  signed float32 components by feature hashing of its tokens, then L2
  normalization. `hash64-v2` is the same with another seed. It stands in for
  an embedding model so that a model change can be tested. It is not a
  relevance claim and is not the G5 model.

A derived entry is `(namespace, key, sha256, model, vector)`. It holds no
value bytes.

### 12.3 Where the derived index lives, and who may read it

One derived index per OS identity, under that uid's cache directory, outside
the data directory: `$XDG_CACHE_HOME/moobfs-g4/` (default
`~/.cache/moobfs-g4/`), directory `0700`, files `0600`, owned by the uid. A
uid indexes only what it can open itself. Uid 1001 and uid 1002 therefore
each derive their own entries for `team`. That duplicate work is recorded,
not bounded. Root is not claimed to be kept out of a cache.

The **eligible set** of a uid is every memory `.json` of the corpus, in
`alice`, `bob` and `team`, that the uid can open and read at that moment.
`salary.json` is outside uid 1001's eligible set and inside uid 1002's.

The **oracle** is a fresh scan: the eligible set walked, read, hashed and
embedded from nothing, with the current model, by code that does not read
the derived index. The model function is shared, because it is the model.

### 12.4 How the index follows changes

The watcher reuses the state-based design of journal
[0092](history/0092-userspace-index-baseline.md): one inotify watch per
directory, added before the directory is read; every event becomes a path to
look at again; each path ends equal to the disk at that moment, including
"gone". Events watched: close after write, create, delete, both sides of a
rename, and `IN_ATTRIB` on files and directories. A mode change on a
directory re-evaluates its subtree. `IN_Q_OVERFLOW` forces a full pass. So
does a start, which also covers any change made while no watcher ran.

inotify on a layer directory reports changes made through the layer (journal
[0030](history/0030-verification-notify-landlock-timing.md)). A change made
directly on the lower tree is not claimed to reach a watcher on the mount. It
is reconciled by the next full pass. A full pass reuses a cached vector when
`(namespace, key, sha256, model)` matches, and embeds otherwise.

A derived search is exact: score every entry of the uid's index under the
requested namespace prefix, with the current model only, top k, ties broken
by `(namespace, key)`. Each candidate is then opened and read as the caller.
A candidate whose bytes no longer hash to the entry's `sha256`, or that the
caller can no longer open, is not returned, and its path is queued for the
watcher. That read is not a permission system and does not hide counts,
scores or timing (ROADMAP rule 4).

Code goes in `bench/client/` next to the adapter: the watcher and reconciler
in one module, the mounted test in another. inotify is called through
`ctypes` on libc, as the adapter already calls `ioctl`.

### 12.5 Scenarios

Host and corpus are those of sections 3 and 4: the dedicated Debian host,
ext4, the same LangGraph pins, seed 1, the 7 000 adapter files and
`salary.json`. Uid 1001 and uid 1002 each run one watcher. Unless a scenario
says otherwise, uid 1002 makes the changes in `team` and in `bob`, and both
inventories are compared.

| Id | Change | What must follow |
| --- | --- | --- |
| E1 | 100 updates of existing keys through the adapter, values changed | new `sha256`, new vector |
| E2 | 100 new keys through the adapter | new entries |
| E3 | 100 deletes through the adapter | entries gone |
| E4 | 50 renames with `mv` inside `team`, outside the adapter | old key gone, new key present; re-embeds recorded |
| E5 | uid 1002 changes 10 of its own `team` files from `0664` to `0600` | out of uid 1001's index; still in uid 1002's |
| E6 | root changes `team` to `2700`, then back to `2770` after reconciliation | every `team` entry out of uid 1001's index and back again |
| E7 | uid 1001's watcher killed with SIGKILL; 200 changes of E1 to E3 kinds; watcher restarted | start pass reconciles them |
| E8 | `fs.inotify.max_queued_events` set to 64 as root before uid 1001's watcher starts; watcher stopped with SIGSTOP; 1 000 changes; SIGCONT | at least one `IN_Q_OVERFLOW` recorded, then a full pass; the sysctl is restored afterwards |
| E9 | model switched from `hash64-v1` to `hash64-v2` for uid 1001 while its watcher runs | every entry re-embedded with v2 |
| E10 | root writes 20 files directly on the lower tree, then uid 1001's watcher is restarted | whether the running watcher saw them is recorded; the start pass reconciles them |
| E11 | two publishers of one key in `team`, 30 rounds, as in section 4 | last-rename-wins; one entry per final name |

### 12.6 Bounds that pass or fail G4 (proposed)

All required. One miss fails the package. "Reconciled" means the watcher has
an empty queue and no full pass running after the scenario's last change.

**Inventory.** After each scenario is reconciled, each uid's derived index
equals that uid's oracle: 0 missing entries, 0 extra entries, 0 `sha256`
differences, 0 entries of another model, 0 vector differences.

**Nothing obsolete served.** During and after every scenario, a derived
search returns 0 items whose returned bytes differ from the entry they were
scored with, 0 deleted keys once reconciled, and, after E9's switch, 0 items
scored with a `hash64-v1` vector. During E9 a memory not yet re-embedded is
absent from derived results; the count of such absences is recorded.

**Revocation.** After E5 and E6 are reconciled, uid 1001's index holds 0
entries for the revoked files and its derived searches return 0 of them,
neither as a value nor as a ranked key. Between the change and its
reconciliation, the item may be scored and then dropped because `open`
fails; those drops are counted and do not fail the case. No value of a
revoked file reaches uid 1001 after its `open` would fail. Content a process
already read before the revocation is not retracted.

**Protected cache.** Uid 1002's `open` of uid 1001's cache directory and
files fails, and the reverse. No cache file contains a value's canonical
JSON or its `note` string.

**Freshness, live (F1).** With the watcher running and one writer, over E1,
E2 and E3: from the return of the adapter's `put` (or of `unlink` for E3) to
the moment the other uid's index reflects it, p99 at most 1 s and maximum at
most 5 s, in each of 3 runs. 0 changes never reflected. The live sidecar of
journal [0071](history/0071-client-stage2-live-mirror.md) set attributes 17.9
ms after mtime at p99 and 306 ms at most; the bound leaves room for hashing
and embedding in Python. It is a workload choice, not a guarantee.

**Freshness, recovery (F2).** After E7, E8, E9 and E10, the start or overflow
pass of uid 1001 (6 000 eligible files, plus E2's new keys) reaches the
inventory bound above in at most 60 s, in each of 3 runs.

**Real overflow.** E8 records at least one `IN_Q_OVERFLOW`. A run with none
is not a pass of E8; the queue size is not adjusted after a result.

### 12.7 Recorded and not bounded

The size of each uid's cache, the watcher's `VmHWM`, re-embeds per scenario
(E4 is expected to reuse vectors), the duplicate `team` work of two uids,
derived-search latency, and whether E10's running watcher saw the lower-tree
writes. None of these can pass or fail G4 or G6.

Still unbound after this amendment: G5's relevance floor and recall@k, any
derived-search latency ceiling, and the G2 latency ceiling. A change of the
concurrency or the corpus is a new dated amendment, before its run.

## 13. G5 amendment, 2026-10-25 — proposed, not accepted

Status: proposed by journal [0200](history/0200-g5-protocol-amendment.md).
The maintainer has not accepted it, nothing below has run, and no number in
this section was measured for it. Once accepted, sections 13.1 to 13.6 are
fixed under the rule of section 1. Until then G5 stays unstarted.

Acceptance note, 2026-10-25 (journal [0201](history/0201-g5-hybrid-retrieval-package.md)):
the maintainer accepted this section as written, before any download or
run. Sections 13.1 to 13.6 are fixed from that point. The heading keeps its
original wording.

Harness note, 2026-10-26 (journal [0201](history/0201-g5-hybrid-retrieval-package.md)):
the first full run stopped at its first search, after every index was built,
because `QUERY2` refused the two private domains (about 25,000 files each)
with `E2BIG` under the default `index_build_work`. The gate now mounts with
`index_build_work=65536`, the administrative maximum of
[ADR-0020](decisions/0020-configurable-query-build-work-per-mount.md), and
issues one `QUERY2` per domain before indexing. No bound, corpus or engine of
this section changed.

Result note, 2026-10-26 (journal [0201](history/0201-g5-hybrid-retrieval-package.md)):
the mounted gate met every bound of section 13.6. The two paths and the
oracle gave the same top 10 everywhere; fusion's Q1 `recall_all@10` was
0.845 against lexical's 0.809; Q1 p95 at most 0.135 s and Q3 p95 at most
1.525 s across the 3 runs; 0 revoked turns returned.

### 13.1 What G5 is in this batch, and what it does not change

Hybrid retrieval in user space: a structured filter answered by `QUERY2`, a
lexical branch, an exact vector branch and their fusion, over memories the
caller can read. The question is whether that path returns the same ranked
results as the same engines over plain files with a user-space metadata
index, and whether fusion is worth keeping over either branch alone.

No change to `layer/`, the UAPI, the disk format or the G1 adapter's
`put`. No vector or approximate-nearest-neighbour code in the kernel. Both
derived indexes (lexical and vector) follow the lifecycle of section 12,
kept by the G4 watcher, in each uid's cache outside the data directory. No
approximate mode is in this batch: HNSW or another one is tried only if the
exact path misses section 13.6's latency bound, and only after a further
amendment sets its recall threshold.

### 13.2 Questions, corpus and labels

The labeled set is LongMemEval (Wu et al., ICLR 2025), file
`longmemeval_s_cleaned` from the dataset `xiaowu0162/longmemeval-cleaned`
(MIT), pinned by its revision hash when downloaded, before the first run.
Each instance has a question, a dated history of about 40 sessions and the
ids of the sessions that hold the evidence (`answer_session_ids`); turns that
hold evidence carry `has_answer`.

Sample: 110 instances drawn with `Random(1)` from the instances whose
`question_id` does not end in `_abs`, stratified by `question_type` in
proportion to the file, rounding by largest remainder. The draw is made from
the file's order and recorded before any embedding runs. If the pinned file
cannot be obtained, the run is blocked; another dataset needs a new
amendment.

One memory per turn. Each turn is one adapter file, key
`<question_id>-s<session>-t<turn>`, value `{role, content, session_id,
date}`, with typed attributes `question_id` (string), `role` (string) and
`date` (time, from `haystack_dates`). The text that is indexed and embedded
is `content` only.

| Domain | Owner : group, mode | Instances | Published by |
| --- | --- | --- | --- |
| `alice` | 1001, `0700` | first 50 of the draw | uid 1001 |
| `bob` | 1002, `0700` | next 50 | uid 1002 |
| `team` | root : `moobteam`, `2770` | last 10 | uid 1002, `0664` |

Uid 1001 asks the questions of `alice` and `team`; uid 1002 those of `bob`
and `team`.

### 13.3 Engines, chosen before the comparison

- **Vector:** `sentence-transformers/all-MiniLM-L6-v2` (Apache-2.0, 384
  dimensions, input truncated at 256 word pieces), through
  `sentence-transformers`, with the model revision and package versions
  pinned in the run's `environment.txt` before the first run. Cosine
  similarity on normalized vectors, scored for every eligible turn: exact.
  The number of truncated turns is recorded.
- **Lexical:** SQLite FTS5 with its `bm25()` function and default
  parameters, default `unicode61` tokenizer, from the Python `sqlite3` of
  the host. One FTS5 table per uid, holding every memory that uid can
  read, so its term statistics come from what that uid can read and never
  from another uid's files. A filter restricts the rows scored, not the
  statistics.
- **Fusion:** reciprocal rank fusion with k = 60 (Cormack et al. 2009) over
  each branch's top 100.
- Ties in every branch are broken by key. Top k is 10.

These are the candidates of [research-algorithm-fit](research-algorithm-fit.md)
sections 2.3 and 2.4. No other engine is compared in this batch.

### 13.4 The two paths and the oracle

- **MoobFS path:** the eligible set is the `QUERY2` answer for the filter on
  the mounted layer, with every page followed. The branches score only that
  set.
- **Plain-files path:** the same files on a plain ext4 directory, not
  mounted through the layer, with a SQLite metadata table per uid (`key,
  question_id, role, date`) kept by the same watcher, answering the same
  filter. Same branches; its derived indexes are built the same way over
  that directory.
- **Oracle:** for each query, the eligible set walked from the files as the
  caller and every eligible turn scored by the vector model, embedded afresh;
  for the lexical branch, an FTS5 table built fresh from every memory the
  caller can read, queried for the eligible rows only. It reads no
  maintained index.

Filters, each run for every question of the caller:

| Id | Filter | Eligible set |
| --- | --- | --- |
| Q1 | `question_id` = the question's | one instance, about 500 turns |
| Q2 | Q1 and `role` = `user` | about half of Q1 |
| Q3 | none, the caller's whole private domain | about 25 000 turns, more than one `QUERY2` page |

Q3 is scored but has no relevance label of its own: its evidence is the
question's instance, mixed with the other 49 instances. It tests paging,
scale and the latency bound.

### 13.5 Metrics

Session level, from the sessions of the top 10 turns: `recall_any@10` (at
least one evidence session) and `recall_all@10` (every evidence session).
Turn level: nDCG@10 with `has_answer` as binary relevance (Järvelin and
Kekäläinen 2002). Each is averaged over the questions, per branch, for Q1
and Q2, with a 95% paired bootstrap interval of 10 000 resamples, seed 1.
The primary metric is `recall_all@10` on Q1.

### 13.6 Bounds that pass or fail G5 (proposed)

All required. One miss fails the package.

**Same answers on both paths.** For every question, filter and branch, the
MoobFS path's top 10 equals the plain-files path's top 10, keys and order:
0 differences.

**Exact.** Each branch's top 10 equals the oracle's for that branch: 0
differences, Q1 to Q3.

**Authorization.** 0 results, in any branch or filter, outside the caller's
eligible set. Uid 1001 gets 0 `bob` turns; uid 1002 gets 0 `alice` turns.
Revocation: uid 1002 changes the evidence turns of one `team` instance to
`0600`; once the watcher has reconciled, uid 1001 gets 0 of them in any
branch. The in-flight window is counted, as in section 12.6. Scores, counts
and timing are not claimed to be hidden (ROADMAP rule 4).

**Usable results.** Every query returns min(10, eligible) results. A
resource error from `QUERY2` fails the query with the error and no list, as
in section 5.

**Relevance floor.** On Q1, fusion's mean `recall_all@10` is at least the
better single branch's minus 0.02. If not, the result is published, G5 does
not pass as hybrid retrieval, and the branch that did better is recorded as
the recommendation for a later decision. No absolute recall number passes or
fails G5; all are published.

**Latency, warm, MoobFS path, fusion, including query embedding.** Q1 p95 at
most 300 ms. Q3 p95 at most 2 s. In each of 3 runs. These are this host's
workload choices, not guarantees, and they do not compare the two paths.

### 13.7 Recorded and not bounded

Plain-files path latency and its difference from the MoobFS path (a
comparison needs the method of section 7 and is not claimed here), CPU time
and peak RSS of the searching process and of each watcher, index sizes,
embedding time of the corpus, truncated turns, Q2's and Q3's metrics, the
per-type breakdown, and the integration burden of each path: the plain-files
path's metadata table and its synchronization, which the MoobFS path does not
need.

Still unbound after this amendment: any approximate mode and its recall
threshold, the G2 latency ceiling, and the G6 final evaluation.
*Note, 2026-10-28:* the G2 latency ceiling is section 14.

## 14. G2 latency ceiling, 2026-10-28 — accepted by the maintainer

Registered before any measurement of the module with ADR-0027, with the maintainer's choice of
relative bounds, so that no number depends on one host's speed.

### 14.1 Shape

The dedicated Debian host of section 9, ext4 lower, `domain_depth=1`, two unprivileged uids:
alice (uid 1001) with one private domain of 600 memories, and bob (uid 1002), the noisy
neighbour, with four private domains of 2 000 memories each, planted through the store as in
`g2_share.py`. `index_budget` is alice's measured bytes plus one and a half times those of one of
bob's domains: alice's domain and one of bob's fit together, a second of bob's does not, so each
of bob's builds releases another of his, and alice stays within her share.

### 14.2 Phases

1. **Uncontended.** Alice's domain is built once; then 200 warm searches of alice's selective
   query (`kind = decision`, limit 100), each timed around the store's `search()`.
2. **Contended.** A process of bob's queries his four domains in turn without pause, so that
   every query builds a domain and releases another of his (ADR-0021), for the whole phase.
   Alice runs the same 200 warm searches meanwhile. Each of bob's queries is timed.

### 14.3 Bounds, in each of 3 runs

- **Warm contention bound.** Alice's p99 in phase 2 is at most 2 times her p99 in phase 1.
- **Build bound.** The p99 of bob's queries in phase 2, each of which builds a domain, is at
  most 10 s, the cooperative deadline of ADR-0019.
- **No partial answer.** Every one of alice's searches returns the same items as her first;
  every one of bob's returns all 2 000 items of the domain or fails with `ENOMEM`, `E2BIG`,
  `ETIMEDOUT` or `EDQUOT` and no list.
- **Fairness.** Alice's domain is not rebuilt during phase 2 (`domain_builds` grows only by
  bob's builds; no release of alice's domain in the `moobfs_domain_release` trace).

A run that misses a bound is published as a failure of G2 on this module, with its numbers.

### 14.4 Recorded and not bounded

p50 and p95 of both phases, `index_used`, `domains_built`, `domain_builds` and
`domain_releases` before and after each phase, bob's error counts, and the peak RSS of both
processes. Section 7's method (30 repetitions, frequency held, bootstrap) is not claimed: the
bound is a ratio within one run, and no comparison with another module or engine is made.

### 14.5 Result, 2026-10-28 (journal 0228)

Dedicated host, kernel `6.12.111+deb13-amd64`, the module of ADR-0027, `g2_contention.py`:
alice 257 400 bytes, one of bob's domains 854 600, budget 1 539 300. In each of 3 runs alice's
p99 was 33.8, 33.8 and 35.6 ms alone and 36.8, 37.4 and 38.0 ms beside bob (ratios 1.09, 1.11,
1.07); bob's p99 0.154, 0.145 and 0.145 s; no partial answer; alice's domain never released;
`G2_CONTENTION_PASS`. Of bob's 219 to 223 queries per run, 66 or 67 built a domain and the rest
were refused with `EDQUOT` by ADR-0019's per-uid build admission, so the neighbour measured is
the one that limit leaves. The bounds of 14.3 are met on this host; G2's other acceptance items
(user-visible diagnostics, a deployment recipe) are not part of this section.
