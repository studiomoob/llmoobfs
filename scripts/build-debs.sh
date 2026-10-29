#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Roadmap item C6 (ADR-0028): builds the experimental moobfs-dkms and moobfs-tools packages
# on Debian. Needs build-essential, debhelper and dh-dkms. Nothing is installed or published.
#
#   sh scripts/build-debs.sh [VERSION [OUTDIR]]
#
# VERSION defaults to MODULE_VERSION of layer/main.c; OUTDIR to build/deb. The sources are
# copied to a temporary directory, so the tree gets no build output. Existing packages in
# OUTDIR are not replaced without FORCE=1 (AGENTS.md). MOOB_COMMIT names the commit in the
# changelog where git is missing (the VM test's container).
set -eu
cd "$(dirname "$0")/.."
die() { echo "build-debs: $*" >&2; exit 1; }
VERSION=${1:-$(sed -n 's/^MODULE_VERSION("\(.*\)");$/\1/p' layer/main.c)}
OUT=${2:-build/deb}
case $VERSION in
'' | *[!0-9A-Za-z.+~]* | [!0-9]*) die "version '$VERSION' is not a Debian native version" ;;
esac
for t in dpkg-buildpackage dh dh_dkms; do
	command -v "$t" >/dev/null 2>&1 || die "$t not found: apt install build-essential debhelper dh-dkms"
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
src=$work/moobfs-$VERSION
mkdir -p "$src"
# only what the two packages are built from; layer/ build output stays behind
tar -cf - Makefile LICENSE common include layer/*.c layer/*.h layer/Makefile tools man \
	scripts/supported-kernels | tar -xf - -C "$src"
cp -R packaging/debian "$src/debian"
when=$(git log -1 --format=%cD 2>/dev/null || date -R)
commit=${MOOB_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}
cat >"$src/debian/changelog" <<CHANGES
moobfs ($VERSION) unstable; urgency=medium

  * Experimental build of commit $commit by scripts/build-debs.sh; not published.

 -- MoobFS maintainers <claudio@studiomoob.com>  $when
CHANGES
(cd "$src" && dpkg-buildpackage -us -uc -b >"$work/build.log" 2>&1) || {
	tail -n 40 "$work/build.log" >&2
	die "dpkg-buildpackage failed"
}
mkdir -p "$OUT"
for deb in "$work"/*.deb; do
	dest=$OUT/$(basename "$deb")
	if [ "${FORCE:-}" != 1 ] && { [ -e "$dest" ] || [ -L "$dest" ]; }; then
		die "$dest exists; FORCE=1 to replace it"
	fi
	cp "$deb" "$dest"
	echo "built $dest"
done
