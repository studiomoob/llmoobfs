#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Smack run of the layer (journal 0294, security roadmap S12), in a virtme-ng guest of the KASAN
# kernel pin of .github/workflows/kasan.yml built with vm/kasan.config and vm/smack.config, kept
# in KCACHE (default ~/.cache/moobfs-smack). Debian's kernels do not build Smack. Needs, on the
# host: what vm/kasan-local.sh needs, setfattr and getfattr (attr), setpriv (util-linux) and a
# static busybox. Rules and labels are set in the guest only. Run from the repository root.
# Exit status 0 only on MOOBFS_SMACK_RESULT: fails=0.
set -eu
PATH=$PATH:/usr/sbin:/sbin; export PATH
R=$(pwd); B=$R/build/smack
[ -f vm/smack-guest.sh ] || { echo "smack-local: run from the repository root" >&2; exit 1; }
for t in vng setfattr getfattr setpriv mkfs.ext4; do
    command -v $t > /dev/null || { echo "MOOBFS_SMACK_FAIL: $t is needed on the host"; exit 1; }
done
file -L /bin/busybox | grep -q 'statically linked' ||
    { echo "MOOBFS_SMACK_FAIL: /bin/busybox must be static"; exit 1; }
KERNEL_VERSION=$(sed -n "s/^ *KERNEL_VERSION: *//p" .github/workflows/kasan.yml)
KERNEL_SHA256=$(sed -n "s/^ *KERNEL_SHA256: *//p" .github/workflows/kasan.yml)
[ -n "$KERNEL_VERSION" ] && [ -n "$KERNEL_SHA256" ] || { echo "smack-local: no KERNEL pin in kasan.yml" >&2; exit 1; }
KCACHE=${KCACHE:-$HOME/.cache/moobfs-smack}
KDIR=$KCACHE/linux-$KERNEL_VERSION
if [ ! -f "$KDIR/arch/x86/boot/bzImage" ] || ! cat vm/kasan.config vm/smack.config | cmp -s - "$KDIR/.moobfs-smack.config"; then
    mkdir -p "$KCACHE"
    if [ ! -d "$KDIR" ]; then
        curl -fsSL -o "$KCACHE/linux.tar.xz" "https://cdn.kernel.org/pub/linux/kernel/v${KERNEL_VERSION%%.*}.x/linux-$KERNEL_VERSION.tar.xz"
        echo "$KERNEL_SHA256  $KCACHE/linux.tar.xz" | sha256sum -c -
        mkdir -p "$KDIR.tmp"
        tar -xJf "$KCACHE/linux.tar.xz" -C "$KDIR.tmp" --strip-components=1
        mv "$KDIR.tmp" "$KDIR"
        rm -f "$KCACHE/linux.tar.xz"
    fi
    (cd "$KDIR" && vng --kconfig --config "$R/vm/kasan.config" --config "$R/vm/smack.config" && vng --build)
    cat vm/kasan.config vm/smack.config > "$KDIR/.moobfs-smack.config"
fi
grep -q '^CONFIG_SECURITY_SMACK=y' "$KDIR/.config" || { echo "MOOBFS_SMACK_FAIL: the kernel was built without Smack"; exit 1; }
make layer KDIR="$KDIR"
mkdir -p "$B"
# static, so that they run the same whatever the guest's root filesystem labels are
gcc -static -O2 -Iinclude tests/query.c common/attr.c -o "$B/query"
gcc -static -O2 -Iinclude tools/moobfs-attr.c common/attr.c -o "$B/moobfs-attr"
gcc -static -O2 -Wall -Wextra -Werror tests/listdir.c -o "$B/listdir"
rm -f "$B/lower.img"
truncate -s 256M "$B/lower.img" && mkfs.ext4 -q -F "$B/lower.img"
(cd "$KDIR" && vng --user root --cpus 2 --memory 3G --verbose \
    --disk "$B/lower.img" --rwdir "$B" \
    --exec "sh $R/vm/smack-guest.sh $R $B") > "$B/console.log" 2>&1 || true
grep -E '^MOOBFS_SMACK|^SMACK_|^  ' "$B/console.log" || true
if grep -E 'BUG: KASAN|KASAN: |BUG:|Oops:|\] WARNING:|possible (circular|recursive) locking|UBSAN:' "$B/console.log"; then
    echo "MOOBFS_SMACK_FAIL: kernel diagnostics (see build/smack/console.log)"; exit 1
fi
grep -q '^MOOBFS_SMACK_RESULT: fails=0$' "$B/console.log"
