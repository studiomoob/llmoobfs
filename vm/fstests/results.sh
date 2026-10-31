#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# One line per test of an fstests run of vm/fstests-local.sh: pass, fail, or notrun with the
# first line of its reason, from the Ran, Not run and Failures lists of ./check and the .notrun
# files (the format of vm/fstests/results-*.txt, journal 0239).
#
#   sh vm/fstests/results.sh build/fstests LOWERFS
set -eu
D=$1; L=$2
c=$D/console-$L.log
res=$D/tree/results
ran=$(grep '^Ran:' "$c" | tail -1 | cut -d: -f2)
[ -n "$ran" ] || { echo "fstests results: no final Ran list in $c" >&2; exit 1; }
nr=" $(grep '^Not run:' "$c" | tail -1 | cut -d: -f2) "
fl=" $(grep '^Failures:' "$c" | tail -1 | cut -d: -f2) "
for t in $ran; do
    case "$fl" in *" $t "*) echo "fail $t"; continue ;; esac
    case "$nr" in
    *" $t "*) echo "notrun $t $(head -1 "$res/$t.notrun" 2> /dev/null || true)"; continue ;;
    esac
    echo "pass $t"
done
