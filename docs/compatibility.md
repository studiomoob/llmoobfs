# Supported kernels and compatibility policy

Date: 2026-10-13. Status: **accepted** by the maintainer on 2026-10-13 (security roadmap S6,
journal [0080](history/0080-compatibility-policy.md)). It states what is true today and the rules
for what changes. It gathers what [spec-m2](spec-m2.md) §10, [spec-query-v2](spec-query-v2.md),
the README and [distribution-strategy](distribution-strategy.md) §6 already say. Where a rule
needs code that does not exist yet, the rule says so.

The one-page summary of the matrix and the known limitations is [matrix-and-limits.md](matrix-and-limits.md)
(journal 0282); this page keeps the rules.

There are three interfaces, each with its own version and its own rule.

## 1. Kernels

**Today.** Three series, x86-64: Debian 13's 6.12 builds, Debian's 7.3.3 (journal
[0277](history/0277-kernels-6-12-115-and-7-3-supported.md)) and Ubuntu 26.04's 7.0 series
`7.0.0-*-generic`, of which `7.0.0-43-generic` was tested (journal
[0281](history/0281-port-ubuntu-7-0.md), [ADR-0036](decisions/0036-ubuntu-abi-series-policy.md)).
On 7.0 and 7.3 only the layer module (`layer/`) builds and is tested; the M1 read-only module
(`kernel/`) stays 6.12-only.

| Kernel | Functional suite | KASAN, UBSAN, lockdep | fstests `quick` |
| --- | --- | --- | --- |
| Debian 6.12.111+deb13-amd64 | yes, every pull request | no (distribution kernel) | yes, three lower filesystems, by hand |
| Debian 6.12.113+deb13-amd64 | yes, every pull request (first passed in PR 148) | no (distribution kernel) | yes, three lower filesystems, on the dedicated host (journal 0206) |
| Debian 6.12.115+deb13.1-amd64 | yes, every pull request (job added in journal 0277) | no (distribution kernel) | yes, three lower filesystems, on the dedicated host (journal 0272; btrfs with `exclude-quick-btrfs.txt`, journal 0277) |
| Debian 7.3.3+deb14-amd64 (layer module only) | yes, every pull request (job added in journal 0277) | no (distribution kernel) | the upstream 7.3.3 build below stands for it (journal 0275) |
| Upstream 6.12.116, `vm/kasan.config` | yes, every pull request that touches `layer/` or `vm/` | yes | no |
| Upstream 7.3.3, `vm/kasan.config` | yes, every pull request that touches `layer/` or `vm/` (job added in journal 0277) | yes | yes, three lower filesystems, built with `vm/fstests.config` (journal 0275) |
| Ubuntu 26.04 `7.0.0-*-generic` series (layer module only; ADR-0036) | `7.0.0-43-generic`, every pull request (job added in journal 0281) | no (distribution kernel) | `7.0.0-43-generic`, three lower filesystems, Ubuntu's own build (journal 0281; `generic/798` excluded) |
| Upstream 7.0.14, `vm/kasan.config` | yes, every pull request that touches `layer/` or `vm/` (job added in journal 0281) | yes | no |
| Anything else | no | no | no |

**Rules.**

1. A kernel is **supported** when the full suite (`make vm-test`) and the KASAN job pass on it in
   CI for every change to `layer/`. Everything else is unsupported, including other 6.12
   builds: the module uses in-kernel interfaces that change between versions without notice.
   *Note, 2026-11-10 ([ADR-0036](decisions/0036-ubuntu-abi-series-policy.md), journal
   [0281](history/0281-port-ubuntu-7-0.md)):* for Ubuntu kernels a whole ABI series is
   supported. `scripts/supported-kernels` lists `7.0.0-*-generic`, where `*` is one or more
   digits, and `make install`, `moobfs mount` and DKMS accept any release of it. The suite ran on
   one release, `7.0.0-43-generic`, which the documents name. A weekly job
   (`.github/workflows/ubuntu-drift.yml`) builds the layer against the newest Ubuntu 26.04
   kernel, and moving the native job to a newer ABI reruns rule 3's evidence. Debian entries stay
   exact releases.
2. The module refuses to build against a kernel outside the list instead of building with
   warnings. (Not implemented: today the build does not check the version.)
   *Note, 2026-10-25 (journal 0197):* `make install` now refuses a kernel that is not in
   `scripts/supported-kernels` unless `MOOB_ALLOW_UNSUPPORTED=1`; `make layer` itself still does
   not check. A Debian 13 system installed from the 2026-10-21 cloud image runs
   `6.12.113+deb13-amd64`, which is outside the list (issue 142).
   *Note, 2026-11-09 (journal [0277](history/0277-kernels-6-12-115-and-7-3-supported.md)):*
   the maintainer amended this rule; the refusal is split by tool. `make install`
   (`scripts/install.sh`, unless `MOOB_ALLOW_UNSUPPORTED=1`) and DKMS (`BUILD_EXCLUSIVE_KERNEL`,
   generated from `scripts/supported-kernels`) refuse every release outside the list. `make layer`
   refuses by API series only, through the `#error` of `layer/moobl_compat.h`: releases from 6.13
   up to before 7.3 do not build. A refusal by exact release in `make layer` would break the
   builds that compile the module against kernels deliberately not on the list (the upstream
   KASAN kernels 6.12.116 and 7.3.3, the KCSAN and coverage kernel 6.12.116, the static-analysis
   headers, the compile
   probes of journal 0272), about ten builds.
   *Note, 2026-11-10 (journal [0281](history/0281-port-ubuntu-7-0.md)):* the `#error` window of
   `layer/moobl_compat.h` is now 6.13 up to before 7.0, plus 7.1 and 7.2. 7.0 and 7.3 build. The
   list refusal of `make install` and DKMS reads the series entry of ADR-0036.
3. A kernel is added by a pull request that adds it to CI and records one fstests `quick` run per
   lower filesystem. It is removed when its upstream series ends or when keeping it needs code the
   maintainer does not want to carry; removal is announced in the changelog one release ahead.
   *Note, 2026-10-26 (journal 0203):* the full suite passes on Debian 6.12.113 in CI (first in
   PR 148); the three fstests `quick` runs are still pending. It is not in
   `scripts/supported-kernels` and is not yet supported.
   *Note, 2026-10-27 (journal 0206):* the three runs passed (631 tests each, 0 failures, with the
   exclusion list), and 6.12.113 was added to `scripts/supported-kernels`. A fresh Debian 13
   install from the 2026-10-21 cloud image is now accepted by `make install` as it is.
   *Note, 2026-11-09 (journal [0277](history/0277-kernels-6-12-115-and-7-3-supported.md)):* two
   releases were added. Debian's `6.12.115+deb13.1-amd64` (trixie-security, which an updating
   Debian 13 host installs by itself): the full suite passed on it and fstests `quick` passed on
   ext4 and XFS (631 tests each); on btrfs only `generic/301` failed, a threshold fstests skips for
   btrfs but applies over the layer (journal 0272), and the maintainer decided to exclude it on
   btrfs in `quick` as it already was in `auto` (`vm/fstests/exclude-quick-btrfs.txt`).
   The rerun on btrfs with that list (2026-11-09, 06:49–08:55 BRT) passed all 630 tests.
   Debian's `7.3.3+deb14-amd64`, by the
   maintainer's decision after the port of ADR-0034 (journal 0275): the full suite passed on it
   (layer module only; the M1 module stays 6.12-only), the full suite passed under KASAN, UBSAN and
   lockdep on upstream 7.3.3, and fstests `quick` passed on the three lowers (631 each) over
   upstream 7.3.3 built with `vm/fstests.config`, not over Debian's build of the same release. Both
   have native CI jobs, and upstream 7.3.3 has its own leg of the KASAN workflow; their first runs
   on GitHub are the pull request of this batch.
   *Note, 2026-11-10 (journal [0281](history/0281-port-ubuntu-7-0.md)):* Ubuntu 26.04's 7.0
   series was added as `7.0.0-*-generic` (ADR-0036), by the maintainer's decision under ADR-0034
   point 3. The evidence is on the release `7.0.0-43-generic` (Ubuntu `7.0.0-43.43`, upstream
   base 7.0.14): the full suite passed on it (layer module only), the full suite passed under
   KASAN, UBSAN and lockdep on upstream 7.0.14 with `vm/kasan.config`, and fstests `quick`
   passed on the three lowers over Ubuntu's own build (`vm/fstests-ubuntu.sh`). It has a native
   CI job, a third leg of the KASAN workflow and the weekly drift build.
4. Lower filesystems: ext4, btrfs and XFS, on a local block device. A network or FUSE filesystem
   as the lower is unsupported.
   *Note, 2026-10-29 (journal 0234):* one reason is now part of the rule. Since journal 0216
   (GHSA-756j-cgwq-gvwm) domain and file records keep the lower inode's `i_generation` and are
   rebuilt or dropped when it differs, which detects an inode number reused after a removal
   behind the layer. A supported lower must therefore give each new inode a generation: ext4
   (random), XFS (random on v5 without `ikeep`, otherwise bumped on free) and btrfs (the creating
   transaction) do in Linux 6.12.112. The NFS client leaves it 0 for every inode, and FUSE takes
   the server's value, 0 in libfuse unless the filesystem fills it; over them a reused number is
   not detected. On ext4 without `metadata_csum` (not the `mke2fs` default) the owner of an inode
   can set its generation with `EXT4_IOC_SETVERSION` directly on the lower, which can make stale
   records current again; [when-not-to-use.md](when-not-to-use.md) §3 gives the details.
5. Architecture: x86-64 only until another one runs the same two jobs.

## 2. Attribute encoding (what is on disk)

**Today.** The only persistent format is the `user.moob.*` encoding, version 1, declared by
`user.moob.format` on the lower root (absent means version 1). The module refuses to mount a tree
that declares a version it does not know.

**Rules.**

1. A tree written by a released version stays readable by every later version. Data is never the
   price of an upgrade.
2. A change that old modules would misread needs a new format number, a specification, tests of
   invalid inputs, an offline migration tool, and an ADR (this is already in `AGENTS.md`).
3. Adding a type or relaxing a limit is such a change. Tightening a limit is not allowed for
   values an earlier version accepted.

## 3. Ioctl interface (UAPI)

**Today.** `MOOB_UAPI_VERSION` 1 (`GET_INFO`, `QUERY`) and `MOOB_QUERY2_VERSION` 2 (`QUERY2`,
`QUERY2_BATCH`), both experimental: the README says they may change with no promise. Every
request carries its version; an unknown version or a reserved field that is not zero returns
`EINVAL`; an unknown command returns `ENOTTY`. `GET_INFO` reports the versions and a capability
mask.

**Rules, from the first release that is not marked experimental.**

1. A structure, command number or meaning that shipped does not change. New behaviour is a new
   command, a new version number in the request, or a bit in a field that was reserved and
   checked for zero.
2. Clients discover features from `GET_INFO` (versions, capabilities, flags), never from the
   module or kernel version.
3. A command is removed only after one release in which it is documented as deprecated and
   `GET_INFO` says so.
4. Until that first release, the interface may change in any release; every change is listed in
   the changelog under its own heading, and the version number in the request changes when a
   request that used to be valid changes meaning.

**Experimental limit decision, 2026-10-23 (ADR-0019).** A valid `QUERY2`
or `QUERY2_BATCH` request may now fail with `EDQUOT` (unprivileged build
admission), `E2BIG` (scan work) or `ETIMEDOUT` (cooperative build deadline;
ten seconds of the builder's CPU time or 120 s elapsed since ADR-0031, accepted,
which only admits builds the elapsed deadline refused).
Successful request and cursor meanings, structure layouts and command
numbers do not change, so the request versions stay 2. This is a new
resource-availability failure, not a new interpretation of a successful
request. It is nevertheless a compatibility cost for callers that assumed
every otherwise valid domain could be indexed; the changelog and query
specification state it explicitly. No disk format changes.

**Reserved byte of the query term, 2026-10-28 (ADR-0026).** The implicit tail padding byte of
`struct moob_query_term` became `pad[3]`, checked for zero (`EINVAL`). Layouts, command numbers
and request versions do not change; a request that left a nonzero byte there, valid before, is
refused. No disk format changes.

## 4. Mount options and tools

Mount options follow the UAPI rule: an option that shipped keeps its meaning; an unknown option
refuses the mount. The command-line tools (`moobfs-attr`, `moobfs-check`, `moobfs-index`) keep
their exit statuses and the tab-separated output that scripts read; human-readable text may change.

## 5. Releases

Versions are `MAJOR.MINOR.PATCH`, with `-experimental` while §3 rule 4 applies. A fix for a
defect that affects security or durability gets a patch release of the latest version, and an
advisory when it is a vulnerability under `SECURITY.md`; older versions are not patched.

## Open points

- Rule 1.2 (refusing to build against an unlisted kernel) needs a build-time check, which is a
  code change not made yet.
  *Note, 2026-10-28 (journal 0221):* one interface is now checked at build time:
  `layer/main.c` asserts the signature of `backing_file_open()` that the supported kernels have
  (the user file, from the 6.12 stable backport of the backing-file LSM hooks; the v6.12 tag
  takes the user path) and fails with a message naming this page. It checks the type, not the
  version, so it is not the general refusal rule 1.2 asks for.
  *Note, 2026-11-09 (journal [0277](history/0277-kernels-6-12-115-and-7-3-supported.md)):*
  closed by the amendment of rule 2 noted there: `make install` and DKMS refuse by exact release,
  `make layer` by API series (`layer/moobl_compat.h`).
- Decided on 2026-10-13, later the same day: instead of a patch release of `v0.2.0-experimental`,
  a new pre-release `v0.3.0-experimental` carries every fix (journal 0087).
- 2026-10-19: `v0.4.0-experimental` carries the fixes made since, F2 (ADR-0016) and F4 among them
  (journal 0135).
- 2026-10-20: `v0.4.1-experimental` fixes the query slowdown of `v0.4.0-experimental` (journal 0139) and
  adds the tests of journals 0140 and 0141 (journal 0142).
- 2026-10-28: `v0.5.0-experimental` carries the fixes of F13 to F17 and of review rounds 0144 and
  0155. A minor version: the reserved byte of a query term must be zero (ADR-0026) and two lookup
  errnos changed (`EREMOTE`, `ESTALE`); sizes, offsets, command numbers and the on-disk
  conventions did not (journal 0226).
