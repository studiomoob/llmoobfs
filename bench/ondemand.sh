#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# What ADR-0012 changes, measured on one mount with the module that is loaded (journal 0098):
# mount time, the time of the first query on one domain, the index memory with that one domain
# in use, the steady-state query latency, and the time and memory of the mount-wide QUERY that
# needs every domain. Run it once per module on the same data to compare; REPS sets the
# repetitions of the steady-state part (default 3).
#
#   sudo env BIN=<built tools> sh bench/ondemand.sh <layer mount> <domain name> <out dir>
#
# The mount is unmounted and mounted again several times; nothing is written to it.
set -eu
BIN=${BIN:-build/host}
LAYER=$1; DOM=$2; OUT=$3
[ "$(id -u)" = 0 ] || { echo "ondemand.sh: run as root" >&2; exit 1; }
opts=$(awk -v m="$LAYER" '$2 == m { print $4 }' /proc/mounts | sed 's/^rw,relatime,//')
[ -n "$opts" ] || { echo "ondemand.sh: $LAYER is not a mount" >&2; exit 1; }
mkdir -p "$OUT"
now() { date +%s%N; }
ms() { echo $(( ($(now) - $1) / 1000000 )); }
used() { "$BIN/query" "$LAYER" info | sed -n 's/.* used=\([0-9]*\) entries=\([0-9]*\) inodes=\([0-9]*\)$/used=\1 entries=\2 inodes=\3/p'; }
built() { "$BIN/query" "$LAYER" info | sed -n 's/^domains_built=\([0-9]*\) .*/\1/p'; }
{ uname -r; cat /sys/module/moobfs_layer/version; cat /sys/module/moobfs_layer/srcversion; echo "mount options: $opts"; if [ -f "$(dirname "$0")/cpu-freq.sh" ]; then sh "$(dirname "$0")/cpu-freq.sh" show; else echo "cpufreq: not recorded"; fi; } > "$OUT/environment.txt"
: > "$OUT/times.txt"
for mode in cold warm cold warm; do
    umount "$LAYER"
    if [ $mode = cold ]; then sync; echo 3 > /proc/sys/vm/drop_caches; fi
    t=$(now); mount -t moobfs_layer -o "$opts" none "$LAYER"; m=$(ms $t)
    u0=$(used)
    t=$(now); n=$("$BIN/query" "$LAYER/$DOM" q2 kind eq string note | wc -l); q1=$(ms $t)
    u1=$(used); b1=$(built)
    t=$(now); "$BIN/query" "$LAYER/$DOM" q2 kind eq string note > /dev/null; q2=$(ms $t)
    echo "$mode mount_ms=$m after_mount[$u0] first_query_ms=$q1 results=$n after_first_query[$u1] domains_built=${b1:-n/a} second_query_ms=$q2" | tee -a "$OUT/times.txt"
done
# the mount-wide QUERY, which needs every domain. This script predates the build limits of
# ADR-0019 and ADR-0020 (index_build_work, the build deadline): a build that exceeds them is
# refused with E2BIG or ETIMEDOUT, and that refusal is recorded instead of a time (journal 0238).
wide() {
    t=$(now)
    if err=$("$BIN/query" "$LAYER" build 2>&1 >/dev/null); then a="mount_wide_query_ms=$(ms "$t")"
    else
        e=$(ms "$t")
        case $err in
            *"Argument list too long"*) a="mount_wide_query=refused(E2BIG) after_ms=$e" ;;
            *"Connection timed out"*) a="mount_wide_query=refused(ETIMEDOUT) after_ms=$e" ;;
            *) echo "ondemand.sh: mount-wide QUERY: $err" >&2; exit 1 ;;
        esac
    fi
    echo "$1 $a after[$(used)] domains_built=$(built)" | tee -a "$OUT/times.txt"
}
umount "$LAYER"; sync; echo 3 > /proc/sys/vm/drop_caches
mount -t moobfs_layer -o "$opts" none "$LAYER"
wide cold
umount "$LAYER"; mount -t moobfs_layer -o "$opts" none "$LAYER"
wide warm

# steady state, one domain built and nothing else (the mount is fresh again)
umount "$LAYER"; mount -t moobfs_layer -o "$opts" none "$LAYER"
printf '%s\n' "namespace eq string $DOM agent eq string a07" "agent eq string a07" "kind eq string note" > "$OUT/q-dom.txt"
printf '%s\n' "kind eq string note" "agent eq string a07" > "$OUT/q-recent.txt"
"$BIN/query" "$LAYER/$DOM" q2 kind eq string note > /dev/null
for r in $(seq "${REPS:-3}"); do
    d=$OUT/rep$r; mkdir -p "$d/v2-dom" "$d/v2-recent"
    "$BIN/bench-load" --workload W3 --target "$LAYER/$DOM" --root "$LAYER/$DOM" --query2 1 --uid 1001 --queries "$OUT/q-dom.txt" --repeat 50 --out "$d/v2-dom/w3"
    "$BIN/bench-load" --workload W3 --target "$LAYER/$DOM" --root "$LAYER/$DOM" --query2 1 --uid 1001 --order created:time:desc --limit 20 --queries "$OUT/q-recent.txt" --repeat 50 --out "$d/v2-recent/w3"
done
echo "steady state with one domain: [$(used)] domains_built=$(built)" | tee -a "$OUT/times.txt"
echo "ONDEMAND_DONE"
