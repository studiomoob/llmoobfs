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
| `make test` | `attr_test` (host unit tests of the typed attribute encoding), `diag_test` (the readers of `moobfs diagnose`, `tools/diag.h`, over a fake `/proc/self/cgroup` and cgroup tree: the `0::` line, the walk to the root, the smallest limit, which level's own limit was reached between two readings, module parameters; journal 0243) and `tests/test_images.py`: nine tests covering exact-byte sharing, distinct data, hash boundary sizes (0 to 65536 bytes), an independent CRC vector, deterministic images, overwrite refusal, limits, 128 objects and 15 malformed variants | `build/host/` |
| `make test BUILD=build/asan CC=clang CFLAGS=... LDFLAGS=...` | Same, under ASan and UBSan (see development.md for the exact flags) | `build/asan/` |
| `make fuzz FUZZ_TIME=30` | libFuzzer, `FUZZ_TIME` seconds per target (`FUZZ_TARGETS`, default all three): `attr` over `common/attr.c` (`tests/fuzz_attr.c`), `image` over the image parser `common/format.c` that the M1 module and `fsck.moobfs` share (`tests/fuzz_image.c`, which writes the checksums an input's fields ask for so that mutations pass them), `tools` over the query terms of `tools/query-common.h` and the ACL value of `tools/ext-acl.h` (`tests/fuzz_tools.c`); seeded by `tests/fuzz-corpus/<target>` (journal 0237) | `build/host/fuzz_<target>`, new inputs in `build/host/fuzz-corpus/<target>/`, crashes in `build/host/fuzz-crashes/` |
| `make static` and `.github/workflows/static.yml` | Security roadmap S5 and S19 (journal 0242), needs only Docker: in the image of `tests/static/Dockerfile` (Debian's 6.12.113 headers for gcc, upstream 6.12.112 configured with `LLVM=1`, smatch built from a fixed commit), `W=1` with gcc and with clang, `sparse`, `smatch -p=kernel`, Coccinelle (`make coccicheck MODE=report`), the clang analyzer (`make clang-analyzer`) and `kernel-doc -Wall` over `layer/`, `kernel/` and `include/`. `scripts/static-triage.py` fails on a finding not in `tests/static/baseline.txt` (each entry with its reason), on an entry no longer reported, and when a checker misses the known defect of the canary module `tests/static/canary`. Ends in `STATIC_OK`. Not a required check | `build/static/` (logs, `findings.txt`, `versions.txt`) |
| `make mutants-check` | Security roadmap S14 (journal 0242), needs only Docker, in the same image: each mutant of `tests/mutation/homogeneity.mut` (one check of the homogeneity rule removed or weakened) applies at exactly one place, builds against the headers, and changes the object code of `layer/index.o` (an unchanged object means an equivalent mutant). `python3 scripts/mutants.py run [M01 ...]` runs each mutant's `MOOB_ONLY` steps with `make vm-test` from a copy of `HEAD` and reports it killed or survived; on 2026-10-30 it killed all 23 (journal 0242) | nothing kept (`check`); `build/mutation/` (`run`) |
| `make attr-tools` | Linux-only `moobfs-attr`, `moobfs-check`, `moobfs-index` | `build/host/` |
| `make hostile-test` / `make json-test` | Linux, no module (CI job "Tools and sanitizers", under ASan and UBSan): `tests/hostile-user.sh` runs `moobfs-check` and `moobfs-index` over the adversarial trees of `tests/hostile.c` (S4), `tests/tools-json.sh` checks both JSON documents and `moobfs diagnose`'s usage and no-layer exit (64, 69), and since journal 0232 `tests/tools-walk.sh` (`hostile-test` only) runs both tools while `hostile swap` exchanges a directory and a link to outside the root with `renameat2(RENAME_EXCHANGE)` (no answer may come from outside; the `nftw()` tools failed most rounds), checks that planted links and a FIFO are not followed, walks 150 levels with 64 descriptors and, as a user other than root, keeps an unreadable file reported as before. Each skips or fails cleanly where `TMPDIR` has no user extended attributes | a `mktemp -d` under `TMPDIR`, removed at exit |
| `make module` / `make layer` | `kernel/moobfs.ko` / `layer/moobfs_layer.ko` against `KDIR` | in-tree (`kernel/`, `layer/`) |
| `make vm-test` | Docker image, both modules, tests and the full guest run (M1 regression, layer suite, acceptance) on Debian **6.12.111+deb13-amd64** by default under QEMU TCG; `MOOB_DEBIAN_KERNEL=6.12.113 make vm-test` selects **6.12.113+deb13-amd64** | `build/vm/` |
| `make install-test` | Follows [operations.md](operations.md) in a disposable Debian 13 VM (official generic cloud image of 2026-10-21, QEMU TCG): `make install` refused for an unlisted kernel release (a `KVER` override), installed on the image's own 6.12.113 kernel, mounted by hand, by `fstab` at boot and by a mount unit, two users checked, including `moobfs query` (`CLI_QUERY`), the `user-.slice.d` drop-in of section 5 setting `MemoryMax=` on each user's slice and `moobfs diagnose` reading it there, with its `no-memory-limit` warning before the drop-in (`MEMORY_LIMIT`, journal 0243), uninstalled. Ends in `MOOBFS_INSTALL_PASS`; any step prints `MOOBFS_INSTALL_FAIL: <why>`. Proves the guide's commands on that image only; not real hardware, Secure Boot or another distribution (journal 0197) | `build/install-test/` |
| `make install-unit` | `scripts/test-install.sh`, as an ordinary user in a scratch `DESTDIR` (Linux, GNU coreutils; CI job "Tools and sanitizers"): `install.sh` refuses a regular file, a dangling or live symbolic link, a FIFO and a directory at a destination without `FORCE=1`, and a directory even with it; ignores a link planted at the old fixed temporary name; does not replace an entry created after its existence check; with `FORCE=1` replaces a link itself, not its target; leaves no temporary file; `uninstall` removes only regular files. Ends in `INSTALL_UNIT_PASS`. |
| `make pylib-test` | The Python library (`python/`, roadmap C5) installed with `pip` into a fresh venv, setuptools pinned by hash, then `python/tests` run against the installed package (CI job "Tools and sanitizers"): encoding and validation against `common/attr.c`'s rules, ctypes layout against `include/moobfs_ioctl.h`, matching and ordering, publishing and the scan's homogeneity rule on a plain Linux directory, and since journal 0231 a directory or a file swapped for a symbolic link during the walk, through a hook and through a `RENAME_EXCHANGE` race (root-only cases skipped as an ordinary user). The kernel backend is the `pylib` step of the suite (journal 0204). `test_langgraph.py` (the LangGraph store against `InMemoryStore`, files shared with the G1 adapter, and since journal 0227 batched operations with invalid labels) runs only where `langgraph` is installed, not in CI; on the dedicated host it ran with G1's packages, and G1's mounted gate ran on the store with `--store package` (journal 0207). `test_mcp.py` (the MCP tools through an in-process client; since journal 0227 also a directory of the path swapped for a symbolic link while the agent reads, writes and deletes, a FIFO, and saved ids and capabilities) runs only where `mcp` is installed; `python/tests/mcp_mounted.py` is the C7 acceptance on a mounted layer, as root, through stdio servers started as two users (journal 0208) | `build/host/pylib-venv/` |
| `make container-test` | Follows [container-integration.md](container-integration.md) in the VM of `make install-test` (`MOOB_TEST=container`), with Docker and Podman: read, write and `setxattr` through a bind of the agent's domain under its uid, an invalid value refused by the kernel, `QUERY2` answered inside the container, `GET_INFO` refused there (exit 69), a second agent under another uid isolated, a wrong uid refused (exit 77), the read-only bind of the whole root (`QUERY2` and `GET_INFO` work, writes refused), remapped user namespaces and idmapped binds (no translation), and rootless Podman with `--userns=keep-id` reported (journal 0199). Ends in `MOOBFS_CONTAINER_PASS`. `MOOB_HOST_QEMU=1` runs either test with QEMU on a Linux host, under KVM when available | `build/container-test/` |
| `make package-test` | Builds two versions of `moobfs-dkms` and `moobfs-tools` (`scripts/build-debs.sh`) and installs them in the VM of `make install-test`, booted with UEFI Secure Boot (OVMF with the Microsoft keys; `MOOB_TEST=package`): DKMS refuses an unlisted kernel with exit 77, the module is refused until the guest's DKMS key is written into the firmware's MokList with `virt-fw-vars` (in place of MokManager's screen) and loads after, an unsigned build is refused, two users stay isolated, upgrade and roll back keep `moobfs query` and `moobfs-check` equal to the oracle, purge leaves the lower readable, and the package refuses a `make install` copy. Marker `MOOBFS_PACKAGE_PASS`; passed on 2026-10-29 in 469 s under KVM ([journal 0229](history/0229-dkms-and-debian-packages.md)) | `build/package-test/` |
| `make repro-test` | `scripts/repro-ko.sh` (security roadmap S10): builds the unsigned `layer/moobfs_layer.ko` for each kernel of `scripts/supported-kernels` with `make layer`, and again with the `MAKE` line of the DKMS package in DKMS's build directory, in two fresh `debian:trixie-slim` containers pinned by digest that differ in build path, uid, host name, time zone, umask and time; the modules must be byte-identical, the two containers must have installed the same package versions, a copy signed with a throwaway key must give the unsigned module back once the signature is removed, and the `.deb` packages of `scripts/build-debs.sh` must be byte-identical too (`MOOB_REPRO_DEBS=0` skips them). Markers `MOOBFS_REPRO_PASS: <sha256> moobfs_layer.ko <kernel>` and `MOOBFS_REPRO_DEB_PASS`; first passed on 2026-10-29 ([journal 0236](history/0236-reproducible-module-build.md)). Needs Docker on Linux, no VM | `build/repro/` |
| `make bench-test` and `.github/workflows/bench.yml` | The on-demand index benchmark of issue #68 in the VM of `make install-test` under KVM (`MOOB_TEST=bench`, `vm/bench-test/guest.sh`; refuses to run under TCG): builds the module and `make layer bench-tools attr-tools` in the guest, makes a lower of `MOOB_BENCH_FS` (`ext4`, `btrfs` or `xfs`, default `ext4`) on a second virtual disk (a sparse raw file in the host's page cache, guest flushes ignored, so the "cold" rows are cold in the guest only; `MOOB_BENCH_CACHE=direct` opens it with O_DIRECT, under which the cold first `QUERY2` of `ns3` exceeded the ten-second build deadline on the test host and was refused with `ETIMEDOUT`; that deadline counted elapsed time, and ADR-0031, accepted, counts CPU time), mounts the layer, writes 60 000 memories of profile 8 with W1 and runs `bench/ondemand.sh` on `ns3`; `times.txt`, `environment.txt`, `system.txt` (CPU, vCPUs, memory, disk, filesystem, commit) and `bench/report.py` output are copied out. Marker `MOOBFS_BENCH_PASS`. The workflow runs it weekly, on demand and on pull requests touching the bench harness, once per lower filesystem on GitHub's `ubuntu-24.04` runner, and writes a table against the Mac mini reference to the job summary (`bench/ondemand-summary.py`); a mount-wide `QUERY` refused by the build limits of ADR-0019/ADR-0020 (`E2BIG`, `ETIMEDOUT`) is recorded by `bench/ondemand.sh`, not fatal; it fails on a harness failure or on counts different from the reference's, never on a timing. **A trend monitor on shared virtual hardware, not a reference measurement**: it does not replace a run on other physical hardware (issue #68 stays open). amd64 only: arm64 runners have no KVM (journal 0124) and the kernel list is amd64 (journal 0238) | `build/bench-test/`, Actions artifact `bench-ondemand-<fs>` |
| KASAN workflow | Upstream **6.12.112** with `vm/kasan.config` (KASAN, UBSAN, lockdep), the same `vm/suite.sh` inside virtme-ng. Runs when a pull request touches `layer/`, `vm/`, `include/`, `common/`, `tests/`, `sqlite/`, `tools/`, the `Makefile` or the workflow; not for `python/`, `bench/` or documents (journal 0209). The built kernel is cached under its version, config and compiler, so a hit skips the build | `build/vng/`, `build/linux/` |

Both Debian kernel image/header pairs are pinned (6.12.111-1 and 6.12.113-1). The existing `Native Linux VM` CI job retains the
6.12.111 default and its required-check name; `Native Linux VM (Debian 6.12.113)`
runs the same target on every pull request; it first passed in PR 148.
fstests `quick` with the exclusion list passed on 6.12.113 for ext4, btrfs
and XFS on the dedicated host (journal 0206), and the `auto` group ran once
there on the same kernel (journal 0239), so 6.12.113 is supported and
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
`MOOB_ONLY=buildtime` is a second opt-in regression (ADR-0031). With the
test-only `scan_delay_us` module parameter, a build that sleeps 12 s (as on a
slow lower) must complete: the ten-second deadline counts the builder's CPU
time. With the test-only `scan_spin_us`, a build that busy-waits 12 s must
fail with `ETIMEDOUT` and untouched outputs, and a build that sleeps 126 s
must fail the same way at the 120 s elapsed backstop; after each, the next
query builds a complete domain. Its success marker is `BUILD_TIME_PASS`; it
is not in the default suite because it deliberately waits about two and a
half minutes. Passed in a dedicated VM and under KASAN/UBSAN/lockdep for ADR-0031, and failed
with main's module at `bt-sleep-refused` (journal 0241).
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

| S9 limit marker (journal 0235) | Proves | Does not prove |
| --- | --- | --- |
| `LIMITS_BUDGET_PASS` | On a 1 MiB `index_budget`, a domain larger than the whole budget is refused with `ENOMEM` to its own uid; nothing is released, the index's byte count is unchanged and another uid's domain within its share is answered without a rebuild | Behavior with more than two uids or under the shrinker |
| `MEMCG_HOLD_CAPPED`, `MEMCG_BUILD_CAPPED`, `LIMITS_MEMCG_PASS` | In a 16 MiB cgroup, descriptors with retained `QUERY2` buffers fill it until a query fails with `ENOMEM`, request untouched, while another uid outside it gets the oracle's answer, and the buffers are freed on exit; in a 4 MiB cgroup a 6,000-file build fails with `ENOMEM` and leaves nothing built, and the same uid builds it outside the cgroup | A cap on hosts whose cgroups have no `memory.max`; the OOM-killer path (the test process is made unkillable so the cap shows as `ENOMEM`) |
| `DIAG_BUDGET_PASS` | Step `diag` (journal 0243): on a 1 MiB `index_budget`, `moobfs query` of a domain larger than the budget exits 75 with a message naming the index budget and 1 MiB and no memory cgroup; nothing is built; `moobfs diagnose` as a user exits 2 with `no-memory-limit` and `administrator-only`, the role `domain` and the budget; as root with `scan_delay_us` set it reports `test-parameter`, the index's bytes and the tracepoints | Messages under more than one cgroup level holding a limit; a cgroup reached by another process at the same moment |
| `DIAG_MEMCG_PASS`, `DIAG_MEMCG_SKIPPED` | In `/moob-dg/leaf` below a 4 MiB `memory.max` on `/moob-dg`, a 6,000-file build through `moobfs query` exits 75 with a message naming `/moob-dg` and its 4 MiB, nothing built; `moobfs diagnose` there names the same limit and `limit-reached`; the same uid outside the cgroup gets the oracle's 6,000 results. Skipped where the guest lacks the memory controller (a failure in the Debian harness) | The OOM-killer path (the test process is unkillable); a systemd slice (covered by `make install-test`'s `MEMORY_LIMIT`) |
| `LIMITS_MEMCG_SKIPPED` | The guest lacks cgroup v2 or the memory controller (the KASAN kernel); the memcg half did not run | Anything about the memcg half; the Debian harness treats this as a failure |
| `MOUNT_SCALE_RESULT`, `MOUNT_SCALE_FIRST_QUERY`, `MOUNT_SCALE_OBSERVED` | Optional step `mountscale`: wall time and Slab change of mount and unmount over 100 to 200,100 files with and without `noindex`, index bytes 0 after the mount, and the first query of a domain over `index_build_work` refused with `E2BIG` while a small domain is answered | A stable timing distribution (one guest that may share its host), other lower filesystems, a slow disk |

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
| `LAYER_PASSTHROUGH_TESTS_PASS` | `tests/layer.c`, once per lower filesystem and once more with `nolinks` | Create, write, pread, append, truncate, shared and private mmap, mkdir/rmdir, readdir, chmod, unlink, rename, xattr, hard links and symlinks behave as on the lower tree, with the same inode numbers and bytes visible below; `FICLONE`/`FIDEDUPERANGE` share extents on btrfs and XFS and fail cleanly on ext4; a set-user-ID bit given on the lower behind the layer goes with the next write, `copy_file_range`, clone, `fallocate`, `splice` from a pipe or `sendfile` through it as uid 1001, also after an earlier write cached `S_NOSEC` (GHSA-6xf4-r246-92r6; splice and sendfile since journal 0233) | Full POSIX conformance (fstests' `quick` group passes with an exclusion list and its `auto` group ran once, outside the suite, `vm/fstests/`, journal 0239); other lower filesystems |
| `PASSTHROUGH_PASS` | `tests/passthrough.c`, once per lower filesystem (issue #115, journal 0224) | `posix_fadvise(DONTNEED)` on a layer descriptor empties the lower page cache (`PT_FADVISE_PASS`) and `readahead(2)` fills it (`PT_READAHEAD_PASS`), counted with `mincore` on the lower file; `statx` reports the lower's `STATX_DIOALIGN`, `STATX_SUBVOL`, `STATX_WRITE_ATOMIC` and encrypted, verity and DAX attributes (`PT_STATX_PASS`); a file removed behind the layer frees its blocks once a lookup through the layer drops the name (`PT_DROP_INODE_PASS`); `O_NOATIME` set with `F_SETFL` after open keeps the lower's access time and clearing it lets a read move it (`PT_NOATIME_PASS`) | `cachestat(2)` (no hook: it reads the layer's empty mapping); fields the three lowers of the suite do not report (atomic writes on 6.12 ext4 and XFS, encryption, verity, DAX); the space of a removed file whose layer name is never looked up again (it waits for dentry reclaim) |
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
| `HOMOGENEITY_GAPS_PASS: <fs>` | `vm/suite.sh` | The two parts of the homogeneity rule round 2 found untested (journal 0155, T3 and T4): an extended ACL on the domain directory, set before the build, keeps a file the ACL's user cannot read out of that user's answers; an attributed file written, and a file renamed, under a 0700 directory inside a built domain stay out of another user's answers while `fallback_domains` is 0. Since journal 0242 (S14, mutants M16 and M19): an attributed directory below a 0700 directory, read by the build, and one renamed there after the build, stay out of 1001's answers in domain `ds14`, and a file deeper than 64 levels given an attribute is counted heterogeneous. Also in the same step: a cursor changed outside its query hash is refused by the MAC alone (T2), and `RENAME_WHITEOUT` is refused (V6). Each check has a negative round on branch `neg/homogeneity-gaps` (journal 0156) | Other ACL shapes (default ACLs, group entries); links under a private directory |
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

### QUERY2 against the lower's own permission decision (journals 0116, 0215)

`MOOB_ONLY=permmatrix make vm-test` runs `tests/permmatrix.c` on ext4, btrfs
and XFS (ext4 only until journal 0233); the full suite includes it too. Twelve
seeds plant random modes, owners and groups, and named-user access ACLs on some
files and directories; the oracle is the lower kernel
(`getxattr` and a listing of each directory on the way, with the identity's
credentials), not a copy of the rule.

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `PERMMATRIX_PASS identities=... mismatches=0` | For one seed, no identity gets a file the lower would refuse it, from the partition and again in fallback mode (per-result checks) after a chmod through the layer | Shapes outside the seeds; default ACLs; SELinux labels on the lower |
| `QUERY_BUILD_MOVED_PASS` | Inside this step: a search-only directory renamed during the domain's first build stays out of the partition | Other renames during a build |
| `PERMMATRIX_STEP_PASS: <fs>, ...` | On that lower: the twelve seeds, counts showing that they met cases the rules before journals 0116 and 0215 got wrong, and the three fixtures for a search-only directory made through the layer after the build, created in a built domain and renamed during the first build, and (journal 0242, mutants M22 and M23) a passing directory with an attribute and 600 files moved during the first build below a 0700 directory, kept out of 1001's answer | A complete suite by itself |

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
names the build recorded. The waiting QUERY2 starts once the build counter
shows the part outside the domain built, not after a fixed wait (journal 0233).
Up to four attempts per lower; an attempt that misses the interleaving is
inconclusive, never a pass, and the next one plants twice the files (10 000,
then 20 000, 40 000 and 80 000) where the lower has the inodes for them (the
64 MiB ext4 image has 16 384, so ext4 stays at 10 000).

| Marker | Proves | Does not prove |
| --- | --- | --- |
| `ROOTPATH_RACE_PASS` | After that interleaving (QUERY2 seen in `D` state while the holder builds, rename before the holder returned, a build by the waiter counted), the mount-wide QUERY returns every attributed file, under the new name, each resolving to its inode | The unwidened microsecond window, or renames of a directory above the domain |
| `ROOTPATH_PASS: <fs> (<n> files, attempt <k>)` | The check above passed on that lower, at the size and attempt named | A complete suite by itself |

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
| `MOOBFS_KASAN_PASS: ...` | The full `vm/suite.sh` passed on upstream 6.12.112 with KASAN (generic, inline, vmalloc), UBSAN bounds, `PROVE_LOCKING`, `DEBUG_ATOMIC_SLEEP`, `DEBUG_LIST`, `DEBUG_VM`, `PROVE_RCU`, and since journal 0237 `FORTIFY_SOURCE`, `HARDENED_USERCOPY`, `SLAB_FREELIST_HARDENED`/`_RANDOM`, `BUG_ON_DATA_CORRUPTION`, KFENCE, `SCHED_STACK_END_CHECK` and `DEBUG_OBJECTS` (`_FREE`, `_RCU_HEAD`), and `dmesg` contains none of `BUG: KASAN`, `KASAN:`, `BUG:`, `Oops:`, `WARNING:`, `possible circular/recursive locking`, `INFO: task/rcu`, `UBSAN:` | KMSAN (not enabled); `init_on_alloc`/`init_on_free` (off here on purpose, on in the Debian 6.12.113 job); data races (KCSAN runs separately, `kcsan.yml`, journal 0127); code paths the suite does not reach; other kernel versions |
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
| `MINACL_PASS` | `MOOB_ONLY=minacl`: on ext4 only (debugfs plants the fixtures), two minimal access ACLs written on the unmounted filesystem, one equal to the mode `0644` and one refusing others; `QUERY2` of uid 1001 answers the first and leaves out the second and a named-user ACL, returns nothing uid 1001 cannot read, and equals `moobfs-index --domain`, `moobfs-check --domain-depth` and, when the guest has `python3` (KASAN), the Python scan (journal 0222); then (journal 0242, mutant M11) a file whose ACL debugfs wrote raw with a wrong version, so that every read of it fails with `EINVAL`, renamed through the layer into the built domain, is left out of uid 1001's answer |
| `IXLOCK_PASS` | `MOOB_ONLY=ixlock`: on ext4, while one domain build and then another hold the index lock for writing (test parameter `scan_hold_us`, 2.5 s per file, 3 files each, measured from 1 s into the build), 8 layer inodes of a third domain, which have no index record, are instantiated during the first build and evicted with `drop_caches` during the second, each in under 1 s, and each build is still running when its measurement ends (issue #117, journal 0223) |
| `LANDLOCK_PASS` | Every run, after `ixlock`: on ext4, as uid 1001 under `tests/landlock_run.c` (directory listing beneath one tree, file reads beneath a subdirectory and the program's directory; `/proc/self/mountinfo` checked to be unreadable), `moobfs query` on a domain gives the same answer as without the sandbox and as `moobfs-index --domain`, in text and JSON; a directory that is not a domain and a path off the layer give exit 69 as without it; `moobfs info --json` on the mount root equals the unconfined output with `index_build_work` and `index_idle` `null`, and `info` from a domain directory finds the mount root; on a mount without domains the root still answers the mount-wide `QUERY` (issue #167, journal 0230) |
| `LANDLOCK_SKIPPED` | No CLI-under-Landlock test: the guest kernel has no Landlock |
| `PYLIB_SKIPPED` | No Python library test: the guest has no `python3` (the `vm-test` initramfs); the KASAN guest boots the host's root and runs it, ending in `MOOBFS_PYLIB_PASS`; since journal 0209 the KASAN job runs only for kernel-side changes, so a change to `python/` alone is checked by `make pylib-test` and not by this step |
| `LOWERRO_PASS: ... idmapped lower: not tested ...` | Read-only/freeze checks ran, but an idmapped bind lower could not be created; only `idmapped lower: refused` proves that case |
| `ipc` binary present | Built/staged only, not invoked by the suite; no `IPC_PRIMITIVES_PASS` evidence |

`tests/notify.c` is a standalone investigation helper, not built by
`vm/build-tests.sh` or invoked by this suite; `tests/landlock_run.c` is built and
run by the step `landlock` since journal 0230. `faults` is exclusive
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
can admit larger domains at a cost in synchronous work; the build deadline
(ten seconds of CPU time, 120 s elapsed, ADR-0031) and rebuild-frequency
limits still apply.
Do not alter the backing image or the lower tree from outside while mounted.
Signed-module policies of a particular VM may reject unsigned modules. Other
kernel versions are unverified.

## 9. What is not covered

- Linux security modules beyond the SELinux run above (AppArmor, Smack, and Landlock
  beyond the CLI run of the step `landlock`).
- Inside `make vm-test`: fstests over the layer (only the vendored `fsx`; fstests
  runs separately, `vm/fstests/`, with `quick` passing and one `auto` run, journal
  0239), syzkaller over the ioctls and `setxattr` (run by hand, seven six-hour rounds,
  `vm/syzkaller-local.sh`), and KMSAN: see [security-roadmap.md](security-roadmap.md)
  S2, S3 and S7 to S8.
- Kernels other than Debian 6.12.111 and upstream 6.12.112; architectures other
  than x86-64.
- Any performance, latency or memory measurement (M4, [benchmark-plan.md](benchmark-plan.md)).
- Acceptance over btrfs and XFS (it runs over ext4 only); `ipc` in the suite.

## 10. CI and documentation

GitHub Actions runs `Tools and sanitizers` (host tests, ASan/UBSan, 30 s of
fuzzing per libFuzzer target, site link check), `Lint` (shellcheck, SPDX headers, DCO sign-off on
pull requests) and `Native Linux VM` on pushes to `main` and on pull requests;
`Native Linux VM (Debian 6.12.113)` runs on the same pushes and pull requests;
`KASAN and lockdep` runs on pull requests touching `layer/` or `vm/`, weekly and
on demand. `Long fuzzing` (`fuzz.yml`) runs the three libFuzzer targets 45 minutes each, twice a
week and on demand, with the corpus kept in the Actions cache (journal 0237). Logs are temporary Actions artifacts; permanent conclusions belong
on demand; `Weekly benchmark` (`bench.yml`, journal 0238) runs the on-demand index benchmark
weekly, on demand and on pull requests touching its harness, as a trend monitor. Logs are temporary Actions artifacts; permanent conclusions belong
in the journal with links to the run. The Pages workflow publishes only `site/`.
Preview locally with `python3 -m http.server 8080 --directory site`.
