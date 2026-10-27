#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# User-space first line of security roadmap S4: adversarial lower trees (tests/hostile.c) read by
# the tools that share the attribute code with the module. Meant to run with the tools built
# under address and undefined-behaviour sanitizers; any report is a failure of the caller's grep.
# Needs a directory on a filesystem with user extended attributes (TMPDIR, default /tmp).
#   hostile-user.sh BUILD_DIR [SEEDS]
set -u
BUILD=$1; SEEDS=${2:-20}
T=$(mktemp -d "${TMPDIR:-/tmp}/moob-hostile.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "HOSTILE_USER_FAIL: $1" >&2; exit 1; }
# the tools leak their path lists at exit by design; only memory errors matter here
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}; export ASAN_OPTIONS
seed=1
while [ "$seed" -le "$SEEDS" ]; do
    rm -rf "$T/low"; mkdir "$T/low"
    "$BUILD/hostile" plant "$T/low" "$seed" 200 > "$T/plant.txt" || fail "plant $seed"
    grep -q 'attributes_set=0 ' "$T/plant.txt" && fail "no user extended attributes on $T"
    "$BUILD/moobfs-check" "$T/low" > "$T/check.json"; [ $? -eq 2 ] || fail "check $seed did not report problems"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["problem_count"] == len(d["problems"])' "$T/check.json" ||
        fail "check $seed printed a document that is not JSON or miscounts its problems"
    for n in k0 k1 k2 k3 k4 k5; do
        "$BUILD/moobfs-index" "$T/low" $n exists - - > /dev/null || fail "index $n $seed"
    done
    "$BUILD/moobfs-index" "$T/low" k0 eq string a k1 exists - - > /dev/null || fail "index and $seed"
    "$BUILD/moobfs-index" --order k2 int64 asc "$T/low" k2 ge int64 0 > /dev/null || fail "index order $seed"
    for d in d0 d1 d2 d3 d4; do
        "$BUILD/moobfs-index" --domain "$T/low/$d" k0 exists - - > /dev/null || fail "index domain $d $seed"
    done
    "$BUILD/hostile" mutate "$T/low" "$seed" 200 > /dev/null || fail "mutate $seed"
    "$BUILD/moobfs-check" "$T/low" > "$T/check.json"; [ $? -le 2 ] || fail "check after mutate $seed"
    "$BUILD/moobfs-index" "$T/low" k0 exists - - > /dev/null || fail "index after mutate $seed"
    seed=$((seed + 1))
done
echo "HOSTILE_USER_PASS: $SEEDS trees through moobfs-check and moobfs-index"
