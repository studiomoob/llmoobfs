# Security and robustness roadmap for the layer

Status: **proposal** (2026-10-10). No item below marked "to do" has been implemented.
*Note, 2026-10-29 (journal 0234):* that status line is the one of 2026-10-10. Most items have
progressed or closed since, each with dated notes below; "Where each item stands" gives the
current state in one place.
Origin: the survey recorded in [docs/history/0017](history/0017-macos-and-security-posture.md),
which compares MoobFS with the kernel documentation for new filesystems, with the filesystem
fuzzing literature (Janus, Hydra, LFUZZ) and with the CVE history of overlayfs, eCryptfs,
ntfs3, JFS, ext4 and OpenZFS. This document is the list of techniques to implement, with a
suggested order and a done-when criterion. Design decisions go to ADRs; execution evidence
goes to `docs/history/`.

Principle: a module runs in ring 0, so a bug in it is a bug in the whole kernel. No technique
eliminates that. The goal is to reduce probability and impact with input validated at the
edge, detectors in CI, continuous fuzzing, a minimal surface and a written threat model. A
crash triggerable by an ordinary user is a vulnerability (denial of service), even without an
exploit, as the XFS and ext4 maintainers treat it.

## Where each item stands (2026-10-31)

<!-- current-state -->
State on `main` at `7ac52cd`, with the rows of batch 10: S1 (journal 0240), S2 and S3 (journal 0239), S5 and S14 to S21 (journal 0242), S9 (journal 0243); summarised from the dated notes of each item below.

| Item | State |
| --- | --- |
| S1 threat model | Done ([ADR-0011](decisions/0011-threat-and-credential-model.md)). Findings F1 to F17 are fixed on `main` (journal 0228), with the reused-inode records of GHSA-756j-cgwq-gvwm (journal 0216; its limits are in [when-not-to-use.md](when-not-to-use.md) §3). An external reviewer's AI-assisted private reports found kernel defects, all fixed with regression tests; there is no independent human review of the kernel code. The hand-read review of the credential paths and the `QUERY2` authorization is a stage-3 gate, not a stage-2 one ([ADR-0030](decisions/0030-stage-2-review-criterion.md)). |
| S2 fstests | `quick` group done on ext4, btrfs and XFS with a justified exclusion list (journal 0091). The `auto` group ran once on the three lowers on Debian 6.12.113 (journal 0239): 1, 4 and 1 failures of 779, none a layer defect, now in exclusion lists. Open: a repeat of `auto` with those lists, and a make target. |
| S3 syzkaller | **Done** (journal 0245, ADR-0032). Seven six-hour runs, no open report; every handler function exercised, `moobl_xattr_get()` included (journal 0239). The handlers reach 688 of 781 KCOV call sites, 88.1 %, above the 85 % threshold, which counts call sites since ADR-0032; the manager's own count, which includes 28 PCs that are not call sites, was 84.9 % in the seventh run. |
| S4 hostile lower | Done for its scope (journal 0073). |
| S5 static analysis | `checkpatch.pl` clean and required in the `Lint` job. `make static` runs `W=1` (gcc and clang), `sparse`, `smatch`, Coccinelle, the clang analyzer and `kernel-doc` on both modules and the UAPI header, with six findings justified in `tests/static/baseline.txt` and none fixed (journal 0242); `.github/workflows/static.yml` runs it on every push and pull request; its first GitHub runs, on PR 179 on 2026-10-30, passed after two fixes (journal 0242); not yet a required check. |
| S6 disclosure and versions | Done: `SECURITY.md` and [compatibility.md](compatibility.md); the KASAN/lockdep job is a required check of `main`. Open: a build-time refusal of an unlisted kernel (`make install` and the DKMS package refuse one, `make layer` does not). |
| S7 continuous fuzzing | Three libFuzzer targets (attributes, the image parser, the tool parsers) run 30 s each per push, and `.github/workflows/fuzz.yml` runs them 45 min twice a week with a cached corpus (journal 0237); its first scheduled run is pending. Locally: 30–60 min per target, no crash. OSS-Fuzz is not applied for. |
| S8 runtime hardening | KCSAN over `buildrace` and `release` in CI (journal 0127), with no race in the module; the KASAN kernel adds KFENCE, `FORTIFY_SOURCE`, `HARDENED_USERCOPY`, freelist hardening, `BUG_ON_DATA_CORRUPTION` and `DEBUG_OBJECTS`, and the full suite passed under them (journal 0237); a read-only review found no defect. Open: KMSAN. |
| S9 resource limits | Done (ADR-0029, accepted 2026-10-29; journal 0235): memory-cgroup charges (ADR-0018), bounded build work and rebuild frequency (ADR-0019, ADR-0020), fair shares of the index budget (ADR-0027) and the per-call result `limit`, each tested when hit (suite step `limits`); the mount scans nothing (`mountscale`). Residual: without `memory.max` on the users' cgroups a user can pin about 2 MiB of kernel memory per open descriptor; `docs/operations.md` §5 sets `MemoryMax=` with a user-slice drop-in and `moobfs diagnose` warns when no limit applies (journal 0243, checked in a dedicated VM). |
| S10 signing and distribution | The done criterion is met in a VM (journal 0229, ADR-0028), and the unsigned `.ko` and the packages build reproducibly (`make repro-test`, journal 0236). Open: real firmware and hardware, a rebuild by a third party, no published package. |
| S11 M3 and M4 surfaces | Nothing recorded yet. |
| S12 SELinux | One run with SELinux enforcing (journal 0104): F6 and the labels read with the caller's credentials fixed; the rule `fs_use_xattr moobfs_layer` is needed. Open: mmap, attribute writes, rename and link under a policy, MLS/MCS, a distribution booted with SELinux, the per-result check over a lower labelled by SELinux (journal 0215), Smack; AppArmor is outside the item. |
| S13 tracepoints | Done (journal 0102). |
