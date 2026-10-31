#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# One night of syzkaller on the test host (security roadmap S18, journal 0248): the build steps
# and a run of vm/syzkaller-local.sh with the S3 descriptions and seeds, the corpus kept from
# night to night, then the call-site figure of vm/syzkaller-replay.sh over the night's corpus
# and the seeds. Meant for cron (docs/testing.md has the entries):
#
#   SYZ=<syzkaller checkout> GO=<go binary dir> sh vm/syzkaller-nightly.sh <reports dir>
#
# The night runs from its start to NIGHT_END (default 05:00, local time); fuzzing stops
# NIGHT_REPLAY_S seconds (default 3600) before that, to leave the replay its time, and the
# replay starts no program after NIGHT_END, so the host is free in the daytime. Each night has
# a directory <reports dir>/<date the night began>/ with build.log, run.log, manager.log,
# summary.txt, funccover.csv, crashes/, corpus.db, replay/ and report.txt, and one line in
# <reports dir>/nights.txt. Phases leave a marker as they end, so a night stopped by a power cut
# resumes when the script is started again (cron's @reboot with NIGHT_RESUME=1): fuzzing goes
# on for the time left from the kept corpus, what the first part found is kept in part<N>/,
# and a night whose window has passed gets its report with what it has. The replay's cache
# (<reports dir>/replay-cache) keeps each program's coverage, so later nights replay only new
# programs. NIGHT_CPUS pins everything (taskset list, e.g. 4-7); REPLAY_GUESTS, SYZ_VMS and
# SYZ_CACHE are passed on. Run from the repository root of a copy that is not being edited.
set -eu
REP=${1:?reports dir}
SYZ=${SYZ:?syzkaller checkout}
: "${GO:?directory of the go binary}"
END=${NIGHT_END:-05:00}
REPLAY_S=${NIGHT_REPLAY_S:-3600}
[ -f vm/syzkaller-local.sh ] || { echo "syzkaller-nightly: run from the repository root" >&2; exit 1; }
mkdir -p "$REP"
exec 9> "$REP/.lock"
flock -n 9 || { echo "syzkaller-nightly: another night is running" >&2; exit 1; }
B=$(pwd)/build/syzkaller
pin() { if [ -n "${NIGHT_CPUS:-}" ]; then taskset -c "$NIGHT_CPUS" "$@"; else "$@"; fi; }
stamp() { date '+%F %T %Z'; }
# the night's window: from 23:00 or whenever it started, to NIGHT_END on the next morning
night_end() { next=$(date -d "@$(($(date -d "$1 12:00" +%s) + 86400))" +%F); date -d "$next $END" +%s; }

now=$(date +%s)
if [ "${NIGHT_RESUME:-0}" = 1 ]; then
    # after a reboot: the latest night without a report, if any
    ID=
    for d in "$REP"/20*-*-*/; do
        if [ -d "$d" ] && [ ! -f "$d/report.txt" ]; then ID=$(basename "$d"); fi
    done
    [ -n "$ID" ] || exit 0
else
    # a night that began before midnight keeps that date until noon
    ID=$(date -d '@'$((now - 43200)) +%F)
fi
N=$REP/$ID
mkdir -p "$N"
if [ -f "$N/report.txt" ]; then exit 0; fi
WEND=$(night_end "$ID")
FEND=$((WEND - REPLAY_S))
echo "$(stamp) start (resume=${NIGHT_RESUME:-0})" >> "$N/events.txt"
if [ ! -f "$N/start" ]; then stamp > "$N/start"; fi
REV=$(git rev-parse --short HEAD 2> /dev/null || cat REVISION 2> /dev/null || echo unknown)

if [ ! -f "$N/permchurn.done" ] && [ $((FEND - $(date +%s))) -gt 600 ]; then
    # S15's longer nightly campaign. This script runs from the integrated batch tree.
    FIRST=$(date -d "$ID" +%Y%m%d)
    LEFT=$((FEND - $(date +%s)))
    LIMIT=${NIGHT_PERMCHURN_S:-1200}
    [ "$LIMIT" -le "$LEFT" ] || LIMIT=$LEFT
    PC_RC=0
    MOOB_ONLY=permchurn MOOB_PERMCHURN=${MOOB_PERMCHURN:-24,96,$FIRST} \
        pin timeout --kill-after=15 "$LIMIT" sh vm/kasan-local.sh > "$N/permchurn.log" 2>&1 || PC_RC=$?
    echo "$PC_RC" > "$N/permchurn.exit"
    if [ "$PC_RC" = 0 ] && grep -q '^MOOBFS_KASAN_ONLY_PASS:' "$N/permchurn.log"; then
        stamp > "$N/permchurn.done"
    fi
    echo "$(stamp) extended permissions exit=$PC_RC" >> "$N/events.txt"
fi

if [ ! -f "$N/fuzz.done" ] && [ $((FEND - $(date +%s))) -gt 600 ]; then
    if [ -f "$N/fuzz.start" ]; then
        # a run that did not end: keep what it found before the manager's workdir is renewed
        k=1; while [ -d "$N/part$k" ]; do k=$((k + 1)); done
        mkdir -p "$N/part$k"
        cp "$B/manager.log" "$N/part$k/" 2> /dev/null || true
        if [ -d "$B/workdir/crashes" ]; then cp -r "$B/workdir/crashes" "$N/part$k/"; fi
        echo "$(stamp) fuzzing resumed after an interruption, part$k kept" >> "$N/events.txt"
    fi
    LEFT=$((WEND - $(date +%s)))
    if ! pin timeout --kill-after=15 "$LEFT" sh vm/syzkaller-local.sh 1 kernel module image descriptions > "$N/build.log" 2>&1; then
        echo "$(stamp) build failed" >> "$N/events.txt"
        tail -n 20 "$N/build.log" > "$N/report.txt"
        echo "$ID $REV build-failed" >> "$REP/nights.txt"
        exit 1
    fi
    DUR=$((FEND - $(date +%s)))
    if [ "$DUR" -le 0 ]; then
        echo "$(stamp) build used the fuzzing window" >> "$N/events.txt"
        DUR=0
    fi
    stamp > "$N/fuzz.start"
    echo "$(stamp) fuzzing for $DUR s" >> "$N/events.txt"
    FUZZ_RC=1
    if [ "$DUR" -gt 0 ]; then
        LEFT=$((WEND - $(date +%s)))
        FUZZ_RC=0
        KEEP_CORPUS=1 pin timeout --kill-after=15 "$LEFT" sh vm/syzkaller-local.sh "$DUR" run > "$N/run.log" 2>&1 || FUZZ_RC=$?
    fi
    echo "$FUZZ_RC" > "$N/fuzz.exit"
    for f in manager.log summary.txt manager.cfg funccover.csv workdir/corpus.db; do
        if [ -s "$B/$f" ]; then cp "$B/$f" "$N/"; fi
    done
    rm -rf "$N/crashes"
    if [ -d "$B/workdir/crashes" ]; then cp -r "$B/workdir/crashes" "$N/crashes"; fi
    stamp > "$N/fuzz.done"
    echo "$(stamp) fuzzing ended" >> "$N/events.txt"
fi

if [ -f "$N/fuzz.done" ] && [ ! -f "$N/replay.done" ] && [ "$(date +%s)" -lt "$WEND" ]; then
    echo "$(stamp) replay" >> "$N/events.txt"
    LEFT=$((WEND - $(date +%s)))
    if REPLAY_DEADLINE=$WEND REPLAY_CACHE=$REP/replay-cache pin timeout --kill-after=15 "$LEFT" sh vm/syzkaller-replay.sh \
        "$N/replay" "$N/corpus.db" vm/syzkaller/seeds > "$N/replay.log" 2>&1; then
        stamp > "$N/replay.done"
    fi
    rm -rf "$N/replay/progs"
    echo "$(stamp) replay ended" >> "$N/events.txt"
fi

# the report, with what the night has
{
    echo "night=$ID revision=$REV started=$(cat "$N/start") reported=$(stamp)"
    if [ -f "$N/permchurn.exit" ]; then echo "permchurn_exit=$(cat "$N/permchurn.exit")"; fi
    if [ ! -f "$N/permchurn.done" ]; then echo "permchurn: not complete"; fi
    if [ ! -f "$N/fuzz.done" ]; then echo "fuzzing: did not end (see events.txt)"; fi
    if [ -f "$N/fuzz.exit" ]; then echo "fuzzing_exit=$(cat "$N/fuzz.exit")"; fi
    if [ -f "$N/summary.txt" ]; then cat "$N/summary.txt"; fi
    for d in "$N"/part*/crashes/*/; do
        if [ -f "$d/description" ]; then sed 's/^/crash (earlier part): /' "$d/description"; fi
    done
    if [ -s "$N/funccover.csv" ]; then python3 vm/syzkaller/callsites.py funccover "$N/funccover.csv"; fi
    if [ -f "$N/replay/replay.txt" ]; then sed 's/^/replay: /' "$N/replay/replay.txt"; fi
    if [ -f "$N/replay/callsites.txt" ]; then tail -n 1 "$N/replay/callsites.txt"; fi
    if [ ! -f "$N/replay.done" ]; then echo "replay: not complete"; fi
    sed 's/^/event: /' "$N/events.txt"
} > "$N/report.tmp"
mv "$N/report.tmp" "$N/report.txt"
line="$ID $REV"
line="$line $(grep -o 'corpus=[0-9]* coverage=[0-9]*' "$N/report.txt" || true)"
line="$line execs=$(grep -o 'exec total=[0-9]*' "$N/report.txt" | cut -d= -f2 || true)"
line="$line $(grep -o 'crash_dirs=[0-9]*' "$N/report.txt" || echo crash_dirs=?)"
line="$line $(grep '^MANAGER_HANDLERS ' "$N/report.txt" | cut -d' ' -f2-3 | sed 's/^/manager=/;s/ /_/' || true)"
line="$line $(grep '^CALLSITES ' "$N/report.txt" | cut -d' ' -f2-3 | sed 's/^/callsites=/;s/ /_/')"
if [ ! -f "$N/fuzz.done" ]; then line="$line fuzz-interrupted"; fi
if [ ! -f "$N/replay.done" ]; then line="$line replay-incomplete"; fi
echo "$line" >> "$REP/nights.txt"
cat "$N/report.txt"
