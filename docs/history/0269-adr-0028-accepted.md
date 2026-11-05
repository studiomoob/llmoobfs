# 0269 — ADR-0028 accepted

Date: 2026-11-05. Status: **done**. Calendar time: a review of about half an hour; effort the
same.

## Decision

The maintainer reviewed ADR-0028 (item C6: `moobfs-dkms` and `moobfs-tools`, the kernel matrix
as exact releases from `scripts/supported-kernels`, signing with the host's own DKMS key, the
refusal to install beside a `make install` copy, no automatic unload, and the Secure Boot VM
test) and accepted it as implemented, with no change. Nothing is published; the packages stay
behind stage 2 and the "Public release" row of `ROADMAP.md`.

## Concerns raised, kept as limitations

The maintainer named the narrow test base as a weakness: one distribution, one architecture,
one kernel release, and no real hardware or real MokManager screen. They are the limitations
the ADR already states. Exact releases were kept over accepting every 6.12.x: a release joins
the matrix only after the suite and KASAN pass on it (`docs/compatibility.md`), at the cost of a
module that is not built after a kernel update until then. Widening the test base (more
kernel releases, a second architecture, hardware run by third parties) is work for stage 2,
not part of this decision.

## Changes

ADR-0028's status line; `CHANGELOG.md`; the old wording added to `docs/retired-claims.txt`.
