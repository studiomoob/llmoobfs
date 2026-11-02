#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs the layer suite (vm/kasan-guest.sh) in a virtme-ng guest booted from the kernel of
# .github/workflows/coverage.yml (vm/kasan.config with vm/gcov.config), then copies the layer's
# gcov data out of debugfs into build/gcov/ (security roadmap S17, journal 0247). The data files
# are read with cat: debugfs reports them with size 0 (Documentation/dev-tools/gcov.rst).
# Exits with the suite's status, after the copy, so that a failing run still leaves its coverage.
# Usage (inside the guest, as root): sh vm/gcov-guest.sh
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/build/gcov
sh "$REPO/vm/kasan-guest.sh"
rc=$?
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
rm -rf "$OUT/gcda"
mkdir -p "$OUT/gcda"
find /sys/kernel/debug/gcov -name '*.gcda' -path '*/layer/*' > "$OUT/gcda-list.txt" 2>/dev/null
n=0
while read -r f; do
    cat "$f" > "$OUT/gcda/${f##*/}" && n=$((n + 1))
done < "$OUT/gcda-list.txt"
echo "MOOBFS_GCOV_COLLECTED: $n data files of the layer (suite exit status $rc)"
exit "$rc"
