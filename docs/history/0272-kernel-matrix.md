# 0272 — Widening the kernel matrix (in progress)

Date: started 2026-11-07, 18:40 Brasília time. Status: **in progress**. Estimated effort L.

## Goal

The second phase agreed on 2026-11-07 (journal 0270): run the module on more than one kernel
series. The maintainer confirmed the matrix (6.12 plus newer series) and, once the probe below
showed the module must change for them, a second narrow exception to the freeze (ADR-0034).

## Compile-only probe (2026-11-07, 18:40–18:50, test host)

The `layer/` of `claudiofp/b14-stage2` (with `common/` and `include/`), unchanged, built with
`make -C linux-<v> M=... modules` against `defconfig` plus POSIX ACLs on ext4, btrfs and XFS and
tracepoints, after `modules_prepare`; tarballs from cdn.kernel.org checked against
`sha256sums.asc`. Nothing was loaded.

| Release | Result |
| --- | --- |
| 6.12.116 | builds, no error, no warning |
| 6.18.62 | 55 errors, all in `main.c` |
| 7.3.3 | 66 errors, all in `main.c` |
| 6.6.162, 6.1.192 | stop at the missing `linux/backing-file.h` |

A first run of the probe failed on 6.12.116 because the copy lacked `common/`; it was a fault of
the probe, corrected by copying the tree as the build expects.

## Next steps

1. The KASAN kernel from 6.12.112 to 6.12.116 (pin and hash in `kasan.yml`), with a full KASAN
   run.
2. The port to 7.3 within ADR-0034, read against the 7.3.3 sources, then the suite and KASAN on
   a 7.3 kernel.
