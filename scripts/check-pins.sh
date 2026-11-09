#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Prints every pinned version the builds and tests depend on, checks that the copies of a pin
# agree with each other, and verifies the upstream kernel tarball's SHA-256 against kernel.org's
# published list (issue #14). Run from the repository root; needs curl for the last check
# (--offline skips it). How to update the pins together: docs/development.md, "Pinned versions".
set -eu
offline=0
[ "${1:-}" = "--offline" ] && offline=1
fail=0
bad() { echo "PIN MISMATCH: $*"; fail=1; }
yaml() { sed -n "s/^ *$1: *//p" "$2"; }

kver=$(yaml KERNEL_VERSION .github/workflows/kasan.yml)
ksum=$(yaml KERNEL_SHA256 .github/workflows/kasan.yml)
echo "upstream kernel (KASAN workflow):  linux-$kver  sha256 $ksum"
# the 7.3 leg of the KASAN workflow (journal 0277); it has no copies elsewhere
k73ver=$(yaml KERNEL73_VERSION .github/workflows/kasan.yml)
k73sum=$(yaml KERNEL73_SHA256 .github/workflows/kasan.yml)
echo "upstream kernel (KASAN, 7.3 leg):   linux-$k73ver  sha256 $k73sum"
case "$k73ver" in 7.3.*) ;; *) bad "kasan.yml KERNEL73_VERSION ($k73ver) is not a 7.3 release" ;; esac

# copies of the upstream kernel pin
[ "$(yaml KERNEL_VERSION .github/workflows/kasan-arm64.yml)" = "$kver" ] || bad "kasan-arm64.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/kasan-arm64.yml)" = "$ksum" ] || bad "kasan-arm64.yml KERNEL_SHA256 differs from kasan.yml"
[ "$(yaml KERNEL_VERSION .github/workflows/kcsan.yml)" = "$kver" ] || bad "kcsan.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/kcsan.yml)" = "$ksum" ] || bad "kcsan.yml KERNEL_SHA256 differs from kasan.yml"
[ "$(yaml KERNEL_VERSION .github/workflows/interop.yml)" = "$kver" ] || bad "interop.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/interop.yml)" = "$ksum" ] || bad "interop.yml KERNEL_SHA256 differs from kasan.yml"
[ "$(yaml KERNEL_VERSION .github/workflows/coverage.yml)" = "$kver" ] || bad "coverage.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/coverage.yml)" = "$ksum" ] || bad "coverage.yml KERNEL_SHA256 differs from kasan.yml"
dver=$(sed -n 's/^KERNEL_VERSION=//p' docs/development.md | head -n 1)
dsum=$(sed -n 's/^KERNEL_SHA256=//p' docs/development.md | head -n 1)
[ "$dver" = "$kver" ] || bad "docs/development.md KERNEL_VERSION ($dver) differs from kasan.yml ($kver)"
[ "$dsum" = "$ksum" ] || bad "docs/development.md KERNEL_SHA256 differs from kasan.yml"
ctag=$(sed -n 's/^TAG=//p' scripts/checkpatch.sh)
echo "checkpatch.pl (scripts/checkpatch.sh): $ctag"
[ "$ctag" = "v$kver" ] || bad "scripts/checkpatch.sh TAG ($ctag) differs from the KASAN kernel (v$kver)"

# the Debian image and kernel package of make vm-test
# vm/Dockerfile takes its base from vm/run.sh (MOOB_BASE: trixie for 6.12, sid for 7.3.3, journal
# 0275); its ARG default is the trixie image
vbase=$(sed -n 's/^ARG MOOB_BASE=//p' vm/Dockerfile | head -n 1)
rtrixie=$(sed -n 's/^ *\*) MOOB_BASE=\(debian:trixie[^ ;]*\) *;;.*/\1/p' vm/run.sh)
rsid=$(sed -n 's/^ *7\.3\.3) MOOB_BASE=\(debian:sid[^ ;]*\) *;;.*/\1/p' vm/run.sh)
echo "Debian image (vm/Dockerfile):       $vbase"
echo "Debian image for 7.3.3 (vm/run.sh): $rsid"
[ -n "$rsid" ] || bad "vm/run.sh has no sid base image for 7.3.3"
[ "$rtrixie" = "$vbase" ] || bad "vm/run.sh trixie base ($rtrixie) differs from vm/Dockerfile's default ($vbase)"
echo "Debian kernel package:              $(grep -o 'linux-image-[^ ]*' vm/Dockerfile | head -n 1)"
img=$(grep -o 'linux-image-[0-9.]*+deb13-amd64=[0-9.-]*' vm/Dockerfile | head -n 1)
hdr=$(grep -o 'linux-headers-[0-9.]*+deb13-amd64=[0-9.-]*' vm/Dockerfile | head -n 1)
[ "${img#linux-image-}" = "${hdr#linux-headers-}" ] || bad "vm/Dockerfile kernel image ($img) and headers ($hdr) differ"

# the image of make static (journal 0242): same upstream kernel, base image and Debian kernel
sarg() { sed -n "s/^ARG $1=//p" tests/static/Dockerfile; }
[ "$(sarg KERNEL_VERSION)" = "$kver" ] || bad "tests/static/Dockerfile KERNEL_VERSION differs from kasan.yml"
[ "$(sarg KERNEL_SHA256)" = "$ksum" ] || bad "tests/static/Dockerfile KERNEL_SHA256 differs from kasan.yml"
[ "$(sed -n 's/^FROM //p' tests/static/Dockerfile)" = "$vbase" ] ||
	bad "tests/static/Dockerfile and vm/Dockerfile use different base images"
grep -q "linux-headers-$(sarg DEBIAN_KERNEL)+deb13-amd64=" vm/Dockerfile ||
	bad "tests/static/Dockerfile DEBIAN_KERNEL ($(sarg DEBIAN_KERNEL)) is not a kernel of vm/Dockerfile"
echo "static analysis (tests/static/Dockerfile): Debian headers $(sarg DEBIAN_KERNEL), smatch $(sarg SMATCH_COMMIT)"
echo "  $(grep -o 'sparse=[^ ]*' tests/static/Dockerfile) $(grep -o 'coccinelle=[^ ]*' tests/static/Dockerfile)"

# virtme-ng for the KASAN harness
echo "virtme-ng (vm/kasan-requirements.txt): $(sed -n 's/^virtme-ng==\([^ ]*\).*/\1/p' vm/kasan-requirements.txt)"

# the tools of the interoperability test (journal 0140)
for p in FSELECT_VERSION CSHATAG_VERSION RECOLL_DEB KFMD_DEB; do
	echo "interop.yml $p: $(yaml "$p" .github/workflows/interop.yml)"
done

# the tarballs against kernel.org's signed lists of sums (the signature itself is not checked here)
if [ "$offline" -eq 0 ]; then
	for pin in "$kver $ksum" "$k73ver $k73sum"; do
		v=${pin% *}
		sum=${pin#* }
		major=${v%%.*}
		list=$(curl -fsSL "https://cdn.kernel.org/pub/linux/kernel/v$major.x/sha256sums.asc") || { echo "cannot fetch kernel.org sha256sums.asc"; exit 2; }
		up=$(printf '%s\n' "$list" | sed -n "s/^\([0-9a-f]\{64\}\)  linux-$v\.tar\.xz$/\1/p")
		if [ -z "$up" ]; then
			bad "linux-$v.tar.xz is not in kernel.org's sha256sums.asc"
		elif [ "$up" != "$sum" ]; then
			bad "kernel.org lists $up for linux-$v.tar.xz, the workflows pin $sum"
		else
			echo "kernel.org sha256sums.asc agrees with the pinned SHA-256 of linux-$v"
		fi
	done
fi
[ "$fail" -eq 0 ] && echo "PINS_OK"
exit "$fail"
