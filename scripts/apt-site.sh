#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# ADR-0041: run by .github/workflows/pages.yml before the site is deployed. Checks the signed
# index under site/apt/ against the published key, builds every published version again from
# its tag (scripts/apt-debs.sh, with the snapshot site/apt/builds gives), and puts the packages
# into site/apt/pool/ only if each one's SHA-256 is the one the signed Packages gives. Any
# difference fails the deploy. Needs Docker, gpgv and git with the tags.
#
#   sh scripts/apt-site.sh
#
# site/apt/builds has one line per published version: "<version> <tag> <snapshot>". Without it
# (nothing published yet) the script does nothing. Prints MOOBFS_APT_SITE_PASS or
# MOOBFS_APT_FAIL.
set -eu
cd "$(dirname "$0")/.."
die() { echo "MOOBFS_APT_FAIL: $*" >&2; exit 1; }
A=site/apt
[ -f "$A/builds" ] || { echo "apt-site: no $A/builds, nothing published"; exit 0; }
D=$A/dists/experimental
K=$A/moobfs-archive-keyring.pgp
for f in "$K" "$D/InRelease" "$D/Release" "$D/main/binary-amd64/Packages"; do
	[ -f "$f" ] || die "$f is missing"
done
[ ! -e "$A/pool" ] || die "$A/pool exists: packages are not committed (ADR-0041)"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# 1. InRelease verifies with the published key and signs exactly the committed Release.
gpgv --keyring "$K" --output "$work/Release" "$D/InRelease" 2>"$work/gpgv.log" ||
	{ cat "$work/gpgv.log" >&2; die "InRelease does not verify with $K"; }
cmp -s "$work/Release" "$D/Release" || die "InRelease signs another Release than $D/Release"
# 2. Release gives the SHA-256 of the committed Packages.
want=$(awk '/^SHA256:/ { on = 1; next } /^[^ ]/ { on = 0 } on && $3 == "main/binary-amd64/Packages" { print $1 }' "$D/Release")
[ "$want" = "$(sha256sum "$D/main/binary-amd64/Packages" | cut -d' ' -f1)" ] ||
	die "Release does not give the SHA-256 of Packages"
# 3. Every published version again from its tag, at its snapshot.
grep -v '^#' "$A/builds" | while read -r version tag snap; do
	[ -n "$version" ] || continue
	sh scripts/apt-debs.sh "$tag" "$snap" "$work/$version" | grep '^MOOBFS_APT_DEB' ||
		die "the build of $tag failed"
done
mkdir -p "$work/pool"
cp "$work"/*/*.deb "$work/pool/"
# 4. Packages and the builds agree both ways: each listed file built with its hash, nothing extra.
awk '/^Filename: / { f = $2 } /^SHA256: / { print $2, f }' "$D/main/binary-amd64/Packages" >"$work/listed"
[ -s "$work/listed" ] || die "Packages lists no package"
while read -r h f; do
	b=$work/pool/$(basename "$f")
	[ -f "$b" ] || die "$f is listed but was not built"
	[ "$(sha256sum "$b" | cut -d' ' -f1)" = "$h" ] || die "$(basename "$f") built from its tag is not the signed one"
done <"$work/listed"
[ "$(find "$work/pool" -name '*.deb' | wc -l)" -eq "$(wc -l <"$work/listed")" ] ||
	die "a build gave a package that Packages does not list"
mkdir -p "$A/pool/main/m/moobfs"
cp "$work"/pool/*.deb "$A/pool/main/m/moobfs/"
echo "MOOBFS_APT_SITE_PASS: $(wc -l <"$work/listed" | tr -d ' ') packages built from their tags, equal to the signed index"
