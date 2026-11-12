#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd /work
MOOB_DEBIAN_KERNEL=${MOOB_DEBIAN_KERNEL:-6.12.111}
case "$MOOB_DEBIAN_KERNEL" in
    6.12.111|6.12.113) KERNEL="$MOOB_DEBIAN_KERNEL+deb13-amd64" ;;
    6.12.115) KERNEL="$MOOB_DEBIAN_KERNEL+deb13.1-amd64" ;;   # a trixie-security build
    7.3.3|7.3.4) KERNEL="$MOOB_DEBIAN_KERNEL+deb14-amd64" ;;  # sid, journals 0275 and 0287
    *) echo "Unsupported Debian VM kernel: $MOOB_DEBIAN_KERNEL" >&2; exit 1 ;;
esac
# MOOB_UBUNTU_KERNEL=7.0.0-43 (vm/run.sh, journal 0281) replaces the Debian kernel with Ubuntu
# 26.04's generic flavour of that ABI.
case "${MOOB_UBUNTU_KERNEL:-}" in
    '') ;;
    7.0.0-43) KERNEL="$MOOB_UBUNTU_KERNEL-generic" ;;
    *) echo "Unsupported Ubuntu VM kernel: $MOOB_UBUNTU_KERNEL" >&2; exit 1 ;;
esac
# MOOB_ARCH=s390x (vm/run.sh, journal 0280): Debian's s390x build of the same kernel, booted by
# QEMU's s390x machine with virtio-blk over CCW and the SCLP console; the default is amd64.
QEMU=qemu-system-x86_64 QEMU_PKG=qemu-system-x86 MACHINE=q35 CONSOLE=ttyS0
if [ "${MOOB_ARCH:-amd64}" = s390x ]; then
    KERNEL="${KERNEL%-amd64}-s390x" QEMU=qemu-system-s390x QEMU_PKG=qemu-system-s390x MACHINE=s390-ccw-virtio CONSOLE=ttysclp0
fi
# The M1 module (kernel/) builds on 6.12 only; on 7.x the guest runs the layer suite alone.
case "$KERNEL" in 7.*) M1=0 ;; *) M1=1 ;; esac
test -d "/lib/modules/$KERNEL/build"
make tools BUILD=build/linux
MOOB_BUILD_DIR=build/linux python3 tests/test_images.py
python3 tests/test_harness_verdicts.py
[ "$M1" = 0 ] || make module KDIR="/lib/modules/$KERNEL/build"
make layer KDIR="/lib/modules/$KERNEL/build"
mkdir -p build/vm
# Temporary work is confined to build; generated image creators remain exclusive.
STAGE=$(mktemp -d /work/build/vm/stage.XXXXXX)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/bin" "$STAGE/dev" "$STAGE/proc" "$STAGE/sys" "$STAGE/data"
cp /bin/busybox "$STAGE/bin/busybox"
ln -s busybox "$STAGE/bin/sh"
cp vm/init "$STAGE/init"
cp vm/suite.sh "$STAGE/suite.sh"
chmod +x "$STAGE/init"
[ "$M1" = 0 ] || cp kernel/moobfs.ko "$STAGE/moobfs.ko"
cp layer/moobfs_layer.ko "$STAGE/moobfs_layer.ko"
# Lower filesystems for the layer: ext4, btrfs and XFS as in-tree modules with their
# dependency chains, plus virtio_blk for the disks that carry the lower images.
mkdir -p "$STAGE/mods"
python3 vm/stage-modules.py "$KERNEL" "$STAGE/mods" "$STAGE/loop.ko"
# Lower images are sparse disks attached through virtio, not initramfs files.
rm -f build/vm/lower-ext4.img build/vm/lower-btrfs.img build/vm/lower-xfs.img
truncate -s 64M build/vm/lower-ext4.img
mke2fs -q -F -t ext4 -b 4096 -L moob-lower build/vm/lower-ext4.img
truncate -s 256M build/vm/lower-btrfs.img
mkfs.btrfs -q -f -L moob-lower build/vm/lower-btrfs.img
truncate -s 300M build/vm/lower-xfs.img
mkfs.xfs -q -f -L moob-lower build/vm/lower-xfs.img
# Blank disks for the power-loss test: one to be formatted under dm-log-writes, one for its log.
rm -f build/vm/scratch.img build/vm/log.img
truncate -s 64M build/vm/scratch.img
truncate -s 64M build/vm/log.img
# MOOB_CROSS=DIR (journal 0280): the lower disks carry the trees that vm/cross-machine.sh wrote
# on another machine, /cross its oracle and M1 image, for the step crossmachine. Their hashes
# are checked first, and fsck.moobfs built here must report on the M1 image what the writer's did.
if [ -n "${MOOB_CROSS:-}" ]; then
    (cd "$MOOB_CROSS" && sha256sum -c --quiet SHA256SUMS) || { echo "MOOB_CROSS: hashes differ" >&2; exit 1; }
    for fs in ext4 btrfs xfs; do cp --sparse=always "$MOOB_CROSS/lower-$fs.img" "build/vm/lower-$fs.img"; done
    cp -r "$MOOB_CROSS/guest" "$STAGE/cross"
    cp "$MOOB_CROSS/writer-machine.txt" "$STAGE/cross/"
    build/linux/fsck.moobfs "$MOOB_CROSS/guest/m1.img" > build/vm/cross-m1-fsck.json
    cmp build/vm/cross-m1-fsck.json "$MOOB_CROSS/m1-fsck.json" || { echo "MOOB_CROSS: fsck.moobfs differs" >&2; exit 1; }
    echo "CROSS_FSCK_PASS: fsck.moobfs on $(uname -m) reports the M1 image as on $(head -1 "$MOOB_CROSS/writer-machine.txt")"
fi
# Dynamically linked clients and tools for the acceptance tests, with their libraries.
mkdir -p "$STAGE/tools"
stage_bin() {   # real binaries live in /tools so busybox --install cannot shadow them
    cp -L "$1" "$STAGE/tools/$(basename "$1")"
    ldd "$1" | awk '/=> \//{print $3} /^[[:space:]]*\/(lib|usr)/{print $1}' | sort -u | while read -r lib; do
        mkdir -p "$STAGE$(dirname "$lib")"
        cp -L "$lib" "$STAGE$lib"
    done
}
stage_bin /usr/bin/git
stage_bin /usr/bin/sqlite3
stage_bin /usr/sbin/dmsetup
stage_bin /usr/sbin/mke2fs
stage_bin /usr/sbin/e2fsck
stage_bin /usr/sbin/debugfs
stage_bin /usr/bin/setfacl
stage_bin /usr/bin/getfacl
mkdir -p "$STAGE/usr/lib/git-core"
ln -s /tools/git "$STAGE/usr/lib/git-core/git"
sh vm/build-tests.sh "$STAGE"
make attr-tools BUILD=build/linux CFLAGS="-O2 -g -static"
cp build/linux/moobfs build/linux/moobfs-attr build/linux/moobfs-check build/linux/moobfs-index "$STAGE/"
make bench-tools BUILD=build/linux CFLAGS="-O2 -g -static" SQLITE_LIBS="-lsqlite3 -ldl -lpthread"
cp build/linux/bench-gen build/linux/bench-load build/linux/bench-sqlclient "$STAGE/" && cp bench/queries-w3.txt "$STAGE/bench-queries-w3.txt"
cp bench/sqlbase.sh "$STAGE/bench-sqlbase.sh" && cp bench/equiv.sh "$STAGE/bench-equiv.sh" && cp bench/run.sh "$STAGE/bench-run.sh"
MOOB_BUILD_DIR=build/linux python3 tests/test_images.py --fixtures "$STAGE/data"
build/linux/fsck.moobfs "$STAGE/data/shared.img" > build/vm/image-report.json
cp "$STAGE/data/shared.img" build/vm/shared.img
[ "$M1" = 0 ] || cp kernel/moobfs.ko build/vm/moobfs.ko
cp layer/moobfs_layer.ko build/vm/moobfs_layer.ko
cp "/boot/config-$KERNEL" build/vm/kernel.config
{
    printf 'Kernel: %s\n' "$KERNEL"
    gcc --version | head -1
    $QEMU --version | head -1
    dpkg-query -W 'linux-image-*' 'linux-headers-*' gcc "$QEMU_PKG"
    if [ "$M1" = 1 ]; then M1KO=kernel/moobfs.ko; else M1KO=; fi
    sha256sum "/boot/vmlinuz-$KERNEL" $M1KO layer/moobfs_layer.ko build/vm/shared.img
} > build/vm/environment.txt
(cd "$STAGE" && find . -print0 | sort -z | cpio --null -o --format=newc 2>/dev/null | gzip -n) > build/vm/initramfs.cpio.gz
set +e
# A guard against a hung guest, not a time budget. The complete suite reached the previous
# 1200 s guard during the btrfs release step under TCG after adding rootpath and overbudget,
# and the 2400 s one on the dedicated host's TCG after the permission matrix grew (journal 0219).
ACCEL=tcg
[ "${MOOB_VM_KVM:-}" = 1 ] && ACCEL=kvm
# On s390x the CCW virtio disks finish probing in any order, so vda is not always the first
# disk (journal 0280); each gets a serial that vm/suite.sh maps back. amd64 is unchanged.
set --
for d in lower-ext4:ext4 lower-btrfs:btrfs lower-xfs:xfs scratch:scratch log:log; do
    if [ "$MACHINE" = s390-ccw-virtio ]; then
        set -- "$@" -drive "file=build/vm/${d%%:*}.img,format=raw,if=none,id=${d#*:}" \
            -device "virtio-blk,drive=${d#*:},serial=moob-${d#*:}"
    else
        set -- "$@" -drive "file=build/vm/${d%%:*}.img,format=raw,if=virtio"
    fi
done
timeout 3600 $QEMU \
    -machine $MACHINE -accel $ACCEL -cpu max -m 512M -smp 2 \
    "$@" \
    -kernel "/boot/vmlinuz-$KERNEL" -initrd build/vm/initramfs.cpio.gz \
    -append "console=$CONSOLE rdinit=/init panic=-1 loglevel=4${MOOB_ONLY:+ moob_only=$(echo "$MOOB_ONLY" | tr ' ' ',')}${MOOB_PERMCHURN:+ moob_permchurn=$MOOB_PERMCHURN}${MOOB_SELFCHECK:+ moob_selfcheck=$MOOB_SELFCHECK}${MOOB_WALKLAT:+ moob_walklat=$MOOB_WALKLAT}" \
    -nographic -no-reboot -nic none > build/vm/console.log 2>&1
RESULT=$?
set -e
cat build/vm/console.log
test "$RESULT" -eq 0
if [ "$M1" = 1 ]; then
    grep -q '^MOOBFS_VM_PASS:' build/vm/console.log
else
    grep -q '^MOOBFS_VM_SKIPPED:' build/vm/console.log
fi
if [ -n "${MOOB_ONLY:-}" ]; then
    grep -q '^MOOBFS_ONLY_PASS:' build/vm/console.log   # a partial run, asked for with MOOB_ONLY
else
    grep -q '^MOOBFS_LAYER_PASS:' build/vm/console.log
    grep -q '^MOOBFS_ACCEPTANCE_PASS:' build/vm/console.log
fi
! grep -q '^MOOBFS_VM_FAIL:' build/vm/console.log
