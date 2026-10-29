#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of vm/install-test/ (MOOB_TEST=bench,
# journal 0238). The steps of issue #68 on the image's own kernel, on a lower of MOOB_BENCH_FS
# (ext4, btrfs or xfs) made on the second virtual disk: build, load the module, mount, generate
# 60 000 memories of profile 8, write them with W1, run bench/ondemand.sh on domain ns3, and
# leave the results in /home/tester/results.tar for in-container.sh to copy out.
#   phase 0: everything above (one boot)
# A trend monitor on a shared virtual machine, not a reference measurement: docs/testing.md.
set -eu
cd /home/tester
SRC=/home/tester/llmoobfs
KVER=6.12.113+deb13-amd64   # the kernel of the 2026-10-21 cloud image, as in vm/install-test/guest.sh
FS=${MOOB_BENCH_FS:-ext4}
DEV=/dev/vdb                # the second -drive of in-container.sh, an empty sparse file
LOWERFS=/srv/moob/$FS
LAYER=/srv/moob/layer
OUT=/home/tester/results
BIN=$SRC/build/host
mark() { echo "$*"; }
# what there is of the results goes out on a failure too (in-container.sh copies results.tar)
save() {
	[ ! -d "$OUT" ] || { tar -C /home/tester -cf /home/tester/results.tar results && chown tester:tester /home/tester/results.tar; }
}
fail() { save 2>/dev/null || true; echo "MOOBFS_BENCH_FAIL: $*"; exit 1; }
now() { date +%s; }

phase0() {
	[ "$(uname -r)" = "$KVER" ] || fail "the image booted $(uname -r), not $KVER"
	case $FS in
	ext4) pkg=e2fsprogs ;;
	btrfs) pkg=btrfs-progs ;;
	xfs) pkg=xfsprogs ;;
	*) fail "MOOB_BENCH_FS=$FS, not ext4, btrfs or xfs" ;;
	esac
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -q >/dev/null
	apt-get install -y -q build-essential libsqlite3-dev attr python3 util-linux "$pkg" \
		"linux-headers-$KVER" >/dev/null
	mkdir -p "$SRC" && tar -xf src.tar -C "$SRC"
	cd "$SRC"
	make layer >build-layer.log 2>&1 || { tail -n 20 build-layer.log; fail "make layer"; }
	make bench-tools attr-tools >build-tools.log 2>&1 || { tail -n 20 build-tools.log; fail "make bench-tools attr-tools"; }
	[ -x "$BIN/query" ] || fail "bench-tools did not build $BIN/query"
	mark "BUILT: layer/moobfs_layer.ko and $(cd "$BIN" && echo *)"

	# The lower: refuse a device that already carries a filesystem (never overwrite one)
	[ -b "$DEV" ] || fail "no second disk $DEV"
	if blkid -p "$DEV" >/dev/null 2>&1; then fail "$DEV already carries a filesystem"; fi
	case $FS in
	ext4) mkfs.ext4 -q "$DEV" ;;
	btrfs) mkfs.btrfs -q "$DEV" >/dev/null ;;
	xfs) mkfs.xfs -q "$DEV" ;;
	esac
	mkdir -p "$LOWERFS" "$LAYER"
	mount "$DEV" "$LOWERFS"
	mkdir "$LOWERFS/lower"
	insmod layer/moobfs_layer.ko
	mount -t moobfs_layer -o "lower=$LOWERFS/lower" none "$LAYER"
	mark "MOUNTED: $(findmnt -n -o FSTYPE,OPTIONS "$LOWERFS") under $(findmnt -n -o FSTYPE,OPTIONS "$LAYER")"
	# bench/ondemand.sh runs the steady-state queries with --uid 1001: setuid needs no account,
	# but one makes the identity explicit (tester, the cloud-init user, is 1000)
	getent passwd 1001 >/dev/null || useradd -u 1001 -M -s /usr/sbin/nologin moobbench
	mark "UID_1001: $(getent passwd 1001 | cut -d: -f1)"

	mkdir -p "$OUT"
	"$BIN/bench-gen" --seed 1 --inodes 60000 --profile 8 --out /home/tester/m.txt
	t=$(now)
	"$BIN/bench-load" --manifest /home/tester/m.txt --target "$LAYER" --workload W1 --procs 4 \
		--out /home/tester/w1 || fail "bench-load W1"
	sync
	load_s=$(($(now) - t))
	mark "LOADED: 60000 memories with W1 in $load_s s"
	t=$(now)
	env BIN="$BIN" sh bench/ondemand.sh "$LAYER" ns3 "$OUT" >"$OUT/ondemand.log" 2>&1 ||
		{ tail -n 20 "$OUT/ondemand.log"; fail "bench/ondemand.sh"; }
	grep -q '^ONDEMAND_DONE' "$OUT/ondemand.log" || fail "bench/ondemand.sh did not finish"
	mark "ONDEMAND: $(($(now) - t)) s"
	python3 bench/report.py --out "$OUT/report" "$OUT"/rep[0-9]* >report.log 2>&1 ||
		{ tail -n 20 report.log; fail "bench/report.py"; }
	# What issue #68 asks to send back besides times.txt and environment.txt
	{
		echo "commit: ${MOOB_COMMIT:-unknown}"
		echo "kernel: $(uname -r)"
		echo "lower filesystem: $FS on $DEV ($(findmnt -n -o OPTIONS "$LOWERFS"))"
		case ${MOOB_BENCH_CACHE:-host} in
		direct) echo "lower disk: virtio-blk, raw sparse file on the host, O_DIRECT, guest flushes ignored, $(lsblk -dn -o SIZE "$DEV")" ;;
		*) echo "lower disk: virtio-blk, raw sparse file in the host's page cache (cold rows are cold in the guest only), guest flushes ignored, $(lsblk -dn -o SIZE "$DEV")" ;;
		esac
		echo "cpu: $(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -n 1) (as the guest sees it, -cpu host)"
		echo "vcpus: $(nproc)"
		echo "memory: $(sed -n 's/^MemTotal: *//p' /proc/meminfo)"
		echo "hypervisor: KVM (QEMU, Debian 13 generic cloud image 20261021-2638)"
		echo "load_W1_s: $load_s"
	} >"$OUT/system.txt"
	cat "$OUT/times.txt" "$OUT/system.txt"
	umount "$LAYER"
	save
	mark "PHASE_DONE: 0"
}

case ${1:-} in
0) phase0 ;;
*) fail "usage: guest.sh 0" ;;
esac
