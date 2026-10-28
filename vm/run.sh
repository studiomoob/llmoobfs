#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/.."
MOOB_DEBIAN_KERNEL=${MOOB_DEBIAN_KERNEL:-6.12.111}
docker build --platform linux/amd64 --build-arg "MOOB_DEBIAN_KERNEL=$MOOB_DEBIAN_KERNEL" -t moobfs-m1-test -f vm/Dockerfile .
# MOOB_ONLY="btrfs powerloss" make vm-test runs only those suite steps (vm/suite.sh)
# MOOB_VM_KVM=1 gives the container /dev/kvm and the guest runs under KVM instead of TCG
set --
if [ "${MOOB_VM_KVM:-}" = 1 ]; then
    [ -w /dev/kvm ] || { echo "MOOB_VM_KVM=1 needs a writable /dev/kvm" >&2; exit 1; }
    set -- --device /dev/kvm
fi
docker run --rm --platform linux/amd64 "$@" -e MOOB_DEBIAN_KERNEL="$MOOB_DEBIAN_KERNEL" -e MOOB_ONLY="${MOOB_ONLY:-}" -e MOOB_VM_KVM="${MOOB_VM_KVM:-}" -v "$PWD:/work" moobfs-m1-test sh vm/in-container.sh
