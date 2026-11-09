#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Review of 3627172 (journal 0153): reproductions of findings R2-1 (tests/review-deep-tree.c)
# and R2-2 (tests/review-samedir-rename.c), each with a control run. Run inside the project's
# moobfs-m1-test Docker image with this tree at /work:
#   docker run --rm --platform linux/amd64 -v "$PWD:/work" moobfs-m1-test sh tests/review-3627172-vm.sh
# Modules are loaded only by the disposable QEMU guest. Each run uses a new directory.
# Since journal 0276 the suite steps deeptree and samedir (vm/suite.sh) make the same checks
# in every make vm-test; this script stays as the record of the original reproduction.
set -eu
cd /work
KERNEL=6.12.111+deb13-amd64
mkdir -p /work/build/review
RUN=$(mktemp -d /work/build/review/vm.XXXXXX)
STAGE=$RUN/stage
mkdir -p "$STAGE/bin" "$STAGE/dev" "$STAGE/proc" "$STAGE/sys" "$STAGE/tmp" "$STAGE/mods"
chmod 0755 "$STAGE"
cp /bin/busybox "$STAGE/bin/"
ln -s busybox "$STAGE/bin/sh"
make layer KDIR="/lib/modules/$KERNEL/build" > "$RUN/build.log" 2>&1
cp layer/moobfs_layer.ko "$STAGE/"
for src in tests/review-deep-tree.c tests/review-samedir-rename.c; do
    gcc -static -O2 -Wall -Wextra -Werror -Iinclude "$src" -o "$STAGE/bin/$(basename "$src" .c)"
done
python3 - "$KERNEL" "$STAGE/mods" <<'PY'
import lzma
import sys
from pathlib import Path
base = Path("/lib/modules") / sys.argv[1]
deps = {}
for line in (base / "modules.dep").read_text().splitlines():
    key, _, value = line.partition(":")
    deps[key] = value.split()
order = []
def visit(name):
    for dep in deps.get(name, []):
        visit(dep)
    if name not in order:
        order.append(name)
for name in ("kernel/crypto/crc32c_generic.ko.xz",
             "kernel/drivers/block/virtio_blk.ko.xz",
             "kernel/fs/ext4/ext4.ko.xz"):
    visit(name)
out = Path(sys.argv[2])
with (out / "order").open("w") as listing:
    for name in order:
        target = Path(name).name.removesuffix(".xz")
        (out / target).write_bytes(lzma.decompress((base / name).read_bytes()))
        listing.write(target + "\n")
PY
cat > "$STAGE/init" <<'INIT'
#!/bin/sh
/bin/busybox --install -s /bin
export PATH=/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
fail() { echo "REVIEW_VM_FAIL: $*"; poweroff -f; }
while read -r name; do insmod "/mods/$name" || fail "$name"; done < /mods/order
mkdir -p /lower /layer /trace
mount -t ext4 /dev/vda /lower || fail ext4
insmod /moobfs_layer.ko || fail moobfs_layer
mount -t moobfs_layer -o lower=/lower none /layer || fail layer
mount -t tracefs tracefs /trace || fail tracefs
echo 1 > /trace/events/moobfs/moobfs_scan_reread/enable
echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us
echo "R2-1 CONTROL: directory at depth 6 made private, file below it at depth 11"
review-deep-tree /layer/r21-control 10 5
echo "R2-1 DEEP: directory at depth 66 made private, file below it at depth 68"
review-deep-tree /layer/r21-deep 66 65
echo "R2-1 VALUE: no chmod, value of a file at depth 72 changed"
review-deep-tree /layer/r21-value 70 0
echo 1000000 > /sys/module/moobfs_layer/parameters/scan_delay_us
echo "R2-2 CONTROL: rename after the build"
review-samedir-rename /layer/r22-control -1
echo "R2-2 RACE: rename 300 ms into the build, the scan sleeping 1 s per file"
review-samedir-rename /layer/r22-race 300000
echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us
echo "TRACE_BEGIN"
cat /trace/trace
echo "TRACE_END"
umount /layer || fail unmount-layer
rmmod moobfs_layer || fail unload
umount /lower || fail unmount-lower
echo "DMESG_BEGIN"
dmesg
echo "DMESG_END"
echo "REVIEW_VM_DONE"
poweroff -f
INIT
chmod +x "$STAGE/init"
truncate -s 256M "$RUN/lower.img"
mke2fs -q -F -t ext4 -b 4096 "$RUN/lower.img"
(cd "$STAGE" && find . -print0 | sort -z | cpio --null -o --format=newc 2>/dev/null | gzip -n) > "$RUN/initramfs.cpio.gz"
sha256sum layer/moobfs_layer.ko "/boot/vmlinuz-$KERNEL" > "$RUN/hashes.txt"
timeout 300 qemu-system-x86_64 -machine q35 -accel tcg -cpu max -m 768M -smp 2 \
    -drive "file=$RUN/lower.img,format=raw,if=virtio" \
    -kernel "/boot/vmlinuz-$KERNEL" -initrd "$RUN/initramfs.cpio.gz" \
    -append "console=ttyS0 rdinit=/init panic=-1 loglevel=4" \
    -nographic -no-reboot -nic none > "$RUN/console.log" 2>&1
echo "$RUN"
grep -E '^R2-|REVIEW2_RESULT|RENAMED|REVIEW_|moobfs_scan_reread:' "$RUN/console.log"
grep -q '^REVIEW_VM_DONE' "$RUN/console.log"
# Expectations inverted after the fixes (journal 0156); before them, at 3627172, the defect
# lines below appeared and this script checked for them.
# R2-1: the control hides the file after the chmod; nothing beyond the placeable depth is
# indexed, so the deep runs never answer the file (and never the old value)
grep -q 'REVIEW2_RESULT depth=10 chmod_depth=5 uid=1001 before=1 exists=0 eq_x=0 eq_y=0 open=-1 errno=13' "$RUN/console.log"
if grep -q 'REVIEW2_RESULT depth=66 chmod_depth=65 uid=1001 before=1 exists=1 eq_x=1' "$RUN/console.log"; then exit 1; fi
if grep -q 'REVIEW2_RESULT depth=70 chmod_depth=0 uid=1001 before=1 exists=1 eq_x=1' "$RUN/console.log"; then exit 1; fi
grep -q 'REVIEW2_RESULT depth=66 chmod_depth=65 uid=1001 before=0 exists=0 eq_x=0 eq_y=0 open=-1 errno=13' "$RUN/console.log"
grep -q 'REVIEW2_RESULT depth=70 chmod_depth=0 uid=1001 before=0 exists=0 eq_x=0 eq_y=0 open=0 errno=0' "$RUN/console.log"
# R2-2: after the inode is evicted both the control and the race name b
grep -q 'REVIEW2_RESULT samedir delay=-1 cached=1 after_drop=1 path_after_drop=b exists_b=1 exists_a=0' "$RUN/console.log"
if grep -q 'REVIEW2_RESULT samedir delay=300000 cached=1 after_drop=1 path_after_drop=a' "$RUN/console.log"; then exit 1; fi
grep -q 'REVIEW2_RESULT samedir delay=300000 cached=1 after_drop=1 path_after_drop=b exists_b=1 exists_a=0' "$RUN/console.log"
grep -q 'moobfs_scan_reread:' "$RUN/console.log"
