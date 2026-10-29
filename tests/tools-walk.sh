#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# moobfs-check and moobfs-index walk by directory descriptor (journal 0232): no symbolic link is
# followed below the root, even one swapped in for a directory while the tools walk, and the
# walk holds a bounded number of descriptors on a deep tree. Linux, no module; needs user
# extended attributes in TMPDIR (default /tmp) and renameat2(RENAME_EXCHANGE) there.
#   tools-walk.sh BUILD_DIR      (TOOLS_WALK_SECONDS, default 4: length of each race)
set -u
BUILD=$1; SECONDS_EACH=${TOOLS_WALK_SECONDS:-4}
T=$(mktemp -d "${TMPDIR:-/tmp}/moob-walk.XXXXXX") || exit 1
swapper=""
trap '[ -z "$swapper" ] || kill "$swapper" 2>/dev/null; rm -rf "$T"' EXIT
fail() { echo "TOOLS_WALK_FAIL: $1" >&2; exit 1; }
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}; export ASAN_OPTIONS
A=$BUILD/moobfs-attr

# Outside the walked root: a directory and a file whose attributes must never be reported.
# "leak" is a valid attribute the index would answer; "Leak" an invalid name check would report.
mkdir -p "$T/out" "$T/root/dom/sub"
: > "$T/out/f"
"$A" set "$T/out/f" leak string outside 2>/dev/null || { echo "TOOLS_WALK_SKIP: no user extended attributes in $T"; exit 0; }
python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.moob.Leak", b"\x01x")' "$T/out/f" || fail "set an invalid name"
"$A" set "$T/out" leak string outside || fail "set on a directory"
: > "$T/root/dom/sub/f"
"$A" set "$T/root/dom/sub/f" kind string inside || fail "set inside"
ln -s ../../out "$T/root/dom/alt"

# leaks LABEL: run every tool once over the tree; count the answers that came from outside
leaks=0; runs=0
probe_tools() {
    runs=$((runs + 1))
    "$BUILD/moobfs-index" "$T/root" leak exists - - > "$T/i1" 2>/dev/null
    "$BUILD/moobfs-index" --domain "$T/root/dom" leak exists - - > "$T/i2" 2>/dev/null
    "$BUILD/moobfs-check" "$T/root" > "$T/c1" 2>/dev/null
    "$BUILD/moobfs-check" --domain-depth 1 "$T/root" > "$T/c2" 2>/dev/null
    if [ -s "$T/i1" ] || [ -s "$T/i2" ] || grep -q '"Leak"' "$T/c1" "$T/c2"; then
        leaks=$((leaks + 1))
        [ "$leaks" -gt 1 ] || { echo "tools-walk: $1: first answer from outside the root:"; cat "$T/i1" "$T/i2"; grep -h '"Leak"' "$T/c1" "$T/c2"; } >&2
    fi
}

# 1. Static: a link to a directory and to a file outside, and a FIFO, are never followed.
ln -s ../../out/f "$T/root/dom/flink"
mkfifo "$T/root/dom/fifo"
probe_tools static
[ "$leaks" -eq 0 ] || fail "a planted link was followed"
"$BUILD/moobfs-index" --domain "$T/root/dom" kind eq string inside | grep -q "	sub/f$" || fail "the file inside is not found"
rm "$T/root/dom/flink" "$T/root/dom/fifo"

# 2. Race: dom/sub (a directory) and dom/alt (a link to out) trade names while the tools walk.
"$BUILD/hostile" swap "$T/root/dom" sub alt "$SECONDS_EACH" > "$T/swap.txt" &
swapper=$!
while kill -0 "$swapper" 2>/dev/null; do probe_tools race; done
wait "$swapper" || fail "swap: $(cat "$T/swap.txt")"
swapper=""
grep -q '^HOSTILE_SWAP: exchanges=[1-9]' "$T/swap.txt" || fail "no exchange was made"
[ "$leaks" -eq 0 ] || fail "$leaks of $runs rounds answered from outside the root while a directory and a link traded names"
race_runs=$runs

# 3. A tree deeper than the descriptors the process may hold: the walk stays bounded and finds
# every file (each level has one file with a typed attribute, and a second directory to come
# back to after the deep one).
rm -rf "$T/root/dom"
d=$T/root/deep; i=0; depth=150
mkdir "$d"
while [ "$i" -lt "$depth" ]; do
    : > "$d/f"; "$A" set "$d/f" kind string deep
    mkdir "$d/z" "$d/n"; : > "$d/z/g"; "$A" set "$d/z/g" kind string deep
    d=$d/n; i=$((i + 1))
done
# run a tool with at most 64 descriptors (ulimit -n is not POSIX sh)
fewfds() { python3 -c 'import os,resource,sys; resource.setrlimit(resource.RLIMIT_NOFILE, (64, 64)); os.execv(sys.argv[1], sys.argv[1:])' "$@"; }
got=$(fewfds "$BUILD/moobfs-index" "$T/root" kind eq string deep | wc -l | tr -d ' ')
[ "$got" -eq $((2 * depth)) ] || fail "deep tree under 64 descriptors: $got of $((2 * depth)) files"
fewfds "$BUILD/moobfs-check" "$T/root" > "$T/deep.json" || fail "check on the deep tree"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["typed_attributes"] == 2 * int(sys.argv[2]), d["typed_attributes"]' "$T/deep.json" "$depth" ||
    fail "check on the deep tree under 64 descriptors"

# 4. A file the caller may not read is still listed, as a read by path listed it (only as a user
# other than root: root reads it).
if [ "$(id -u)" -ne 0 ]; then
    : > "$T/root/closed"; "$A" set "$T/root/closed" kind string closed; chmod 000 "$T/root/closed"
    "$BUILD/moobfs-check" "$T/root" > "$T/closed.json"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert [x["problem"] for x in d["problems"] if x["path"] == "closed"] == ["cannot read value"], d["problems"]' "$T/closed.json" ||
        fail "an unreadable file is not reported as before"
    chmod 600 "$T/root/closed"
    unread=" and an unreadable file"
else
    unread=""
fi
echo "TOOLS_WALK_PASS: static links and FIFO, $race_runs rounds of 4 tool runs during a directory/link exchange, $depth levels under 64 descriptors$unread"
