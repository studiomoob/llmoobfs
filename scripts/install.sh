#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Installs or removes the MoobFS layer module and its user-space tools on a Linux host
# (roadmap item C2, docs/operations.md). Called by `make install` and `make uninstall`.
#
#   sh scripts/install.sh install     build against KVER, copy, depmod
#   sh scripts/install.sh uninstall   remove what install copied, depmod
#
# Environment: KVER (default: uname -r), KDIR (default: /lib/modules/$KVER/build),
# PREFIX (default: /usr/local), DESTDIR (staging root; skips depmod and the root check),
# FORCE=1 (replace files that already exist), MOOB_ALLOW_UNSUPPORTED=1 (install on a kernel
# outside scripts/supported-kernels; the result is untested and unsupported).
set -eu
cd "$(dirname "$0")/.."

ACTION=${1:-}
KVER=${KVER:-$(uname -r)}
KDIR=${KDIR:-/lib/modules/$KVER/build}
PREFIX=${PREFIX:-/usr/local}
DESTDIR=${DESTDIR:-}
BUILD=build/install
MODDIR=$DESTDIR/lib/modules/$KVER/updates/moobfs
BINDIR=$DESTDIR$PREFIX/bin
MANDIR=$DESTDIR$PREFIX/share/man/man1
# the list `moobfs mount` checks the running kernel against (ADR-0035)
SHAREDIR=$DESTDIR$PREFIX/share/moobfs
TOOLS='moobfs moobfs-attr moobfs-check moobfs-index'

die() { echo "moobfs install: $*" >&2; exit 1; }

need_root() {
	[ -n "$DESTDIR" ] || [ "$(id -u)" -eq 0 ] || die "run as root (sudo make $ACTION), or set DESTDIR"
}

supported() {
	grep -v '^#' scripts/supported-kernels | grep -qx "$KVER"
}

check_kernel() {
	if supported; then
		return 0
	fi
	if [ "${MOOB_ALLOW_UNSUPPORTED:-}" = 1 ]; then
		echo "moobfs install: WARNING: kernel $KVER is not in scripts/supported-kernels;" >&2
		echo "moobfs install: the module was never tested on it (docs/compatibility.md)" >&2
		return 0
	fi
	echo "moobfs install: kernel $KVER is not supported. Supported:" >&2
	grep -v '^#' scripts/supported-kernels | sed 's/^/  /' >&2
	die "see docs/operations.md; MOOB_ALLOW_UNSUPPORTED=1 overrides at your own risk"
}

# Creation must not overwrite existing files by default (AGENTS.md). Any entry counts as
# existing, a dangling symbolic link included. The copy goes to a new temporary name from
# mktemp(1), never a fixed one, and reaches the destination through link(2), which fails on an
# existing entry, so one that appears after the check is not replaced either. FORCE=1 renames
# over a file or link; a directory is never replaced.
place() {   # place <source> <destination> <mode>
	if [ -d "$2" ] && [ ! -L "$2" ]; then
		die "$2 is a directory; not replacing it"
	fi
	if [ "${FORCE:-}" != 1 ] && { [ -e "$2" ] || [ -L "$2" ]; }; then
		die "$2 exists; uninstall first, or FORCE=1 to replace it"
	fi
	mkdir -p "$(dirname "$2")"
	tmp=$(mktemp "$(dirname "$2")/.moobfs-install.XXXXXX") || die "cannot create a file next to $2"
	if ! { cp "$1" "$tmp" && chmod "$3" "$tmp"; }; then
		rm -f "$tmp"
		die "cannot copy $1 next to $2"
	fi
	if [ "${FORCE:-}" = 1 ]; then
		mv -fT "$tmp" "$2" || { rm -f "$tmp"; die "cannot replace $2"; }
	else
		ln -T "$tmp" "$2" 2>/dev/null || { rm -f "$tmp"; die "$2 exists; uninstall first, or FORCE=1 to replace it"; }
		rm -f "$tmp"
	fi
	echo "installed $2"
}

secure_boot_note() {
	if command -v mokutil >/dev/null 2>&1 && mokutil --sb-state 2>/dev/null | grep -q 'SecureBoot enabled'; then
		echo "moobfs install: Secure Boot is enabled; the kernel refuses this unsigned module" >&2
		echo "moobfs install: until you sign it with your own MOK (docs/operations.md, Secure Boot)" >&2
	fi
}

do_install() {
	need_root
	check_kernel
	[ -d "$KDIR" ] || die "no kernel headers at $KDIR (Debian: apt install linux-headers-$KVER)"
	make layer KDIR="$KDIR"
	make attr-tools BUILD="$BUILD"
	place layer/moobfs_layer.ko "$MODDIR/moobfs_layer.ko" 644
	for t in $TOOLS; do
		place "$BUILD/$t" "$BINDIR/$t" 755
		place "man/$t.1" "$MANDIR/$t.1" 644
	done
	place scripts/supported-kernels "$SHAREDIR/supported-kernels" 644
	if [ -z "$DESTDIR" ]; then
		depmod -a "$KVER"
		secure_boot_note
	fi
	echo "moobfs install: done for kernel $KVER; next: docs/operations.md, Mount"
}

remove_installed() {   # remove_installed <path>
	if [ -f "$1" ] && [ ! -L "$1" ]; then
		rm -f "$1"
		echo "removed $1"
	elif [ -e "$1" ] || [ -L "$1" ]; then
		echo "left $1: not a regular file, so install did not put it there" >&2
	fi
}

do_uninstall() {
	need_root
	if [ -z "$DESTDIR" ] && grep -q '^moobfs_layer ' /proc/modules 2>/dev/null; then
		die "moobfs_layer is loaded; unmount every moobfs_layer mount and run rmmod moobfs_layer first"
	fi
	remove_installed "$MODDIR/moobfs_layer.ko"
	for t in $TOOLS; do
		remove_installed "$BINDIR/$t"
		remove_installed "$MANDIR/$t.1"
	done
	remove_installed "$SHAREDIR/supported-kernels"
	rmdir "$MODDIR" "$SHAREDIR" 2>/dev/null || true
	[ -n "$DESTDIR" ] || depmod -a "$KVER"
	echo "moobfs uninstall: done for kernel $KVER; lower directories are untouched"
}

# scripts/test-install.sh sources this file to test place() and do_uninstall().
[ "${MOOB_INSTALL_SOURCED:-}" != 1 ] || return 0

case $ACTION in
install) do_install ;;
uninstall) do_uninstall ;;
*) die "usage: sh scripts/install.sh install|uninstall" ;;
esac
