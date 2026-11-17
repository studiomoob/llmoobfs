#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# ADR-0041: builds the moobfs-dkms and moobfs-tools packages of a tag for the apt repository, in
# the pinned debian:trixie-slim image with the Debian archive pinned to a snapshot.debian.org
# time, so that the same tag and time give the same packages on any host and at any later date.
# Needs Docker and git with the tag. Nothing is installed on the host.
#
#   sh scripts/apt-debs.sh TAG SNAPSHOT OUTDIR
#
# SNAPSHOT is a snapshot.debian.org time, YYYYMMDDTHHMMSSZ; site/apt/builds lists the one each
# published version was built with. The source is `git archive TAG`; SOURCE_DATE_EPOCH is the
# tag's commit time and MOOB_COMMIT its 12-digit abbreviation, so neither depends on the clone.
# OUTDIR gets the two packages and packages.txt (the build's toolchain); it must not exist
# (AGENTS.md). Prints MOOBFS_APT_DEB: <sha256> <file> per package.
set -eu
IMAGE=debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

if [ "${1:-}" = --in-container ]; then
	# /src.tar is the tag's tree (read-only); /out this build's output directory
	export DEBIAN_FRONTEND=noninteractive
	rm -f /etc/apt/sources.list.d/debian.sources
	# snapshot.debian.org serves old Release files whose signatures still verify; check-valid-until
	# is off because a pinned time is old on purpose. Plain HTTP, as the image's own sources: the
	# slim image has no CA certificates, and apt checks the archive's signature either way.
	echo "deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/$MOOB_SNAPSHOT trixie main" \
		>/etc/apt/sources.list
	{ apt-get -o Acquire::Retries=5 update &&
		apt-get -o Acquire::Retries=5 install -y --no-install-recommends build-essential debhelper dh-dkms; } \
		>/out/apt.log 2>&1 || { tail -n 20 /out/apt.log; exit 1; }
	dpkg-query -W -f '${Package} ${Version}\n' | sort >/out/packages.txt
	mkdir -p /build/moobfs /out/deb
	tar -xf /src.tar -C /build/moobfs
	chown -R 4242:4242 /build /out/deb
	(cd /build/moobfs && setpriv --reuid=4242 --regid=4242 --clear-groups -- \
		env HOME=/tmp sh scripts/build-debs.sh "$MOOB_DEB_VERSION" /out/deb) >/out/deb.log 2>&1 ||
		{ tail -n 40 /out/deb.log; exit 1; }
	exit 0
fi

cd "$(dirname "$0")/.."
die() { echo "MOOBFS_APT_FAIL: $*" >&2; exit 1; }
[ $# -eq 3 ] || die "usage: sh scripts/apt-debs.sh TAG SNAPSHOT OUTDIR"
TAG=$1 SNAP=$2 OUT=$3
command -v docker >/dev/null 2>&1 || die "docker not found"
case $SNAP in
[0-9][0-9][0-9][0-9][0-1][0-9][0-3][0-9]T[0-2][0-9][0-5][0-9][0-5][0-9]Z) ;;
*) die "snapshot '$SNAP' is not YYYYMMDDTHHMMSSZ" ;;
esac
commit=$(git rev-parse --verify -q "$TAG^{commit}") || die "no tag $TAG"
if [ -e "$OUT" ] || [ -L "$OUT" ]; then die "$OUT exists"; fi
epoch=$(git log -1 --format=%ct "$commit")
short=$(git rev-parse --short=12 "$commit")
version=$(git show "$commit:layer/main.c" | sed -n 's/^MODULE_VERSION("\(.*\)");$/\1/p')
[ -n "$version" ] || die "no MODULE_VERSION in $TAG"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
git archive --format=tar -o "$OUT/src.tar" "$commit"
echo "apt-debs: $TAG ($short), version $version, SOURCE_DATE_EPOCH $epoch, snapshot $SNAP"
docker run --rm --platform linux/amd64 \
	-e SOURCE_DATE_EPOCH="$epoch" -e MOOB_COMMIT="$short" -e MOOB_DEB_VERSION="$version" \
	-e MOOB_SNAPSHOT="$SNAP" \
	-v "$OUT/src.tar:/src.tar:ro" -v "$OUT:/out" -v "$PWD/scripts/apt-debs.sh:/apt-debs.sh:ro" \
	"$IMAGE" sh /apt-debs.sh --in-container || die "build failed; logs in $OUT"
docker run --rm -v "$OUT:/o" "$IMAGE" chown -R "$(id -u):$(id -g)" /o
rm -f "$OUT/src.tar"
mv "$OUT"/deb/*.deb "$OUT"/ && rmdir "$OUT/deb"
for deb in "$OUT"/*.deb; do
	[ -e "$deb" ] || die "no package built"
	echo "MOOBFS_APT_DEB: $(sha256sum "$deb" | cut -d' ' -f1) $(basename "$deb")"
done
