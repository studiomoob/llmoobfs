# 0285 — Canonical's DNS unreachable over IPv4: apt retries and a fallback Ubuntu mirror

Date: 2026-11-11. Work started 13:30 Brasília time; ended 13:39. Status: **done locally, not
published.** Estimated effort S; the time recorded is calendar time of one session, close to the
effort.

## Incident

The post-merge CI run on `main` at `d96b0db` (run 43038158762, pull request 194) failed four
attempts, from about 11:40 BRT, in two jobs: "Native Linux VM (Ubuntu 7.0.0-43)", while
`apt-get update` ran in the `ubuntu:26.04` image (`Could not resolve 'archive.ubuntu.com'`), and
"Lint", step "Manual pages (mandoc lint)", on the runner itself (`Temporary failure resolving
'azure.archive.ubuntu.com'`). The code was not the cause.

Diagnosis, checked on 2026-11-11 from this Mac and from the LinuxONE host (journal 0280):
Canonical's authoritative nameservers for `ubuntu.com` (`ns1`, `ns2`, `ns3.canonical.com`) did not
answer over IPv4 (185.125.190.65 and .66; `ns3` lists the same two addresses) and did answer over
IPv6; `8.8.8.8` returned `SERVFAIL` with extended error 22, "No Reachable Authority — at
delegation ubuntu.com". GitHub's hosted runners are IPv4-only, so no `*.ubuntu.com` name resolved
there. `http://mirrors.edge.kernel.org/ubuntu/`, whose domain is served by other nameservers,
answered.

## Decision (maintainer, 2026-11-11)

No hard-coded addresses. apt gets retries and, on Ubuntu, one fallback mirror under another
domain, `mirrors.edge.kernel.org`, for both the archive and the security pocket. apt still checks
the archive's signatures with the image's own Ubuntu keyring, so the mirror is not trusted for
content. Alternatives: pinning the IPv6-reachable addresses in `/etc/hosts` (rejected: addresses
change, and the runners have no IPv6); waiting for Canonical (rejected as the only answer: the
required checks of `main` cannot pass meanwhile); a caching proxy (more than the incident needs).

Checked before choosing the mirror: `InRelease` of `resolute`, `resolute-updates`,
`resolute-security`, `noble`, `noble-updates` and `noble-security` answer 200 there, and so do
the pool paths of the four pinned packages at `7.0.0-43.43` (`linux-image-7.0.0-43-generic`, in
`pool/main/l/linux-signed/`; `linux-modules-7.0.0-43-generic`, `linux-headers-7.0.0-43-generic`
and `linux-headers-7.0.0-43`, in `pool/main/l/linux/`).

## Change

- `vm/apt-install.sh` (new): writes `/etc/apt/apt.conf.d/80moob-retries` (`Acquire::Retries "5"`,
  `Acquire::http::Timeout` and `Acquire::https::Timeout` 20 s), then runs `apt-get update` with
  `APT::Update::Error-Mode=any` (a failed index is an error, not a warning that leaves the old
  index) and `apt-get install`. If that fails on Ubuntu, it moves `/etc/apt/sources.list` and
  everything in `sources.list.d/` to `/etc/apt/moob-disabled/`, writes one deb822 source for
  `<codename>`, `-updates` and `-security` from `http://mirrors.edge.kernel.org/ubuntu`
  (`MOOB_APT_FALLBACK` overrides it), signed by `/usr/share/keyrings/ubuntu-archive-keyring.gpg`,
  and tries once more. It prints which mirror served the install. On Debian there is no fallback:
  it fails as before, with the retries.
- `vm/Dockerfile`: the one `apt-get` of the image goes through it (Debian and Ubuntu bases alike;
  the Debian bases gain only the retries and the stricter update).
- `vm/fstests-ubuntu/Dockerfile`: its three `apt-get` steps go through it.
- `vm/ubuntu-drift.sh`: its install goes through it (the weekly drift check runs in an
  `ubuntu:26.04` container on a runner, so it had the same exposure).
- `.github/workflows/ci.yml`, job "Lint": the shellcheck and mandoc installs on the runner go
  through it (`sudo sh vm/apt-install.sh ...`). The other workflows' runner installs were left as
  they are: the incident hit these two, and each change to a workflow is a change to review.

## Evidence (local, this Mac, Docker Desktop on Apple silicon, amd64 under emulation)

- **Forced fallback**, 13:34–13:36 BRT: the pinned `ubuntu:26.04` image, its `URIs:` lines
  rewritten to `http://archive.invalid/ubuntu` (unresolvable), then `sh vm/apt-install.sh` with
  the four pinned kernel packages. Output: `Err: ... Could not resolve 'archive.invalid'` for each
  index after the retries; `apt-install: the image's own sources failed; retrying once from
  http://mirrors.edge.kernel.org/ubuntu (archive and security)`; `Get:1
  http://mirrors.edge.kernel.org/ubuntu resolute InRelease`, `resolute-updates InRelease`,
  `resolute-security InRelease`; the four packages fetched from `resolute-updates/main` at
  `7.0.0-43.43` (the 170 MB modules package once interrupted and fetched again, `Ign:11` then
  `Get:11`: the retries at work); `Setting up` each; `apt-install: installed from the fallback
  mirror http://mirrors.edge.kernel.org/ubuntu`; exit 0; `dpkg-query` lists the four at
  `7.0.0-43.43`. A first run of the same test, 13:32–13:34, gave the same result.
- **Full image, unforced**, 13:36–13:39 BRT: `docker build --no-cache --platform linux/amd64` of
  `vm/Dockerfile` with the arguments `vm/run.sh` passes for `MOOB_UBUNTU_KERNEL=7.0.0-43` (base
  `ubuntu:26.04@sha256:6a06111c…`). The primary path could not be shown working: the incident was
  still on, and from this Mac too `apt-get update` failed with `Could not resolve
  'archive.ubuntu.com'` and `Could not resolve 'security.ubuntu.com'` at 41 s. The script then
  printed `apt-install: the image's own sources failed; retrying once from
  http://mirrors.edge.kernel.org/ubuntu (archive and security)` and, at 155 s, `apt-install:
  installed from the fallback mirror http://mirrors.edge.kernel.org/ubuntu`; build exit 0. In the
  image, `dpkg-query` lists the four pinned kernel packages at `7.0.0-43.43`, `qemu-system-x86`
  `1:10.2.1+ds-1ubuntu3.3` and `busybox-static` `1:1.37.0-7ubuntu1`, and the original sources are
  in `/etc/apt/moob-disabled/`. So the fallback was shown twice, once forced and once by the
  incident itself; the unchanged primary path (the image's own sources answering) was not.
- actionlint 1.7.7 (pinned by digest) on `.github/workflows/ci.yml`: no finding. shellcheck
  0.9.0 on `vm/apt-install.sh` and `vm/ubuntu-drift.sh`: no finding. Then shellcheck 0.9.0 over what `make lint` lists, SPDX, `STEPS_FILTER_PASS`,
  `RETIRED_CLAIMS_OK`, `PINS_OK`, `MATRIX_PAGE_OK`, the site and FAQ checks, `make test` and
  `git diff --check origin/main`: pass.

## Not verified

- The primary path of `vm/apt-install.sh` with Ubuntu's own mirrors answering (they did not
  resolve during the whole session); on a Debian base it is the path every build takes, not
  rebuilt here.

- The scripts on a GitHub runner: the Lint job and the Ubuntu VM job run on the next pull request.
- The fallback on the runner's own `ubuntu-24.04` sources (`noble`, the runner's mirror list
  under `mirror+file:`): the same code path as in the image, not run there. The noble suites on
  the mirror answered 200.
- `vm/fstests-ubuntu/Dockerfile` and `vm/ubuntu-drift.sh` were not built or run with the change.
- Debian bases: only the retries and `Error-Mode=any` change; not rebuilt here.

## Recovery

`archive.ubuntu.com` answered again from this host in the late afternoon. The failed jobs of
run 43038158762 were rerun unchanged, and attempt 5 passed at 17:02 BRT; every workflow on
`main` at `d96b0db` is green. The failure was the outside DNS outage only.

## Next step

The fallback still has to run on a GitHub runner; that happens with the pull request of this
batch.
