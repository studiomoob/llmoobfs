#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# ADR-0041: makes the apt repository's index from the packages of every published version.
# Needs apt-ftparchive (apt-utils) on Linux. Nothing is signed here: the maintainer signs
# dists/experimental/Release offline with scripts/apt-sign.sh.
#
#   sh scripts/apt-index.sh DEBDIR OUT
#
# DEBDIR holds the .deb files (every version that is to stay published); OUT, which must not
# exist (AGENTS.md), gets pool/main/m/moobfs/ with copies of them and
# dists/experimental/{Release,main/binary-amd64/Packages}. The architecture-independent
# moobfs-dkms is listed under binary-amd64, as apt-ftparchive does by default. Release is dated
# SOURCE_DATE_EPOCH when set: apt-ftparchive 3.0.3 writes the current time, so the Date line is
# replaced afterwards (Release does not hash itself). The same packages and date give the same
# index.
set -eu
die() { echo "MOOBFS_APT_FAIL: $*" >&2; exit 1; }
[ $# -eq 2 ] || die "usage: sh scripts/apt-index.sh DEBDIR OUT"
DEBS=$1 OUT=$2
command -v apt-ftparchive >/dev/null 2>&1 || die "apt-ftparchive not found (apt-utils)"
if [ -e "$OUT" ] || [ -L "$OUT" ]; then die "$OUT exists"; fi
set -- "$DEBS"/moobfs-*.deb
[ -e "$1" ] || die "no moobfs-*.deb in $DEBS"
mkdir -p "$OUT/pool/main/m/moobfs" "$OUT/dists/experimental/main/binary-amd64"
cp "$@" "$OUT/pool/main/m/moobfs/"
cd "$OUT"
apt-ftparchive -o APT::FTPArchive::MD5=false -o APT::FTPArchive::SHA1=false packages pool >dists/experimental/main/binary-amd64/Packages
cd dists/experimental
apt-ftparchive \
	-o APT::FTPArchive::MD5=false -o APT::FTPArchive::SHA1=false \
	-o APT::FTPArchive::Release::Origin=MoobFS \
	-o APT::FTPArchive::Release::Label=MoobFS \
	-o APT::FTPArchive::Release::Suite=experimental \
	-o APT::FTPArchive::Release::Codename=experimental \
	-o APT::FTPArchive::Release::Architectures=amd64 \
	-o APT::FTPArchive::Release::Components=main \
	-o "APT::FTPArchive::Release::Description=MoobFS experimental packages (ADR-0041); for disposable test hosts only" \
	release . >Release.new
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
	when=$(LC_ALL=C date -u -d "@$SOURCE_DATE_EPOCH" '+%a, %d %b %Y %H:%M:%S +0000')
	sed "s/^Date: .*/Date: $when/" Release.new >Release
	rm Release.new
else
	mv Release.new Release
fi
echo "apt-index: $(grep -c '^Package:' main/binary-amd64/Packages) packages; Release:"
cat Release
