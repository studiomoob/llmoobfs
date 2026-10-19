#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Interoperability probe: what common xattr-aware tools see and do through a layer mount
# (getfattr/setfattr, fselect, cshatag, Recoll, KDE KFileMetaData). Runs inside a virtme-ng
# guest booted by .github/workflows/interop.yml; the runner's root filesystem is the guest's,
# so the tools apt-installed on the runner are found here. It records, it does not judge:
# every command is printed with "+", followed by its output and "[exit N]". Only a setup
# failure ends it early (INTEROP_SETUP_FAIL); the end marker is INTEROP_DONE.
# Usage (inside the guest, as root): sh vm/interop-guest.sh
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/build/interop
BIN=$OUT/bin   # moobfs-attr, moobfs_layer.ko, fselect, cshatag and kfmd when they were installed
LOWER=/tmp/moob-lower LAYER=/tmp/moob-layer
# the lower image: virtio-blk with virtme-ng 1.41 (CI), virtio-scsi with older releases
DEV=${DEV:-/dev/vda}
[ -b "$DEV" ] || DEV=/dev/sda
PATH=$BIN:$PATH
HOME=/tmp/home
export PATH HOME
mkdir -p "$HOME"

section() { echo; echo "=== INTEROP: $* ==="; }
run() { printf '+'; printf ' %s' "$@"; echo; "$@" 2>&1; echo "[exit $?]"; }
have() { command -v "$1" > /dev/null 2>&1; }
setup_fail() { echo "INTEROP_SETUP_FAIL: $*"; dmesg | tail -40; exit 1; }

section setup
uname -r
mkdir -p "$LOWER" "$LAYER"
mount -t ext4 "$DEV" "$LOWER" || setup_fail "lower mount"
insmod "$BIN/moobfs_layer.ko" || setup_fail insmod
mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || setup_fail "layer mount"
grep -E 'moobfs_layer| /tmp/moob-lower ' /proc/mounts
D=$LAYER/notes L=$LOWER/notes
mkdir "$D" || setup_fail mkdir
printf 'Decision: keep ext4 as the lower filesystem.\n' > "$D/a.txt"
printf '# Meeting note\nThe tags of this note come from Dolphin.\n' > "$D/b.md"
printf 'Draft text with plain tags only.\n' > "$D/c.txt"

section "typed attributes written with moobfs-attr (a.txt, b.md)"
run moobfs-attr set "$D/a.txt" kind string decision
run moobfs-attr set "$D/a.txt" score int64 42
run moobfs-attr set "$D/a.txt" weight double 0.75
run moobfs-attr set "$D/a.txt" pinned bool true
run moobfs-attr set "$D/a.txt" created time 1760000000
run moobfs-attr set "$D/a.txt" blob raw deadbeef
run moobfs-attr set "$D/b.md" kind string note
run moobfs-attr list "$D/a.txt"

section "desktop attributes written with setfattr (b.md, c.txt)"
run setfattr -n user.xdg.tags -v "project,moobfs" "$D/b.md"
run setfattr -n user.xdg.comment -v "reviewed on Monday" "$D/b.md"
run setfattr -n user.baloo.rating -v 8 "$D/b.md"
run setfattr -n user.xdg.tags -v draft "$D/c.txt"

section "getfattr: text and hex, through the layer and on the lower"
run getfattr --version
run getfattr -d -m - "$D/a.txt" "$D/b.md" "$D/c.txt"
run getfattr -d -m - -e hex "$D/a.txt" "$D/b.md"
run getfattr -d -m - -e hex "$L/a.txt"
run getfattr -n user.moob.kind --only-values "$D/a.txt"
run sh -c "getfattr -n user.moob.kind --only-values '$D/a.txt' | od -An -c"

section "setfattr: a plain-text value under user.moob.* (expected: refused, EINVAL)"
run setfattr -n user.moob.x -v hello "$D/c.txt"
run getfattr -n user.moob.x "$L/c.txt"
run setfattr -n user.moob.x -v "" "$D/c.txt"
echo "a value written by setfattr in hex with the string type byte first:"
run setfattr -n user.moob.x -v 0x0168656c6c6f "$D/c.txt"
run moobfs-attr get "$D/c.txt" x
run setfattr -x user.moob.x "$D/c.txt"

section "Dolphin-style user.xdg.tags round trip through the layer"
run setfattr -n user.xdg.tags -v "alpha,beta" "$D/c.txt"
run getfattr -n user.xdg.tags "$D/c.txt"
run getfattr -n user.xdg.tags "$L/c.txt"
run setfattr -n user.xdg.tags -v "alpha,beta,gamma" "$D/c.txt"
run getfattr -n user.xdg.tags -e hex "$D/c.txt"
run setfattr -x user.xdg.tags "$D/c.txt"
run getfattr -n user.xdg.tags "$L/c.txt"
run setfattr -n user.xdg.tags -v draft "$D/c.txt"

section fselect
if have fselect; then
    run fselect --version
    run fselect "path, xattr(user.moob.kind) from $D"
    run sh -c "fselect 'path, xattr(user.moob.kind) from $D' | od -An -c"
    run fselect "path, xattr(user.moob.score), xattr(user.moob.pinned) from $D"
    run fselect "path, xattr(user.xdg.tags), xattr(user.baloo.rating) from $D"
    run fselect "path from $D where has_xattr(user.moob.kind)"
    run fselect "path from $D where has_xattr(user.xdg.tags)"
    run fselect "path, has_xattrs, xattr_count from $D"
    run fselect "path from $D where xattr(user.moob.kind) = 'decision'"
    run fselect "path from $D where xattr(user.moob.kind) like '%decision'"
    run fselect "path from $D where xattr(user.xdg.tags) like '%moobfs%'"
else
    echo "SKIP: fselect not installed"
fi

section cshatag
if have cshatag; then
    run sh -c 'cshatag -h 2>&1 | head -5'
    run cshatag "$D/a.txt" "$D/b.md"
    run getfattr -d -m 'user.shatag' "$D/a.txt"
    run getfattr -d -m 'user.shatag' "$L/a.txt"
    echo "unchanged file, second run:"
    run cshatag "$D/a.txt" "$D/b.md"
    echo "content changed through the layer, mtime updated (an ordinary edit):"
    printf 'Appended line.\n' >> "$D/b.md"
    run cshatag "$D/b.md"
    echo "content changed through the layer, mtime restored (what silent corruption looks like):"
    touch -r "$D/a.txt" /tmp/a.ref
    printf 'Decision: keep XFS as the lower filesystem.\n' > "$D/a.txt"
    touch -r /tmp/a.ref "$D/a.txt"
    run cshatag "$D/a.txt"
    echo "typed attributes after cshatag wrote its own:"
    run moobfs-attr list "$D/a.txt"
    run cshatag -recursive "$D"
else
    echo "SKIP: cshatag not installed"
fi

section recoll
if have recollindex && have recollq; then
    run sh -c 'dpkg-query -W -f "\${Package} \${Version}\n" recoll recollcmd 2>/dev/null'
    C=/tmp/rcl
    mkdir -p "$C"
    cat > "$C/recoll.conf" << EOF
topdirs = $D
skippedPaths =
skippedNames = lost+found
noaspell = 1
loglevel = 3
EOF
    # xattrtofields: name without "user." -> field; prefixes make a field searchable as
    # field:term; stored makes it come back in the results
    cat > "$C/fields" << 'EOF'
[prefixes]
moobkind = XYMK
moobscore = XYMS
xdgtags = XYXT
xdgcomment = XYXC
[stored]
moobkind =
moobscore =
xdgtags =
xdgcomment =
[xattrtofields]
moob.kind = moobkind
moob.score = moobscore
xdg.tags = xdgtags
xdg.comment = xdgcomment
EOF
    run cat "$C/fields"
    run recollindex -c "$C"
    for q in 'moobkind:decision' 'moobkind:note' 'xdgtags:moobfs' 'xdgtags:project' 'xdgcomment:reviewed' 'moobscore:42' 'decision'; do
        run recollq -c "$C" -m "$q"
    done
    echo "the stored fields of the moobkind:decision hit, base64-decoded, bytes shown with od:"
    recollq -c "$C" -F "filename moobkind moobscore xdgtags" 'moobkind:decision OR xdgtags:moobfs' 2> /dev/null |
        while read -r line; do
            for w in $line; do
                printf '%s\n' "$w" | base64 -d 2> /dev/null | od -An -c | head -3
                echo "--"
            done
        done
else
    echo "SKIP: recoll (recollindex/recollq) not installed"
fi

section "KDE KFileMetaData (UserMetaData, KF5)"
if have kfmd; then
    run kfmd show "$D/a.txt"
    run kfmd show "$D/b.md"
    run kfmd settags "$D/c.txt" "kde,tagged"
    run kfmd setrating "$D/c.txt" 6
    run kfmd setcomment "$D/c.txt" "written by KFileMetaData"
    run kfmd show "$D/c.txt"
    run getfattr -d -m - "$L/c.txt"
else
    echo "SKIP: kfmd (KFileMetaData helper) was not built"
fi

section "final state, through the layer and on the lower"
run getfattr -R -d -m - -e hex "$D"
run getfattr -R -d -m - -e hex "$L"
umount "$LAYER" && umount "$LOWER" && rmmod moobfs_layer
dmesg > "$OUT/dmesg.txt"
grep -E 'BUG:|Oops:|\] WARNING:|^WARNING:' "$OUT/dmesg.txt" | grep -v 'Speculative Return' | head -10
echo "INTEROP_DONE"
