# ADR-0036 — Ubuntu kernels are supported by ABI series; Debian stays exact-release

Date: 2026-11-10. Status: **accepted** by the maintainer on 2026-11-10 (journal
[0281](../history/0281-port-ubuntu-7-0.md)). Amends rule 1 of `docs/compatibility.md` §1 for
Ubuntu kernels only, and adds Ubuntu 26.04 GA's 7.0 as a target series under
[ADR-0034](0034-kernel-compat-freeze-exception.md) point 3. Estimated effort: S (hours) for the
decision, the list format and the drift job; the port it accompanies is M (journal 0281).
Estimate, not labor time.

## Context

`scripts/supported-kernels` lists exact `uname -r` values, and `make install`, `moobfs mount`
and DKMS's `BUILD_EXCLUSIVE_KERNEL` accept only those. That fits Debian, whose stable kernels change
release a few times a year and each one is a new upstream stable release. Ubuntu 26.04 LTS ships
Linux 7.0 as `7.0.0-N-generic`, and N changed about ten times in the 6.5 months after release
(14, 15, 26, 28, 31, 38, 39, 41, 42, 43; 44 in `-proposed` on 2026-11-10, survey of 2026-11-10).
Every one of them is a new `uname -r`. Upstream 7.0 is end of life, so Ubuntu carries its fixes
alone. An exact-release list would leave an updated Ubuntu host unsupported within weeks.
Ubuntu 24.04's HWE kernel is the same 7.0 build (`7.0.0-42.42~24.04.1` in `noble-updates`).

## Decision

1. **Ubuntu 26.04 GA's 7.0 is a target series** under ADR-0034 point 3, for the layer module
   only (the M1 module, `kernel/`, stays 6.12-only). That also covers 24.04's HWE kernel on the
   same series.
2. **DKMS and `make install` accept the whole series `7.0.0-*-generic`.** An entry of
   `scripts/supported-kernels` may hold one `*`, which stands for one or more digits (the ABI
   number) and nothing else. `scripts/install.sh`, `tools/workspace.h` (`moobfs mount`) and
   `packaging/debian/rules` (the regular expression of `BUILD_EXCLUSIVE_KERNEL`) read it the same
   way. `make layer` keeps refusing by API series only (the `#error` of `layer/moobl_compat.h`),
   as the amendment of rule 2 in journal 0277 says.
3. **The documents state exactly which release was tested.** The series is accepted, while the
   tested release is named: today `7.0.0-43-generic` (Ubuntu `7.0.0-43.43`, upstream base 7.0.14)
   for the native suite and fstests `quick`, and upstream 7.0.14 with `vm/kasan.config` for
   KASAN, UBSAN and lockdep. A newer ABI of the series is accepted but not tested until its own
   run.
4. **A weekly CI job builds against the newest Ubuntu 26.04 kernel**
   (`.github/workflows/ubuntu-drift.yml`): it installs today's `linux-headers-generic` of
   resolute, checks that the list still names its release, and builds the layer. The
   `static_assert`s of `layer/moobl_compat.h` turn a change of form inside the series into a
   build failure with a message. The job does not run the suite. The native job's release
   (`MOOB_UBUNTU_KERNEL`) moves to a newer ABI by a pull request that reruns the evidence of
   rule 3.
5. **Debian stays exact-release.** Its entries keep the form they have, and a new Debian build
   joins the list under the existing rule.

## Alternatives

- **Exact releases for Ubuntu too, refreshed per SRU.** Rejected: about one list change, one CI
  job change and three fstests runs every three weeks, and an updated host is refused in
  between.
- **Accept any `7.0.*` release.** Rejected: it would also accept other flavours (`-lowlatency`,
  `-64k`) and other distributions' 7.0 builds that were never built here.
- **Configure-time feature probes, as OpenZFS does.** Already considered and not taken in
  ADR-0034 point 2. The `static_assert`s and the weekly build cover the same risk for one
  series.

## Consequences

- The supported set on Ubuntu includes releases on which the suite never ran. The risk is a change
  of meaning without a change of type inside the series (survey risk 2). The weekly build catches
  type changes only, so the tested release is named everywhere the list is stated.
- When Ubuntu's 26.04 HWE stack moves to another series (7.3, expected with 26.04.2), that series
  needs its own entry, its own tests and its own decision. This ADR does not cover it.
- `docs/compatibility.md` §1 rule 1 gets a dated note pointing here.
