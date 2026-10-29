#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Security roadmap S10, second item (journal 0236): builds layer/moobfs_layer.ko twice for
# each kernel of scripts/supported-kernels, in two fresh debian:trixie-slim containers pinned
# by digest, and requires byte-identical modules. The two builds differ in build path, user,
# host name, time zone, umask and wall-clock time; each also builds with the MAKE line of the
# DKMS package in DKMS's build directory, which must give the same module. With MOOB_REPRO_DEBS=1 (the default) the
# two Debian packages of scripts/build-debs.sh are built in the same containers and compared
# too. Needs Docker on a Linux host; nothing is installed on the host, no module is loaded.
#
#   sh scripts/repro-ko.sh            (or: make repro-test)
#
# Prints MOOBFS_REPRO_PASS: <sha256> <file> <kernel> per module and MOOBFS_REPRO_DEB_PASS:
# <sha256> <file> per package, or MOOBFS_REPRO_FAIL and exits 1. The outputs stay in
# MOOB_REPRO_OUT (default build/repro), which is not replaced without FORCE=1 (AGENTS.md).
# SOURCE_DATE_EPOCH and MOOB_COMMIT default to the commit of HEAD; set both where git is
# missing. The recipe each container runs is the in-container branch below.
set -eu
IMAGE=debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

# Debian package version of the headers of each listed kernel release (as in vm/Dockerfile).
headers_version() {
	case $1 in
	6.12.111+deb13-amd64) echo 6.12.111-1 ;;
	6.12.113+deb13-amd64) echo 6.12.113-1 ;;
	*) return 1 ;;
	esac
}

if [ "${1:-}" = --in-container ]; then
	# $2 = build directory, $3 = uid to build as; /src is the source copy (read-only),
	# /out this run's output directory. The rest comes from the environment.
	dir=$2
	uid=$3
	export DEBIAN_FRONTEND=noninteractive
	pkgs="gcc make libc6-dev openssl"
	for k in $MOOB_KERNELS; do pkgs="$pkgs linux-headers-$k=$(headers_version "$k")"; done
	[ "$MOOB_REPRO_DEBS" = 1 ] && pkgs="$pkgs build-essential debhelper dh-dkms"
	# shellcheck disable=SC2086 # one word per package
	{ apt-get update && apt-get install -y --no-install-recommends $pkgs; } >/out/apt.log 2>&1 ||
		{ tail -n 20 /out/apt.log; exit 1; }
	# the toolchain this run used; both runs must agree before their outputs are compared
	dpkg-query -W -f '${Package} ${Version}\n' | sort >/out/packages.txt
	mkdir -p "$dir"
	cp -R /src/. "$dir/"
	chown -R "$uid:$uid" "$dir"
	as_user() { setpriv --reuid="$uid" --regid="$uid" --clear-groups -- "$@"; }
	# the DKMS build directory and command of packaging/debian/moobfs-dkms.dkms.in
	dkms_tree=/var/lib/dkms PACKAGE_NAME=moobfs PACKAGE_VERSION=$MOOB_DEB_VERSION
	dkdir=$dkms_tree/$PACKAGE_NAME/$PACKAGE_VERSION/build
	dkmake=$(sed -n 's/^MAKE\[0\]="\(.*\)"$/\1/p' /src/packaging/debian/moobfs-dkms.dkms.in)
	mkdir -p "$dkdir"
	cp -R /src/. "$dkdir/"
	chown -R "$uid:$uid" "$dkdir"
	for k in $MOOB_KERNELS; do
		kv="KBUILD_BUILD_TIMESTAMP=@$SOURCE_DATE_EPOCH KBUILD_BUILD_USER=moobfs KBUILD_BUILD_HOST=moobfs"
		# shellcheck disable=SC2086 # kv is three assignments
		(cd "$dir" && as_user env $kv make layer KDIR="/usr/src/linux-headers-$k") \
			>"/out/build-$k.log" 2>&1 || { tail -n 20 "/out/build-$k.log"; exit 1; }
		# shellcheck disable=SC2034 # read by the eval below
		kernel_source_dir=/usr/src/linux-headers-$k
		# shellcheck disable=SC2086 # kv is three assignments
		(cd "$dkdir" && eval "as_user env $kv $dkmake") >"/out/dkms-$k.log" 2>&1 ||
			{ tail -n 20 "/out/dkms-$k.log"; exit 1; }
		mkdir -p "/out/$k"
		cp "$dir/layer/moobfs_layer.ko" "/out/$k/moobfs_layer.ko"
		cp "$dkdir/layer/moobfs_layer.ko" "/out/$k/moobfs_layer.dkms.ko"
		# Signing appends a signature that depends on the key, so the unsigned module is the
		# reproducible artifact. Sign a copy with a throwaway key, as DKMS does with the
		# host's own key, and check that removing the signature gives the unsigned module back.
		if [ "$MOOB_REPRO_SIGN" = 1 ]; then
			openssl req -new -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=repro-throwaway \
				-outform DER -keyout /tmp/k.key -out /tmp/k.der >/dev/null 2>&1
			cp "/out/$k/moobfs_layer.ko" /tmp/signed.ko
			"/usr/src/linux-headers-$k/scripts/sign-file" sha256 /tmp/k.key /tmp/k.der /tmp/signed.ko
			cp /tmp/signed.ko "/out/$k/moobfs_layer.signed.ko"
		fi
		for d in "$dir" "$dkdir"; do
			as_user make -C "/usr/src/linux-headers-$k" M="$d/layer" clean >/dev/null 2>&1
		done
	done
	if [ "$MOOB_REPRO_DEBS" = 1 ]; then
		mkdir -p /out/deb
		chown "$uid:$uid" /out/deb
		(cd "$dir" && as_user env HOME=/tmp sh scripts/build-debs.sh "$MOOB_DEB_VERSION" /out/deb) \
			>/out/deb.log 2>&1 || { tail -n 40 /out/deb.log; exit 1; }
	fi
	exit 0
fi

cd "$(dirname "$0")/.."
die() { echo "MOOBFS_REPRO_FAIL: $*"; exit 1; }
command -v docker >/dev/null 2>&1 || die "docker not found"
OUT=${MOOB_REPRO_OUT:-build/repro}
if [ -e "$OUT" ] || [ -L "$OUT" ]; then
	[ "${FORCE:-}" = 1 ] || die "$OUT exists; FORCE=1 to replace it"
	docker run --rm -v "$(cd "$(dirname "$OUT")" && pwd):/p" "$IMAGE" rm -rf "/p/$(basename "$OUT")"
fi
kernels=${MOOB_KERNELS:-$(grep -v '^#' scripts/supported-kernels)}
for k in $kernels; do headers_version "$k" >/dev/null || die "no pinned headers for $k"; done
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null)} ||
	die "no git: set SOURCE_DATE_EPOCH and MOOB_COMMIT"
MOOB_COMMIT=${MOOB_COMMIT:-$(git rev-parse --short HEAD 2>/dev/null || echo unknown)}
debs=${MOOB_REPRO_DEBS:-1}
sign=${MOOB_REPRO_SIGN:-1}
version=$(sed -n 's/^MODULE_VERSION("\(.*\)");$/\1/p' layer/main.c)
mkdir -p "$OUT/src"
OUT=$(cd "$OUT" && pwd)
# the files the module and the packages are built from, as the working tree has them
tar -cf - Makefile LICENSE common include layer/*.c layer/*.h layer/Makefile tools man \
	scripts/supported-kernels scripts/build-debs.sh packaging | tar -xf - -C "$OUT/src"
echo "repro: commit $MOOB_COMMIT, SOURCE_DATE_EPOCH $SOURCE_DATE_EPOCH, image $IMAGE"

# run NAME DIR UID HOST TZ UMASK
run() {
	mkdir -p "$OUT/$1"
	echo "repro: build $1 in $2 as uid $3, host $4, TZ $5, umask $6, $(date -u +%H:%M:%SZ)"
	docker run --rm --hostname "$4" -e TZ="$5" \
		-e SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH" -e MOOB_COMMIT="$MOOB_COMMIT" \
		-e MOOB_KERNELS="$kernels" -e MOOB_REPRO_DEBS="$debs" -e MOOB_REPRO_SIGN="$sign" \
		-e MOOB_DEB_VERSION="$version" \
		-v "$OUT/src:/src:ro" -v "$OUT/$1:/out" -v "$PWD/scripts/repro-ko.sh:/repro-ko.sh:ro" \
		"$IMAGE" sh -c "umask $6 && sh /repro-ko.sh --in-container '$2' $3" ||
		die "build $1 failed; logs in $OUT/$1"
}
run a /build/moobfs 0 builder-a UTC 022
run b /var/tmp/elsewhere/a-much-longer-build-path/moobfs-src 4242 another-host JST-9 002
# outputs belong to the caller again
docker run --rm -v "$OUT:/o" "$IMAGE" chown -R "$(id -u):$(id -g)" /o

cmp -s "$OUT/a/packages.txt" "$OUT/b/packages.txt" ||
	die "the two containers installed different packages (archive moved?): diff $OUT/a/packages.txt $OUT/b/packages.txt"
echo "repro: toolchain $(grep -E '^(gcc-14|binutils|linux-kbuild-[^ ]+) ' "$OUT/a/packages.txt" | tr '\n' ' ')"
fail=0
for k in $kernels; do
	a=$OUT/a/$k/moobfs_layer.ko
	b=$OUT/b/$k/moobfs_layer.ko
	if cmp -s "$a" "$b"; then
		echo "MOOBFS_REPRO_PASS: $(sha256sum "$a" | cut -d' ' -f1) moobfs_layer.ko $k"
	else
		echo "MOOBFS_REPRO_FAIL: moobfs_layer.ko for $k differs: diffoscope $a $b"
		fail=1
		continue
	fi
	for r in a b; do
		cmp -s "$OUT/$r/$k/moobfs_layer.dkms.ko" "$a" || {
			echo "MOOBFS_REPRO_FAIL: build $r: the DKMS command gives another module for $k: diffoscope $a $OUT/$r/$k/moobfs_layer.dkms.ko"
			fail=1
		}
	done
	if [ "$sign" = 1 ]; then
		# appended signature: payload, struct module_signature (12 bytes, sig_len big-endian
		# in its last 4), then the 28-byte marker (include/linux/module_signature.h)
		s=$OUT/b/$k/moobfs_layer.signed.ko
		size=$(wc -c <"$s")
		marker=$(tail -c 28 "$s")
		siglen=$(tail -c 32 "$s" | head -c 4 | od -An -tu1 | awk '{ print (($1 * 256 + $2) * 256 + $3) * 256 + $4 }')
		head -c $((size - 28 - 12 - siglen)) "$s" >"$OUT/b/$k/stripped.ko"
		if [ "$marker" = "~Module signature appended~" ] && cmp -s "$OUT/b/$k/stripped.ko" "$a"; then
			echo "MOOBFS_REPRO_SIGNED_PAYLOAD: signed module $(sha256sum "$s" | cut -d' ' -f1), $siglen-byte signature removed, equals the unsigned module"
		else
			echo "MOOBFS_REPRO_FAIL: the signed module of $k without its signature differs from the unsigned one"
			fail=1
		fi
	fi
done
if [ "$debs" = 1 ]; then
	for deb in "$OUT"/a/deb/*.deb; do
		[ -e "$deb" ] || { echo "MOOBFS_REPRO_FAIL: no package built"; fail=1; break; }
		n=$(basename "$deb")
		if cmp -s "$deb" "$OUT/b/deb/$n"; then
			echo "MOOBFS_REPRO_DEB_PASS: $(sha256sum "$deb" | cut -d' ' -f1) $n"
		else
			echo "MOOBFS_REPRO_FAIL: $n differs: diffoscope $deb $OUT/b/deb/$n"
			fail=1
		fi
	done
fi
exit "$fail"
