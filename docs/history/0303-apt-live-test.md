# 0303 — The published apt repository installed in a VM: `make apt-live-test`

Date: 2026-11-17. Work started at 18:35 Brasília time and ended at 18:49. Status: **done.**
Estimated effort S (hours); the time recorded is calendar time.

## Goal

Install the published packages (journal 0302) the way `docs/operations.md` tells a user, on a
disposable Debian 13 VM, with the module built by DKMS and loaded. Journal 0302 had installed them
in a container only, which has no kernel of its own.

## Decision

A permanent VM step instead of a one-off run, so every published version can be checked the same
way: `make apt-live-test` (`MOOB_TEST=apt-live` in `vm/install-test/`, guest
`vm/apt-test/live.sh`). Unlike `make apt-test`, it uses the network, the public URL and the
project's real key, and it fails unless the downloaded key has the fingerprint `SECURITY.md` gives
(written into the script). No Secure Boot: `make package-test` covers it.

## Evidence

On the dedicated host, at `3b418d0`, 18:41–18:48 under KVM (383 s in the guest):

- the key from `https://studiomoob.github.io/llmoobfs/apt/moobfs-archive-keyring.pgp` has
  fingerprint `35E6715F0D27D8FACD8456C294918AEE2850F202`;
- `apt-get update` with the deb822 source and `Signed-By` gave no warning; apt installed
  `moobfs-dkms` and `moobfs-tools` `0.6.2`;
- DKMS built `moobfs_layer` `0.6.2` for `6.12.113+deb13-amd64`; it loaded;
- a layer mounted over an ext4 lower, and `moobfs-attr set` through it reached the lower;
- purge removed both packages and DKMS's entry.

`MOOBFS_APTLIVE_PASS`, rc=0.

## Limits

One run, Debian 13 amd64, one kernel (the image's 6.12.113), no Secure Boot. The step depends on
the site being up and on the network.

## Also in this batch

`docs/security-roadmap.md`, row S10, still said "no published package"; journal 0302 missed it.
It now says the packages are published, and the old wording is in `docs/retired-claims.txt`.

## Next step

Run `make apt-live-test` after each publication; record it in that publication's journal.
