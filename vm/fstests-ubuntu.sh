#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# fstests over the layer on Ubuntu 26.04's own kernel (7.0.0-43-generic, journal 0281) from a
# host that runs another distribution: vm/fstests-local.sh runs inside the image of
# vm/fstests-ubuntu/Dockerfile, with /dev/kvm, and boots the image's kernel with MOOB_KVER.
#
#   sh vm/fstests-ubuntu.sh FSTESTS_SRC LOWERFS [check arguments...]
#
# FSTESTS_SRC is a checkout of xfstests-dev at the tag of vm/fstests/README.md; it is mounted
# read-only, copied and rebuilt inside the container against Ubuntu's libraries. Run from the
# repository root on a host with Docker and a writable /dev/kvm. The default arguments are those
# of the rule-3 runs: -g quick with vm/fstests/exclude-quick.txt, and on btrfs also
# exclude-quick-btrfs.txt (journal 0277). MOOB_FSTESTS_CPUS and MOOB_FSTESTS_MEM pass through;
# MOOB_DOCKER_CPUS (for example 2,3) pins the container. Runs on several lowers can share a host
# when each uses its own copy of the repository (build/fstests is per copy).
set -eu
SRC=${1:?usage: vm/fstests-ubuntu.sh FSTESTS_SRC LOWERFS [check arguments...]}
LOWERFS=${2:?usage: vm/fstests-ubuntu.sh FSTESTS_SRC LOWERFS [check arguments...]}
shift 2
if [ $# -eq 0 ]; then
    set -- -g quick -E /work/vm/fstests/exclude-quick.txt
    [ "$LOWERFS" != btrfs ] || set -- "$@" -E /work/vm/fstests/exclude-quick-btrfs.txt
fi
[ -x "$SRC/check" ] || { echo "fstests-ubuntu: $SRC is not an fstests checkout" >&2; exit 1; }
[ -w /dev/kvm ] || { echo "fstests-ubuntu: needs a writable /dev/kvm" >&2; exit 1; }
SRC=$(cd "$SRC" && pwd)
TAG=moobfs-fstests-ubuntu
docker build -q -t "$TAG" vm/fstests-ubuntu >/dev/null
# shellcheck disable=SC2016  # expanded by the container's shell
docker run --rm --device /dev/kvm ${MOOB_DOCKER_CPUS:+--cpuset-cpus "$MOOB_DOCKER_CPUS"} \
    -e MOOB_KVER=7.0.0-43-generic -e MOOB_FSTESTS_CPUS="${MOOB_FSTESTS_CPUS:-}" \
    -e MOOB_FSTESTS_MEM="${MOOB_FSTESTS_MEM:-}" -v "$SRC:/fstests-src:ro" -v "$PWD:/work" "$TAG" \
    sh -euc 'cp -a /fstests-src /tmp/fstests
        make -C /tmp/fstests realclean >/dev/null 2>&1 || make -C /tmp/fstests clean >/dev/null 2>&1 || true
        make -C /tmp/fstests -j"$(nproc)" >/tmp/fstests-build.log 2>&1 || { tail -30 /tmp/fstests-build.log; exit 1; }
        exec sh vm/fstests-local.sh /tmp/fstests "$@"' fstests-ubuntu "$LOWERFS" "$@"
