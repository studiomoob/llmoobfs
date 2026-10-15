#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Prepares the syzkaller guest image for the layer (journal 0109): the module and a service that
# loads it on every boot. The fuzzer's programs mount the layer themselves, inside the private
# root the executor gives each program (vm/syzkaller/moobfs.txt). Run as root by
# vm/syzkaller-local.sh: image-setup.sh <image file> <moobfs_layer.ko>.
set -eu
IMG=$1; KO=$2
MNT=$(mktemp -d)
mount -o loop "$IMG" "$MNT"
trap 'umount "$MNT"; rmdir "$MNT"' EXIT
mkdir -p "$MNT/moob"
cp "$KO" "$MNT/moob/moobfs_layer.ko"
cat > "$MNT/etc/systemd/system/moobfs.service" <<'UNIT'
[Unit]
Description=MoobFS layer for syzkaller
DefaultDependencies=no
After=local-fs.target
Before=ssh.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/insmod /moob/moobfs_layer.ko

[Install]
WantedBy=multi-user.target
UNIT
ln -sf /etc/systemd/system/moobfs.service "$MNT/etc/systemd/system/multi-user.target.wants/moobfs.service"
echo "image-setup: done"
