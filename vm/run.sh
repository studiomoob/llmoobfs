#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/.."
MOOB_DEBIAN_KERNEL=${MOOB_DEBIAN_KERNEL:-6.12.111}
# Debian's 7.3.3 is in sid only on 2026-11-08 (forky has 7.2.15): its headers need sid's compiler
case "$MOOB_DEBIAN_KERNEL" in
    7.3.3) MOOB_BASE=debian:sid-slim@sha256:07a3205e8583fdff12d2b05bd030f50437e9fd340ee616df0c05dc77af75767b ;;
    *) MOOB_BASE=debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f ;;
esac
# MOOB_ARCH=s390x runs the suite in a big-endian s390x guest on an s390x host; exploratory, not
# a supported kernel (docs/compatibility.md, journal 0280). The default, amd64, is unchanged.
MOOB_ARCH=${MOOB_ARCH:-amd64}
case "$MOOB_ARCH" in
    amd64) TAG=moobfs-m1-test ;;
    s390x) TAG=moobfs-m1-test-s390x ;;
    *) echo "Unsupported MOOB_ARCH: $MOOB_ARCH" >&2; exit 1 ;;
esac
docker build --platform "linux/$MOOB_ARCH" --build-arg "MOOB_ARCH=$MOOB_ARCH" --build-arg "MOOB_BASE=$MOOB_BASE" --build-arg "MOOB_DEBIAN_KERNEL=$MOOB_DEBIAN_KERNEL" -t "$TAG" -f vm/Dockerfile .
# MOOB_ONLY="btrfs powerloss" make vm-test runs only those suite steps (vm/suite.sh)
# MOOB_PERMCHURN=SEEDS,STEPS[,FIRST] lengthens the permchurn step (vm/suite.sh, journal 0250)
# MOOB_SELFCHECK=1 runs the suite with the index self-check on (S16, journal 0271)
# MOOB_CROSS=DIR with MOOB_ONLY=crossmachine reads what vm/cross-machine.sh wrote elsewhere (0280)
# MOOB_VM_KVM=1 gives the container /dev/kvm and the guest runs under KVM instead of TCG
set --
if [ "${MOOB_VM_KVM:-}" = 1 ]; then
    [ -w /dev/kvm ] || { echo "MOOB_VM_KVM=1 needs a writable /dev/kvm" >&2; exit 1; }
    set -- --device /dev/kvm
fi
docker run --rm --platform "linux/$MOOB_ARCH" "$@" -e MOOB_ARCH="$MOOB_ARCH" -e MOOB_DEBIAN_KERNEL="$MOOB_DEBIAN_KERNEL" -e MOOB_ONLY="${MOOB_ONLY:-}" -e MOOB_PERMCHURN="${MOOB_PERMCHURN:-}" -e MOOB_SELFCHECK="${MOOB_SELFCHECK:-}" -e MOOB_VM_KVM="${MOOB_VM_KVM:-}" -e MOOB_CROSS="${MOOB_CROSS:-}" -v "$PWD:/work" "$TAG" sh vm/in-container.sh
