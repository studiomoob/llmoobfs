# ADR-0034 — A second narrow exception to the kernel freeze: compatibility with newer kernels

Date: 2026-11-07. Status: **accepted** by the maintainer on 2026-11-07 (journal 0272). Amends
[ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md) by one exception, beside the
one of [ADR-0033](0033-s16-freeze-exception.md); the freeze otherwise stays. Estimated effort:
L (a week or more) for the port; S for this decision. Estimate, not labor time.

*Note, 2026-11-10:* the maintainer made Ubuntu 26.04 GA's 7.0 a target series under point 3
(journal [0281](../history/0281-port-ubuntu-7-0.md)); [ADR-0036](0036-ubuntu-abi-series-policy.md)
sets how Ubuntu's ABI series enter `scripts/supported-kernels`. Point 3's text is kept as it was.

## Context

The module is built and tested on one kernel series: `scripts/supported-kernels` lists Debian
13's 6.12.111 and 6.12.113, and KASAN runs on upstream 6.12.112. The maintainer named the narrow
test base as a weakness (journal 0269) and accepted widening the kernel matrix (journal 0270).

A compile-only probe on 2026-11-07 built the unchanged `layer/` against the `defconfig` of
kernel.org releases (tarballs checked against kernel.org's `sha256sums.asc`): 6.12.116 builds
with no error or warning; 6.18.62 fails with 55 errors and 7.3.3 with 66, all in `layer/main.c`,
the glue with the VFS (`lookup_one_len*` replaced by `lookup_one*`, `struct renamedata`,
`d_revalidate`'s signature, `struct fileattr` replaced by `struct file_kattr`,
`struct backing_file_ctx`, `s_d_op`, `generic_drop_inode`, `AT_GETATTR_NOSEC`, the backing-file
splice helpers, and in 7.3 `i_state` and `vfs_create`); 6.6.162 and 6.1.192 stop at the missing
`linux/backing-file.h` (6.8 and later). Whether `layer/index.c` builds there was not reached.

The kernels Debian ships on that date: 6.12.113 in Debian 13, 7.2.12 in trixie-backports, 7.3.3
in Debian 14 (testing). No Debian release ships 6.18.

## Decision

The module may change, within these bounds, to build and run on newer kernels:

1. Only adaptations to kernel interfaces that changed: a function renamed, a signature or a
   structure changed, a helper removed. No change of behavior, of the UAPI or of the on-disk
   format, and the 6.12 build stays as it is.
2. The adaptations live in one header, `layer/moobl_compat.h`, and in the call sites that
   need them, keyed on `LINUX_VERSION_CODE` for the listed releases, with a `static_assert` on
   the signature where a stable backport may change a form within a series (journal 0221, M8.1,
   `backing_file_open()`). Feature probes at configure time, as OpenZFS does
   (`config/kernel-*.m4`), were considered: they survive distribution backports better, but the
   matrix is exact releases, which a version key with a type check covers, at much less
   machinery.
3. The target is 7.3 first (Debian 14, and the base of the next trixie-backports kernel), then
   7.2 if it differs. 6.18 only if a distribution the project targets ships it; 6.6 and 6.1 not.
4. A kernel joins `scripts/supported-kernels` only under the existing rule
   (`docs/compatibility.md` §1): the full suite and KASAN/UBSAN/lockdep on it, and fstests
   `quick` on each lower. Each port step follows the rules for a kernel change in `AGENTS.md`.
5. The upstream KASAN kernel moves from 6.12.112 to the current 6.12 release (6.12.116 on this
   date), a test-harness change with no module change.

## Consequences

- The module gains a second kernel series; CI gains a job per series.
- Risk: a port can change behavior where an interface changed its semantics, not only its
  form (for example `d_revalidate` now gets the parent and the name). Each such site is read
  against the kernel source of the release and named in the journal.
- Alternative not taken: keep 6.12 only until stage 3. The project would stay on one series
  that distributions leave behind.
