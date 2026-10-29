#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Roadmap item C2: follow docs/operations.md in a disposable Debian 13 VM (the official
# generic cloud image, which boots the distribution's amd64 kernel). Needs only Docker.
# The tracked and new (not ignored) files of the working tree are copied into the guest;
# nothing on the host is installed or loaded. Log: build/install-test/log.txt; final marker MOOBFS_INSTALL_PASS.
set -eu
cd "$(dirname "$0")/../.."
# MOOB_TEST=container runs the container recipe instead (roadmap C3, build/container-test/)
out=build/install-test
[ "${MOOB_TEST:-install}" = container ] && out=build/container-test
[ "${MOOB_TEST:-install}" = package ] && out=build/package-test
# MOOB_TEST=bench runs bench/ondemand.sh on a lower of MOOB_BENCH_FS (journal 0238, build/bench-test/)
[ "${MOOB_TEST:-install}" = bench ] && out=build/bench-test
mkdir -p "$out"
# --no-xattrs: macOS tar would otherwise store its provenance attribute in every entry
git ls-files -z --cached --others --exclude-standard |
	COPYFILE_DISABLE=1 tar --no-xattrs --null -T - -cf "$out/src.tar"
# MOOB_HOST_QEMU=1: run QEMU on this Linux host (KVM when /dev/kvm is writable) instead of in the
# container; needs qemu-system-x86, qemu-utils, openssh-client, python3 and curl. Nothing of
# MoobFS is installed or loaded on the host either way.
if [ "${MOOB_HOST_QEMU:-}" = 1 ]; then
	MOOB_WORK=$PWD MOOB_TEST="${MOOB_TEST:-install}" MOOB_BENCH_FS="${MOOB_BENCH_FS:-ext4}" exec sh vm/install-test/in-container.sh
fi
docker build --platform linux/amd64 -t moobfs-install-test -f vm/install-test/Dockerfile vm/install-test
# KVM inside the container when the host has it (the dedicated Linux host); TCG otherwise
KVM=
[ ! -w /dev/kvm ] || KVM=--device=/dev/kvm
# shellcheck disable=SC2086   # $KVM is empty or one option
docker run --rm --platform linux/amd64 $KVM -e MOOB_COMMIT="${MOOB_COMMIT:-$(git rev-parse --short HEAD)}" -e MOOB_TEST="${MOOB_TEST:-install}" -e MOOB_BENCH_FS="${MOOB_BENCH_FS:-ext4}" -v "$PWD:/work" moobfs-install-test sh vm/install-test/in-container.sh
