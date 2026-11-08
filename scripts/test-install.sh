#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Tests that scripts/install.sh never replaces an existing entry without FORCE=1, whatever its
# type, and that its temporary copy cannot be redirected (journal 0197, correction before merge).
# Needs GNU coreutils (Linux); runs as an ordinary user in a scratch directory.
#   sh scripts/test-install.sh
set -eu
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
MOOB_INSTALL_SOURCED=1
DESTDIR=$T/root
# shellcheck source=scripts/install.sh
. "$(dirname "$0")/install.sh"

fail() { echo "INSTALL_UNIT_FAIL: $*"; exit 1; }
no_temporary() {   # no_temporary <directory>: place left no .moobfs-install.* behind
	for f in "$1"/.moobfs-install.*; do
		[ ! -e "$f" ] && [ ! -L "$f" ] || return 1
	done
}
src=$T/src
printf 'new\n' >"$src"
outside=$T/outside
d=$BINDIR
mkdir -p "$d"

refused() {   # refused <case> <destination> [FORCE]: place must fail and leave no temporary
	if (FORCE=${3:-} place "$src" "$2" 755) >"$T/out" 2>&1; then
		fail "$1: replaced $2"
	fi
	no_temporary "$(dirname "$2")" || fail "$1: temporary left behind"
}
reset_outside() { printf 'outside\n' >"$outside"; }
outside_intact() { [ "$(cat "$outside")" = outside ] || fail "$1: wrote through a link"; }

# fresh destination
(place "$src" "$d/fresh" 755) >/dev/null || fail "fresh install"
[ "$(cat "$d/fresh")" = new ] && [ "$(stat -c %a "$d/fresh")" = 755 ] || fail "fresh content or mode"
no_temporary "$d" || fail "fresh: temporary left behind"

# regular file
printf 'old\n' >"$d/file"
refused "regular file" "$d/file"
[ "$(cat "$d/file")" = old ] || fail "regular file changed"

# dangling symbolic link: test -e is false for it
ln -s "$T/nowhere" "$d/dangling"
refused "dangling link" "$d/dangling"
[ -L "$d/dangling" ] && [ ! -e "$T/nowhere" ] || fail "dangling link followed or replaced"

# link to a file elsewhere
reset_outside
ln -s "$outside" "$d/link"
refused "link" "$d/link"
outside_intact "link"

# FIFO and directory; a directory also with FORCE=1
mkfifo "$d/fifo"
refused "fifo" "$d/fifo"
mkdir "$d/dir"
refused "directory" "$d/dir"
refused "directory with FORCE" "$d/dir" 1
[ -z "$(ls -A "$d/dir")" ] || fail "something was put inside the directory"

# the fixed temporary name of the first version, planted as a link, is not used
reset_outside
ln -s "$outside" "$d/planted.moobfs-new"
(place "$src" "$d/planted" 755) >/dev/null || fail "install next to a planted name"
outside_intact "planted temporary name"
[ "$(cat "$d/planted")" = new ] || fail "planted: wrong content"

# an entry that appears after the existence check is not replaced (the final step is link(2))
reset_outside
# shellcheck disable=SC2317,SC2329 # called by place() in place of mktemp(1); 0.9 reports SC2317
mktemp() { t=$(command mktemp "$@") && ln -s "$outside" "$d/race" && echo "$t"; }
refused "entry created after the check" "$d/race"
unset -f mktemp
[ -L "$d/race" ] || fail "race: link replaced"
outside_intact "race"

# FORCE=1 replaces a file, and a link itself rather than its target
(FORCE=1 place "$src" "$d/file" 644) >/dev/null || fail "FORCE on a file"
[ "$(cat "$d/file")" = new ] && [ "$(stat -c %a "$d/file")" = 644 ] || fail "FORCE: content or mode"
reset_outside
(FORCE=1 place "$src" "$d/link" 644) >/dev/null || fail "FORCE on a link"
[ ! -L "$d/link" ] && [ "$(cat "$d/link")" = new ] || fail "FORCE: link not replaced"
outside_intact "FORCE on a link"

# uninstall removes only regular files
mkdir -p "$MODDIR" "$MANDIR"
printf 'ko\n' >"$MODDIR/moobfs_layer.ko"
reset_outside
ln -s "$outside" "$BINDIR/moobfs-attr"
(do_uninstall) >/dev/null 2>"$T/out" || fail "uninstall"
[ ! -e "$MODDIR/moobfs_layer.ko" ] || fail "uninstall left the module"
[ -L "$BINDIR/moobfs-attr" ] && grep -q 'not a regular file' "$T/out" || fail "uninstall removed a link"
outside_intact "uninstall"

# a space in the staging root: every path stays one word, so nothing else is removed
(
	DESTDIR="$T/with space"
	MODDIR=$DESTDIR/lib/modules/$KVER/updates/moobfs
	BINDIR=$DESTDIR$PREFIX/bin
	MANDIR=$DESTDIR$PREFIX/share/man/man1
	SHAREDIR=$DESTDIR$PREFIX/share/moobfs
	mkdir -p "$BINDIR"
	printf 'tool\n' >"$BINDIR/moobfs"
	printf 'decoy\n' >"$T/with"
	do_uninstall >/dev/null 2>&1 &&
		[ ! -e "$BINDIR/moobfs" ] && [ "$(cat "$T/with")" = decoy ]
) || fail "uninstall with a space in DESTDIR"

echo "INSTALL_UNIT_PASS"
