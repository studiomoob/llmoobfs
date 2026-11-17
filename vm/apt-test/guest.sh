#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of `make apt-test` (ADR-0041). The host
# serves four copies of a signed apt repository at http://10.0.2.2:8001/ (made by
# scripts/apt-index.sh and signed with throwaway keys, never the project's key):
#   good1/     version V1, signed with key A
#   good2/     versions V1 and V2, signed with key A
#   otherkey/  as good2, signed with key B
#   tampered/  as good2, with one byte of moobfs-tools V2 changed after signing
# and key A at /key-a.pgp. Phase 0: install from a deb822 source with Signed-By, upgrade, roll
# back; apt refuses an index signed by another key, a changed package and a source without
# Signed-By; purge. Prints markers.
set -eu
KVER=6.12.113+deb13-amd64   # the kernel of the 2026-10-21 cloud image (journal 0206)
V1=0.5.0~apttest1
V2=0.5.0~apttest2
REPO=http://10.0.2.2:8001
KEYRING=/usr/share/keyrings/moobfs-archive-keyring.pgp
SRC=/etc/apt/sources.list.d/moobfs.sources
mark() { echo "$*"; }
fail() { echo "MOOBFS_APT_FAIL: $*"; exit 1; }
export DEBIAN_FRONTEND=noninteractive

# source <repo> [signed-by]: the deb822 source the guide gives, pointing at one copy
source_to() {
	{
		echo "Types: deb"
		echo "URIs: $REPO/$1/"
		echo "Suites: experimental"
		echo "Components: main"
		[ "${2:-yes}" = no ] || echo "Signed-By: $KEYRING"
	} >"$SRC"
}
versions() {   # versions <v>: both packages at <v>, DKMS module installed for the kernel
	[ "$(dpkg-query -W -f '${Version}' moobfs-dkms)" = "$1" ] || fail "moobfs-dkms is not $1"
	[ "$(dpkg-query -W -f '${Version}' moobfs-tools)" = "$1" ] || fail "moobfs-tools is not $1"
	dkms status moobfs | grep -q "^moobfs/$1, $KVER, x86_64: installed" ||
		fail "dkms status: $(dkms status moobfs)"
}

[ "${1:-}" = 0 ] || fail "unknown phase ${1:-}"
[ "$(uname -r)" = "$KVER" ] || fail "kernel $(uname -r), not $KVER"
apt-get update >/dev/null
apt-get install -y --no-install-recommends curl "linux-headers-$KVER" >/dev/null 2>&1 ||
	fail "headers for $KVER"

# 1. The guide: the key into /usr/share/keyrings, a deb822 source naming it, apt install.
curl -fsS -o "$KEYRING" "$REPO/key-a.pgp"
for k in /etc/apt/trusted.gpg /etc/apt/trusted.gpg.d/*; do
	case $k in *[Mm]oob*) fail "a MoobFS key is trusted globally: $k" ;; esac
done
source_to good1
apt-get update >/tmp/u.log 2>&1 || { cat /tmp/u.log; fail "apt-get update on good1"; }
apt-get install -y moobfs-dkms moobfs-tools >/tmp/i.log 2>&1 || { tail -n 30 /tmp/i.log; fail "apt-get install"; }
versions "$V1"
modprobe moobfs_layer || fail "modprobe after apt install"
rmmod moobfs_layer
mark "APT_INSTALL: moobfs-dkms and moobfs-tools $V1 from good1 with Signed-By; DKMS built the module for $KVER; it loads"

# 2. Upgrade through apt, then roll back to the older version, which stays in the pool.
source_to good2
apt-get update >/tmp/u.log 2>&1 || { cat /tmp/u.log; fail "apt-get update on good2"; }
apt-get install -y --only-upgrade moobfs-dkms moobfs-tools >/tmp/i.log 2>&1 || { tail -n 30 /tmp/i.log; fail "upgrade"; }
versions "$V2"
apt-get install -y --allow-downgrades "moobfs-dkms=$V1" "moobfs-tools=$V1" >/tmp/i.log 2>&1 ||
	{ tail -n 30 /tmp/i.log; fail "roll back"; }
versions "$V1"
mark "APT_UPGRADE: $V1 -> $V2 by apt, rolled back to $V1 with apt install <package>=$V1"

# 3. An index signed by another key: apt update fails and keeps nothing of it.
source_to otherkey
rm -rf /var/lib/apt/lists/10.0.2.2*
if apt-get update >/tmp/u.log 2>&1; then cat /tmp/u.log; fail "apt-get update accepted an index signed by another key"; fi
grep -q -E 'NO_PUBKEY|not signed|Missing key|signatures were invalid' /tmp/u.log || { cat /tmp/u.log; fail "otherkey refused for another reason"; }
apt-cache policy moobfs-tools | grep -q "$REPO/otherkey" && fail "otherkey's packages are candidates"
mark "APT_REFUSED_OTHER_KEY: $(grep -m1 -o -E 'NO_PUBKEY [0-9A-F]+|not signed|Missing key [0-9A-F]+' /tmp/u.log)"

# 4. A package changed after signing: apt downloads it and refuses it.
source_to tampered
rm -rf /var/lib/apt/lists/10.0.2.2*
apt-get update >/tmp/u.log 2>&1 || { cat /tmp/u.log; fail "apt-get update on tampered (its index is good)"; }
apt-get clean
if apt-get install -y "moobfs-tools=$V2" >/tmp/i.log 2>&1; then tail -n 20 /tmp/i.log; fail "a changed package was installed"; fi
grep -q -E 'Hash Sum mismatch|hashes do not match|Hashes of expected file' /tmp/i.log || { tail -n 20 /tmp/i.log; fail "tampered refused for another reason"; }
versions "$V1"
mark "APT_REFUSED_TAMPERED: moobfs-tools $V2 with one byte changed refused (hash mismatch); $V1 still installed"

# 5. A source without Signed-By: the key is not trusted for other sources, so apt refuses.
source_to good2 no
rm -rf /var/lib/apt/lists/10.0.2.2*
if apt-get update >/tmp/u.log 2>&1; then cat /tmp/u.log; fail "apt-get update accepted good2 without Signed-By"; fi
grep -q -E 'NO_PUBKEY|not signed|Missing key' /tmp/u.log || { cat /tmp/u.log; fail "no Signed-By refused for another reason"; }
mark "APT_REFUSED_NO_SIGNED_BY: the key in $KEYRING is trusted only where Signed-By names it"

# 6. Purge: the packages, the module and DKMS's tree go; the source and key are the user's.
source_to good2
apt-get purge -y moobfs-dkms moobfs-tools >/tmp/i.log 2>&1 || { tail -n 20 /tmp/i.log; fail "purge"; }
[ -z "$(dkms status moobfs)" ] || fail "dkms still lists moobfs: $(dkms status moobfs)"
modinfo -k "$KVER" moobfs_layer >/dev/null 2>&1 && fail "a moobfs_layer module is still installed"
rm -f "$SRC" "$KEYRING"
mark "APT_PURGED: packages and module removed; source and key removed by hand"
mark "PHASE_DONE: 0"
