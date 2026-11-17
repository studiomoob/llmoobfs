#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# ADR-0041: the maintainer's one step, on the maintainer's own machine, where the key is. Signs
# the apt repository's Release as InRelease, after checking that Release gives the hashes of the
# Packages next to it and that every package hash in Packages is one of the expected ones.
# Needs gpg. Nothing leaves the machine; the signed files are then committed by pull request.
#
#   sh scripts/apt-sign.sh DIST FINGERPRINT EXPECTED
#
# DIST is the suite's directory (site/apt/dists/experimental); FINGERPRINT the key's full
# fingerprint (SECURITY.md); EXPECTED a file of "<sha256> <file>" lines, the MOOBFS_APT_DEB lines
# of the two builds (scripts/apt-debs.sh on the test host and in CI), which must agree. An
# existing InRelease is not replaced without FORCE=1 (AGENTS.md).
set -eu
die() { echo "apt-sign: $*" >&2; exit 1; }
[ $# -eq 3 ] || die "usage: sh scripts/apt-sign.sh DIST FINGERPRINT EXPECTED"
DIST=$1 FPR=$2 EXP=$3
command -v gpg >/dev/null 2>&1 || die "gpg not found"
case $FPR in *[!0-9A-F]* | '') die "fingerprint must be 40 upper-case hex digits" ;; esac
[ ${#FPR} -eq 40 ] || die "fingerprint must be 40 upper-case hex digits"
sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
rel=$DIST/Release pk=$DIST/main/binary-amd64/Packages
[ -f "$rel" ] && [ -f "$pk" ] || die "no Release or Packages under $DIST"
if [ -e "$DIST/InRelease" ] && [ "${FORCE:-}" != 1 ]; then die "$DIST/InRelease exists; FORCE=1 to replace it"; fi
# Release's SHA256 entry for Packages
want=$(awk '/^SHA256:/ { on = 1; next } /^[^ ]/ { on = 0 } on && $3 == "main/binary-amd64/Packages" { print $1 }' "$rel")
[ "$want" = "$(sha256 "$pk")" ] || die "Release does not give the SHA-256 of Packages"
# every package in Packages must be an expected build
n=0
for h in $(sed -n 's/^SHA256: //p' "$pk"); do
	f=$(awk -v h="$h" '$1 == h { print $2 }' "$EXP")
	[ -n "$f" ] || die "a package in Packages ($h) is not in $EXP"
	echo "apt-sign: expected build $h $f"
	n=$((n + 1))
done
[ "$n" -gt 0 ] || die "Packages lists no package"
gpg --list-secret-keys "$FPR" >/dev/null 2>&1 || die "no secret key $FPR in this keyring"
echo "apt-sign: signing $rel with $FPR ($n packages)"
gpg --batch --yes --local-user "$FPR!" --digest-algo SHA512 --clearsign -o "$DIST/InRelease.new" "$rel"
gpg --batch --status-fd 1 --verify "$DIST/InRelease.new" 2>/dev/null | grep -q "^\[GNUPG:\] VALIDSIG $FPR " ||
	die "the new InRelease does not verify with $FPR"
mv "$DIST/InRelease.new" "$DIST/InRelease"
echo "apt-sign: wrote $DIST/InRelease"
