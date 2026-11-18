#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of `make apt-live-test` (ADR-0041): follows
# docs/operations.md against the published repository, https://studiomoob.github.io/llmoobfs/apt/,
# with the project's real key. Phase 0: the key's fingerprint is the one SECURITY.md gives; apt
# installs both packages; DKMS builds the module for the image's kernel; it loads, a layer mounts
# over an ext4 directory and a typed attribute set through it reaches the lower; purge. Prints
# markers.
set -eu
KVER=6.12.113+deb13-amd64   # the kernel of the 2026-10-21 cloud image (journal 0206)
FPR=35E6715F0D27D8FACD8456C294918AEE2850F202   # SECURITY.md, "Package signing key"
URL=https://studiomoob.github.io/llmoobfs/apt
KEYRING=/usr/share/keyrings/moobfs-archive-keyring.pgp
mark() { echo "$*"; }
fail() { echo "MOOBFS_APTLIVE_FAIL: $*"; exit 1; }
export DEBIAN_FRONTEND=noninteractive

[ "${1:-}" = 0 ] || fail "unknown phase ${1:-}"
[ "$(uname -r)" = "$KVER" ] || fail "kernel $(uname -r), not $KVER"
apt-get update >/dev/null
apt-get install -y --no-install-recommends curl gpg ca-certificates "linux-headers-$KVER" >/dev/null 2>&1 ||
	fail "curl, gpg and the headers for $KVER"

# docs/operations.md, "Or: the experimental packages"
curl -fsSL -o "$KEYRING" "$URL/moobfs-archive-keyring.pgp"
got=$(gpg --show-keys --with-colons "$KEYRING" 2>/dev/null | awk -F: '/^fpr/ { print $10; exit }')
[ "$got" = "$FPR" ] || fail "the published key's fingerprint is $got, not $FPR"
cat >/etc/apt/sources.list.d/moobfs.sources <<SOURCES
Types: deb
URIs: $URL/
Suites: experimental
Components: main
Signed-By: $KEYRING
SOURCES
apt-get update >/tmp/u.log 2>&1 || { cat /tmp/u.log; fail "apt-get update"; }
if grep -q -E '^(W|E):' /tmp/u.log; then cat /tmp/u.log; fail "apt-get update warned"; fi
ver=$(apt-cache policy moobfs-dkms | awk '/Candidate:/ { print $2 }')
[ -n "$ver" ] && [ "$ver" != "(none)" ] || fail "no candidate for moobfs-dkms"
apt-get install -y moobfs-dkms moobfs-tools >/tmp/i.log 2>&1 || { tail -n 30 /tmp/i.log; fail "apt-get install"; }
mark "APTLIVE_INSTALL: moobfs-dkms and moobfs-tools $ver from $URL, key $FPR"
dkms status moobfs | grep -q "^moobfs/$ver, $KVER, x86_64: installed" || fail "dkms status: $(dkms status moobfs)"
modprobe moobfs_layer || fail "modprobe"
mark "APTLIVE_MODULE: DKMS built moobfs_layer $(modinfo -F version moobfs_layer) for $KVER; it loads"

# a layer over an ext4 directory, one typed attribute through it
truncate -s 256M /root/lower.img
mkfs.ext4 -q /root/lower.img
mkdir -p /lower /mnt/layer
mount -o loop /root/lower.img /lower
echo note >/lower/a.md
mount -t moobfs_layer -o lower=/lower none /mnt/layer || fail "mount"
moobfs-attr set /mnt/layer/a.md kind string decision || fail "moobfs-attr set through the layer"
moobfs-attr list /lower/a.md | grep -q decision || fail "the attribute did not reach the lower"
umount /mnt/layer
rmmod moobfs_layer
umount /lower
mark "APTLIVE_MOUNT: a layer over ext4 mounted, a typed attribute set through it is on the lower"

apt-get purge -y moobfs-dkms moobfs-tools >/tmp/i.log 2>&1 || { tail -n 20 /tmp/i.log; fail "purge"; }
[ -z "$(dkms status moobfs)" ] || fail "dkms still lists moobfs"
mark "APTLIVE_PURGED"
mark "PHASE_DONE: 0"
