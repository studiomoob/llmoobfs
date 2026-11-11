#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Finding 0213-3 (journal 0213; test added in journal 0283): moobfs query trusts a QUERY2 answer
# only as far as the UAPI header promises. A count above the request's limit is an error
# (EPROTO, nothing printed); a record whose path_len does not fit path, or that has no NUL after
# its path, is a result with an unknown path. tests/fake-query2.c stands in for the kernel
# through LD_PRELOAD (Linux, glibc). Usage: sh tests/tools-uapi.sh <build dir>
set -eu
build=$1
fail() { echo "TOOLS_UAPI_FAIL: $*"; exit 1; }
# a sanitized CLI built with gcc links libasan dynamically; the stand-in is preloaded before it
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}:verify_asan_link_order=0; export ASAN_OPTIONS
shim=$(cd "$build" && pwd)/fake-query2.so
[ -f "$shim" ] || fail "missing $shim (make $build/fake-query2.so)"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/moob-uapi.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/dom"
fake() { MOOB_FAKE=$1 LD_PRELOAD=$shim "$build/moobfs" query --ino "$tmp/dom" kind exists - -; }
rc=0; fake good > "$tmp/out" 2> "$tmp/err" || rc=$?
[ "$rc" -eq 0 ] && [ "$(cat "$tmp/out")" = "$(printf '7\tabc')" ] ||
    fail "control: exit $rc, output '$(cat "$tmp/out")', $(cat "$tmp/err")"
rc=0; fake count > "$tmp/out" 2> "$tmp/err" || rc=$?
[ "$rc" -ne 0 ] || fail "count above limit: exit 0"
[ ! -s "$tmp/out" ] || fail "count above limit: printed '$(head -c 200 "$tmp/out")'"
grep -q 'Protocol error' "$tmp/err" || fail "count above limit: $(cat "$tmp/err")"
for m in pathlen nonul; do
    rc=0; fake "$m" > "$tmp/out" 2> "$tmp/err" || rc=$?
    [ "$rc" -eq 0 ] && [ "$(cat "$tmp/out")" = "$(printf '7\t?')" ] ||
        fail "$m: exit $rc, output '$(head -c 200 "$tmp/out")'"
done
echo "TOOLS_UAPI_PASS: count above limit refused (EPROTO); path_len out of bounds and a missing NUL give an unknown path"
