# Security policy

MoobFS is experimental kernel code developed by a single maintainer. There is no
stable release, no production security guarantee and no stable UAPI. Run it
only in a disposable VM with synthetic data. This policy describes how to report
a problem and what to expect; the longer-term plan is in
[docs/security-roadmap.md](docs/security-roadmap.md) (items S1 to S22, with implemented tests and remaining gaps recorded per item).

## Supported versions

| Version | Supported |
| --- | --- |
| `main` | Use this. Fixes land here only; there are no backports and no maintenance branches. |
| `v0.5.0-experimental` | Source snapshot of 2026-10-28, not supported: fixes F1 to F17 except fair shares of F2, review 0144 R1/R2 and the defects of review round 2 except the part of issue 117 left open (journal 0226). Its source archive is signed with Sigstore and has SLSA provenance (`docs/security-operations.md`). |
| `v0.4.1-experimental` | Source snapshot of 2026-10-20, not supported: fixes F1 to F12 except fair shares of F2, but contains review 0144 R1/R2, F13 (review 0151) and F14 with the other defects of review round 2 (journal 0155); their corrections (journals 0149, 0150, 0152, 0156) are newer than this tag and are in `v0.5.0-experimental`. It also contains F15, F16, F17 and GHSA-756j-cgwq-gvwm, fixed on `main` in journals 0215, 0216, 0218 and 0222, and the defects of issues 115, 117 (in part) and 119, fixed in journals 0221 to 0224. |
| `v0.4.0-experimental` | Source snapshot, not supported. Its queries are about twice as slow as they should be (a buffer allocated per query, journal 0139). |
| `v0.3.0-experimental` | Source snapshot, not supported. It carries defects fixed since, security ones included (findings F6 to F12 of ADR-0011; see `CHANGELOG.md`). |
| `v0.1.0-experimental`, `v0.2.0-experimental` | Source snapshots, not supported; they carry more of the same defects. |

## How to report a vulnerability

Report privately. Do **not** open a public issue, pull request or discussion.

1. Preferred: [GitHub Security Advisories](https://github.com/studiomoob/llmoobfs/security/advisories/new)
   (private vulnerability reporting for this repository).
2. Alternative: email **claudio@studiomoob.com**. For large or sensitive
   material, first ask by email how to transfer it.

Please include, as far as you can:

- the commit SHA you tested;
- the kernel version (`uname -r`) and whether it was the harness kernel
  (Debian `6.12.111+deb13-amd64` or `6.12.113+deb13-amd64`, or upstream 6.12.112 under KASAN)
  or another;
- the lower filesystem (ext4, btrfs, XFS, other) and the exact mount options
  (`lower=`, `nolinks`, `noindex`, `index_budget=`);
- the relevant `dmesg` excerpt (`BUG`, `WARNING`, `KASAN`, `UBSAN`, lockdep);
- a minimal reproducer (commands, a small `tests/*.c` change or a `vm/suite.sh`
  step), with synthetic data only: no real agent memories, credentials or
  personal data;
- the impact you believe it has (crash, hang, corruption, data loss, information
  disclosure across users, privilege escalation).

## What counts as a vulnerability

Within scope, any of the following triggerable by an unprivileged local user or
by a process with ordinary access to the mount:

- kernel crash, hang or warning (denial of service counts, as it does for
  in-tree filesystems);
- out-of-bounds access, use-after-free, uninitialized memory or any sanitizer
  report;
- information leak between users through `QUERY`, `GET_INFO` or attribute
  handling, including through counts or error codes;
- silent data loss or corruption on the lower filesystem;
- privilege escalation.

Mounting already requires `CAP_SYS_ADMIN` in the initial user namespace, so
behaviour that only a mounter can trigger is a bug, not an escalation, unless it
crosses into another user's data.

## Scope

In scope:

- the stacked layer module (`layer/`), including the `QUERY` and `GET_INFO`
  ioctls (`include/moobfs_ioctl.h`) and the typed-attribute validation;
- the shared attribute encoding and image parser (`common/`, `include/`);
- the user-space tools (`tools/`: `mkfs.moobfs`, `fsck.moobfs`, `moobfs-attr`,
  `moobfs-check`, `moobfs-index`);
- the VM harness (`vm/`) when a defect affects whoever runs it, for example
  writing outside `build/` or loading a module on the host;
- the read-only M1 module (`kernel/`), kept as a regression.

Out of scope:

- defects in the lower filesystems (ext4, btrfs, XFS), in the Debian or upstream
  kernel, in QEMU, Docker or virtme-ng that are unrelated to the layer; report
  them to those projects (for the kernel, follow
  `Documentation/process/security-bugs.rst`). If a MoobFS test exposes such a
  defect we will help with the report;
- attacks that require already having `CAP_SYS_ADMIN` on the host;
- the project website (`site/`) content, unless it serves active code.

## Response targets

These are targets for a single volunteer maintainer, not guarantees:

| Step | Target |
| --- | --- |
| Acknowledge the report | within 7 days |
| Initial assessment (valid, in scope, severity) | within 30 days |
| Status update to the reporter | at least every 30 days until closed |
| Fix | no fixed deadline; agreed with the reporter case by case |

## Coordinated disclosure

- The report stays private until a fix is in `main`. The reporter and the
  maintainer agree on the disclosure date; disclosure may happen earlier if the
  reporter agrees, or if the issue becomes public elsewhere.
- Since there are no supported releases (the `-experimental` tags are source snapshots), "fixed" means a commit on `main` with a test in
  `vm/suite.sh` or `tests/` that reproduces the problem and passes afterwards.
- No CVE is requested while there is no supported release (the tags are
  experimental source snapshots only). The decision is made case by case and
  recorded in the journal entry; if one is requested later, it goes through
  GitHub's CNA from the advisory. No CVE is expected for test-only or
  documentation code.
- Credit is given in the advisory, with the reporter's consent and in the form
  they choose. The journal entry (`docs/history/`) and `CHANGELOG.md` describe the
  reporter by role and link the advisory.
- There is no bug bounty or other financial reward.

## Safe harbor

Security research on MoobFS done in good faith under this policy is authorized
by the MoobFS maintainers. We will not initiate or support legal action against
you for accidental, good-faith violations of this policy, and if a third party
takes action against you over research that followed it, we will make it known
that you acted under it. You are still expected to comply with applicable law.
This applies only to claims under the maintainers' control and does not bind
anyone else. If you are unsure whether something is covered, ask through the
channels above before going further.

Good faith here means:

- you test the code on machines and virtual machines you own or are allowed to
  use, with synthetic data;
- you report privately through the channels above and give us a reasonable time
  to fix before disclosing, as described under *Coordinated disclosure*;
- you stop and report as soon as you reach data that is not yours, and do not
  keep or share it.

This authorization covers the MoobFS code only. It does not cover GitHub, the
project site's hosting, Studio Moob's other systems or anyone else's machines,
and we cannot authorize testing of them; denial-of-service or social-engineering
attempts against people or services are out of scope. The wording is adapted
from the disclose.io safe-harbor terms, CC0 (see `docs/references.md`).

## Operations

How incidents other than a reported vulnerability are handled (a compromised
account or token, a malicious dependency or action, a tampered tag), who has
access to what, which advisories are watched and which third-party components
the project uses: [docs/security-operations.md](docs/security-operations.md).
The same contact is published at
`https://studiomoob.github.io/llmoobfs/.well-known/security.txt`.

## Current boundaries

The [review of 2026-10-20](docs/history/0144-independent-review.md) found two defects beyond
ADR-0011: stale backing-file flags after `F_SETFL` (R1), and operational lower `getxattr`
errors skipped during indexing (R2). Both affect `v0.4.1-experimental`. Corrections are
on `main` and in `v0.5.0-experimental`. The full integrated VM suite and local
KASAN/UBSAN/lockdep run at `49098aa` passed, and the required pull-request
checks passed before that batch merged. R1 synchronizes mutable lower flags before delegated I/O
([0149](docs/history/0149-sync-backing-file-flags.md)). R2 propagates operational read errors
through the existing build/adoption failure paths, while preserving the exclusions for
missing, oversized or malformed attributes ([0150](docs/history/0150-propagate-index-xattr-read-errors.md)).
R1 has a VM reproduction and negative evidence, and both clearing and setting append passed
on ext4, btrfs and XFS in the integrated run. R2 has positive and negative tests injecting the
value-read result; its regression also passed in that full run together with the acceptance
and layer suites. The R2 test does not induce an actual lower-filesystem I/O fault. Consult
those journals for validation scope. The local batch sanitizer gate passed on Linux 6.12.112.

The parser checks image structure and hashes, but checksums are not
authentication. The module validates at mount, not continuously against external
backing-device writes. Neither encryption nor authorization beyond ordinary VFS
access is provided. `QUERY2` answers from the partition of a domain directory the
caller can traverse and read, holding only files every traverser can read; with
domains on, the v1 `QUERY` is limited to `CAP_SYS_ADMIN` (`docs/spec-query-v2.md`).
The VM suite, the KASAN/lockdep workflow and the attribute fuzzer are not a
security audit; no independent human audit of the whole code has taken place. Two independent source reviews by AI
assistants (journals 0111 and 0114) found findings F9 to F12, fixed through private advisories. A third
(journal 0151, at `v0.4.1-experimental`) found that a chmod landing during the first build of a domain
left the file in the domain's `QUERY2` answers to callers who can no longer read it; reproduced in
the VM and fixed in journal 0152, with a suite test and a negative round. Three private reports
from an external reviewer (2026-10-27), credited in the advisories, found F15 (`QUERY2` named
files under a search-only directory, [GHSA-95h9-j7c2-gcw6](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-95h9-j7c2-gcw6), journal 0215),
domain and file records answered for a reused inode number
([GHSA-756j-cgwq-gvwm](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-756j-cgwq-gvwm), journal 0216) and F16 (a set-user-ID bit kept
through a write, [GHSA-6xf4-r246-92r6](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-6xf4-r246-92r6), journal 0218); each is fixed on
`main` with a test and a negative round. The threat and credential
model is [ADR-0011](docs/decisions/0011-threat-and-credential-model.md)
(accepted 2026-10-12): adversaries, promises, non-promises, unprivileged surfaces and the
findings F1 to F17, of which part of F2 (fair shares of the index budget) is open. [docs/security-roadmap.md](docs/security-roadmap.md) lists
fstests, syzkaller, hostile-lower handling and the other open items.

Known availability limit (issue 118): an authorized, unprivileged `QUERY2`
can build a whole domain with the mounter's credentials, again after a
directory rename. The mount's memory budget does not cap the CPU or lower
I/O of that scan. A bounded KVM probe measured the work
([journal 0182](docs/history/0182-query-build-resource-cost-investigation.md)).
The maintainer chose the resource-limit direction; `main` includes
experimental work and rebuild-frequency limits
([ADR-0019](docs/decisions/0019-bound-query-build-work-and-frequency.md))
and a per-mount `index_build_work` choice of 1,024–65,536 units, default
32,768 ([ADR-0020](docs/decisions/0020-configurable-query-build-work-per-mount.md),
[PR 138](https://github.com/studiomoob/llmoobfs/pull/138)).
They do not strictly cap CPU time or physical I/O, nor provide fair index
memory shares. The default still denies the measured valid 17,000-file
domain with `E2BIG`; 65,536 completed that fixture in a dedicated VM.
Over a lower slowed by `dm-delay` the first build of a 5,000-file domain took
9.8 s at 200 ms per I/O and stopped at the deadline with `ETIMEDOUT`, with no
partial answer, at 400 ms; issue 118 was closed with that evidence
([journal 0210](docs/history/0210-security-batch-1-domain-records-and-slow-lower.md)).
A later change stops a budget release from dropping a domain
last built by another filesystem uid: the query returns `ENOMEM` when the
remainder is too small, and one uid can still fill the whole budget
([ADR-0021](docs/decisions/0021-budget-release-keeps-the-builders-domain.md),
[journal 0192](docs/history/0192-same-fsuid-budget-release.md)).
That host regression passed, and the opt-in native-KVM probe passed on the
dedicated host. The steps `release`, `overbudget` and `buildcostevict`
also passed there under KASAN, UBSAN and lockdep. The complete KASAN, UBSAN
and lockdep suite of this change then exited 0 (`MOOBFS_KASAN_PASS`). The
complete native-KVM suite of this change was not run. Local VM and KASAN
suites and all eleven PR checks passed for the earlier guard.
Issue 118 remains open for a real slow lower device, a large
single-project client and broader-workload validation. Avoid untrusted
multi-user query workloads while this issue is open.
