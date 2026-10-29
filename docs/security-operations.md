# Security operations

Date: 2026-10-26 (journal [0205](history/0205-security-operations.md)). Status: documented;
the access table was read from the GitHub API on that date. This page covers what
`SECURITY.md` does not: who can change what, what to do when something other than a reported
vulnerability goes wrong, which advisories concern the project and what it is built from. It
follows the OSTIF security best-practices guide (chapters 2 to 7; see `docs/references.md`).
Reporting a vulnerability is in [SECURITY.md](../SECURITY.md).

## 1. Access

The repository is hosted by the Studio Moob organization on GitHub. Nothing below is a setting
of that organization; the project changes only what belongs to its own repository and site.

| What | State on 2026-10-26 |
| --- | --- |
| Admin on the repository | The three owners of the hosting organization, by inheritance from the organization role; one of them is the maintainer. No direct collaborators on the repository. The external reviewer who reported GHSA-95h9-j7c2-gcw6, GHSA-756j-cgwq-gvwm and GHSA-6xf4-r246-92r6 (2026-10-27) is a collaborator on those three advisories and on their temporary private forks until publication; the forks are to be deleted after publication. |
| Review routing | `CODEOWNERS` routes every path to the maintainer; no approval count is required (`GOVERNANCE.md`). |
| `main` branch | Pull requests and the checks `Tools and sanitizers`, `Native Linux VM` and `Layer suite under KASAN/lockdep` are required. Force pushes and deletion are disabled. Administrators can bypass the classic protection; signed commits are not required. Since 2026-10-28 a repository ruleset `main` repeats these rules with no bypass and allows only merge commits (journal 0217), so that OpenSSF Scorecard can read the protection with the default token (journal 0225). |
| Actions secrets | None. Workflows use the job's `GITHUB_TOKEN` only. |
| Deploy keys | None. |
| Site | The `github-pages` environment, deployed from `main` by `.github/workflows/pages.yml` with an OIDC token (`id-token: write`). |
| Private vulnerability reporting | Enabled. |
| Secret scanning and push protection | Enabled. |
| Dependabot | Alerts enabled; version updates for actions by `.github/dependabot.yml`; automatic security-fix pull requests enabled on 2026-10-26 (journal 0205). |
| Outside GitHub | The maintainer's own kernel.org account and patch-signing key, used for upstream kernel patches, not for this repository. The dedicated benchmark host is reached by an SSH key the maintainer holds; CI has no access to it, and its address is not published. |

Re-read this table when an owner, a collaborator, a secret, a key or a protection rule changes,
and in any case before a tag. Read it again with `gh api repos/studiomoob/llmoobfs/collaborators`,
`.../actions/secrets`, `.../keys` and `.../branches/main/protection`.

## 2. Incidents

An incident is anything that may have let someone change the code, the history, the site or a
tag, or read what they should not: a compromised account or token, a malicious pull request,
dependency or action, a leaked secret, a tampered tag or a defaced site. A vulnerability in the
code itself follows `SECURITY.md` instead.

Steps, in order, by the maintainer (there is one; a second person with access helps when
available):

1. **Contain.** Revoke what may be compromised: GitHub sessions and personal tokens, SSH keys,
   the patch-signing key if it is involved. Disable the affected workflow (`gh workflow disable`)
   if an action is suspected. Do not delete evidence: no force-push, no history rewrite.
2. **Record privately.** Open a draft security advisory in the repository and keep the timeline
   there until it can be public.
3. **Triage.** What could have been changed or read, since when. Compare `main` and every tag
   with an independent clone (`git fsck`, `git log --all`, tag targets against the commits named
   in `CHANGELOG.md`), the action pins with `scripts/check-pins.sh`, the site with its source on
   `main`.
4. **Remediate.** Revert through a pull request with the usual checks. A tampered tag is not
   moved silently: a new tag is made and the old one is named as bad in the release notes and
   `CHANGELOG.md`.
5. **Tell people.** Those who may have used what was affected: a public advisory, a note in the
   README's status, the release page, and the projects or upstream lists involved.
6. **Close.** A journal entry in `docs/history/` with what happened, the cause, the fix and the
   evidence, and an update of section 1 if access changed.

## 3. Advisories that concern the project

| Source | Why |
| --- | --- |
| [linux-cve-announce](https://lore.kernel.org/linux-cve-announce/) | CVEs assigned by the kernel CNA, for `fs/overlayfs`, `fs/` and the VFS the layer stacks on, and for the kernels the harness pins |
| [Debian security tracker](https://security-tracker.debian.org/tracker/source-package/linux), source package `linux` | The Debian 6.12 kernels of `make vm-test` and the CI |
| Dependabot alerts and Scorecard results of the repository | Pinned actions and the Python packages of `vm/kasan-requirements.txt` |
| [SQLite security page](https://www.sqlite.org/cves.html) | The optional SQLite extension (`sqlite/`) links the system's `libsqlite3` |

Subscribing to the first two is the maintainer's action; this list does not claim it is done.

## 4. Third-party components

The kernel module depends only on the kernel it is built against. Everything else below is a
tool, a test or an optional client.

| Component | Where | How it is fixed |
| --- | --- | --- |
| Linux headers, Debian 6.12.111 and 6.12.113; upstream 6.12.112 for KASAN | `vm/Dockerfile`, `.github/workflows/kasan.yml` | Exact package versions and an image digest |
| Debian `trixie-slim` and its packages (gcc, make, libc, QEMU, busybox, e2fsprogs, btrfs-progs, xfsprogs, dmsetup, acl, git, sqlite3, libsqlite3-dev) | `vm/Dockerfile`, `vm/install-test/Dockerfile` | Base image by digest; packages from that release |
| `fsx` and `log-writes`, vendored from fstests | `tests/third_party/` | Upstream commit in each `UPSTREAM` file |
| fstests with the project's patch | `vm/fstests/` | Tag `v2026.09.22`, cloned by hand as `vm/fstests/README.md` says |
| syzkaller | `vm/syzkaller-local.sh` | A local checkout given by `SYZ=`; the revision of each run is in its journal |
| virtme-ng and its Python dependencies | `vm/kasan-requirements.txt` | Version and SHA-256 (`pip --require-hashes`) |
| GitHub Actions | `.github/workflows/` | Full commit SHA; `scripts/check-pins.sh` in CI. The one exception is the SLSA generator of `release.yml`, a reusable workflow that must be referenced by its release tag (`v2.1.0`) |
| `libsqlite3` ≥ 3.38 | optional `sqlite/` extension | The system's library |

**SBOM.** GitHub exports an SPDX SBOM from the dependency graph
(`gh api repos/studiomoob/llmoobfs/dependency-graph/sbom`, or Insights → Dependency graph →
Export SBOM). On 2026-10-26 it listed 19 packages: the pinned actions and the Python packages of
`vm/kasan-requirements.txt`. It does not see the kernel headers, the Debian packages or the
vendored code; the table above is the complete list.

**Release assets.** From the first tag after 2026-10-28, `.github/workflows/release.yml` attaches
to each `v*-experimental` pre-release a source archive of the tag, its SHA-256, a Sigstore bundle
and SLSA provenance (journal 0225). To verify them:

```sh
python -m pip install sigstore
python -m sigstore verify github llmoobfs-<tag>.tar.gz --bundle llmoobfs-<tag>.tar.gz.sigstore.json \
    --repository studiomoob/llmoobfs --ref refs/tags/<tag>
slsa-verifier verify-artifact llmoobfs-<tag>.tar.gz --provenance-path llmoobfs-<tag>.intoto.jsonl \
    --source-uri github.com/studiomoob/llmoobfs --source-tag <tag>
```

The tags before that date have no such assets; their source is the tag itself.

**Reproducible build** of the `.ko` (roadmap item S10, [journal 0236](history/0236-reproducible-module-build.md)):
`make repro-test` (`scripts/repro-ko.sh`) builds the unsigned module twice for each listed
kernel, in two clean `debian:trixie-slim` containers pinned by digest that differ in build path,
user, host name, time zone, umask and time, and requires byte-identical results; the two
experimental `.deb` packages are compared the same way. The unsigned module is the reproducible
artifact: a signature depends on the key that made it (DKMS signs with the host's own key), and
removing it gives the unsigned module back. The hashes of each commit are those the command
prints; the ones recorded for the commit of journal 0236 are in that journal.

## 5. When to raise the security effort

From chapter 7 of the guide, applied here. Any of these is a moment to ask for an outside review
or audit again, and to tell the organizations already contacted that the project's status
changed:

- a first release that is not a source snapshot, or a UAPI declared stable;
- a client running MoobFS outside a test VM, or a second independent client;
- index persistence or any on-disk format change;
- a new third-party library linked into the module or the tools;
- any use of cryptography (none today; encryption is left to the lower filesystem, ADR-0007).
