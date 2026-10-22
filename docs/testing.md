# Testing guide

What each test in MoobFS runs, what its success markers prove and, as important,
what they do not prove. Environment setup, build commands and how to add a test
are in [development.md](development.md).

Never load `moobfs.ko` or `moobfs_layer.ko` on your host. The harness boots a
disposable QEMU guest; that is the only supported way to run the module.

## 1. Prerequisites

| To run | You need |
| --- | --- |
| `make test` | C11 compiler, GNU Make, Python 3 (Linux or macOS) |
| `make fuzz` | LLVM clang with libFuzzer (CI uses it; Apple clang does not ship it) |
| `make vm-test` | Docker. No KVM, no privileged container, no network inside the guest. The first run downloads the Debian base and kernel packages |
| KASAN/lockdep | Linux x86-64 with QEMU and `virtme-ng 1.41`, or trigger `.github/workflows/kasan.yml` |

Disk and time: `build/` grows to several GiB with the kernel tree for KASAN.
Measured wall-clock times per machine are not recorded yet; the CI timeouts are
35 minutes for the VM job and 150 minutes for KASAN, and the QEMU phase of
`make vm-test` is bounded by `timeout 2400`.

## 2. Targets

| Target | What it builds and runs | Where it writes |
| --- | --- | --- |
| `make` / `make tools` | `mkfs.moobfs`, `fsck.moobfs`, `attr_test` | `build/host/` (`BUILD=` overrides) |
| `make test` | `attr_test` (host unit tests of the typed attribute encoding) and `tests/test_images.py`: nine tests covering exact-byte sharing, distinct data, hash boundary sizes (0 to 65536 bytes), an independent CRC vector, deterministic images, overwrite refusal, limits, 128 objects and 15 malformed variants | `build/host/` |
| `make test BUILD=build/asan CC=clang CFLAGS=... LDFLAGS=...` | Same, under ASan and UBSan (see development.md for the exact flags) | `build/asan/` |
| `make fuzz FUZZ_TIME=30` | libFuzzer over `common/attr.c` from `tests/fuzz_attr.c`, seeded by `tests/fuzz-corpus/attr` | `build/host/fuzz_attr`, `build/host/fuzz-corpus/` |
| `make attr-tools` | Linux-only `moobfs-attr`, `moobfs-check`, `moobfs-index` | `build/host/` |
| `make module` / `make layer` | `kernel/moobfs.ko` / `layer/moobfs_layer.ko` against `KDIR` | in-tree (`kernel/`, `layer/`) |
| `make vm-test` | Docker image, both modules, tests and the full guest run (M1 regression, layer suite, acceptance) on Debian **6.12.111+deb13-amd64** under QEMU TCG | `build/vm/` |
| KASAN workflow | Upstream **6.12.112** with `vm/kasan.config` (KASAN, UBSAN, lockdep), the same `vm/suite.sh` inside virtme-ng | `build/vng/`, `build/linux/` |

The Debian base digest and kernel package are pinned, but auxiliary apt packages
are not fully locked: this is not a claim of bit-for-bit toolchain
reproducibility. If the pinned Debian package disappears, update image and
kernel together through a reviewed change and rerun the suite; do not substitute
host headers silently.

`MOOB_ONLY=memcg make vm-test` runs the bounded accounting regression alone in
the Debian guest. It needs cgroup v2 with `CONFIG_MEMCG=y`; the Debian harness
requires that controller and fails if it is missing. The upstream 6.12.112
KASAN kernel used here has `CONFIG_MEMCG` disabled, so its full suite prints
`MEMCG_SKIPPED` and tests memory safety, not cgroup charging.

## 3. Markers and what they mean

A run is read from the console log. Success markers are printed only after the
checks they summarize; a detected failure prints `MOOBFS_VM_FAIL: <step>` and
ends the run. A full-suite marker covers the steps that ran, including any
explicitly reported skips below; it does not imply every optional case ran.
Markers come from three places: `vm/init` and `tests/mounted.c` (M1),
`vm/suite.sh` and the test binaries it calls (layer), `vm/kasan-guest.sh` (KASAN).

| Accounting marker | Proves | Does not prove |
| --- | --- | --- |
| `MEMCG_BUFFER_RESULT` | A maximum-size empty `QUERY2` result buffer is charged to the caller's cgroup while its descriptor remains open | Exact physical footprint or charge migration if the descriptor changes owner |
| `MEMCG_INDEX_RESULT` | Building a bounded attributed domain raises the calling cgroup's unreclaimable slab charge | Fair sharing of the mount-wide index budget or charges to file owners |
| `MEMCG_ACCOUNT_PASS` | Both accounting assertions passed in a guest with the memory controller enabled | Every allocation path or every kernel configuration |
| `MEMCG_SKIPPED` | The guest lacks the memory controller; no accounting assertion ran | Accounting correctness; the Debian harness treats this as a failure |

### M1 regression (`vm/init`, Debian harness only)

| Marker | Printed by | Proves | Does not prove |
| --- | --- | --- | --- |
| `SHARED: inode=A/B physical=P length=L` | `tests/mounted.c` | Two files with different inode numbers report the same physical extent through FIEMAP | Page-cache sharing; anything about writes |
| `MOUNTED_READ_TESTS_PASS` | `tests/mounted.c` | Full reads, positional reads, EOF and seek return exactly the expected fixture bytes on the mounted M1 image | Behaviour on other images or kernels |
| `WRITABLE_MOUNT_REJECTED` | `tests/mounted.c --reject-rw` | A read-write mount of the loop device is refused | Protection against writes to the backing device from outside |
| `MOOBFS_VM_PASS: ...` | `vm/init` | All of the above, remount, `remount,rw` refused, 15 malformed images refused, module unloaded, no `BUG`/`Oops`/`WARNING`/`KASAN` in `dmesg` | KASAN/lockdep (the Debian kernel has neither); performance |

### Layer suite (`vm/suite.sh`, both harnesses)

| Marker | Printed by | Proves | Does not prove |
| --- | --- | --- | --- |
| `LAYER_PASSTHROUGH_TESTS_PASS` | `tests/layer.c`, once per lower filesystem and once more with `nolinks` | Create, write, pread, append, truncate, shared and private mmap, mkdir/rmdir, readdir, chmod, unlink, rename, xattr, hard links and symlinks behave as on the lower tree, with the same inode numbers and bytes visible below; `FICLONE`/`FIDEDUPERANGE` share extents on btrfs and XFS and fail cleanly on ext4 | Full POSIX conformance (fstests ran as its `quick` group only, `vm/fstests/`); other lower filesystems |
| `TYPED_LIMITS_PASS` | `tests/layer.c typed`, once per lower filesystem | Every refusal of a typed attribute write through the layer, with its errno, and the lower file unchanged after each (names and values): `user.moob.format` gives `EPERM`; empty, uppercase, non-ASCII, 65-byte, edge-dot, doubled-dot and out-of-alphabet names give `EINVAL` (also on removal); an empty value, unknown types, wrong lengths of int64, time, double and bool, NaN doubles, bools outside 0/1, strings with NUL, overlong, surrogate, above U+10FFFF or cut sequences, a 1 025-byte string and a 4 097-byte raw give `EINVAL`, also as a replacement; `XATTR_CREATE` on an existing and `XATTR_REPLACE` or removal on an absent typed name give `EEXIST` and `ENODATA`. The 65th distinct typed attribute gives `ENOSPC` while an opaque attribute of the same size still fits; replacing at the limit succeeds, `XATTR_CREATE` of an existing one gives `EEXIST`, a removal frees one slot; `user.moob.format` and opaque names (`user.moob`, `user.moobx.*`, `user.other.*`) do not count. The bounds themselves (1 024-byte string, 64-byte name, +infinity, four-byte UTF-8) are accepted | Which errno `XATTR_REPLACE` on an absent name gives at the limit (the code gives `ENOSPC`, not tested); ENOMEM paths; a lower whose own xattr space holds fewer than 64 entries |
| `HOSTILE_LOWER_PASS` | `vm/suite.sh` with `tests/hostile.c`, once per lower filesystem; 3 trees, or `HOSTILE_SEEDS` | Over trees planted directly on the lower with every rule of the attribute encoding broken, names to skip, files beyond 64 attributes and hard links: the mount succeeds, the index equals the oracle with domains off and on, changes behind the mount neither crash nor hang, a remount equals the oracle again, and only a decimal format marker this module knows is accepted | Anything about a crafted disk image of the lower filesystem; budget exhaustion (S9); syzkaller-style system call sequences (S3) |
| `QUERY_INVALID_REQUESTS_REFUSED` | `tests/query.c invalid` | Malformed `QUERY` requests (wrong version, too many terms, zero limit, unknown flags, nonzero reserved or pad bytes, invalid name alphabet, unknown operator, operator/type mismatches, wrong value length, invalid UTF-8, zero terms, a limit above 4 096, a `QUERY2` term flag, an invalid second term) return `EINVAL`; an unknown ioctl number and a `QUERY` on a directory other than the mount root return `ENOTTY` | Resistance to inputs the test does not construct (syzkaller has not been run) |
| `IDMAP_REFUSED_EINVAL` | `tests/idmap.c idmap` | `mount_setattr` with an idmapping on the layer mount is refused with `EINVAL` | Correct behaviour over an idmapped **lower** mount (untested) |
| `USERNS_MOUNT_REFUSED_EPERM` | `tests/idmap.c userns` | Mounting the layer from an unprivileged user namespace fails with `EPERM` (the filesystem does not set `FS_USERNS_MOUNT`) | Anything about containers that bind-mount an existing layer |
| `ISOLATION_PASS: <fs>` | `vm/suite.sh` | `QUERY` as uid 1001 and 1002 equals the oracle run as that uid, never reveals the other namespace in results or returned counts (response time is not covered; see journal 0030), including beyond one result chunk), ignores attribute changes as a grant, follows chmod and POSIX ACLs immediately and across remount; identity duplicates are detected by `moobfs-check`; the two mount-contract markers above; a 5-second race of chmod/rename/unlink/directory-rename against `QUERY` yields only well-formed answers and a final answer equal to the oracle | Absence of timing channels; isolation under workloads the race does not produce |
| `QUERY2_PASS: <fs>` | `vm/suite.sh` | With the default `domain_depth=1`: `MOOB_IOC_QUERY2` on three domains (two private, one shared) equals `moobfs-index --domain`, with paths relative to the domain; a uid without access to a domain cannot open or query it; subdirectory and root descriptors get `ENOTTY`; malformed requests get `EINVAL`; the v1 `QUERY` is refused to a regular user and still answers root; a stricter file mode, an extended access ACL and a second hard link exclude a file and reverting brings it back; a directory mode change inside a domain is counted in `fallback_domains` and answers stay equal to the oracle; a directory renamed across domains or depths gets `EXDEV`, inside its domain it succeeds; a file moved to another domain changes partition; a remount rebuilds the same answers and clears the fallback | Response time (the timing-channel measurement is separate, journal 0035); ordering, cursor, batch and `MISSING_OK` (later phases); concurrency under domains |
| `QUERY2_ORDER_PASS: <fs>` | `vm/suite.sh` | `QUERY2` with `order_by` equals `moobfs-index --domain --order` ascending and descending, with ties, two value types under one name, string values longer than the cursor's inline space, a range term, and the top 5; paging by cursor with limits 1 to 7 returns exactly the unlimited answer, ordered or not, also in fallback mode and after remount; a cursor is accepted with another limit and refused with `ESTALE` for another query, another order, after fallback and after remount; malformed order fields get `EINVAL`, a garbage cursor `ESTALE`, an absent order attribute an empty answer | Paging under concurrent changes; response time |
| `HOMOGENEITY_GAPS_PASS: <fs>` | `vm/suite.sh` | The two parts of the homogeneity rule round 2 found untested (journal 0155, T3 and T4): an extended ACL on the domain directory, set before the build, keeps a file the ACL's user cannot read out of that user's answers; an attributed file written, and a file renamed, under a 0700 directory inside a built domain stay out of another user's answers while `fallback_domains` is 0. Also in the same step: a cursor changed outside its query hash is refused by the MAC alone (T2), and `RENAME_WHITEOUT` is refused (V6). Each check has a negative round on branch `neg/homogeneity-gaps` (journal 0156) | Other ACL shapes (default ACLs, group entries); links under a private directory |
| `FAULT_INJECTION_PASS` | `vm/kasan-guest.sh` (KASAN harness only: needs `fail-nth`) | `tests/failnth.c` fails the 1st, 2nd, … N-th allocation of each operation in turn; every failed call returns an error an allocation failure explains, no kernel diagnostic follows, and the index equals the oracle once injection stops. Since journal 0156, also the state a fault leaves: a name created, made or linked on the lower is visible through the layer or absent from both (R2-4); a dropped entry takes its record (R2-8); a file whose attribute replacement failed is answered in another built domain, after a rename, with the lower's value (R2-3) | Faults outside allocations (I/O errors of the lower); this root-only marker does not by itself prove T9 |
| `UNPRIV_FAULT_PASS` | `tests/permission-gaps.c faults`, KASAN faults step (journal 0178) | After dropping all uid/gid slots to 1001, plain, ordered and batched QUERY2 on built and fresh domains see only the readable file or return an explained allocation error; a disarmed retry and lower `getxattr` controls verify membership. The targeted run injected 201 real faults over 207 calls; the full KASAN run injected 204 over 210 calls, with zero unexpected outcomes | Every allocation site, other identities or LSMs, I/O faults |
| `RENAMEPATH_FAULT_PASS` | `tests/renamepath-fault.c`, KASAN faults step | An uninjected rename preserves its new path after dentry eviction. A rename with a confirmed allocation failure in path recording returns the new path or `MOOB_RES_PATH_UNKNOWN`, never the old path (R2-6, journal 0170) | ext4, root, one failed allocation; not all rename failure sites |
| `DEADSCAN_PASS` | `tests/deadscan.c`, KASAN faults step | Function tracing observes the first file read after a subdirectory's listing; removing its files and then the directory leaves the domain query successful and empty (R2-10, journal 0170) | ext4, root, one synchronized removal; not every concurrent namespace operation |
| `QUERY2_LEAPFROG_PASS: <fs>` | `vm/suite.sh` | `QUERY2` with two or three equality terms, mixed with `EXISTS` and range terms, equals `moobfs-index --domain`, including empty intersections and a type mismatch; paging with limits 1 and 2 returns the full answer, also in fallback mode; the plan check: with a non-equality first term whose values fall as inodes rise, the answer comes in rising inode order, which only the intersection produces | Speed against the plain scan (measured on the benchmark host by loading the Q2b and Q2c modules in turn) |
| `QUERY2_MISSING_BATCH_PASS: <fs>` | `vm/suite.sh` | Terms with `MOOB_TERM_MISSING_OK` (`gt?`, `le?`, `eq?`) equal `moobfs-index --domain` when combined with a plain term in either position, with `order_by` alone, across a type mismatch (not missing) and when paged; `QUERY2_BATCH` with three requests equals the oracle per request, also in fallback mode; a batch on a domain the caller cannot open, on a subdirectory, with nine requests or with one invalid request is refused; `q2invalid` covers term flags, empty and oversized batches, header fields, a batch that must not run when one request is invalid, and a bad pointer, and (journal 0141) each term check next to the valid request it differs from: `EXISTS` with a value, unknown operators, empty, unterminated and malformed names, pad bytes, an invalid second or last term, raw and unknown types, wrong lengths, NaN, bool outside 0/1, `PREFIX` on non-strings, strings longer than the value buffer or with NUL or invalid UTF-8, an "or missing" term that fails its own check, malformed order names and types | Batches under concurrent changes; response time |
| `BATCH_RUNTIME_ERRORS_PASS` | `tests/batch-errors.c`, run by `vm/suite.sh` before and after fallback on each lower | As uid/gid 1001 with no supplementary groups: nonempty single-query baseline; invalid second request leaves all arrays/headers untouched; fault on the second result leaves a verified earlier result without its updated header; read-only headers fault after all results are copied; a reinitialized valid batch succeeds afterward | Allocation failures, concurrent changes, every possible partial-copy boundary; output from failed calls must be discarded, not consumed as a partial success (journal 0173) |
| `QUERY2_RACE_PASS: <fs>` | `vm/suite.sh`, `tests/query.c race2` | Two query processes share one open file; only the parent prints statistics after validating the child exit status (journal 0171). For 5 s, mode changes, renames, unlink and recreate, files moving to another domain and back, and `chmod` of the domain directory race paged `QUERY2` (limit 7) as uid 1001, cycling the plain, equality and ordered plans: every result is well formed (NUL-terminated, relative, length consistent) and openable unless removed, recreated or denied after the answer; `ESTALE` when the domain switches to fallback restarts paging; afterwards the domain is in fallback, and the answer, paged by 3 or not, equals `moobfs-index --domain` | Absence of all races (5 s of one interleaving per run); duplicates across pages while keys change, which keyset paging allows |
| `LAYER_LOWER_PASS: <fs> reflink=<yes|no>` | `vm/suite.sh`, once per lower filesystem | Everything in `run_lower` for that filesystem: mount contract (no `lower=`, bad `lower=`, stacking on itself all refused), pass-through tests, typed attribute tools (`moobfs-attr`, `moobfs-check` with a planted invalid encoding), index and `QUERY` equal to `moobfs-index` for ten predicates and after rename, directory rename, unlink, removexattr and remount, `noindex` and `index_budget=1M`, isolation, three `fsx` seeds (9000 operations) with the result compared to the lower copy, `nolinks` remount, clean unmount | Durability (that is the lower filesystem's), performance, behaviour with the budget exhausted on large trees |
| `DEMO_PASS: ...` and the `DEMO| ` lines before it | `vm/suite.sh`, once over ext4 after the three lower filesystems | The README demonstration ran as printed: under the default mount options, attributes written through the layer are readable on the lower tree; `QUERY2` as uid 1001 returns its own domain's file and, in a shared domain, only the file every visitor can read; another user's domain gives `EACCES` at `open`; the v1 `QUERY` gives `EPERM` to that user and answers root with all four files; `GET_INFO` counts one heterogeneous file and no fallback domain | Anything beyond those four files; the `DEMO| ` lines are a transcript, not an extra check |
| `SQLITE_RESULT <label>: rows=... integrity=... reads=... missing=[...]` and `SQLITE_VARIANTS: lower=.. alone=.. layer=.. rollback=.. excl=.. again=..` | `vm/suite.sh` | Per-variant counts are printed before any verdict: control on the lower filesystem, WAL alone, WAL with a concurrent reader (twice), rollback journal, `unix-excl` VFS; each exit code is 0 when 200 of 200 rows survive with `integrity_check` ok | Behaviour of SQLite builds or page sizes other than the Debian package in the image |
| `SQLITE_CONCURRENT_OK` | `vm/suite.sh` | The control, the WAL-alone run and both WAL-with-reader runs kept all rows (this is the regression for the phase 2e data-loss bug) | Other concurrency patterns |
| `ACCEPTANCE_CLIENTS_PASS` | `vm/suite.sh` | git (three commits, `fsck --strict`, clean status), SQLite as above and the memory-client convention (`remember`) work on the layer over ext4, and `moobfs-check` finds no problem afterwards | The same over btrfs or XFS (acceptance runs over ext4 only) |
| `POWER_LOSS_REPLAY_PASS: marks mkfs m1 m2 m4 end` | `vm/suite.sh` | With the lower ext4 on `dm-log-writes`, replaying the log to each mark on a zeroed disk, running `e2fsck -fn` and mounting the layer shows every memory synced before the mark whole and later ones absent, with `QUERY` counts matching | Crash consistency without the client's `sync` of the parent directory; other lower filesystems; barriers under real hardware |
| `CASEFOLD_REFUSED_PASS` | `vm/suite.sh`, `tests/layer.c cfsetup/cfcheck` | On an ext4 made with `-O casefold`, a casefolded lower directory and every name in it are refused through the layer with `EOPNOTSUPP` (in either case), ordinary directories next to it work, an index build skips it, and a casefolded lower root refuses the mount (issue #95); `CASEFOLD_SKIPPED` when the kernel or `mke2fs` cannot prepare casefolding | f2fs and tmpfs lowers; a directory casefolded after the layer looked it up |
| `IO_ERROR_INJECTION_PASS` | `vm/suite.sh` | With `dm-flakey error_writes` under the lower ext4, write errors reach the caller, nothing crashes, and after the device heals `e2fsck` and `moobfs-check` are clean and the earlier memory is intact | Read-error paths; errors on btrfs or XFS |
| `MOOBFS_ACCEPTANCE_PASS: ...` | `vm/suite.sh` | The whole acceptance block plus module unload and a clean `dmesg` scan | See the rows above |
| `MOOBFS_LAYER_PASS: ...` | `vm/suite.sh` | The three `LAYER_LOWER_PASS` lines, acceptance, unload, clean `dmesg` | KASAN/lockdep when printed by the Debian harness |

The binary `tests/ipc.c` (`IPC_PRIMITIVES_PASS`: fcntl locks and shared mmap
coherence between two processes) is built and staged but is not currently
invoked by `vm/suite.sh`; it was used during the phase 2e investigation
(journal 0015).

### Decisive permission and independent-oracle checks (journal 0172)

`MOOB_ONLY=permgaps make vm-test` runs `tests/permission-gaps.c` on ext4,
btrfs and XFS; the full suite includes it too. Child processes drop all uid/gid
slots and supplementary groups before opening query descriptors.

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `TARGET_EXEC_PASS` | uid 1001 opens a readable/non-searchable domain successfully, then QUERY2 and QUERY2_BATCH return EACCES; a searchable domain succeeds | Every ACL or namespace configuration |
| `TRUNCATED_PERMISSION_PASS` | With 70 potentially matching hidden candidates, counts and `truncated` reflect readable results only; zero, exact-limit and overflow controls in v1, QUERY2 plain/intersection/ordered asc/desc | Timing channels; arbitrary concurrent permission changes |
| `ATTRIBUTED_DIRECTORY_PASS` | Attributed directories, including a readable/non-searchable final component, agree with lower `getxattr` before and after fallback | Every directory/ACL shape |
| `INDEPENDENT_ENCODING_PASS` | 18 fixed byte/mode/owner fixtures have the expected membership without calling `common/attr.c`; both v1 and QUERY2, uid 1001 | An independent validator of every encoding or randomized hostile input |
| `PERMISSION_GAPS_PASS: <fs>` | The four checks above completed on that lower | A complete suite by itself |
| `UNPRIV_RELEASE_PASS` | The release step queries as uid 1001 before a verified shrinker release and after rebuild, comparing public/hidden controls with the lower | Allocation-fault behavior is checked separately by `UNPRIV_FAULT_PASS` in the KASAN faults step |

`HOSTILE_LOWER_PASS` retains its randomized shared-validator comparison. The
fixed independent fixtures supplement it; they do not make its oracle independent.
`FAULT_INJECTION_PASS` remains the root-only marker. The separate
`UNPRIV_FAULT_PASS` covers T9's allocation-fault cases on the dedicated host
([journal 0178](history/0178-unprivileged-allocation-fault-queries.md)); its
targeted and full KASAN runs passed. The native `make vm-test` suite passed
separately; it does not itself inject T9 allocation faults.

### Domain renamed while a build waits to start (issue 110, journal 0174)

`MOOB_ONLY=rootpath make vm-test` runs `tests/rootpath-race.c` on ext4, btrfs
and XFS; the full suite includes it too. A mount-wide QUERY holds a domain's
build mutex while a QUERY2 on that domain waits for it, and the domain
directory is renamed meanwhile. The files are planted on the lower and the
caches dropped, so the answer of the next mount-wide QUERY comes from the
names the build recorded. Up to three attempts per lower; an attempt that
misses the interleaving is inconclusive, never a pass.

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `ROOTPATH_RACE_PASS` | After that interleaving (QUERY2 seen in `D` state while the holder builds, rename before the holder returned, a build by the waiter counted), the mount-wide QUERY returns every attributed file, under the new name, each resolving to its inode | The unwidened microsecond window, or renames of a directory above the domain |
| `ROOTPATH_PASS: <fs>` | The check above passed on that lower | A complete suite by itself |

The holder may fail with `EAGAIN`, the documented answer of a v1 QUERY whose
domain is taken down while it checks results (`query_repin()`).

### Concurrent index-budget refusals (issue 111, journal 0175)

`MOOB_ONLY=overbudget make vm-test` runs `tests/overbudget-race.c` on ext4,
btrfs and XFS; the full suite includes it too. The fixture alternates queries
between two domains that each fit the budget alone but cannot coexist, while
four other domains are rebuilt after renames. It makes at least 30 alternating
queries per lower, subject to a sixfold time cap.
Journal 0179 records a native KVM negative reproduction and a local correction
for a busy oldest release candidate, followed by passing complete Docker VM,
native KVM and KASAN runs. The KVM wrapper was a dedicated-host cross-check;
`make vm-test` remains the reference invocation.

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `OVERBUDGET_RACE_PASS` | At least 30 queries of the two fitting domains completed without `ENOMEM` or an unexpected error; prints `EAGAIN`, `ENOMEM` and other-error counts | Every possible thread schedule; `EAGAIN` is tolerated during concurrent rename activity |
| `OVERBUDGET_PASS: <fs>` | The fixture passed on that lower | A complete suite by itself |

### KASAN harness (`vm/kasan-guest.sh`)

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `MOOBFS_KASAN_PASS: ...` | The full `vm/suite.sh` passed on upstream 6.12.112 with KASAN (generic, inline, vmalloc), UBSAN bounds, `PROVE_LOCKING`, `DEBUG_ATOMIC_SLEEP`, `DEBUG_LIST`, `DEBUG_VM`, `PROVE_RCU`, and `dmesg` contains none of `BUG: KASAN`, `KASAN:`, `BUG:`, `Oops:`, `WARNING:`, `possible circular/recursive locking`, `INFO: task/rcu`, `UBSAN:` | KMSAN, KFENCE (not enabled); data races (KCSAN runs separately, `kcsan.yml`, journal 0127); code paths the suite does not reach; other kernel versions |
| `MOOBFS_VM_FAIL: kernel diagnostics under KASAN/lockdep` | The suite passed but `dmesg` had a diagnostic; `build/vng/dmesg.txt` has it | — |
| `MOOBFS_VM_FAIL: missing <tool> on the host` / `missing disk /dev/vdX` | Environment error before the suite started | — |

### Interoperability test (`vm/interop-guest.sh`, `interop.yml`)

On demand only, never a required check (journal 0140). A plain build of the pinned 6.12.112
kernel under virtme-ng, one layer mount over ext4. Each check prints `INTEROP_OK: <step>` or
`INTEROP_CHECK_FAIL: <step>`; `INTEROP_OBSERVED:` lines record what the tools show of the typed
encoding and are not judged.

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `INTEROP_PASS` | Every step below passed: `einval-plain-moob` (setfattr of a plain or empty value under `user.moob.*` fails with `EINVAL`, nothing reaches the lower), `hex-typed-write` (a value with the type byte in hex is accepted and read back by `moobfs-attr`), `hex-layer-equals-lower` and `-final` (`getfattr -e hex` prints the same attributes and bytes through the layer and on the lower), `xdg-tags-roundtrip` (set, replace and remove `user.xdg.tags`, seen identically on both sides), `fselect-has-xattr` and `fselect-xdg-value`, `cshatag-ok`, `cshatag-outdated`, `cshatag-corrupt` (content changed with the mtime restored, exit 5) and `cshatag-keeps-typed`, `recoll-moob-field` and `recoll-xdg-fields` (field searches over `[xattrtofields]` find exactly the right files), `kernel-log` (no `BUG:`, `Oops:`, `WARNING:` or panic in `dmesg`) | KFileMetaData (run and recorded when its helper builds, `INTEROP_SKIP: kfmd` otherwise); Baloo's indexer and Dolphin; numeric search of typed values; other lower filesystems; KASAN |
| `INTEROP_FAIL: <steps>` | Lists every failed step; `setup (<what>)` when the mount, the module or a required tool was missing | — |

### Failure marker

`MOOBFS_VM_FAIL: <step>` names the failing step as written in the `fail` call
(for example `query-eq-btrfs`, `iso-leak-1001-xfs`, `lw-check-m2`). Search the
step name in `vm/suite.sh` to find the exact command. `vm/init` appends the last
60 lines of `dmesg`; `vm/kasan-guest.sh` appends 80 and saves the full
`dmesg.txt`.

## 4. Reading the logs

`build/vm/` (Debian harness):

| File | Purpose |
| --- | --- |
| `console.log` | The whole guest console; search for `_PASS`, `MOOBFS_VM_FAIL:` and `SQLITE_` |
| `environment.txt` | Kernel, compiler, QEMU, package versions and SHA-256s of what was booted |
| `kernel.config` | Exact tested kernel configuration |
| `image-report.json` | `fsck.moobfs` report of the M1 fixture: inodes, shared object, offsets, hashes |
| `shared.img`, `moobfs.ko`, `moobfs_layer.ko`, `initramfs.cpio.gz` | Image, modules and guest filesystem used |

`build/vng/` (KASAN): `console.log`, `dmesg.txt`; the kernel configuration is
`build/linux/.config`. In CI both sets are uploaded as artifacts
(`native-vm-evidence` for 30 days, `kasan-evidence`).

Expected noise that is **not** a failure:

- rejection messages for the 15 malformed M1 images and for the `remount,rw`
  attempt (the test asserts they are rejected);
- `mount` failures for the three invalid layer mounts at the start of the suite
  (no `lower=`, nonexistent lower, stacking on itself);
- the lines `note: a temporary file survived the crash at <mark> (allowed)` and
  `note: writes were absorbed by the page cache before the device reported errors`;
- unsigned/out-of-tree module taint messages;
- `SQLITE_RESULT` lines for `layer-rollback` and `layer-wal-unix-excl` with a
  nonzero exit code are printed for information; the verdict uses `lower`,
  `alone`, `layer` and `again` only.

A failure is: `MOOBFS_VM_FAIL:`, a missing required final marker, QEMU exiting
nonzero (including the 2400-second timeout), or in KASAN any of the `dmesg`
patterns above. **QEMU exit 0 alone does not prove success:** `vm/init` powers
off on both failure and success. `vm/in-container.sh` requires exit 0, all three
full-run markers (`MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`,
`MOOBFS_ACCEPTANCE_PASS`) and no `MOOBFS_VM_FAIL`; a requested partial run
requires `MOOBFS_ONLY_PASS` in place of the two layer/acceptance markers.

Read these coverage exclusions even when the aggregate marker passes:

| Console result | Coverage actually obtained |
| --- | --- |
| `CASEFOLD_SKIPPED` | No casefold-refusal test: kernel or `mke2fs` cannot prepare the fixture |
| `STALL_SKIPPED` | No stalled-FUSE test: `/dev/fuse` is unavailable; KASAN has FUSE built in |
| `LOWERRO_PASS: ... idmapped lower: not tested ...` | Read-only/freeze checks ran, but an idmapped bind lower could not be created; only `idmapped lower: refused` proves that case |
| `ipc` binary present | Built/staged only, not invoked by the suite; no `IPC_PRIMITIVES_PASS` evidence |

`tests/landlock_run.c` and `tests/notify.c` are standalone investigation helpers,
not built by `vm/build-tests.sh` or invoked by this suite. `faults` is exclusive
to the KASAN harness; a full Debian run does not execute `failnth`,
`renamepath-fault` or `deadscan` scenarios merely because their binaries compile.

Correction dated 2026-10-21 (journal 0171, issue 114 T11): clarify the guest
poweroff verdict and optional skips; the QEMU guard has been 1200 seconds since
journal 0153. `ipc` is compiled by the shared build list, as already recorded
above; it is execution that is absent.

## 5. Running a single test

`MOOB_ONLY=<step> make vm-test` runs selected named steps of `vm/suite.sh` in a
disposable VM and stops at the first failure. It ends in `MOOBFS_ONLY_PASS`,
which is partial evidence, not a substitute for the complete suite. The step
names are checked by `vm/suite.sh` before the guest starts.

Correction dated 2026-10-20: this section previously said the switch was only
planned, although section 6 and the suite already described and implemented it.

## 6. KASAN locally and in CI

While developing, `MOOB_ONLY="btrfs powerloss" make vm-test` (or the same variable before
`sh vm/kasan-local.sh`) runs only those steps of `vm/suite.sh`; the accepted
names are listed in its `MOOB_ONLY` validation. Such a run ends in `MOOBFS_ONLY_PASS` and is not
evidence for a pull request.

The complete `ext4`, `btrfs` and `xfs` steps include `tests/fcntl-flags.c`:
after clearing `O_APPEND` with `F_SETFL`, a positional write must overwrite at
its requested offset; after setting it again, the write must append (journal
0149). `MOOB_ONLY=xattrread make vm-test` exercises injected `EIO` and `ENOMEM`
on a listed typed value during index construction, retry, adoption into a
built domain and an `ENODATA` list/get race (journal 0150). It injects the
value-read result in the module; it does not simulate a physical lower-I/O fault.

See [development.md](development.md) section 6 for the command-by-command
reproduction of `.github/workflows/kasan.yml`. The job starts on every pull
request and is a required check of `main`; it runs the suite when the pull
request touches `layer/`, `vm/`, `include/`, `common/`, `tests/`, the `Makefile`
or the workflow, and passes at once otherwise. It also runs weekly on Monday,
and on demand with `gh workflow run kasan.yml --ref <branch>`. For `layer/` or
`vm/` changes the PR must link a green run on the final revision.

On a Linux host with KVM, `sh vm/kasan-local.sh` runs the same job: the same pinned kernel
(checked against the SHA-256 in the workflow), `vm/kasan.config`, test binaries and
`vm/kasan-guest.sh`. It builds the kernel once into `~/.cache/moobfs-kasan` (rebuilt only when
`vm/kasan.config` changes), so later runs cost only the module, the tests and the suite. It
needs the packages of the workflow's install step, virtme-ng from `vm/kasan-requirements.txt`
(a virtualenv is fine) and membership in the `kvm` group. A local pass is a faster development
loop; the workflow on the pull request stays the required check.

### SQLite virtual table

`MOOB_ONLY=sqlext make vm-test` (journal 0108) runs the step that compares the `moobfs` virtual
table (`sqlite/`) with an ordinary SQLite table filled from the lower filesystem, statement by
statement, and prints each plan. Marker: `SQLEXT_PASS`. It is part of the complete run.

### Read-only, frozen and idmapped lower

`MOOB_ONLY=lowerro make vm-test` (journal 0117) gives the layer a read-only bind mount as lower
(every change must fail with `EROFS`, the lower unchanged), freezes the lower ext4 (a create, a
`chmod` and an attribute set must wait for the thaw with no kernel warning) and gives it an
idmapped bind mount (refused at mount when one can be created; otherwise the
marker explicitly says `idmapped lower: not tested`). Marker: `LOWERRO_PASS`.
It is part of the complete run; inspect the idmap suffix independently.

### SELinux

`sh vm/selinux-local.sh` (journal 0104) runs, on a Debian host with KVM and the module built,
a guest of the host's kernel booted with SELinux enforcing: a confined client compared with
direct access on the lower, file by file, and a `QUERY2`, under the stock policy and with
`vm/selinux/moobfs_layer.cil`. The script's header lists the host packages and the one change
to `/etc/selinux/semanage.conf`; SELinux is not activated on the host. Markers:
`MOOBFS_SELINUX_RESULT: <run> differ=<n> denied_directly=<n>` per run, `MOOBFS_SELINUX_FAIL`
on any failed check. Not part of `make vm-test` nor of CI.

## 7. Adding a lower filesystem

Each new lower filesystem widens the tested matrix and needs a journal entry; it
does not widen the project's scope without an ADR. Files to touch:

1. `vm/Dockerfile`: the user-space package (for example `f2fs-tools`).
2. `vm/in-container.sh`: the module path in the `visit(...)` list so its chain
   is extracted from `modules.dep`; a sparse image (`truncate` + `mkfs`); one
   more `-drive ... if=virtio` line.
3. `vm/init` and `vm/kasan-guest.sh`: a `DEV_<FS>` variable for the new virtio
   device (the order of `-drive`/`--disk` lines defines `/dev/vdX`).
4. `vm/suite.sh`: `run_lower <fs> "$DEV_<FS>" <reflink yes|no>` and the header
   comment.
5. `vm/kasan.config`: `CONFIG_<FS>=y` (and its ACL option).
6. `.github/workflows/kasan.yml`: the apt package, the image creation and one
   more `--disk`.
7. `docs/testing.md`: this guide and the marker tables; `README.md` matrix.

## 8. Manual mount inside your own disposable Linux VM

Use the tested kernel and matching headers; compile the module in that VM. The
following assumes the M1 image from the README has been created:

```sh
make module KDIR=/lib/modules/$(uname -r)/build
sudo insmod kernel/moobfs.ko
sudo mkdir -p /mnt/moob
sudo mount -t moobfs -o loop,ro build/demo.img /mnt/moob
ls -li /mnt/moob
cmp /mnt/moob/alpha.txt /mnt/moob/beta.txt
cat /mnt/moob/alpha.txt
sudo umount /mnt/moob
sudo rmmod moobfs
```

For the layer: `make layer`, `insmod layer/moobfs_layer.ko`, then
`mount -t moobfs_layer -o lower=<dir>[,nolinks][,noindex][,index_budget=<n>][,domain_depth=<0-8>] none <mnt>`.
`domain_depth` defaults to 1 (first-level directories are domains for `MOOB_IOC_QUERY2`); 0 turns
domains off and keeps the v1 `QUERY` open to every user (`docs/spec-query-v2.md`).
Do not alter the backing image or the lower tree from outside while mounted.
Signed-module policies of a particular VM may reject unsigned modules. Other
kernel versions are unverified.

## 9. What is not covered

- Linux security modules beyond the SELinux run above (AppArmor, Smack, Landlock).
- xfstests over the layer (only the vendored `fsx`), syzkaller over the ioctls
  and `setxattr`, a hostile lower tree, KMSAN/KCSAN/KFENCE: see
  [security-roadmap.md](security-roadmap.md) S2 to S4 and S7 to S8.
- Kernels other than Debian 6.12.111 and upstream 6.12.112; architectures other
  than x86-64.
- Any performance, latency or memory measurement (M4, [benchmark-plan.md](benchmark-plan.md)).
- Acceptance over btrfs and XFS (it runs over ext4 only); `ipc` in the suite.

## 10. CI and documentation

GitHub Actions runs `Tools and sanitizers` (host tests, ASan/UBSan, 30 s of
fuzzing, site link check), `Lint` (shellcheck, SPDX headers, DCO sign-off on
pull requests) and `Native Linux VM` on pushes to `main` and on pull requests;
`KASAN and lockdep` runs on pull requests touching `layer/` or `vm/`, weekly and
on demand. Logs are temporary Actions artifacts; permanent conclusions belong
in the journal with links to the run. The Pages workflow publishes only `site/`.
Preview locally with `python3 -m http.server 8080 --directory site`.
