#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd /work
MOOB_DEBIAN_KERNEL=${MOOB_DEBIAN_KERNEL:-6.12.111}
case "$MOOB_DEBIAN_KERNEL" in
    6.12.111|6.12.113) KERNEL="$MOOB_DEBIAN_KERNEL+deb13-amd64" ;;
    *) echo "Unsupported Debian VM kernel: $MOOB_DEBIAN_KERNEL" >&2; exit 1 ;;
esac
test -d "/lib/modules/$KERNEL/build"
make tools BUILD=build/linux
MOOB_BUILD_DIR=build/linux python3 tests/test_images.py
python3 tests/test_harness_verdicts.py
make module KDIR="/lib/modules/$KERNEL/build"
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
cp kernel/moobfs.ko "$STAGE/moobfs.ko"
cp layer/moobfs_layer.ko "$STAGE/moobfs_layer.ko"
# Lower filesystems for the layer: ext4, btrfs and XFS as in-tree modules with their
# dependency chains, plus virtio_blk for the disks that carry the lower images.
mkdir -p "$STAGE/mods"
python3 - "$KERNEL" "$STAGE/mods" <<'PY'
import lzma, sys
from pathlib import Path
base = Path('/lib/modules') / sys.argv[1]
dep = {}
for line in (base / 'modules.dep').read_text().splitlines():
    k, _, v = line.partition(':')
    dep[k.strip()] = v.split()
order = []
def visit(m):
    for d in dep.get(m, []):
        visit(d)
    if m not in order:
        order.append(m)
# Filesystems request the crc32c shash through the crypto API; modules.dep does not record that.
for m in ('kernel/crypto/crc32c_generic.ko.xz', 'kernel/drivers/block/virtio_blk.ko.xz',
          'kernel/fs/ext4/ext4.ko.xz', 'kernel/fs/btrfs/btrfs.ko.xz', 'kernel/fs/xfs/xfs.ko.xz',
          'kernel/drivers/md/dm-mod.ko.xz', 'kernel/drivers/md/dm-log-writes.ko.xz',
          'kernel/drivers/md/dm-flakey.ko.xz'):
    visit(m)
out = Path(sys.argv[2])
with open(out / 'order', 'w') as f:
    for m in order:
        name = Path(m).name.replace('.ko.xz', '.ko')
        (out / name).write_bytes(lzma.decompress((base / m).read_bytes()))
        f.write(name + '\n')
print('lower module chain:', ' '.join(Path(m).name for m in order))
PY
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
stage_bin /usr/bin/setfacl
stage_bin /usr/bin/getfacl
mkdir -p "$STAGE/usr/lib/git-core"
ln -s /tools/git "$STAGE/usr/lib/git-core/git"
sh vm/build-tests.sh "$STAGE"
python3 - "$KERNEL" "$STAGE/loop.ko" <<'PY'
import lzma
from pathlib import Path
import sys
source = Path('/lib/modules') / sys.argv[1] / 'kernel/drivers/block/loop.ko.xz'
Path(sys.argv[2]).write_bytes(lzma.decompress(source.read_bytes()))
PY
make attr-tools BUILD=build/linux CFLAGS="-O2 -g -static"
cp build/linux/moobfs build/linux/moobfs-attr build/linux/moobfs-check build/linux/moobfs-index "$STAGE/"
make bench-tools BUILD=build/linux CFLAGS="-O2 -g -static" SQLITE_LIBS="-lsqlite3 -ldl -lpthread"
cp build/linux/bench-gen build/linux/bench-load build/linux/bench-sqlclient "$STAGE/" && cp bench/queries-w3.txt "$STAGE/bench-queries-w3.txt"
cp bench/sqlbase.sh "$STAGE/bench-sqlbase.sh" && cp bench/equiv.sh "$STAGE/bench-equiv.sh" && cp bench/run.sh "$STAGE/bench-run.sh"
MOOB_BUILD_DIR=build/linux python3 tests/test_images.py --fixtures "$STAGE/data"
build/linux/fsck.moobfs "$STAGE/data/shared.img" > build/vm/image-report.json
cp "$STAGE/data/shared.img" build/vm/shared.img
cp kernel/moobfs.ko build/vm/moobfs.ko
cp layer/moobfs_layer.ko build/vm/moobfs_layer.ko
cp "/boot/config-$KERNEL" build/vm/kernel.config
{
    printf 'Kernel: %s\n' "$KERNEL"
    gcc --version | head -1
    qemu-system-x86_64 --version | head -1
    dpkg-query -W 'linux-image-*' 'linux-headers-*' gcc qemu-system-x86
    sha256sum "/boot/vmlinuz-$KERNEL" kernel/moobfs.ko layer/moobfs_layer.ko build/vm/shared.img
} > build/vm/environment.txt
(cd "$STAGE" && find . -print0 | sort -z | cpio --null -o --format=newc 2>/dev/null | gzip -n) > build/vm/initramfs.cpio.gz
set +e
# A guard against a hung guest, not a time budget. The complete suite reached the previous
# 1200 s guard during the btrfs release step under TCG after adding rootpath and overbudget,
# and the 2400 s one on the dedicated host's TCG after the permission matrix grew (journal 0219).
ACCEL=tcg
[ "${MOOB_VM_KVM:-}" = 1 ] && ACCEL=kvm
timeout 3600 qemu-system-x86_64 \
    -machine q35 -accel $ACCEL -cpu max -m 512M -smp 2 \
    -drive file=build/vm/lower-ext4.img,format=raw,if=virtio \
    -drive file=build/vm/lower-btrfs.img,format=raw,if=virtio \
    -drive file=build/vm/lower-xfs.img,format=raw,if=virtio \
    -drive file=build/vm/scratch.img,format=raw,if=virtio \
    -drive file=build/vm/log.img,format=raw,if=virtio \
    -kernel "/boot/vmlinuz-$KERNEL" -initrd build/vm/initramfs.cpio.gz \
    -append "console=ttyS0 rdinit=/init panic=-1 loglevel=4${MOOB_ONLY:+ moob_only=$(echo "$MOOB_ONLY" | tr ' ' ',')}" \
    -nographic -no-reboot -nic none > build/vm/console.log 2>&1
RESULT=$?
set -e
cat build/vm/console.log
test "$RESULT" -eq 0
grep -q '^MOOBFS_VM_PASS:' build/vm/console.log
if [ -n "${MOOB_ONLY:-}" ]; then
    grep -q '^MOOBFS_ONLY_PASS:' build/vm/console.log   # a partial run, asked for with MOOB_ONLY
else
    grep -q '^MOOBFS_LAYER_PASS:' build/vm/console.log
    grep -q '^MOOBFS_ACCEPTANCE_PASS:' build/vm/console.log
fi
! grep -q '^MOOBFS_VM_FAIL:' build/vm/console.log
