#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs the layer suite inside a virtme-ng guest booted from a kernel built with
# vm/kasan.config (KASAN, UBSAN, lockdep). The host checkout is the guest's root,
# so this runs from the repository directory with build/ writable.
# Usage (inside the guest, as root): sh vm/kasan-guest.sh
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO" || exit 1
OUT=$REPO/build/vng
mkdir -p "$OUT/tools"
for t in git sqlite3 dmsetup mke2fs e2fsck setfacl getfacl; do
    p=$(command -v "$t") || { echo "MOOBFS_VM_FAIL: missing $t on the host"; exit 1; }
    ln -sf "$p" "$OUT/tools/$t"
done
fail() {
    echo "MOOBFS_VM_FAIL: $*"
    dmesg | tail -80
    dmesg > "$OUT/dmesg.txt"
    exit 1
}
# kdiag: diagnostics in a dmesg capture; KCSAN reports fail only with a racing access in the
# layer (vm/kdiag.sh, journal 0115)
. "$REPO/vm/kdiag.sh"
# the detectors of this kernel, named in the final marker: vm/kasan.config or vm/kcsan.config
if dmesg | grep -q 'kcsan: enabled'; then SAN="KCSAN and lockdep"; else SAN="KASAN, UBSAN and lockdep"; fi
echo "kernel: $(uname -r); KASAN init lines in dmesg: $(dmesg | grep -c 'KernelAddressSanitizer'); lockdep: $(grep -c . /proc/lockdep_stats 2>/dev/null || echo absent)"
# A diagnostic emitted before anything of ours is loaded belongs to the boot of the debug
# kernel under emulation, not to the layer; it also taints the kernel and turns lockdep off,
# so the run would prove nothing. Report it as an environment failure (the workflow retries).
dmesg > "$OUT/dmesg-boot.txt"
# A kernel warning starts its message ("] WARNING: CPU: ...", lockdep's "] WARNING: possible ..."):
# matched there only, since with KVM the guest sees the host CPU and prints the informational
# "Speculative Return Stack Overflow: WARNING: See ..." at boot (pull request 74).
if ! kdiag "$OUT/dmesg-boot.txt" 'BUG: KASAN|KASAN: |BUG:|Oops:|^WARNING:|UBSAN:' > /dev/null; then
    grep -E -A12 'BUG: KASAN|KASAN: |BUG:|Oops:|\] WARNING:|^WARNING:|UBSAN:' "$OUT/dmesg-boot.txt" | head -40
    echo "MOOBFS_VM_ENV_FAIL: kernel diagnostic before the layer was loaded (boot of the debug kernel)"
    exit 2
fi
BIN=$OUT TOOLS=$OUT/tools MODS=$OUT/nomods LOWER=/tmp/moob-lower LAYER=/tmp/moob-layer   # the 9p root is read-only
DEV_EXT4=/dev/vda DEV_BTRFS=/dev/vdb DEV_XFS=/dev/vdc DEV_SCRATCH=/dev/vdd DEV_LOG=/dev/vde
export BIN TOOLS MODS LOWER LAYER DEV_EXT4 DEV_BTRFS DEV_XFS DEV_SCRATCH DEV_LOG
for d in $DEV_EXT4 $DEV_BTRFS $DEV_XFS $DEV_SCRATCH $DEV_LOG; do [ -b "$d" ] || fail "missing disk $d"; done
. "$REPO/vm/suite.sh"
dmesg > "$OUT/dmesg.txt"
if ! kdiag "$OUT/dmesg.txt" 'BUG: KASAN|KASAN: |BUG:|Oops:|^WARNING:|possible (circular|recursive) locking|INFO: (task|rcu)|UBSAN:'; then
    echo "MOOBFS_VM_FAIL: kernel diagnostics under KASAN/lockdep"
    exit 1
fi
# Systematic allocation-failure injection (journal 0096): every allocation made with the module
# on the stack is failed in turn, one system call at a time (tests/failnth.c, the kernel's own
# fail-nth). No kernel diagnostic may follow, and once injection stops the index must equal the
# oracle, or say that it is incomplete and equal the oracle after a remount.
if [ -z "${MOOB_ONLY:-}" ] || case " ${MOOB_ONLY:-} " in *" faults "*) true ;; *) false ;; esac; then
    mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug || fail fi-debugfs
    [ -d /sys/kernel/debug/failslab ] || fail "fi-no-failslab (kernel built without CONFIG_FAILSLAB)"
    insmod "$BIN/moobfs_layer.ko" || fail fi-insmod
    text=$(cat /sys/module/moobfs_layer/sections/.text); size=$(cat /sys/module/moobfs_layer/coresize)
    hi=${text%????????}; lo=${text#"$hi"}   # the address does not fit the shell's signed arithmetic
    for f in failslab fail_page_alloc; do
        d=/sys/kernel/debug/$f
        echo N > $d/ignore-gfp-wait
        [ -f $d/ignore-gfp-highmem ] && echo N > $d/ignore-gfp-highmem
        echo "$text" > $d/require-start
        printf '%s%08x' "$hi" $((0x$lo + size)) > $d/require-end
        echo 32 > $d/stacktrace-depth
        echo 0 > $d/probability; echo 0 > $d/verbose
    done
    mount -t ext4 "$DEV_EXT4" "$LOWER" || fail fi-lower-mount
    rm -rf "$LOWER/fi" && mkdir -m 755 "$LOWER/fi" || fail fi-mkdir
    mount -t moobfs_layer -o lower="$LOWER/fi",domain_depth=1 none "$LAYER" || fail fi-layer-mount
    dmesg -c > /dev/null
    "$BIN/failnth" "$LAYER" "$LOWER/fi" > "$OUT/failnth.txt" 2>&1; rc=$?
    cat "$OUT/failnth.txt"
    [ $rc -eq 0 ] || fail "fi-unexplained-error (see build/vng/failnth.txt)"
    "$BIN/permission-gaps" faults "$LAYER" "$LOWER/fi" || fail fi-unprivileged-permissions
    for f in failslab fail_page_alloc; do echo 0 > /sys/kernel/debug/$f/require-start; echo 0 > /sys/kernel/debug/$f/require-end; done
    dmesg > "$OUT/dmesg-faults.txt"
    if ! kdiag "$OUT/dmesg-faults.txt" 'BUG: KASAN|KASAN: |BUG:|Oops:|^WARNING:|possible (circular|recursive) locking|INFO: (task|rcu)|UBSAN:'; then
        fail "fi-kernel-diagnostic under fault injection"
    fi
    fi_cmp() {   # kernel QUERY2 against the oracle on the domains matching the pattern, by path
        for p in $LOWER/fi/$1; do
            d=${p##*/}
            "$BIN/query" "$LAYER/$d" q2 kind exists - - | cut -f2 | sort > /tmp/fi.k || return 1
            "$BIN/moobfs-index" --domain "$LOWER/fi/$d" kind exists - - | cut -f2 | sort > /tmp/fi.o || return 1
            cmp -s /tmp/fi.k /tmp/fi.o || { echo "domain $d: kernel $(wc -l < /tmp/fi.k) oracle $(wc -l < /tmp/fi.o)"; diff /tmp/fi.k /tmp/fi.o | head -5; return 1; }
        done
    }
    "$BIN/query" "$LAYER" info > /tmp/fi.info || fail fi-info
    flags=$(sed -n 's/.* flags=\(0x[0-9a-f]*\).*/\1/p' /tmp/fi.info)
    # The domains failnth only built (e*) were never written while built: whatever the faults
    # did to their builds, what is in the index now must be the whole answer.
    fi_cmp 'e*' || fail "fi-built-under-faults-differs-from-oracle"
    if [ $(( flags & 0x8 )) -ne 0 ]; then
        echo "FAULTS: the index declared itself incomplete (flags=$flags), as designed when an update fails after the lower write"
    else
        fi_cmp '*' || fail "fi-index-differs-from-oracle without the incomplete flag"
    fi
    umount "$LAYER" || fail fi-umount
    mount -t moobfs_layer -o lower="$LOWER/fi",domain_depth=1 none "$LAYER" || fail fi-remount
    fi_cmp '*' || fail fi-index-differs-after-remount
    # R2-6: a failed path allocation must not retain the pre-rename name after eviction.
    "$BIN/renamepath-fault" "$LAYER/r26-control" 0 || fail r26-control
    "$BIN/renamepath-fault" "$LAYER/r26-fault" 1 || fail r26-stale-path
    echo "RENAMEPATH_FAULT_PASS: control and injected rename after dentry eviction"
    # R2-10: remove a directory after the scan has listed its names. Function tracing
    # observes the actual scan; the existing delay parameter keeps the window open.
    mountpoint -q /sys/kernel/tracing || mount -t tracefs tracefs /sys/kernel/tracing || fail r210-tracefs
    echo 2000000 > /sys/module/moobfs_layer/parameters/scan_delay_us
    "$BIN/deadscan" "$LAYER/r210" || fail r210-dead-directory
    echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us
    echo "DEADSCAN_PASS: removal after listing does not fail the domain query"
    umount "$LAYER" && rm -rf "$LOWER/fi" && umount "$LOWER" || fail fi-cleanup
    # a reference the layer kept on a lower dentry shows only here, when the lower goes
    # ("Dentry still in use", found by syzkaller after a failed mount, journal 0112)
    dmesg > "$OUT/dmesg-lower.txt"
    if ! kdiag "$OUT/dmesg-lower.txt" 'BUG: Dentry|still in use|BUG:|^WARNING:'; then
        fail "fi-lower-pinned after the layer was unmounted"
    fi
    rmmod moobfs_layer || fail fi-rmmod
    echo "FAULT_INJECTION_PASS: $(grep '^FAILNTH_DONE' "$OUT/failnth.txt" | cut -d' ' -f2-); no kernel diagnostic; index equal to the oracle"
fi
if [ -n "${MOOB_ONLY:-}" ]; then
    echo "MOOBFS_KASAN_ONLY_PASS: steps $MOOB_ONLY under $SAN with no kernel diagnostics (not a complete run)"
else
    echo "MOOBFS_KASAN_PASS: layer suite under $SAN with no kernel diagnostics"
fi
