#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Finding 0211-3 (journal 0211; test added in journal 0283): vm/syzkaller/image-setup.sh runs as
# root on the host and writes into the syzkaller image through a loop mount, so an absolute
# symbolic link planted in the image must not carry a write to the host (nosymfollow). Two
# 16 MiB ext4 images made with mke2fs -d: on the clean one the script completes and installs the
# unit; on the one whose unit file is a link to a path on the host, it fails and the host path
# is not created. Root, loop devices and e2fsprogs needed (a privileged container will do).
# Run from the repository root: sh scripts/test-image-setup.sh
set -eu
fail() { echo "IMAGE_SETUP_UNIT_FAIL: $*"; exit 1; }
[ "$(id -u)" -eq 0 ] || fail "needs root (loop mounts)"
tmp=$(mktemp -d)
marker=/tmp/moob-image-setup-marker.$$
trap 'umount "$tmp/m" 2>/dev/null || true; rm -rf "$tmp" "$marker"' EXIT
[ ! -e "$marker" ] || fail "$marker exists before the run"
printf 'not a module\n' > "$tmp/fake.ko"
for kind in clean tampered; do
    mkdir -p "$tmp/$kind/etc/systemd/system/multi-user.target.wants"
done
ln -s "$marker" "$tmp/tampered/etc/systemd/system/moobfs.service"
for kind in clean tampered; do
    mke2fs -q -F -t ext4 -d "$tmp/$kind" "$tmp/$kind.img" 16M || fail "mke2fs $kind"
done
sh vm/syzkaller/image-setup.sh "$tmp/clean.img" "$tmp/fake.ko" > "$tmp/clean.log" 2>&1 ||
    fail "clean image refused: $(cat "$tmp/clean.log")"
mkdir "$tmp/m"
mount -o loop,ro "$tmp/clean.img" "$tmp/m" || fail "mount the clean image"
grep -q 'ExecStart=/sbin/insmod /moob/moobfs_layer.ko' "$tmp/m/etc/systemd/system/moobfs.service" ||
    fail "clean image: unit not written"
cmp -s "$tmp/fake.ko" "$tmp/m/moob/moobfs_layer.ko" || fail "clean image: module not copied"
umount "$tmp/m"
if sh vm/syzkaller/image-setup.sh "$tmp/tampered.img" "$tmp/fake.ko" > "$tmp/tampered.log" 2>&1; then
    fail "tampered image accepted"
fi
[ ! -e "$marker" ] || fail "the write followed the image's link to the host: $marker"
echo "IMAGE_SETUP_UNIT_PASS: clean image set up; a link to the host inside the image stopped the write"
