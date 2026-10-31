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

# copies of the upstream kernel pin
[ "$(yaml KERNEL_VERSION .github/workflows/kasan-arm64.yml)" = "$kver" ] || bad "kasan-arm64.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/kasan-arm64.yml)" = "$ksum" ] || bad "kasan-arm64.yml KERNEL_SHA256 differs from kasan.yml"
[ "$(yaml KERNEL_VERSION .github/workflows/kcsan.yml)" = "$kver" ] || bad "kcsan.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/kcsan.yml)" = "$ksum" ] || bad "kcsan.yml KERNEL_SHA256 differs from kasan.yml"
[ "$(yaml KERNEL_VERSION .github/workflows/interop.yml)" = "$kver" ] || bad "interop.yml KERNEL_VERSION differs from kasan.yml"
[ "$(yaml KERNEL_SHA256 .github/workflows/interop.yml)" = "$ksum" ] || bad "interop.yml KERNEL_SHA256 differs from kasan.yml"
dver=$(sed -n 's/^KERNEL_VERSION=//p' docs/development.md | head -n 1)
dsum=$(sed -n 's/^KERNEL_SHA256=//p' docs/development.md | head -n 1)
[ "$dver" = "$kver" ] || bad "docs/development.md KERNEL_VERSION ($dver) differs from kasan.yml ($kver)"
[ "$dsum" = "$ksum" ] || bad "docs/development.md KERNEL_SHA256 differs from kasan.yml"
ctag=$(sed -n 's/^TAG=//p' scripts/checkpatch.sh)
echo "checkpatch.pl (scripts/checkpatch.sh): $ctag"
[ "$ctag" = "v$kver" ] || bad "scripts/checkpatch.sh TAG ($ctag) differs from the KASAN kernel (v$kver)"

# the Debian image and kernel package of make vm-test
echo "Debian image (vm/Dockerfile):       $(sed -n 's/^FROM //p' vm/Dockerfile | head -n 1)"
echo "Debian kernel package:              $(grep -o 'linux-image-[^ ]*' vm/Dockerfile | head -n 1)"
img=$(grep -o 'linux-image-[0-9.]*+deb13-amd64=[0-9.-]*' vm/Dockerfile | head -n 1)
hdr=$(grep -o 'linux-headers-[0-9.]*+deb13-amd64=[0-9.-]*' vm/Dockerfile | head -n 1)
[ "${img#linux-image-}" = "${hdr#linux-headers-}" ] || bad "vm/Dockerfile kernel image ($img) and headers ($hdr) differ"

# the image of make static (journal 0242): same upstream kernel, base image and Debian kernel
sarg() { sed -n "s/^ARG $1=//p" tests/static/Dockerfile; }
[ "$(sarg KERNEL_VERSION)" = "$kver" ] || bad "tests/static/Dockerfile KERNEL_VERSION differs from kasan.yml"
[ "$(sarg KERNEL_SHA256)" = "$ksum" ] || bad "tests/static/Dockerfile KERNEL_SHA256 differs from kasan.yml"
[ "$(sed -n 's/^FROM //p' tests/static/Dockerfile)" = "$(sed -n 's/^FROM //p' vm/Dockerfile | head -n 1)" ] ||
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

# the tarball against kernel.org's signed list of sums (the signature itself is not checked here)
if [ "$offline" -eq 0 ]; then
	major=${kver%%.*}
	list=$(curl -fsSL "https://cdn.kernel.org/pub/linux/kernel/v$major.x/sha256sums.asc") || { echo "cannot fetch kernel.org sha256sums.asc"; exit 2; }
	up=$(printf '%s\n' "$list" | sed -n "s/^\([0-9a-f]\{64\}\)  linux-$kver\.tar\.xz$/\1/p")
	if [ -z "$up" ]; then
		bad "linux-$kver.tar.xz is not in kernel.org's sha256sums.asc"
	elif [ "$up" != "$ksum" ]; then
		bad "kernel.org lists $up for linux-$kver.tar.xz, the workflows pin $ksum"
	else
		echo "kernel.org sha256sums.asc agrees with the pinned SHA-256"
	fi
fi
[ "$fail" -eq 0 ] && echo "PINS_OK"
exit "$fail"
