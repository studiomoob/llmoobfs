#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs the KASAN/lockdep job of .github/workflows/kasan.yml on a local Linux host with KVM, with the
# same pinned kernel, config, test binaries and guest script. The kernel is built once and kept in
# KCACHE (default ~/.cache/moobfs-kasan), so later runs only rebuild the module and the tests.
# Run from the repository root as a user in the kvm group (no root needed):
#   sh vm/kasan-local.sh
# Needs the packages and virtme-ng listed in the workflow's "Install" step (virtme-ng from
# vm/kasan-requirements.txt). Exit status 0 only on MOOBFS_KASAN_PASS. HOSTILE_SEEDS=N plants N adversarial
# lower trees per filesystem instead of 3 (security roadmap S4). MOOB_ONLY="xfs ioerror" runs only those
# suite steps and ends in MOOBFS_KASAN_ONLY_PASS. MOOB_PERMCHURN=SEEDS,STEPS[,FIRST] lengthens the
# permchurn step (vm/suite.sh). MOOB_SELFCHECK=1 runs it with the index self-check on (S16). MOOB_KASAN_KERNEL=7.3
# runs the workflow's 7.3 leg (KERNEL73_VERSION and KERNEL73_SHA256) instead of the 6.12 one (journal 0279).
# This is a faster loop for development; the required check is still the workflow on the pull request.
set -eu
PATH=$PATH:/usr/sbin:/sbin   # mke2fs, mkfs.btrfs, mkfs.xfs and dmsetup for a non-root user
case ${MOOB_KASAN_KERNEL:-6.12} in
6.12) KPIN=KERNEL ;;
7.3) KPIN=KERNEL73 ;;
*) echo "kasan-local: MOOB_KASAN_KERNEL must be 6.12 or 7.3" >&2; exit 1 ;;
esac
KERNEL_VERSION=$(sed -n "s/^ *${KPIN}_VERSION: *//p" .github/workflows/kasan.yml)
KERNEL_SHA256=$(sed -n "s/^ *${KPIN}_SHA256: *//p" .github/workflows/kasan.yml)
[ -n "$KERNEL_VERSION" ] && [ -n "$KERNEL_SHA256" ] || { echo "kasan-local: no ${KPIN} pin in kasan.yml" >&2; exit 1; }
# KCONFIG selects another fragment (vm/kcsan.config for the data-race run); give it its own KCACHE
KCONFIG=${KCONFIG:-vm/kasan.config}
KCACHE=${KCACHE:-$HOME/.cache/moobfs-kasan}
KDIR=$KCACHE/linux-$KERNEL_VERSION
R=$(pwd)
[ -f vm/kasan-guest.sh ] || { echo "kasan-local: run from the repository root" >&2; exit 1; }
[ -w /dev/kvm ] || echo "kasan-local: /dev/kvm not writable; QEMU will emulate (slow)" >&2
command -v vng >/dev/null || { echo "kasan-local: vng (virtme-ng) not in PATH" >&2; exit 1; }

# On an s390x host (journal 0280, exploratory) the same kernel and fragment build for s390, and
# the CCW disks, which finish probing in any order, are named by serial for vm/suite.sh. Any
# other host is treated as x86-64, as before.
case "$(uname -m)" in
    # the virtme-ng-init in virtme-ng's wheel is an x86-64 binary: the guest boots with the
    # shell init instead (as .github/workflows/kasan-arm64.yml does)
    s390x) KIMAGE=arch/s390/boot/bzImage KEXTRA=vm/kasan-s390x.config VNGINIT=--no-virtme-ng-init ;;
    *) KIMAGE=arch/x86/boot/bzImage KEXTRA='' VNGINIT='' ;;
esac
# kernel: rebuilt only when missing or when $KCONFIG (and KEXTRA) changed since the last build
if [ ! -f "$KDIR/$KIMAGE" ] || ! cat "$KCONFIG" ${KEXTRA:+"$KEXTRA"} | cmp -s - "$KDIR/.moobfs-kasan.config"; then
    mkdir -p "$KCACHE"
    if [ ! -d "$KDIR" ]; then
        curl -fsSL -o "$KCACHE/linux.tar.xz" "https://cdn.kernel.org/pub/linux/kernel/v${KERNEL_VERSION%%.*}.x/linux-$KERNEL_VERSION.tar.xz"
        echo "$KERNEL_SHA256  $KCACHE/linux.tar.xz" | sha256sum -c -
        mkdir -p "$KDIR.tmp"
        tar -xJf "$KCACHE/linux.tar.xz" -C "$KDIR.tmp" --strip-components=1
        mv "$KDIR.tmp" "$KDIR"
        rm -f "$KCACHE/linux.tar.xz"
    fi
    (cd "$KDIR" && vng --kconfig --config "$R/$KCONFIG" ${KEXTRA:+--config "$R/$KEXTRA"} && vng --build)
    cat "$KCONFIG" ${KEXTRA:+"$KEXTRA"} > "$KDIR/.moobfs-kasan.config"
fi

# module and test binaries, as in the workflow
make layer KDIR="$KDIR"
mkdir -p build/vng
make attr-tools BUILD=build/vng CFLAGS="-O2 -g -static"
make bench-tools BUILD=build/vng CFLAGS="-O2 -g -static" SQLITE_LIBS="-lsqlite3 -ldl -lpthread"
cp bench/queries-w3.txt build/vng/bench-queries-w3.txt
cp bench/sqlbase.sh build/vng/bench-sqlbase.sh
cp bench/equiv.sh build/vng/bench-equiv.sh
cp bench/run.sh build/vng/bench-run.sh
sh vm/build-tests.sh build/vng
cp layer/moobfs_layer.ko build/vng/
rm -f build/vng/lower-*.img build/vng/scratch.img build/vng/log.img
truncate -s 64M build/vng/lower-ext4.img && mke2fs -q -F -t ext4 -b 4096 -L moob-lower build/vng/lower-ext4.img
truncate -s 256M build/vng/lower-btrfs.img && mkfs.btrfs -q -f -L moob-lower build/vng/lower-btrfs.img
truncate -s 300M build/vng/lower-xfs.img && mkfs.xfs -q -f -L moob-lower build/vng/lower-xfs.img
truncate -s 64M build/vng/scratch.img build/vng/log.img

# vng gives each disk its path as serial; on s390x short names, links in the kernel tree, make
# the serials vm/suite.sh looks for (moob-ext4, ...)
set --
for d in lower-ext4:ext4 lower-btrfs:btrfs lower-xfs:xfs scratch:scratch log:log; do
    if [ "$KIMAGE" = arch/s390/boot/bzImage ]; then
        ln -sfn "$R/build/vng/${d%%:*}.img" "$KDIR/moob-${d#*:}"
        set -- "$@" --disk "moob-${d#*:}"
    else
        set -- "$@" --disk "$R/build/vng/${d%%:*}.img"
    fi
done
# one retry on a boot-time diagnostic of the debug kernel (MOOBFS_VM_ENV_FAIL), as in the workflow
for attempt in 1 2; do
    (cd "$KDIR" && vng --user root --cpus 2 --memory 3G --verbose ${VNGINIT:+"$VNGINIT"} \
        --rwdir "$R/build" \
        "$@" \
        --exec "HOSTILE_SEEDS=${HOSTILE_SEEDS:-3} MOOB_ONLY='${MOOB_ONLY:-}' MOOB_PERMCHURN='${MOOB_PERMCHURN:-}' MOOB_SELFCHECK='${MOOB_SELFCHECK:-}' sh $R/vm/kasan-guest.sh") > "build/vng/console-$attempt.log" 2>&1 || true
    cp "build/vng/console-$attempt.log" build/vng/console.log
    grep -q '^MOOBFS_VM_ENV_FAIL:' build/vng/console.log || break
    echo "kasan-local: attempt $attempt hit a boot-time diagnostic; retrying once"
done
grep -E '^MOOBFS_[A-Z_]*(PASS|FAIL)' build/vng/console.log | tail -5
if [ -n "${MOOB_ONLY:-}" ]; then
    grep -q '^MOOBFS_KASAN_ONLY_PASS:' build/vng/console.log || exit 1
else
    grep -q '^MOOBFS_KASAN_PASS:' build/vng/console.log || exit 1
fi
if grep -q '^MOOBFS_VM_FAIL:\|^MOOBFS_VM_ENV_FAIL:' build/vng/console.log; then exit 1; fi
echo "kasan-local: pass (console in build/vng/console.log)"
