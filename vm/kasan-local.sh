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
# suite steps and ends in MOOBFS_KASAN_ONLY_PASS. This is a faster loop
# for development; the required check is still the workflow on the pull request.
set -eu
PATH=$PATH:/usr/sbin:/sbin   # mke2fs, mkfs.btrfs, mkfs.xfs and dmsetup for a non-root user
KERNEL_VERSION=$(sed -n 's/^ *KERNEL_VERSION: *//p' .github/workflows/kasan.yml)
KERNEL_SHA256=$(sed -n 's/^ *KERNEL_SHA256: *//p' .github/workflows/kasan.yml)
# KCONFIG selects another fragment (vm/kcsan.config for the data-race run); give it its own KCACHE
KCONFIG=${KCONFIG:-vm/kasan.config}
KCACHE=${KCACHE:-$HOME/.cache/moobfs-kasan}
KDIR=$KCACHE/linux-$KERNEL_VERSION
R=$(pwd)
[ -f vm/kasan-guest.sh ] || { echo "kasan-local: run from the repository root" >&2; exit 1; }
[ -w /dev/kvm ] || echo "kasan-local: /dev/kvm not writable; QEMU will emulate (slow)" >&2
command -v vng >/dev/null || { echo "kasan-local: vng (virtme-ng) not in PATH" >&2; exit 1; }

# kernel: rebuilt only when missing or when $KCONFIG changed since the last build
if [ ! -f "$KDIR/arch/x86/boot/bzImage" ] || ! cmp -s "$KCONFIG" "$KDIR/.moobfs-kasan.config"; then
    mkdir -p "$KCACHE"
    if [ ! -d "$KDIR" ]; then
        curl -fsSL -o "$KCACHE/linux.tar.xz" "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz"
        echo "$KERNEL_SHA256  $KCACHE/linux.tar.xz" | sha256sum -c -
        mkdir -p "$KDIR.tmp"
        tar -xJf "$KCACHE/linux.tar.xz" -C "$KDIR.tmp" --strip-components=1
        mv "$KDIR.tmp" "$KDIR"
        rm -f "$KCACHE/linux.tar.xz"
    fi
    (cd "$KDIR" && vng --kconfig --config "$R/$KCONFIG" && vng --build)
    cp "$KCONFIG" "$KDIR/.moobfs-kasan.config"
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
gcc -static -O2 -Wall -Wextra -Werror tests/layer.c -o build/vng/layer
gcc -static -O2 -Wall -Wextra -Werror -Iinclude tests/failnth.c -o build/vng/failnth
gcc -static -O2 -Wall -Wextra -Werror tests/hostile.c -o build/vng/hostile
gcc -static -O2 -Wall -Wextra -Werror -Iinclude tests/buildrace.c -lpthread -o build/vng/buildrace
gcc -static -O2 -Wall -Wextra -Werror -Iinclude tests/query.c common/attr.c -o build/vng/query
gcc -static -O2 -Wall -Wextra -Werror tests/ipc.c -o build/vng/ipc
gcc -static -O2 -Wall -Wextra -Werror tests/reconf.c -o build/vng/reconf
gcc -static -O2 -Wall -Wextra -Werror -Iinclude -DSQLITE_CORE tests/sqlq.c sqlite/moobfs_vtab.c common/attr.c -lsqlite3 -lm -ldl -lpthread -o build/vng/sqlq
gcc -static -O2 -Wall -Wextra -Werror tests/idmap.c -o build/vng/idmap
gcc -static -O2 -Itests/third_party/fsx tests/third_party/fsx/fsx.c -o build/vng/fsx
gcc -static -O2 tests/third_party/log-writes/replay-log.c tests/third_party/log-writes/log-writes.c -o build/vng/replay-log
cp layer/moobfs_layer.ko build/vng/
rm -f build/vng/lower-*.img build/vng/scratch.img build/vng/log.img
truncate -s 64M build/vng/lower-ext4.img && mke2fs -q -F -t ext4 -b 4096 -L moob-lower build/vng/lower-ext4.img
truncate -s 256M build/vng/lower-btrfs.img && mkfs.btrfs -q -f -L moob-lower build/vng/lower-btrfs.img
truncate -s 300M build/vng/lower-xfs.img && mkfs.xfs -q -f -L moob-lower build/vng/lower-xfs.img
truncate -s 64M build/vng/scratch.img build/vng/log.img

# one retry on a boot-time diagnostic of the debug kernel (MOOBFS_VM_ENV_FAIL), as in the workflow
for attempt in 1 2; do
    (cd "$KDIR" && vng --user root --cpus 2 --memory 3G --verbose \
        --rwdir "$R/build" \
        --disk "$R/build/vng/lower-ext4.img" --disk "$R/build/vng/lower-btrfs.img" \
        --disk "$R/build/vng/lower-xfs.img" --disk "$R/build/vng/scratch.img" --disk "$R/build/vng/log.img" \
        --exec "HOSTILE_SEEDS=${HOSTILE_SEEDS:-3} MOOB_ONLY='${MOOB_ONLY:-}' sh $R/vm/kasan-guest.sh") > "build/vng/console-$attempt.log" 2>&1 || true
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
