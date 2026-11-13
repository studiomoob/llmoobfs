# Matrix and known limitations

One page, for the technical preview (stage 2 of [distribution-strategy.md](distribution-strategy.md)
§8). It points to the sources of truth instead of copying them; `scripts/check-matrix-page.sh`
checks in CI that the releases below match `scripts/supported-kernels` and the KASAN workflow's
pins. The rules behind the list are [compatibility.md](compatibility.md) §1.

## Kernels and architectures

| Kernel | Arch | Status | What runs on it |
| --- | --- | --- | --- |
<!-- supported-kernels -->
| Debian `6.12.111+deb13-amd64` | x86-64 | supported | full suite every PR; fstests `quick` on 3 lowers by hand ([compatibility.md](compatibility.md) §1) |
| Debian `6.12.113+deb13-amd64` | x86-64 | supported | full suite every PR; fstests `quick` ([0206](history/0206-debian-6-12-113-supported.md)) and `auto` once ([0239](history/0239-s2-auto-and-s3-round.md)) |
| Debian `6.12.115+deb13.1-amd64` | x86-64 | supported | full suite every PR; fstests `quick` ([0272](history/0272-kernel-matrix.md), [0277](history/0277-kernels-6-12-115-and-7-3-supported.md)) |
| Debian `7.3.3+deb14-amd64` | x86-64 | supported, layer module only | full suite every PR ([0277](history/0277-kernels-6-12-115-and-7-3-supported.md)); the M1 module stays 6.12-only |
| Debian `7.3.4+deb14-amd64` | x86-64 | supported, layer module only | full suite every PR ([0287](history/0287-debian-7-3-4.md)); the M1 module stays 6.12-only |
| Ubuntu 26.04 `7.0.0-*-generic` (also 24.04 HWE) | x86-64 | supported series, layer module only ([ADR-0036](decisions/0036-ubuntu-abi-series-policy.md)) | tested on `7.0.0-43-generic`: full suite every PR, fstests `quick` on 3 lowers ([0281](history/0281-port-ubuntu-7-0.md)); weekly build on the newest ABI |
<!-- /supported-kernels -->
<!-- kasan-pins -->
| upstream 6.12.116, `vm/kasan.config` | x86-64 | test build, not an install target | KASAN, UBSAN, lockdep every PR touching `layer/` or `vm/` |
| upstream 7.3.3, `vm/kasan.config` | x86-64 | test build, not an install target | KASAN, UBSAN, lockdep every PR touching `layer/` or `vm/`; fstests `quick` on 3 lowers ([0275](history/0275-port-7-3.md)) |
| upstream 7.3.4, `vm/kasan.config` | x86-64 | test build, not an install target | KASAN, UBSAN, lockdep every PR touching `layer/` or `vm/`; fstests `quick` on 3 lowers ([0287](history/0287-debian-7-3-4.md)) |
| upstream 7.0.14, `vm/kasan.config` | x86-64 | test build, not an install target | KASAN, UBSAN, lockdep every PR touching `layer/` or `vm/` ([0281](history/0281-port-ubuntu-7-0.md)) |
<!-- /kasan-pins -->
| upstream 6.12.112 (the earlier pin) | x86-64 | test build, past runs | KCSAN ([0274](history/0274-kcsan-corrected-code.md)); syzkaller rounds ([0143](history/0143-syzkaller-fifth-run-threshold-met.md), [0239](history/0239-s2-auto-and-s3-round.md)) |
| upstream 6.12 KASAN, emulated | arm64 | exploratory | weekly and on demand, ext4 step by default ([0124](history/0124-big-endian-pass-arm64-without-kvm.md), [0137](history/0137-arm64-guest-boots-under-emulation.md)) |
| Debian 6.12.111 and upstream 6.12.116 KASAN, KVM | s390x | exploratory, one host, by hand | full suite twice; KASAN step by step; trees from little-endian read the same ([0280](history/0280-first-s390x-run.md)) |
| anything else | — | not supported | `make install` and DKMS refuse it; `make layer` refuses only by API series |

**Lower filesystems:** ext4, btrfs and XFS on a local block device, on every row marked
"supported" and in the KASAN builds. **Packaging:** `moobfs-dkms` and `moobfs-tools` built and
installed in one VM, also under Secure Boot with lockdown, not published ([0229](history/0229-dkms-and-debian-packages.md),
[ADR-0028](decisions/0028-dkms-and-debian-packages.md)). What each suite step proves:
[testing.md](testing.md) §3; what no step covers: [testing.md](testing.md) §9. **Leaks between
users:** none known open; the fixed ones are the published
[advisories](https://github.com/studiomoob/llmoobfs/security/advisories) and the findings of
[review-findings.md](review-findings.md); GHSA-756j-cgwq-gvwm is fixed and published, not an open
leak ([0283](history/0283-stage-2-decisions.md)).

## Known limitations

- **No independent human review** of the kernel code; automated tools and AI-assisted reviews
  only ([ADR-0030](decisions/0030-stage-2-review-criterion.md); open gaps in [review-findings.md](review-findings.md#gaps)).
- **Not for production**: source pre-release tags only, the UAPI may change ([compatibility.md](compatibility.md) §3).
- **NFS, FUSE or any network lower**: unsupported; no inode generation, so a reused inode number
  is not detected (GHSA-756j-cgwq-gvwm; [when-not-to-use.md](when-not-to-use.md) §3).
- **ext4 without `metadata_csum`**: the owner of a file can set its generation on the lower and
  make stale records current again (GHSA-756j-cgwq-gvwm; [when-not-to-use.md](when-not-to-use.md) §3).
- **Issue [#117](https://github.com/studiomoob/llmoobfs/issues/117)**, a documented stage-2 limit: an inode with an index record
  still takes the mount-wide index lock on instantiation and eviction; contention, no wrong answer ([0283](history/0283-stage-2-decisions.md)).
- **Slow debug kernels**: a large domain's build stops at the 10 s CPU bound (`ETIMEDOUT`) and
  five steps fail, two more miss time thresholds; left unchanged by decision ([0280](history/0280-first-s390x-run.md), [testing.md](testing.md) §9).
- **s390x**: the `ixlock` 1 s threshold cannot tell a lock wait from `drop_caches`' own cost
  there; one host, no fstests, no 7.3 ([0280](history/0280-first-s390x-run.md)).
- **A very slow lower disk**: CPU-time build deadline, 120 s elapsed backstop; no run on a slow device ([ADR-0031](decisions/0031-build-deadline-counts-cpu-time.md)).
- **Index in RAM only**, per-mount `index_budget`; one uid may fill it; no CPU or I/O cap on a
  build ([when-not-to-use.md](when-not-to-use.md) §3).
- **Stale paths after a directory rename** until remount; **lower changed while mounted** is
  undefined; **root bypasses the layer** ([distribution-strategy.md](distribution-strategy.md) §7).
- **Depth**: nothing below 64 path components is indexed (F14, [when-not-to-use.md](when-not-to-use.md) §3).
- **LSMs**: SELinux needs a policy rule; a confined mounter reads through the layer with no rule
  on its callers' domains since ADR-0037 ([0104](history/0104-selinux-investigation.md),
  [0286](history/0286-open-lower-files-as-the-mounter.md)); AppArmor: one run ([0290](history/0290-apparmor-run.md)); Smack never ran.
- **Not covered by the suite**: acceptance over btrfs and XFS, KMSAN, any performance number
  beyond pilot-grade ([testing.md](testing.md) §9, [0088](history/0088-m4-report.md)).
- **`cachestat(2)`** on a layer file reports the layer's own, empty page cache: the data is
  cached in the lower's file, as overlayfs reports it; fstests' generic/798 excluded ([0281](history/0281-port-ubuntu-7-0.md)).
- **Mounting** needs `CAP_SYS_ADMIN` in the initial user namespace; user-namespace and idmapped
  mounts are refused ([when-not-to-use.md](when-not-to-use.md) §3).
