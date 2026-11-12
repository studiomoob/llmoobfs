#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# SELinux run of the layer (journal 0104, security roadmap S12), in a virtme-ng guest of the
# running Debian kernel booted with security=selinux. Needs, on the host: the packages
# selinux-policy-default, selinux-policy-dev, policycoreutils, checkpolicy and semodule-utils;
# "policy-version = 33" in /etc/selinux/semanage.conf (Linux 6.12 loads up to version 33); vng
# in PATH; the module built in layer/. SELinux is not activated on the host: the policy store
# there only builds the policy files the guest loads. Run from the repository root.
set -eu
PATH=$PATH:/usr/sbin:/sbin; export PATH
R=$(pwd); B=$R/build/selinux
mkdir -p "$B"
# static, so that a confined process can run them from a labelled tmpfs
gcc -static -O2 -Iinclude tests/query.c common/attr.c -o "$B/query"
gcc -static -O2 -Iinclude tools/moobfs-attr.c common/attr.c -o "$B/moobfs-attr"
make -C vm/selinux -f /usr/share/selinux/devel/Makefile moobtest.pp > "$B/build.log" 2>&1
sudo semodule -i vm/selinux/moobtest.pp
sudo semodule -r moobfs_layer 2> /dev/null || true
sudo install -m 644 -o "$(id -u)" /etc/selinux/default/policy/policy.33 "$B/policy-stock.33"
sudo semodule -i vm/selinux/moobfs_layer.cil
sudo install -m 644 -o "$(id -u)" /etc/selinux/default/policy/policy.33 "$B/policy-fsuse.33"
for run in stock fsuse; do
    truncate -s 256M "$B/lower.img" && mkfs.ext4 -q -F "$B/lower.img"
    vng -r --user root --cpus 2 --memory 2G --append "security=selinux" \
        --disk "$B/lower.img" --rwdir "$B" \
        --exec "sh $R/vm/selinux-guest.sh $R $B/policy-$run.33 $run" > "$B/console-$run.log" 2>&1 || true
    grep -E '^MOOBFS_SELINUX|^  |^labels' "$B/console-$run.log" || true
done
