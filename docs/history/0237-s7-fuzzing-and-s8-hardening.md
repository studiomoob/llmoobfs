# 0237 — Longer fuzzing (S7) and runtime hardening in the KASAN kernel (S8)

Date: 2026-10-29. Status: implemented on branch `claudiofp/b8-s7s8-hardening`, run locally on
the test host; the new workflow has not run on GitHub (it runs once the batch's pull request is
merged). Work started: 2026-10-29 12:45 UTC; ended about 15:20 UTC. Wall-clock time, mostly
fuzzing, the kernel build and the KASAN suite on the shared test host; not an engineering-effort
measurement. Estimated effort: M; actual: S to M (one working day of calendar time, about
three hours of attended work).

## Goal

Advance items S7 (continuous and larger fuzzing) and S8 (runtime hardening) of
`docs/security-roadmap.md`. Before this change the only fuzz target was `tests/fuzz_attr.c`,
30 s per CI run, and the KASAN kernel (`vm/kasan.config`) had none of the hardening options S8
lists except `DEBUG_LIST`.

## Decisions

- **Two cheap targets over parsers that read untrusted input.** `tests/fuzz_image.c` fuzzes
  `moob_parse()` of `common/format.c`, which the M1 module includes (`kernel/format.c`) and
  `fsck.moobfs` runs on an image file; fuzzing that parser was the unmet exit criterion of the
  frozen M1 hardening (ROADMAP). Its checksums (superblock and table CRC32C, payload SHA-256)
  would stop almost every mutation at the first check, so with bit 0 of the first byte set the
  target first writes the checksums the image's own fields ask for; with the bit clear the
  bytes are parsed as given. The image is zero-padded to whole blocks and the reader fails a
  read outside it. An accepted image is checked against the invariants the module relies on
  (`object_index` below `object_count`, name length). `tests/fuzz_tools.c` fuzzes
  `parse_terms()` of `tools/query-common.h` (words split at NUL, each in its own allocation so
  ASan sees a read past its end) and `moob_ext_acl()` of `tools/ext-acl.h`, with `getxattr`
  replaced by a macro that returns the fuzz input or a chosen errno. `common/` and `tools/` are
  unchanged. Seeds come from `tests/fuzz-corpus/gen-tool-seeds.py`.
- **`make fuzz` runs every target** (`FUZZ_TARGETS`, default `attr image tools`), `FUZZ_TIME`
  seconds each, and writes new inputs to `build/host/fuzz-corpus/<target>`. Until now the seed
  directory `tests/fuzz-corpus/attr` was the first corpus argument, the one libFuzzer writes
  new inputs to, so a local run added files to the source tree. Crashes go to
  `build/host/fuzz-crashes/`. CI keeps `FUZZ_TIME=30`, now per target (about one minute more).
- **A separate workflow, `fuzz.yml`, not a longer CI step.** One job per target (matrix), 45
  minutes by default, 1 to 300 on demand; scheduled Wednesday and Saturday, because the Actions
  cache evicts an entry not read for seven days and a weekly run would sit at that edge. The
  corpus is restored from the newest earlier run (`actions/cache/restore` with a per-run key and
  a prefix `restore-keys`), merged down with `-merge=1` and saved even when the fuzz step failed
  (`actions/cache/save`, `if: always()`); corpus, crash files and log are uploaded as an
  artifact for 30 days; the job summary gets the final statistics. `permissions: contents:
  read`, actions pinned by the same commit hashes as the other workflows, the input validated
  as a number before use, `shell: bash` so that `tee` does not hide a crash's exit status.
  Rejected: raising `FUZZ_TIME` in `ci.yml` (every pull request would wait); downloading the
  previous run's artifact (needs `actions: read` and a token); OSS-Fuzz or ClusterFuzzLite
  (outside services or third-party actions; not authorized, drafted below).
- **Janus, Hydra and LFUZZ not adopted.** They fuzz a filesystem image together with system
  calls through the Linux Kernel Library. The layer has no image of its own; its untrusted
  "image" is the lower tree's `user.moob.*` attributes, already fuzzed by the hostile trees of
  S4 (in the VM and through the tools) and reached by syzkaller (S3) through `setxattr` and the
  ioctls. Porting the out-of-tree module to LKL, whose Janus fork is stalled since 2019
  (`docs/ecosystem.md`), would cost weeks for little new reach. The M1 image is covered by
  `fuzz_image` in user space, which runs the same parser code.
- **S8 options added to the KASAN kernel**, those of Debian's own configuration that do not
  change what the detectors see: `FORTIFY_SOURCE`, `HARDENED_USERCOPY`,
  `SLAB_FREELIST_HARDENED`, `SLAB_FREELIST_RANDOM`, `BUG_ON_DATA_CORRUPTION`, KFENCE (100 ms,
  the default), `SCHED_STACK_END_CHECK`, and `DEBUG_OBJECTS` with `_FREE` and `_RCU_HEAD` (the
  module frees domains with `kfree_rcu` and dentry data with `call_rcu`). Each one reports
  through `BUG:`, `Oops:` or `WARNING:` lines that `vm/kasan-guest.sh` already fails on.
  `INIT_ON_ALLOC_DEFAULT_ON`/`INIT_ON_FREE_DEFAULT_ON` stay off in this kernel: zeroing every
  allocation would hide uninitialized state from the detectors, the kind of defect the harness
  found before (journal 0016, uninitialized `rwsem`/xarray). Debian's kernel has
  `init_on_alloc=1`, and the `Native Linux VM (Debian 6.12.113)` job runs the whole suite on it.
  `UBSAN_SHIFT` and other new UBSAN checks were left out: the harness fails on any `UBSAN:` line
  of the whole kernel, so a check that fires in core code would fail every run.
- **KMSAN is left open** under S8: it cannot be built together with KASAN and needs its own
  kernel and job, like KCSAN.

## Kernel configurations compared

Debian from `/boot/config-6.12.113+deb13-amd64` on the test host (package
`linux-image-6.12.113+deb13-amd64` 6.12.113-1, the kernel of the Debian 6.12.113 CI job);
KASAN before and after from the `.config` that `vng --kconfig --config vm/kasan.config`
produced from upstream 6.12.112.

| Option | Debian 6.12.113 | KASAN before | KASAN after |
| --- | --- | --- | --- |
| `KASAN`, `UBSAN` (bounds), `PROVE_LOCKING`, `DEBUG_VM`, `DEBUG_ATOMIC_SLEEP` | n | y | y |
| `FORTIFY_SOURCE` | y | n | y |
| `HARDENED_USERCOPY` | y | n | y |
| `SLAB_FREELIST_HARDENED`, `SLAB_FREELIST_RANDOM` | y | n | y |
| `BUG_ON_DATA_CORRUPTION` | y | n | y |
| `DEBUG_LIST`, `LIST_HARDENED` | y | y | y |
| KFENCE | y, sample interval 0 (off unless `kfence.sample_interval` is set) | n | y, 100 ms |
| `SCHED_STACK_END_CHECK` | y | n | y |
| `DEBUG_OBJECTS` (`_FREE`, `_RCU_HEAD`) | n | n | y |
| `INIT_ON_ALLOC_DEFAULT_ON` | y | n | n (on purpose) |
| `INIT_ON_FREE_DEFAULT_ON`, `PAGE_POISONING` | n, y | n, n | n, n |
| `STACKPROTECTOR_STRONG`, `VMAP_STACK`, `INIT_STACK_ALL_ZERO`, `STRICT_MODULE_RWX` | y | y | y |
| `SHUFFLE_PAGE_ALLOCATOR`, `RANDOMIZE_KSTACK_OFFSET_DEFAULT` | y | n | n |
| KCSAN | n | separate kernel (`vm/kcsan.config`, `kcsan.yml`) | same |
| KMSAN | n | n | n |

`refcount_t` has no option: since Linux 5.x every `refcount_t` saturates and warns
(`refcount_warn_saturate()`, `lib/refcount.c:13`), on every kernel the suite runs on.

## Read-only review of the module (no defect)

- `copy_to_user`/`copy_from_user` (`layer/index.c`): `results_to_user()` copies `n` records,
  where `n` is `q->count`, which `query_run()` never raises above `q->limit`, and the buffer
  from `moobl_qbuf_get()` holds at least `limit` records (`qcap >= n` or a new `kvmalloc_array`);
  `do_query2_batch()` copies `b.nqueries * sizeof(*qs)`, with `nqueries` checked against
  `MOOB_QUERY2_BATCH_MAX` and the buffer allocated with the same count; `QUERY`, `QUERY2` and
  `GET_INFO` copy fixed-size structures, `GET_INFO` from a stack structure cleared with
  `memset` first. Nothing `HARDENED_USERCOPY` would refuse was found.
- Reference counts: `struct moobl_domain` uses `refcount_t` (`refcount_inc_not_zero` on lookup,
  `refcount_dec_and_test` on release); dentries use the kernel's `lockref`. No `atomic_t` used
  as a reference count.
- `WARN_ON`: only `WARN_ON_ONCE` (four, none on a path a caller can repeat at will without a
  defect). `pr_*`: the messages a caller can repeat are `_once` (journal 0221); the one plain
  `pr_info` (no user xattr support on the lower) is printed at mount, and the layer has no
  `FS_USERNS_MOUNT`, so only `CAP_SYS_ADMIN` in the initial namespace can repeat it.

## Evidence

Test host: Debian 13, KVM, 8 threads; at most two fuzzer processes at a time, one VM.

Commands (from a copy of the tree in a scratch directory on the host):

```sh
make fuzz FUZZ_TARGETS=attr FUZZ_TIME=1800
make fuzz FUZZ_TARGETS=tools FUZZ_TIME=1800
make fuzz FUZZ_TARGETS=image FUZZ_TIME=3600
# source coverage of the final corpora: clang -fprofile-instr-generate -fcoverage-mapping,
# each target run with -runs=0 over its corpus, llvm-cov-19 report
# MemorySanitizer: clang -fsanitize=fuzzer,memory -fsanitize-memory-track-origins, 300 s each
KCACHE=<scratch>/kcache PATH=$HOME/.venv-vng/bin:$PATH sh vm/kasan-local.sh
```

libFuzzer (clang 19.1, ASan and UBSan), final statistics, no crash and no sanitizer report:

| Target | Time | Executions | exec/s | Edges (`cov`) start → end | Corpus after | Source coverage of the parser |
| --- | --- | --- | --- | --- | --- | --- |
| `attr` | 1 800 s | 258 155 908 | 143 340 | 113 → 113 | 387 files | `common/attr.c` 81.5 % of lines; every function the target calls at 100 % of lines (`moob_attr_strerror` and `moob_attr_type_name`, message tables, are not called) |
| `image` | 3 600 s | 1 620 732 | 450 | 464 → 481 | 449 files | `common/format.c` 100 % of lines, 85.7 % of branches (every rejection reason and the accepted path) |
| `tools` | 1 800 s | 65 787 777 | 36 528 | 162 → 163 | 295 files | `tools/query-common.h` 100 % of lines, 96.9 % of branches; `tools/ext-acl.h` 100 % of lines, 94.7 % of branches |

The `attr` target saturates within seconds (113 edges after the 10-second smoke run as after 30
minutes); longer runs of it mostly re-confirm. The `image` target is slow per input (inputs up to
six 4 KiB blocks, SHA-256 of each payload) and was still finding edges at the end, which is what
the long workflow is for.

MemorySanitizer (clang 19.1, `-fsanitize=fuzzer,memory -fsanitize-memory-track-origins`,
started from the corpora above, 300 s each, default length limit), no report: `attr` 58 816 288
executions, `image` 230 008, `tools` 22 173 501. The targets need only libc, so MSan works
without instrumented libraries; uninitialized reads in the shared parsers would matter to the
kernel copy of `common/format.c` and `common/attr.c`.

KASAN suite with the new options. The kernel (upstream 6.12.112 with the new `vm/kasan.config`)
built without error in a scratch cache, not the shared one (12:55 to 13:40 UTC); `vng
--kconfig` kept every new option (`CONFIG_FORTIFY_SOURCE=y`, `CONFIG_HARDENED_USERCOPY=y`,
`CONFIG_SLAB_FREELIST_HARDENED=y`, `CONFIG_SLAB_FREELIST_RANDOM=y`,
`CONFIG_BUG_ON_DATA_CORRUPTION=y`, `CONFIG_KFENCE=y`, `CONFIG_KFENCE_SAMPLE_INTERVAL=100`,
`CONFIG_SCHED_STACK_END_CHECK=y`, `CONFIG_DEBUG_OBJECTS=y`, `CONFIG_DEBUG_OBJECTS_FREE=y`,
`CONFIG_DEBUG_OBJECTS_RCU_HEAD=y`; `# CONFIG_INIT_ON_ALLOC_DEFAULT_ON is not set`). The module
built against it with fortified string functions without a warning. The full suite
(`kasan-local.sh` without `MOOB_ONLY`, 13:41 to 15:11 UTC, one attempt) ended in
`MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_PYLIB_PASS`, `FAULT_INJECTION_PASS:
operations=22 calls=324 faults=302 unexpected=0` and `MOOBFS_KASAN_PASS: layer suite under
KASAN, UBSAN and lockdep with no kernel diagnostics`; 176 `_PASS` lines. The boot log shows
`kfence: initialized - using 2097152 bytes for 255 objects`; `dmesg` has no `BUG:`, `Oops:`,
`WARNING:`, `ODEBUG`, `usercopy:` or fortify report. As in the batch runs of journals 0230 and
0231, `memcg` and `landlock` were skipped by this minimal kernel (`MEMCG_SKIPPED`,
`LANDLOCK_SKIPPED`). No negative round: this change adds detectors and fixes nothing, so there
is no regression test to fail without it.

## Draft: what an OSS-Fuzz integration would need (not applied)

Not done: applying is an outward action the maintainer has not authorized, and OSS-Fuzz admits
projects with a significant user base or critical role, which an experimental module is not
yet. What it would take: a `projects/moobfs/` directory in the OSS-Fuzz repository with
`project.yaml` (`language: c`, `main_repo`, a maintainer contact, `sanitizers: [address,
undefined, memory]`, `fuzzing_engines: [libfuzzer, afl, honggfuzz]`), a `Dockerfile` from
`gcr.io/oss-fuzz-base/base-builder` that clones the repository, and a `build.sh` that compiles
each `tests/fuzz_<target>.c` with `$CC $CFLAGS ... $LIB_FUZZING_ENGINE -o $OUT/fuzz_<target>`
(no other dependency; the targets need only libc) and zips `tests/fuzz-corpus/<target>` into
`$OUT/fuzz_<target>_seed_corpus.zip`. ClusterFuzzLite would run the same `build.sh` inside our
own Actions, at the price of a third-party action; `fuzz.yml` does the long run without one.

## Changes

- `tests/fuzz_image.c`, `tests/fuzz_tools.c`, `tests/fuzz-corpus/gen-tool-seeds.py`,
  `tests/fuzz-corpus/image/` (5 seeds), `tests/fuzz-corpus/tools/` (15 seeds).
- `Makefile`: `fuzz` runs `FUZZ_TARGETS`, new `fuzz-build`; corpus order fixed.
- `.github/workflows/fuzz.yml` (new); step name in `.github/workflows/ci.yml`.
- `vm/kasan.config`: the S8 options above.
- Documentation: `docs/security-roadmap.md` (S7 status, S8 progress), `ROADMAP.md`,
  `docs/testing.md`, `docs/development.md`, `docs/architecture-overview.md`,
  `scripts/site-journey.py` and the pages it writes, `docs/references.md` §35,
  `docs/retired-claims.txt`, `CHANGELOG.md`, this journal and its index row.

## Limitations

- `fuzz.yml` has not run on GitHub; the cache and summary steps are untested there. The first
  scheduled or dispatched run after the merge is its test, and its report goes to
  `docs/history/` (the S7 done-when).
- The fuzz targets run user-space copies of shared code; `fuzz_image` reaches the M1 kernel
  parser's logic, not the kernel's reader or mount path. The layer's kernel code is fuzzed only
  by syzkaller (S3).
- The checksum fix-up of `fuzz_image` means a fuzzed image that passes is not a byte-exact
  image a writer produced; the raw mode (bit 0 clear) keeps the checksum checks themselves
  under test.
- The hardening options were run under the KASAN kernel on x86-64; the arm64 KASAN job uses the
  same fragment and has not been run with them.
- KMSAN, `init_on_free` and `DEBUG_PAGEALLOC` were not run.

## Next steps

- After the merge, dispatch `fuzz.yml` once (or wait for the Wednesday run), and record its
  summary in a journal.
- A KMSAN kernel and job, like `kcsan.yml`, to close S8.
- An MSan configuration in `fuzz.yml` if the local MemorySanitizer runs stay clean.

## Batch note: one pull request for batches 7 to 9

*Note, 2026-10-29, by the coordinator of the batches.*

**Exception to the one-pull-request-per-batch rule.** Batch 7 (journals 0232–0234, issue #168)
and batch 8 (journals 0235–0237, S9, S10, S7 and S8) were ready within the same hour, and later
the weekly benchmark workflow of journal 0238 was ready too. The maintainer authorized merging
them in one pull request, an exception to AGENTS.md's one pull request per batch. Separate pull
requests would have repeated the full suites on the same tree for no added assurance. None of
the six fronts changes the module's C code. Each front keeps its own commits and journal.

**Full runs, once, on the combined tree** (`claudiofp/BATCH7-8` at the S7/S8 merge; logs outside
the repository):
- `make vm-test` under QEMU TCG: `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS` and
  `MOOBFS_ACCEPTANCE_PASS`. These steps passed among others:
  - `OVERBUDGET_RACE_PASS` on the three lowers;
  - `LIMITS_BUDGET_PASS` and `LIMITS_MEMCG_PASS`;
  - `LANDLOCK_PASS`;
  - `PERMMATRIX_STEP_PASS` and `ROOTPATH_PASS` on ext4, btrfs and XFS;
  - the set-user-ID checks with `splice` and `sendfile`.
- `sh vm/kasan-local.sh` with the new `vm/kasan.config` (`CONFIG_KFENCE=y` and the other options
  of this journal in the built `.config`): `MOOBFS_KASAN_PASS` with no kernel diagnostics.
  - Also `PERMMATRIX_STEP_PASS` and `ROOTPATH_PASS` on the three lowers, `LIMITS_BUDGET_PASS` and
    `MOOBFS_PYLIB_PASS`.
  - `LANDLOCK_SKIPPED`, because that kernel has no Landlock.

**A first `make vm-test` attempt failed for load, not for the code.** It stopped at
`OVERBUDGET_RACE_FAIL: only 22 queries before the time cap` (btrfs), while the new KASAN kernel
was being compiled on every core of the same host. That step counts queries done within a time
cap, so it measures throughput. It passed on the three lowers in the rerun above, when the host
was otherwise idle.
