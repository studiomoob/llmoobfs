#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# AppArmor run of the layer (journal 0290, security roadmap S12), in a virtme-ng guest of the
# running Debian kernel, where AppArmor is the default major LSM. Needs, on the host: the apparmor
# package (apparmor_parser, aa-exec; installed by default on Debian), busybox (static, as Debian
# ships it), vng in PATH and the module built in layer/. Profiles are loaded in the guest only;
# nothing changes in the host's policy. Run from the repository root.
set -eu
PATH=$PATH:/usr/sbin:/sbin; export PATH
R=$(pwd); B=$R/build/apparmor
mkdir -p "$B"
command -v apparmor_parser > /dev/null && command -v aa-exec > /dev/null ||
    { echo "MOOBFS_APPARMOR_FAIL: apparmor_parser and aa-exec are needed on the host"; exit 1; }
file -L /bin/busybox | grep -q 'statically linked' ||
    { echo "MOOBFS_APPARMOR_FAIL: /bin/busybox must be static"; exit 1; }
# static, so that the profiles need no rule for shared libraries
gcc -static -O2 -Iinclude tests/query.c common/attr.c -o "$B/query"
gcc -static -O2 -Iinclude tools/moobfs-attr.c common/attr.c -o "$B/moobfs-attr"
gcc -static -O2 -Wall -Wextra -Werror tests/listdir.c -o "$B/listdir"
truncate -s 256M "$B/lower.img" && mkfs.ext4 -q -F "$B/lower.img"
vng -r --user root --cpus 2 --memory 2G \
    --disk "$B/lower.img" --rwdir "$B" \
    --exec "sh $R/vm/apparmor-guest.sh $R $B" > "$B/console.log" 2>&1 || true
grep -E '^MOOBFS_APPARMOR|^APPARMOR_|^  ' "$B/console.log" || true
grep -q '^MOOBFS_APPARMOR_RESULT: fails=0$' "$B/console.log"
