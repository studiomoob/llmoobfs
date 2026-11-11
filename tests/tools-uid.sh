#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Finding 0213-2 (journal 0213; test added in journal 0283): moobfs-index --uid refuses a value
# that is not a decimal uid (exit 64) instead of reading it as 0, and as root it drops root's
# supplementary groups before the walk, so the oracle sees only what that uid may reach.
# The group part needs root, setpriv and user extended attributes on TMPDIR; it is skipped
# otherwise, and the run says so. Usage: sh tests/tools-uid.sh <build dir>
set -eu
build=$1
fail() { echo "TOOLS_UID_FAIL: $*"; exit 1; }
tmp=$(mktemp -d "${TMPDIR:-/tmp}/moob-uid.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
chmod 0755 "$tmp"
mkdir -m 0755 "$tmp/lower"
n=0
for bad in abc 1000x '' -1 4294967295 4294967296 0x10 1e3; do
    rc=0
    "$build/moobfs-index" --uid "$bad" "$tmp/lower" kind exists - - > /dev/null 2>&1 || rc=$?
    [ "$rc" -eq 64 ] || fail "--uid '$bad' gave exit $rc, not 64"
    n=$((n + 1))
done
groups=skipped
if [ "$(id -u)" -eq 0 ] && command -v setpriv > /dev/null 2>&1; then
    # a directory only group 4242 may enter, holding one attributed file
    mkdir -m 0750 "$tmp/lower/g"
    chgrp 4242 "$tmp/lower/g"
    : > "$tmp/lower/g/f"
    chmod 0644 "$tmp/lower/g/f"
    if "$build/moobfs-attr" set "$tmp/lower/g/f" kind string x > /dev/null 2>&1; then
        # control: uid 1001 with group 4242 reaches the file, so the setup can show a leak
        setpriv --reuid 1001 --regid 1001 --groups 4242 \
            "$build/moobfs-index" "$tmp/lower" kind exists - - > "$tmp/control.txt" ||
            fail "control run as uid 1001 in group 4242"
        grep -q 'g/f$' "$tmp/control.txt" || fail "control: group 4242 does not reach g/f"
        # root holding group 4242 asks for uid 1001: the group must not come along
        setpriv --groups 4242 \
            "$build/moobfs-index" --uid 1001 "$tmp/lower" kind exists - - > "$tmp/uid.txt" ||
            fail "--uid 1001 run"
        if grep -q 'g/f$' "$tmp/uid.txt"; then
            fail "--uid 1001 kept root's supplementary group 4242 and listed g/f"
        fi
        groups=checked
    fi
fi
echo "TOOLS_UID_PASS: $n invalid --uid values exit 64; supplementary groups $groups"
