#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Inside an ubuntu:26.04 container (.github/workflows/ubuntu-drift.yml, ADR-0036): installs the
# newest linux-headers-generic of today's index, checks that scripts/supported-kernels still names
# that release, and builds the layer module against it. A copy of the tree is built, so the source
# can be mounted read-only. Prints "newest Ubuntu 26.04 kernel: RELEASE" and ends in
# UBUNTU_DRIFT_PASS; any failure exits non-zero.
set -eu
# retries and a fallback mirror under another domain (journal 0285)
sh "$(dirname "$0")/apt-install.sh" -qq gcc make libc6-dev kmod linux-headers-generic
dpkg-query -W linux-headers-generic
kver=$(find /usr/src -maxdepth 1 -name 'linux-headers-*-generic' | sed 's|.*/linux-headers-||' | sort -V | tail -n 1)
[ -n "$kver" ] || { echo "ubuntu-drift: no generic headers installed" >&2; exit 1; }
echo "newest Ubuntu 26.04 kernel: $kver"
rm -rf /tmp/w
cp -r "$(dirname "$0")/.." /tmp/w
cd /tmp/w
# the same matching as make install (scripts/install.sh)
eval "$(sed -n '/^kernel_matches() {/,/^}/p; /^supported() {/,/^}/p' scripts/install.sh)"
# shellcheck disable=SC2034  # read by supported()
KVER=$kver
supported || { echo "ubuntu-drift: $kver is outside scripts/supported-kernels" >&2; exit 1; }
make layer KDIR="/usr/src/linux-headers-$kver"
modinfo -F vermagic layer/moobfs_layer.ko
echo "UBUNTU_DRIFT_PASS: layer built against $kver"
