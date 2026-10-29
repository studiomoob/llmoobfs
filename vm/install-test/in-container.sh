#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs inside the moobfs-install-test container (vm/install-test/run.sh). Boots the Debian 13
# generic cloud image under QEMU with a throwaway overlay, configures it through cloud-init
# NoCloud over HTTP, and drives a guest script over SSH, rebooting between its phases.
#   MOOB_TEST=install (default)  vm/install-test/guest.sh, phases 0 1 2 (roadmap C2)
#   MOOB_TEST=container          vm/container-test/guest.sh, phases 0 1 (roadmap C3)
#   MOOB_TEST=package            vm/package-test/guest.sh, phases 0 1 (roadmap C6, ADR-0028):
#     builds the .deb packages first, boots with UEFI Secure Boot, and between the phases
#     powers the guest off and enrols the guest's DKMS key in the firmware's MokList
#   MOOB_TEST=bench              vm/bench-test/guest.sh, phase 0 (journal 0238): bench/ondemand.sh
#     on a lower of MOOB_BENCH_FS (ext4, btrfs or xfs, default ext4) made on a second virtual
#     disk; KVM is required (emulated timings mean nothing) and the results are copied out to
#     build/bench-test/results/
# With /dev/kvm writable the guest runs under KVM, otherwise under TCG emulation. MOOB_WORK is the
# repository root (default /work, the container's bind mount; run.sh sets it for MOOB_HOST_QEMU=1).
set -eu
cd "${MOOB_WORK:-/work}"
if [ -w /dev/kvm ]; then
	ACCEL="-accel kvm -cpu host" ACCEL_NAME=KVM
else
	ACCEL="-accel tcg,thread=multi -cpu max" ACCEL_NAME="TCG emulation"
fi
case ${MOOB_TEST:-install} in
install) OUT=build/install-test GUEST=vm/install-test/guest.sh PHASES="0 1 2" NAME=INSTALL ;;
container) OUT=build/container-test GUEST=vm/container-test/guest.sh PHASES="0 1" NAME=CONTAINER ;;
package) OUT=build/package-test GUEST=vm/package-test/guest.sh PHASES="0 1" NAME=PACKAGE ;;
bench) OUT=build/bench-test GUEST=vm/bench-test/guest.sh PHASES=0 NAME=BENCH ;;
*) echo "unknown MOOB_TEST ${MOOB_TEST}" >&2; exit 64 ;;
esac
CACHE=build/install-test	# the base image is shared by both tests
mkdir -p "$OUT" "$CACHE"
IMG_DATE=20261021-2638
IMG=debian-13-generic-amd64-$IMG_DATE.qcow2
URL=https://cloud.debian.org/images/cloud/trixie/$IMG_DATE/$IMG
# SHA-512 from that directory's SHA512SUMS, read on 2026-10-25
SUM=70ef6adc1c16155f8d575731b37d0f976d1c60b6144ac3f57914a3f4b30b202b69584d5d143aa39da0f0fe81df9e5b698909c0d94c7d91c6f6c4bb6581ead5f0
LOG=$OUT/log.txt
: >"$LOG"
log() { echo "$*" | tee -a "$LOG"; }
GUEST_ENV=
DISKS=
if [ "$NAME" = BENCH ]; then
	BENCH_FS=${MOOB_BENCH_FS:-ext4}
	case $BENCH_FS in ext4 | btrfs | xfs) ;; *) log "MOOBFS_BENCH_FAIL: MOOB_BENCH_FS=$BENCH_FS, not ext4, btrfs or xfs"; exit 64 ;; esac
	[ "$ACCEL_NAME" = KVM ] || { log "MOOBFS_BENCH_FAIL: no writable /dev/kvm; timings under emulation mean nothing"; exit 1; }
	GUEST_ENV="MOOB_BENCH_FS=$BENCH_FS MOOB_COMMIT=${MOOB_COMMIT:-unknown}"
	# The lower gets a disk of its own, a sparse raw file; the guest's flushes are ignored (W1
	# fsyncs 120 000 times and the disk is thrown away). By default the host's page cache stays
	# in front of it (cache=unsafe), so a cold cache in the guest is warm on the host: with
	# O_DIRECT (MOOB_BENCH_CACHE=direct) the cold first QUERY2 of ns3 exceeded the ten-second
	# build deadline of ADR-0019 on the test host and was refused with ETIMEDOUT (journal 0238).
	rm -rf "$OUT/lower.img" "$OUT/results" "$OUT/results.tar"
	truncate -s 8G "$OUT/lower.img"
	case ${MOOB_BENCH_CACHE:-host} in
	host) LOWER_CACHE=cache=unsafe ;;
	direct) LOWER_CACHE=cache.direct=on,cache.no-flush=on ;;
	*) log "MOOBFS_BENCH_FAIL: MOOB_BENCH_CACHE=$MOOB_BENCH_CACHE, not host or direct"; exit 64 ;;
	esac
	GUEST_ENV="$GUEST_ENV MOOB_BENCH_CACHE=${MOOB_BENCH_CACHE:-host}"
	DISKS="-drive file=$OUT/lower.img,if=virtio,format=raw,$LOWER_CACHE"
fi

if [ ! -f "$CACHE/$IMG" ]; then
	curl -fsSL -o "$CACHE/$IMG.part" "$URL"
	mv "$CACHE/$IMG.part" "$CACHE/$IMG"
fi
echo "$SUM  $CACHE/$IMG" | sha512sum -c - >/dev/null || { log "MOOBFS_${NAME}_FAIL: image checksum"; exit 1; }

# A fresh overlay every run: the base image is never written.
rm -f "$OUT/disk.qcow2" "$OUT"/console-*.log
UEFI=
if [ "$NAME" = PACKAGE ]; then
	# Two versions of each package, so the guest can upgrade and roll back. Built from a copy
	# of the tree; a fresh directory every run.
	rm -rf "$OUT/debs"
	sh scripts/build-debs.sh 0.5.0~c6test1 "$OUT/debs/v1" >>"$LOG"
	sh scripts/build-debs.sh 0.5.0~c6test2 "$OUT/debs/v2" >>"$LOG"
	tar -C "$OUT" -cf "$OUT/debs.tar" debs
	# Secure Boot: the firmware's own code with the Microsoft keys enrolled, which accept the
	# image's signed shim; a fresh copy of the variable store every run.
	cp /usr/share/OVMF/OVMF_VARS_4M.ms.fd "$OUT/vars.fd"
	UEFI="-machine q35,smm=on -global driver=cfi.pflash01,property=secure,value=on
		-drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd
		-drive if=pflash,format=raw,unit=1,file=$OUT/vars.fd"
fi
qemu-img create -q -f qcow2 -F qcow2 -b "$(pwd)/$CACHE/$IMG" "$OUT/disk.qcow2" 16G

# The key stays in the container: a bind mount from macOS may not keep mode 0600.
SEED=$(mktemp -d)
KEY=$SEED/key
ssh-keygen -q -t ed25519 -N '' -f "$KEY"
QPID=
HPID=
cleanup() {
	[ -z "$QPID" ] || kill "$QPID" 2>/dev/null || true
	[ -z "$HPID" ] || kill "$HPID" 2>/dev/null || true
	rm -rf "$SEED"
	[ "$NAME" != BENCH ] || rm -f "$OUT/lower.img"
}
trap cleanup EXIT
printf 'instance-id: moobfs-install-test\nlocal-hostname: moobfs-test\n' >"$SEED/meta-data"
cat >"$SEED/user-data" <<EOF
#cloud-config
users:
  - name: tester
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys:
      - $(cat "$KEY.pub")
package_update: false
package_upgrade: false
EOF
python3 -m http.server 8000 --bind 127.0.0.1 --directory "$SEED" >/dev/null 2>&1 &
HPID=$!

boot() {
	# shellcheck disable=SC2086   # $ACCEL, $UEFI and $DISKS hold several options or none
	qemu-system-x86_64 $ACCEL $UEFI -smp 4 -m 4096 \
		-drive file="$OUT/disk.qcow2",if=virtio,format=qcow2 $DISKS \
		-smbios 'type=1,serial=ds=nocloud;s=http://10.0.2.2:8000/' \
		-nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2222-:22 \
		-display none -serial "file:$OUT/console-$1.log" &
	QPID=$!
}

SSH="ssh -q -i $KEY -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 tester@127.0.0.1"
wait_ssh() {
	i=0
	while ! $SSH true 2>/dev/null; do
		i=$((i + 1))
		[ "$i" -lt 180 ] || { log "MOOBFS_${NAME}_FAIL: no ssh after boot $1"; exit 1; }
		sleep 10
	done
	log "BOOTED: $1 after about $((i * 10)) s"
}
# The guest reboots itself; QEMU keeps running across a guest reboot.
reboot_guest() {
	$SSH 'sudo systemctl reboot' || true
	sleep 30
	wait_ssh "$1"
}
# MokManager would ask at the console to confirm `mokutil --import`; the test writes the same
# certificate into the firmware's MokList with virt-fw-vars while the guest is off, with the
# owner GUID shim gives enrolled keys (SHIM_LOCK_GUID).
enrol_mok() {
	scp -q -i "$KEY" -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		tester@127.0.0.1:mok.der "$SEED/mok.der" || { log "MOOBFS_${NAME}_FAIL: no mok.der"; exit 1; }
	$SSH 'sudo systemctl poweroff' || true
	wait "$QPID" || true
	QPID=
	openssl x509 -inform DER -in "$SEED/mok.der" -out "$SEED/mok.pem"
	virt-fw-vars --input "$OUT/vars.fd" --output "$OUT/vars-mok.fd" \
		--add-mok 605dab50-e046-4300-abb6-3dd810dd8b23 "$SEED/mok.pem" >>"$LOG" 2>&1
	mv "$OUT/vars-mok.fd" "$OUT/vars.fd"
	log "MOK_ENROLLED: $(openssl x509 -in "$SEED/mok.pem" -noout -subject -fingerprint -sha256 | tr '\n' ' ')"
	boot "$1"
	wait_ssh "$1"
}
# bench: the guest's results.tar into $OUT/results/, after a failed phase too (what there is)
fetch_results() {
	[ "$NAME" = BENCH ] || return 0
	scp -q -i "$KEY" -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		tester@127.0.0.1:results.tar "$OUT/results.tar" 2>/dev/null || return 1
	tar -xf "$OUT/results.tar" -C "$OUT"	# build/bench-test/results/
}
phase() {
	$SSH "sudo $GUEST_ENV sh guest.sh $1" 2>&1 | tee -a "$LOG" || true
	grep -q "PHASE_DONE: $1" "$LOG" || { fetch_results; log "MOOBFS_${NAME}_FAIL: phase $1"; exit 1; }
}

START=$(date +%s)
boot 1
wait_ssh 1
cp "$GUEST" "$SEED/guest.sh"
FILES="$OUT/src.tar"
[ "$NAME" != PACKAGE ] || FILES="$OUT/debs.tar"
# shellcheck disable=SC2086   # $FILES is one or two paths without spaces
scp -q -i "$KEY" -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	$FILES "$SEED/guest.sh" tester@127.0.0.1:
n=1
for p in $PHASES; do
	if [ "$n" -gt 1 ] && [ "$NAME" = PACKAGE ]; then
		enrol_mok "$n"
	elif [ "$n" -gt 1 ]; then
		reboot_guest "$n"
	fi
	phase "$p"
	n=$((n + 1))
done
log "ELAPSED: $(($(date +%s) - START)) s under $ACCEL_NAME"
if [ "$NAME" = BENCH ]; then
	fetch_results || { log "MOOBFS_BENCH_FAIL: no results.tar"; exit 1; }
fi
case $NAME in
INSTALL) log "MOOBFS_INSTALL_PASS: docs/operations.md followed on Debian 13; refused an unlisted kernel release, installed on the image's 6.12.113, mounted by hand, by fstab at boot and by a mount unit, two users isolated, uninstalled with the lower readable" ;;
PACKAGE) log "MOOBFS_PACKAGE_PASS: moobfs-dkms and moobfs-tools on Debian 13 under Secure Boot; DKMS refused an unlisted kernel, the module was refused before its key was enrolled and loaded after, an unsigned build was refused, two users isolated, upgrade and roll back with the query and moobfs-check equal to the oracle, purge left the lower readable" ;;
BENCH) log "MOOBFS_BENCH_PASS: bench/ondemand.sh on a $BENCH_FS lower, Debian 13, kernel 6.12.113, under KVM; results in $OUT/results" ;;
CONTAINER) log "MOOBFS_CONTAINER_PASS: docs/container-integration.md followed on Debian 13 with Docker and Podman" ;;
esac
