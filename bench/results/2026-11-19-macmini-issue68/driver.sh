#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# issue 68 (journal 0313): QUERY2 steady state on ns3, modules alternated per repetition.
# Bisect variant. Run as root:  sh driver.sh <src dir> <commit> <reps> <module names...> (mods/<name>.ko)
# Layout: $R/<module>/rep<r>/{v2-dom,v2-recent}/w3.0.rec  (bench/report.py layout)
set -eu
SRC=$1; COMMIT=$2; REPS=${3:-30}; shift 3; MODS="$*"
M=/home/debian/perf68
R=$M/results
BIN=$SRC/build/host
LAYER=/srv/moob/udx-layer
LOWER=/srv/moob/ext4/udx-lower
DOM=ns3
[ "$(id -u)" = 0 ] || { echo "run as root" >&2; exit 1; }
mkdir -p "$R"
LOG=$R/driver.log
log() { echo "$(date -Is) $*" | tee -a "$LOG"; }

mod_path() { echo "$M/mods/$1.ko"; }

unload_all() {
    awk '$3 == "moobfs_layer" { print $2 }' /proc/mounts | while read -r mp; do umount "$mp"; done
    if grep -q '^moobfs_layer ' /proc/modules; then rmmod moobfs_layer; fi
}

# environment, once
{
    echo "date: $(date -Is)"
    echo "uname: $(uname -a)"
    echo "commit (main module and all user-space tools): $COMMIT"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo)"
    echo "loadavg before: $(cat /proc/loadavg)"
    echo "moobfs mounts before (unmounted by this driver, not remounted):"
    awk '$3 == "moobfs_layer" { print "  " $2 " " $4 }' /proc/mounts
    echo "module loaded before: $(cat /sys/module/moobfs_layer/srcversion 2>/dev/null || echo none)"
    for m in $MODS; do echo "module $m: $(mod_path $m) sha256 $(sha256sum "$(mod_path $m)" | cut -c1-16)"; done
    echo "cpu-freq show (before fix): $(sh "$SRC/bench/cpu-freq.sh" show)"
} > "$R/environment.txt"

cleanup() {
    rc=$?
    if [ $rc != 0 ]; then
        log "FAILED rc=$rc; cleaning up"
        unload_all || true
        [ -e /run/moob-cpufreq.state ] && echo "cpu-freq restore (after failure) printed: $(sh "$SRC/bench/cpu-freq.sh" restore 2>&1)" >> "$R/environment.txt"
    fi
}
trap cleanup EXIT
unload_all
fix_out=$(sh "$SRC/bench/cpu-freq.sh" fix 2>&1) || fix_out="FIX FAILED: $fix_out"
echo "cpu-freq fix printed: $fix_out" >> "$R/environment.txt"
log "cpu-freq: $fix_out"

printf '%s\n' "namespace eq string $DOM agent eq string a07" "agent eq string a07" "kind eq string note" > "$R/q-dom.txt"
printf '%s\n' "kind eq string note" "agent eq string a07" > "$R/q-recent.txt"

one() { # module rep
    m=$1; r=$2; d=$R/$m/rep$r
    insmod "$(mod_path "$m")"
    sv=$(cat /sys/module/moobfs_layer/srcversion)
    t0=$(date +%s%N)
    mount -t moobfs_layer -o "lower=$LOWER" none "$LAYER"
    mt=$(( ($(date +%s%N) - t0) / 1000000 ))
    # warm-up, discarded: first query (builds ns3 for the on-demand modules), then one full pass of each run
    "$BIN/query" "$LAYER/$DOM" q2 kind eq string note > /dev/null
    w=$R/warmup/$m/rep$r; mkdir -p "$w/v2-dom" "$w/v2-recent"
    "$BIN/bench-load" --workload W3 --target "$LAYER/$DOM" --root "$LAYER/$DOM" --query2 1 --uid 1001 --queries "$R/q-dom.txt" --repeat 50 --out "$w/v2-dom/w3"
    "$BIN/bench-load" --workload W3 --target "$LAYER/$DOM" --root "$LAYER/$DOM" --query2 1 --uid 1001 --order created:time:desc --limit 20 --queries "$R/q-recent.txt" --repeat 50 --out "$w/v2-recent/w3"
    mkdir -p "$d/v2-dom" "$d/v2-recent"
    "$BIN/bench-load" --workload W3 --target "$LAYER/$DOM" --root "$LAYER/$DOM" --query2 1 --uid 1001 --queries "$R/q-dom.txt" --repeat 50 --out "$d/v2-dom/w3"
    "$BIN/bench-load" --workload W3 --target "$LAYER/$DOM" --root "$LAYER/$DOM" --query2 1 --uid 1001 --order created:time:desc --limit 20 --queries "$R/q-recent.txt" --repeat 50 --out "$d/v2-recent/w3"
    info=$("$BIN/query" "$LAYER" info | tr '\n' ' ')
    umount "$LAYER"; rmmod moobfs_layer
    echo "rep=$r module=$m srcversion=$sv mount_ms=$mt cpufreq=[$(sh "$SRC/bench/cpu-freq.sh" show)] mhz=[$(awk '/cpu MHz/ {printf "%d ", $4}' /proc/cpuinfo)] loadavg=[$(cut -d' ' -f1-3 /proc/loadavg)] info=[$info]" >> "$R/runs.txt"
}

n=$(echo $MODS | wc -w)
for r in $(seq "$REPS"); do
    # rotate the order by one module per repetition
    k=$(( r % n )); order=$(echo $MODS | awk -v k=$k '{for(i=1;i<=NF;i++){j=(i+k-1)%NF+1; printf "%s ", $j}}')
    for m in $order; do one "$m" "$r"; done
    log "rep $r done ($order)"
done

unload_all
rest=$(sh "$SRC/bench/cpu-freq.sh" restore 2>&1) || rest="RESTORE FAILED: $rest"
echo "cpu-freq restore printed: $rest" >> "$R/environment.txt"
echo "loadavg after: $(cat /proc/loadavg)" >> "$R/environment.txt"
echo "module loaded after: $(cat /sys/module/moobfs_layer/srcversion 2>/dev/null || echo none)" >> "$R/environment.txt"
log "cpu-freq restore: $rest"
log DRIVER_DONE
