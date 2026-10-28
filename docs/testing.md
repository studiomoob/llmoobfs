# Testing guide

What each test in MoobFS runs, what its success markers prove and, as important,
what they do not prove. Environment setup, build commands and how to add a test
are in [development.md](development.md).

Never load `moobfs.ko` or `moobfs_layer.ko` on a machine you use for anything else. The suite
runs in a disposable QEMU guest. Installing the layer on a dedicated test host is described in
[operations.md](operations.md), and `make install-test` follows that guide in a disposable VM.

## 1. Prerequisites

| To run | You need |
| --- | --- |
| `make test` | C11 compiler, GNU Make, Python 3 (Linux or macOS) |
| `make fuzz` | LLVM clang with libFuzzer (CI uses it; Apple clang does not ship it) |
| `make vm-test` | Docker. No KVM by default (`MOOB_VM_KVM=1` passes a writable `/dev/kvm` to the container), no privileged container, no network inside the guest. The first run downloads the Debian base and kernel packages |
| KASAN/lockdep | Linux x86-64 with QEMU and `virtme-ng 1.41`, or trigger `.github/workflows/kasan.yml` |

Disk and time: `build/` grows to several GiB with the kernel tree for KASAN.
Measured wall-clock times per machine are not recorded yet; the CI timeouts are
35 minutes for the VM job and 150 minutes for KASAN, and the QEMU phase of
`make vm-test` is bounded by `timeout 3600`. On the dedicated i5 host the complete suite took
2575 s under TCG (journal 0219); `MOOB_VM_KVM=1` runs the same guest under KVM there.

## 2. Targets

| Target | What it builds and runs | Where it writes |
| --- | --- | --- |
| `make` / `make tools` | `mkfs.moobfs`, `fsck.moobfs`, `attr_test` | `build/host/` (`BUILD=` overrides) |
| `make test` | `attr_test` (host unit tests of the typed attribute encoding) and `tests/test_images.py`: nine tests covering exact-byte sharing, distinct data, hash boundary sizes (0 to 65536 bytes), an independent CRC vector, deterministic images, overwrite refusal, limits, 128 objects and 15 malformed variants | `build/host/` |
| `make test BUILD=build/asan CC=clang CFLAGS=... LDFLAGS=...` | Same, under ASan and UBSan (see development.md for the exact flags) | `build/asan/` |
| `make fuzz FUZZ_TIME=30` | libFuzzer over `common/attr.c` from `tests/fuzz_attr.c`, seeded by `tests/fuzz-corpus/attr` | `build/host/fuzz_attr`, `build/host/fuzz-corpus/` |
| `make attr-tools` | Linux-only `moobfs-attr`, `moobfs-check`, `moobfs-index` | `build/host/` |
| `make module` / `make layer` | `kernel/moobfs.ko` / `layer/moobfs_layer.ko` against `KDIR` | in-tree (`kernel/`, `layer/`) |
| `make vm-test` | Docker image, both modules, tests and the full guest run (M1 regression, layer suite, acceptance) on Debian **6.12.111+deb13-amd64** by default under QEMU TCG; `MOOB_DEBIAN_KERNEL=6.12.113 make vm-test` selects **6.12.113+deb13-amd64** | `build/vm/` |
| `make install-test` | Follows [operations.md](operations.md) in a disposable Debian 13 VM (official generic cloud image of 2026-10-21, QEMU TCG): `make install` refused for an unlisted kernel release (a `KVER` override), installed on the image's own 6.12.113 kernel, mounted by hand, by `fstab` at boot and by a mount unit, two users checked, including `moobfs query` (`CLI_QUERY`), uninstalled. Ends in `MOOBFS_INSTALL_PASS`; any step prints `MOOBFS_INSTALL_FAIL: <why>`. Proves the guide's commands on that image only; not real hardware, Secure Boot or another distribution (journal 0197) | `build/install-test/` |
| `make install-unit` | `scripts/test-install.sh`, as an ordinary user in a scratch `DESTDIR` (Linux, GNU coreutils; CI job "Tools and sanitizers"): `install.sh` refuses a regular file, a dangling or live symbolic link, a FIFO and a directory at a destination without `FORCE=1`, and a directory even with it; ignores a link planted at the old fixed temporary name; does not replace an entry created after its existence check; with `FORCE=1` replaces a link itself, not its target; leaves no temporary file; `uninstall` removes only regular files. Ends in `INSTALL_UNIT_PASS`. |
| `make pylib-test` | The Python library (`python/`, roadmap C5) installed with `pip` into a fresh venv, setuptools pinned by hash, then `python/tests` run against the installed package (CI job "Tools and sanitizers"): encoding and validation against `common/attr.c`'s rules, ctypes layout against `include/moobfs_ioctl.h`, matching and ordering, publishing and the scan's homogeneity rule on a plain Linux directory (root-only cases skipped as an ordinary user). The kernel backend is the `pylib` step of the suite (journal 0204). `test_langgraph.py` (the LangGraph store against `InMemoryStore`, and files shared with the G1 adapter) runs only where `langgraph` is installed, not in CI; on the dedicated host it ran with G1's packages, and G1's mounted gate ran on the store with `--store package` (journal 0207). `test_mcp.py` (the MCP tools through an in-process client) runs only where `mcp` is installed; `python/tests/mcp_mounted.py` is the C7 acceptance on a mounted layer, as root, through stdio servers started as two users (journal 0208) | `build/host/pylib-venv/` |
| `make container-test` | Follows [container-integration.md](container-integration.md) in the VM of `make install-test` (`MOOB_TEST=container`), with Docker and Podman: read, write and `setxattr` through a bind of the agent's domain under its uid, an invalid value refused by the kernel, `QUERY2` answered inside the container, `GET_INFO` refused there (exit 69), a second agent under another uid isolated, a wrong uid refused (exit 77), the read-only bind of the whole root (`QUERY2` and `GET_INFO` work, writes refused), remapped user namespaces and idmapped binds (no translation), and rootless Podman with `--userns=keep-id` reported (journal 0199). Ends in `MOOBFS_CONTAINER_PASS`. `MOOB_HOST_QEMU=1` runs either test with QEMU on a Linux host, under KVM when available | `build/container-test/` |
| KASAN workflow | Upstream **6.12.112** with `vm/kasan.config` (KASAN, UBSAN, lockdep), the same `vm/suite.sh` inside virtme-ng. Runs when a pull request touches `layer/`, `vm/`, `include/`, `common/`, `tests/`, `sqlite/`, `tools/`, the `Makefile` or the workflow; not for `python/`, `bench/` or documents (journal 0209). The built kernel is cached under its version, config and compiler, so a hit skips the build | `build/vng/`, `build/linux/` |

Both Debian kernel image/header pairs are pinned (6.12.111-1 and 6.12.113-1). The existing `Native Linux VM` CI job retains the
6.12.111 default and its required-check name; `Native Linux VM (Debian 6.12.113)`
runs the same target on every pull request; it first passed in PR 148.
fstests `quick` with the exclusion list passed on 6.12.113 for ext4, btrfs
and XFS on the dedicated host (journal 0206), so 6.12.113 is supported and
listed in `scripts/supported-kernels` ([compatibility.md](compatibility.md) §1,
rule 3; journals 0203 and 0206).

The Debian base digest and kernel packages are pinned, but auxiliary apt packages
are not fully locked: this is not a claim of bit-for-bit toolchain
reproducibility. If the pinned Debian package disappears, update image and
kernel together through a reviewed change and rerun the suite; do not substitute
host headers silently.

`MOOB_ONLY=memcg make vm-test` runs the bounded accounting regression alone in
the Debian guest. It needs cgroup v2 with `CONFIG_MEMCG=y`; the Debian harness
requires that controller and fails if it is missing. The upstream 6.12.112
KASAN kernel used here has `CONFIG_MEMCG` disabled, so its full suite prints
`MEMCG_SKIPPED` and tests memory safety, not cgroup charging.

`MOOB_ONLY=buildcost make vm-test` runs the bounded issue 118 probe on ext4:
1,000 attributed files in one domain, one first build after requesting a guest
cache drop, seven directory
renames and a built-domain query after each build. The query process drops to
uid/gid 1001. The Debian QEMU TCG guest checks build counters and prints
per-ioctl wall time, process CPU time, `/proc/self/io` read-byte deltas and
`/sys/block/vda/stat` sectors-read deltas. The device counter includes all
guest reads during the ioctl, so it is an approximate rather than a
per-process attribution.
Those numbers are exploratory; only native KVM on a dedicated host is used
for performance conclusions. In the first such run (journal 0182), the
`read_bytes` delta was zero even for the cache-dropped first build, so this
process counter is not treated as a measure of the lower-filesystem I/O. The
first sample is not claimed to have incurred physical reads.
`MOOB_ONLY=buildcostlarge` opts into the same probe with 7,500 files; it is
excluded from the full suite to keep its default run bounded.
`MOOB_ONLY=buildcostevict` is a separate opt-in probe: private domains for
uids 1001 and 1002 each fit a budget sized from one measured domain, but
not together. uid 1002's query must return `ENOMEM` without releasing uid
1001's domain, and uid 1001's following query must not rebuild
([ADR-0021](decisions/0021-budget-release-keeps-the-builders-domain.md)).
On 2026-10-25 that step passed under native KVM on the dedicated host and
printed `BUILD_COST_CROSS_USER_HELD` with budget 1762216. The same step
against the pre-filter module stopped at `bce-second-user`. It is not a
complete suite. The same morning, `release`, `overbudget` and
`buildcostevict` passed under KASAN, UBSAN and lockdep
(`MOOBFS_KASAN_ONLY_PASS`). The complete KASAN, UBSAN and lockdep suite of
this change then exited 0 (`MOOBFS_KASAN_PASS`). The complete native-KVM
suite of this change was not run. Journal
0182 recorded the older result, in which the second query released the
first domain. Journal 0192 records these rounds.
`MOOB_ONLY=buildlimit` is an opt-in and slower issue 118 regression. It
reformats the disposable ext4 test device with enough inodes, builds a
17,000-file domain that must fail with `E2BIG` without publishing output,
then exercises 16 admissions per unprivileged UID, 64 across the mount,
another UID, a fresh-domain query, a build by root that the mount-wide limit
does not refuse (`CAP_SYS_ADMIN` is outside the admission, ADR-0026) and
recovery after a real 60-second window. Its success marker is `BUILD_LIMIT_PASS`; it is not part of the
default full suite. It repeats the 17,000-file over-limit check on btrfs and
XFS. A negative control on `neg/issue118-buildlimits-0182`
uses the same guest step against the prior module and must fail.
`MOOB_ONLY=buildwork` is a separate opt-in regression for the merged
`index_build_work` mount policy (ADR-0020). It plants one 17,000-file
domain with six typed attributes per file on disposable ext4, rejects
invalid option values, checks denial under the 32,768 default and the
1,024 minimum, and checks a full answer at the 65,536 maximum. It also
checks mount-option display and same-value/new-value reconfiguration.
Its success marker is `BUILD_WORK_OPTION_PASS`; it is not a full-suite
marker or a slow-device measurement. Run this opt-in step with an ext4
test image of at least 256 MiB and 40,000 inodes; the default 64 MiB
guest image is too small for this fixture.
`MOOB_ONLY=buildtime` is a second opt-in regression. It uses the existing
test-only `scan_delay_us` module parameter to make one domain build exceed
the cooperative ten-second deadline, checks `ETIMEDOUT` and untouched
outputs, clears the delay and verifies that the next query builds a complete
domain. Its success marker is `BUILD_TIME_PASS`; it is not in the default
suite because it deliberately waits about ten seconds.
`MOOB_ONLY=buildmoved` is a third opt-in regression. Function tracing waits
until a first domain build enters the file scan, then renames the scanned
subdirectory inside the same domain. The original and moved scans each fit
alone, but together must return `E2BIG` with no published answer; after the
test-only delay is cleared, a later query must rebuild successfully. Its
success marker is `QUERY_BUILD_MOVED_PASS`. This tests cumulative accounting over
the moved-directory side scan, not every possible retry path.

`sh vm/issue118-policy-host.sh RUN_ID FILES READ_DELAY_MS [plain|six] [current|prelimit] [SEED_RUN]`
is a separate,
opt-in native-KVM probe for issue 118's numerical policy. It requires a
dedicated Linux host with matching kernel headers, KVM and virtme-ng; it
refuses an existing output directory and creates a new ext4 image under
`build/issue118-policy/RUN_ID/`. The guest plants `FILES` empty files with
exclusive creation and measures one unprivileged cold `QUERY2` on the
direct disk, then on the same disk through `dm-delay` with the specified
read delay in milliseconds (writes are not delayed). `six` plants the six
typed attributes of the earlier client-shape control on each file; the
default `plain` plants empty files. `prelimit` compiles the production
module from the pinned pre-guard commit `f283f13` inside a fresh archive
under the run directory; the current module and query helper are unchanged.
The default is `current`. The runner validates an optional `SEED_RUN` by
count and shape, copies its unmounted disk image
into a new output directory and reuses that fixture without planting new
files. This permits matched module comparisons without another worktree.
The query helper validates the complete answer or an `E2BIG`/`ETIMEDOUT`
failure with untouched output.
Its `MOOBFS_POLICY_PASS` marker covers those assertions and guest setup,
not a performance threshold, a real slow device or a strict CPU/I/O cap.
Journal 0185 records each actual run; this command is not in the default
suite.

`sh vm/issue118-policy-pairs.sh PREFIX PAIRS FILES READ_DELAY_MS SHAPE SEED_RUN`
runs that probe sequentially with alternating current/pre-limit module
order in each pair, using a fresh copy of the same seed image per VM.
It refuses previously used run IDs and leaves every console and image for
review. The caller must analyze the paired results; the script does not
declare a performance win from its success marker alone. After archiving
each pair's `console.log` and `environment.txt` under its run ID, validate
and summarize the archive with
`python3 bench/issue118-policy-pairs.py ARCHIVE PREFIX PAIRS FILES READ_DELAY_MS SHAPE SEED_RUN`.
It checks input-image hashes, settings, outcomes and alternating order,
then reports paired medians and a descriptive bootstrap interval as JSON.
Journal 0185 records the completed ten-pair run and its limitations.

`sh vm/issue118-client-mcp-host.sh RUN_ID PROJECTS PAGES_PER_PROJECT`
is an opt-in native-KVM real-client check on the dedicated Linux host. It
creates a uniquely named ext4 image, compiles the current layer and query
helper, and runs ai-memory's `memory_write_page` MCP workload through
`bench/client/stage2.sh` inside the guest. The guest requires every
requested write to succeed, mirrors typed attributes from the client's
SQLite database, and requires zero SQL-versus-`QUERY2` differences across
the six established shapes. It prints `AIMEM_MCP_PASS` only after these
checks. The runner does not report a performance threshold. Journal 0186
records the completed 2,000-write run and its limitations.

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

| Issue 118 probe marker | Proves | Does not prove |
| --- | --- | --- |
| `QUERY_BUILD_COST` | The helper issued one unprivileged `QUERY2` and recorded its wall, CPU, process read-byte and ext4-device sectors-read deltas | A stable performance distribution, precise per-query disk attribution or a resource limit |
| `BUILD_COST_OBSERVED` | Build counters and tracepoints saw one first build and seven post-rename rebuilds, with no extra build on the paired warm queries | Severity on other scales, lower filesystems, kernels or concurrent workloads |
| `BUILD_COST_DEVICE_CONTROL` | A raw read of the disposable ext4 device advanced its sectors-read counter | Exact attribution of all filesystem I/O to the query process |
| `BUILD_COST_CROSS_USER_EVICTION` | Two different unprivileged uids built private domains in turn; the shared budget released the older index and forced its owner to rebuild on the next query | Fairness under arbitrary concurrency or a safe production budget |

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
| `LAYER_PASSTHROUGH_TESTS_PASS` | `tests/layer.c`, once per lower filesystem and once more with `nolinks` | Create, write, pread, append, truncate, shared and private mmap, mkdir/rmdir, readdir, chmod, unlink, rename, xattr, hard links and symlinks behave as on the lower tree, with the same inode numbers and bytes visible below; `FICLONE`/`FIDEDUPERANGE` share extents on btrfs and XFS and fail cleanly on ext4; a set-user-ID bit given on the lower behind the layer goes with the next write, `copy_file_range`, clone or `fallocate` through it as uid 1001, also after an earlier write cached `S_NOSEC` (GHSA-6xf4-r246-92r6) | Full POSIX conformance (fstests ran as its `quick` group only, `vm/fstests/`); other lower filesystems |
| `TYPED_LIMITS_PASS` | `tests/layer.c typed`, once per lower filesystem | Every refusal of a typed attribute write through the layer, with its errno, and the lower file unchanged after each (names and values): `user.moob.format` gives `EPERM`; empty, uppercase, non-ASCII, 65-byte, edge-dot, doubled-dot and out-of-alphabet names give `EINVAL` (also on removal); an empty value, unknown types, wrong lengths of int64, time, double and bool, NaN doubles, bools outside 0/1, strings with NUL, overlong, surrogate, above U+10FFFF or cut sequences, a 1 025-byte string and a 4 097-byte raw give `EINVAL`, also as a replacement; `XATTR_CREATE` on an existing and `XATTR_REPLACE` or removal on an absent typed name give `EEXIST` and `ENODATA`. The 65th distinct typed attribute gives `ENOSPC` while an opaque attribute of the same size still fits; replacing at the limit succeeds, `XATTR_CREATE` of an existing one gives `EEXIST`, a removal frees one slot; `user.moob.format` and opaque names (`user.moob`, `user.moobx.*`, `user.other.*`) do not count. The bounds themselves (1 024-byte string, 64-byte name, +infinity, four-byte UTF-8) are accepted | Which errno `XATTR_REPLACE` on an absent name gives at the limit (the code gives `ENOSPC`, not tested); ENOMEM paths; a lower whose own xattr space holds fewer than 64 entries |
| `CLI_PASS` | `vm/suite.sh` with `tools/moobfs.c` and `tests/asuid.c`, once per lower filesystem (journal 0198) | `moobfs query` as uid 1001 and 1002 equals `moobfs-index --domain` on four domain answers, with one result per call (`--page 1`), ordered descending, limited to two, and in JSON; on a mount without domains the mount-wide `QUERY` equals the oracle; exit 77 and "permission denied" for another user's domain, 69 for the mount root, a non-domain directory, `noindex` and a path off the layer, 65 for invalid terms; `info` hides the counters from a user and shows them to root; `check` runs `moobfs-check` | Large answers across many pages under concurrent writes (the kernel's own `race2` covers the cursor), `ENOMEM`, `EDQUOT` and `ETIMEDOUT` messages (not provoked) |
| `CLI_E2BIG_PASS` | `vm/suite.sh`, ext4 only | A domain of 1100 names under `index_build_work=1024`: `moobfs query` as uid 1001 exits 75 and names `index_build_work` | The other temporary refusals |
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
| `CASEFOLD_REFUSED_PASS` | `vm/suite.sh`, `tests/layer.c cfsetup/cfcheck` | On an ext4 made with `-O casefold`, a casefolded lower directory and every name in it are refused through the layer with `EREMOTE` (in either case, and for `mkdir` and a creation; `EOPNOTSUPP` until journal 0221), a directory casefolded on the lower after the layer entered it refuses every name in it with `ESTALE`, ordinary directories next to it work, an index build skips it, and a casefolded lower root refuses the mount (issue #95); `CASEFOLD_SKIPPED` when the kernel or `mke2fs` cannot prepare casefolding | f2fs and tmpfs lowers |
| `LAYER_SUBVOLUME_PASS`, `NESTED_SUBVOLUME_REFUSED` | `vm/suite.sh` (`btrfs` step), `tests/layer.c svcheck` | A btrfs subvolume below the lower root is listed by `readdir`, and `stat`, `mkdir` and a creation of its name fail with `EXDEV` (journal 0221); it is not entered and not indexed (journal 0083) | Nested subvolumes deeper in the tree; snapshots |
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
| `DOMAINRECS_PASS` | `MOOB_ONLY=domainrecs`: 200 domain directories created, queried and removed on btrfs and on ext4 leave no domain record (`domain_records` test counter) and each new one answers with its own files; 50 chmod-then-rmdir leave no fallback mark; `QUERY2` through a removed directory's descriptor returns `ENOENT` and records nothing (issue #113, journal 0210) |
| `DOMAINREUSE_PASS` | `MOOB_ONLY=domainreuse`: on ext4, XFS and btrfs, domains of uid 1001 removed on the lower; a new directory with a removed domain's inode number answers `QUERY2` of uid 1002 with nothing, whether `QUERY2` or the mount-wide `QUERY` comes first (the latter drops the removed file's entries), and a new file with a removed file's number in another built domain does not match the removed file's values (GHSA-756j-cgwq-gvwm, journal 0216) |
| `MINACL_PASS` | `MOOB_ONLY=minacl`: on ext4 only (debugfs plants the fixtures), two minimal access ACLs written on the unmounted filesystem, one equal to the mode `0644` and one refusing others; `QUERY2` of uid 1001 answers the first and leaves out the second and a named-user ACL, returns nothing uid 1001 cannot read, and equals `moobfs-index --domain`, `moobfs-check --domain-depth` and, when the guest has `python3` (KASAN), the Python scan (journal 0222) |
| `PYLIB_SKIPPED` | No Python library test: the guest has no `python3` (the `vm-test` initramfs); the KASAN guest boots the host's root and runs it, ending in `MOOBFS_PYLIB_PASS`; since journal 0209 the KASAN job runs only for kernel-side changes, so a change to `python/` alone is checked by `make pylib-test` and not by this step |
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

To install the module on a dedicated test host and mount it at boot, follow
[operations.md](operations.md). For a one-off mount: `make layer`, `insmod layer/moobfs_layer.ko`, then
`mount -t moobfs_layer -o lower=<dir>[,nolinks][,noindex][,index_budget=<n>][,index_idle=<0-86400>][,index_build_work=<1024-65536>][,domain_depth=<0-8>] none <mnt>`.
`index_idle` defaults to 60 seconds. `domain_depth` defaults to 1 (first-level directories are domains for `MOOB_IOC_QUERY2`); 0 turns
domains off and keeps the v1 `QUERY` open to every user (`docs/spec-query-v2.md`).
`index_build_work` defaults to 32,768 cumulative units per build. Raising it
can admit larger domains at a cost in synchronous work; the cooperative
ten-second deadline and rebuild-frequency limits still apply.
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
`Native Linux VM (Debian 6.12.113)` runs on the same pushes and pull requests;
`KASAN and lockdep` runs on pull requests touching `layer/` or `vm/`, weekly and
on demand. Logs are temporary Actions artifacts; permanent conclusions belong
in the journal with links to the run. The Pages workflow publishes only `site/`.
Preview locally with `python3 -m http.server 8080 --directory site`.
