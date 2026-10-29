#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of `make package-test` (roadmap item C6,
# ADR-0028), booted with UEFI Secure Boot. Follows docs/operations.md, "Packages", and prints
# markers.
#   phase 0: packages installed; DKMS builds and signs the module for the listed kernel, refuses
#            an unlisted one; the module is refused while its key is not enrolled
#   (the host then powers the guest off and enrols /var/lib/dkms/mok.pub in the MokList)
#   phase 1: key enrolled; an unsigned build is refused, the signed module loads; two users;
#            upgrade and roll back against the oracle; purge; make install copy refused
set -eu
cd /home/tester
KVER=6.12.113+deb13-amd64   # the kernel of the 2026-10-21 cloud image (journal 0206)
V1=0.5.0~c6test1
V2=0.5.0~c6test2
mark() { echo "$*"; }
fail() { echo "MOOBFS_PACKAGE_FAIL: $*"; exit 1; }
as() { u=$1; shift; runuser -u "$u" -- "$@"; }
export DEBIAN_FRONTEND=noninteractive

secure_boot() {
	mokutil --sb-state 2>/dev/null | grep -q 'SecureBoot enabled' || fail "Secure Boot is not enabled"
	grep -q '\[integrity\]\|\[confidentiality\]' /sys/kernel/security/lockdown || fail "lockdown is off"
	mark "SECURE_BOOT: $(mokutil --sb-state | head -n 1); lockdown $(cat /sys/kernel/security/lockdown)"
}

# The one installed version, and the module file DKMS put in place for it.
installed() {   # installed <version>
	st=$(dkms status moobfs)
	[ "$(echo "$st" | grep -c .)" -eq 1 ] || fail "dkms status: $st"
	echo "$st" | grep -q "^moobfs/$1, $KVER, x86_64: installed" || fail "dkms status: $st"
	[ "$(dpkg-query -W -f '${Version}' moobfs-dkms)" = "$1" ] || fail "moobfs-dkms is not $1"
	[ "$(dpkg-query -W -f '${Version}' moobfs-tools)" = "$1" ] || fail "moobfs-tools is not $1"
	# DKMS may compress the module as the kernel's own are (moobfs_layer.ko.xz)
	ko=$(modinfo -k "$KVER" -F filename moobfs_layer)
	case $ko in
	"/lib/modules/$KVER/updates/dkms/moobfs_layer.ko"*) ;;
	*) ls -lR "/lib/modules/$KVER/updates" "/var/lib/dkms/moobfs" >&2; fail "modinfo finds $ko, not the DKMS module" ;;
	esac
	cmp -s "$ko" "/var/lib/dkms/moobfs/$1/$KVER/x86_64/module/$(basename "$ko")" ||
		fail "$ko is not the $1 build"
	[ "$(modinfo -F signer "$ko")" = "DKMS module signing key" ] || fail "$ko is not signed by the DKMS key"
}

mount_layer() {
	modprobe moobfs_layer
	mount -t moobfs_layer -o lower=/srv/moobfs/lower,nolinks none /srv/memory
}

unmount_layer() {
	umount /srv/memory
	rmmod moobfs_layer
}

# The oracle is what the test wrote: every third note of alice's is a decision.
oracle() {
	i=1
	while [ "$i" -le 60 ]; do
		[ $((i % 3)) -ne 0 ] || echo "note-$i.md"
		i=$((i + 1))
	done | sort
}

verify() {   # verify <label>
	as alice moobfs query /srv/memory/alice kind eq string decision | sort >"query-$1.txt"
	cmp -s query-oracle.txt "query-$1.txt" || fail "$1: alice's query differs from the oracle"
	rc=0
	as bob moobfs query /srv/memory/alice kind eq string decision >/dev/null 2>&1 || rc=$?
	[ "$rc" -eq 77 ] || fail "$1: bob's query on alice's domain exited $rc, not 77"
	if as bob cat /srv/memory/alice/note-3.md 2>/dev/null; then fail "$1: bob read alice's note"; fi
	moobfs-check /srv/memory >"check-$1.json" || fail "$1: moobfs-check exit $?"
	cmp -s check-installed.json "check-$1.json" || fail "$1: moobfs-check report changed"
	mark "VERIFIED: $1; $(wc -l <"query-$1.txt") decisions equal to the oracle, bob refused, moobfs-check unchanged"
}

phase0() {
	[ "$(uname -r)" = "$KVER" ] || fail "the image booted $(uname -r), not $KVER"
	apt-get update -q >/dev/null
	apt-get install -y -q mokutil attr "linux-headers-$KVER" >/dev/null
	secure_boot
	tar -xf debs.tar
	# section "Packages": apt resolves dkms and the compiler
	apt-get install -y -q ./debs/v1/moobfs-dkms_${V1}_all.deb ./debs/v1/moobfs-tools_${V1}_amd64.deb \
		>install-v1.log 2>&1 || { tail -n 30 install-v1.log; fail "apt-get install $V1"; }
	installed "$V1"
	mark "INSTALLED: moobfs-dkms and moobfs-tools $V1; dkms built and signed $(modinfo -k "$KVER" -F filename moobfs_layer)"
	rc=0
	dkms build "moobfs/$V1" -k 6.12.999+deb13-amd64 >unlisted.log 2>&1 || rc=$?
	[ "$rc" -eq 77 ] || fail "dkms build for an unlisted kernel exited $rc, not 77"
	grep -q BUILD_EXCLUSIVE unlisted.log || fail "unlisted kernel: unexpected message"
	mark "UNLISTED_KERNEL_REFUSED: dkms exit 77 for 6.12.999+deb13-amd64"
	mokutil --test-key /var/lib/dkms/mok.pub 2>&1 | grep -q 'is not enrolled' || fail "the DKMS key is already enrolled"
	if modprobe moobfs_layer 2>modprobe.err; then fail "the module loaded with its key not enrolled"; fi
	grep -q 'Key was rejected by service' modprobe.err || fail "refusal: $(cat modprobe.err)"
	mark "NOT_ENROLLED_REFUSED: $(cat modprobe.err)"
	# what `mokutil --import` would be given; the host enrols it while the guest is off
	install -m 0644 /var/lib/dkms/mok.pub /home/tester/mok.der
	mark "PHASE_DONE: 0"
}

phase1() {
	[ "$(uname -r)" = "$KVER" ] || fail "booted $(uname -r), not $KVER"
	secure_boot
	mokutil --test-key /var/lib/dkms/mok.pub 2>&1 | grep -q 'is already enrolled' || fail "the DKMS key is not enrolled"
	mark "ENROLLED: $(mokutil --test-key /var/lib/dkms/mok.pub 2>&1)"
	# an unsigned build of the same sources is refused under lockdown
	rm -rf unsigned && cp -R "/usr/src/moobfs-$V1" unsigned
	make -C "/lib/modules/$KVER/build" M="$PWD/unsigned/layer" modules >unsigned.log 2>&1 ||
		{ tail -n 20 unsigned.log; fail "unsigned build"; }
	[ -z "$(modinfo -F signer unsigned/layer/moobfs_layer.ko)" ] || fail "the hand build is signed"
	if insmod unsigned/layer/moobfs_layer.ko 2>insmod.err; then fail "an unsigned module loaded"; fi
	grep -q 'Key was rejected by service' insmod.err || fail "unsigned refusal: $(cat insmod.err)"
	mark "UNSIGNED_REFUSED: $(cat insmod.err)"
	installed "$V1"
	install -d -m 0700 /srv/moobfs
	install -d -m 0755 /srv/moobfs/lower /srv/memory
	mount_layer
	mark "SIGNED_LOADED: $(grep '^moobfs_layer ' /proc/modules | cut -d ' ' -f 1,6)"
	useradd -m alice
	useradd -m bob
	install -d -o alice -g alice -m 0700 /srv/memory/alice
	install -d -o bob -g bob -m 0700 /srv/memory/bob
	i=1
	while [ "$i" -le 60 ]; do
		kind=note
		[ $((i % 3)) -ne 0 ] || kind=decision
		as alice sh -c "echo 'note $i' >/srv/memory/alice/note-$i.md"
		as alice moobfs-attr set "/srv/memory/alice/note-$i.md" kind string "$kind"
		as bob sh -c "echo 'bob $i' >/srv/memory/bob/b-$i.md"
		as bob moobfs-attr set "/srv/memory/bob/b-$i.md" kind string decision
		i=$((i + 1))
	done
	oracle >query-oracle.txt
	moobfs-check /srv/memory >check-installed.json || fail "moobfs-check exit $?"
	verify installed
	# upgrade: the guide unmounts and unloads first, so the new build is the one loaded
	unmount_layer
	apt-get install -y -q ./debs/v2/moobfs-dkms_${V2}_all.deb ./debs/v2/moobfs-tools_${V2}_amd64.deb \
		>install-v2.log 2>&1 || { tail -n 30 install-v2.log; fail "upgrade to $V2"; }
	installed "$V2"
	mount_layer
	verify upgraded
	unmount_layer
	apt-get install -y -q --allow-downgrades ./debs/v1/moobfs-dkms_${V1}_all.deb \
		./debs/v1/moobfs-tools_${V1}_amd64.deb >rollback.log 2>&1 || { tail -n 30 rollback.log; fail "roll back to $V1"; }
	installed "$V1"
	mount_layer
	verify "rolled-back"
	unmount_layer
	apt-get purge -y -q moobfs-dkms moobfs-tools >purge.log 2>&1 || { tail -n 30 purge.log; fail "purge"; }
	if modinfo -k "$KVER" moobfs_layer >/dev/null 2>&1; then fail "module still installed after purge"; fi
	[ -z "$(dkms status moobfs)" ] || fail "dkms still lists moobfs"
	[ ! -e /usr/bin/moobfs ] || fail "tools still installed"
	getfattr -n user.moob.kind /srv/moobfs/lower/alice/note-3.md | grep -q '^user.moob.kind=' ||
		fail "attribute not on the lower"
	grep -q 'note 3' /srv/moobfs/lower/alice/note-3.md || fail "content not on the lower"
	mark "PURGED: module and tools removed; content and user.moob.kind readable on the lower"
	# a copy from `make install` must be removed before the package goes in
	mkdir -p "/lib/modules/$KVER/updates/moobfs"
	: >"/lib/modules/$KVER/updates/moobfs/moobfs_layer.ko"
	if apt-get install -y -q "./debs/v1/moobfs-dkms_${V1}_all.deb" >conflict.log 2>&1; then
		fail "moobfs-dkms installed next to a make install copy"
	fi
	grep -q 'was installed by make install' conflict.log || fail "conflict: unexpected message"
	rm -r "/lib/modules/$KVER/updates/moobfs"
	mark "MAKE_INSTALL_COPY_REFUSED"
	mark "PHASE_DONE: 1"
}

case ${1:-} in
0) phase0 ;;
1) phase1 ;;
*) fail "usage: guest.sh 0|1" ;;
esac
