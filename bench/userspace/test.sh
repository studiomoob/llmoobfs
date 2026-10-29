#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Correctness test of the user-space baseline index (docs/history/0092): udx-index, udx-watch and
# udx-query against the project's oracle, moobfs-index --domain. No performance numbers: a
# container on a laptop is not a benchmark environment.
#
#   sh bench/userspace/test.sh            on the host: runs itself in a privileged Linux container
#   sh bench/userspace/test.sh --inside   what runs in the container (or on any Linux, as root)
#
# Environment: UDX_IMAGE (default ubuntu:24.04; gcc, make, libsqlite3-dev and e2fsprogs are
# installed with apt when missing, and the sqlite3 shell for the plans), UDX_INODES (default 3000), UDX_CHANGES (default 400),
# UDX_SEED (default 7). The container must be privileged: the data set lives on a loop-mounted
# ext4 image, because user.* extended attributes are what is being tested.
# Prints UDX_TEST_PASS or UDX_TEST_FAIL as the last line and exits non-zero on any difference.
set -eu

if [ "${1:-}" != "--inside" ]; then
    repo=$(cd "$(dirname "$0")/../.." && pwd)
    exec docker run --rm --privileged -v "$repo:/src:ro" \
        -e UDX_INODES="${UDX_INODES:-3000}" -e UDX_CHANGES="${UDX_CHANGES:-400}" -e UDX_SEED="${UDX_SEED:-7}" \
        "${UDX_IMAGE:-ubuntu:24.04}" sh /src/bench/userspace/test.sh --inside
fi

SRC=${UDX_SRC:-/src}
W=${UDX_WORK:-/work}
MNT=$W/mnt
T=$MNT/t
INODES=${UDX_INODES:-3000}
CHANGES=${UDX_CHANGES:-400}
SEED=${UDX_SEED:-7}
TAB=$(printf '\t')
failures=0
watch_pid=""

say() { echo "udx-test: $*"; }
bad() { echo "UDX_FAIL: $*"; failures=$((failures + 1)); }
finish() {
    [ -z "$watch_pid" ] || kill "$watch_pid" 2>/dev/null || true
    [ -z "${old_queue:-}" ] || echo "$old_queue" > /proc/sys/fs/inotify/max_queued_events 2>/dev/null || true
    if [ "$failures" -eq 0 ]; then echo "UDX_TEST_PASS: $summary"; else echo "UDX_TEST_FAIL: $failures check(s) failed"; exit 1; fi
}
summary=""

# ---- build -------------------------------------------------------------------------------
if ! command -v gcc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1 || [ ! -e /usr/include/sqlite3.h ] ||
   ! command -v mkfs.ext4 >/dev/null 2>&1 || ! command -v sqlite3 >/dev/null 2>&1; then
    say "installing gcc, make, libsqlite3-dev, sqlite3, e2fsprogs"
    apt-get update -qq >/dev/null
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends gcc make libc6-dev libsqlite3-dev sqlite3 e2fsprogs >/dev/null
fi
if grep -q " $MNT " /proc/mounts; then umount "$MNT"; fi
rm -rf "$W" && mkdir -p "$W/src" "$MNT" "$W/out"
(cd "$SRC" && cp -r Makefile common include tools bench "$W/src/")
B=$W/build
make -s -C "$W/src" BUILD="$B" attr-tools bench-tools userspace-tools
say "built: $(cd "$B" && echo udx-*) with $(gcc --version | head -1), sqlite $(grep -m1 'define SQLITE_VERSION ' /usr/include/sqlite3.h | cut -d'"' -f2), kernel $(uname -r)"

# ---- a filesystem with user.* extended attributes ----------------------------------------
truncate -s 512M "$W/ext4.img"
mkfs.ext4 -q -F "$W/ext4.img"
if mount -o loop "$W/ext4.img" "$MNT" 2>"$W/out/mount.err"; then
    fs="ext4 on a loop device"
else
    say "loop mount failed ($(cat "$W/out/mount.err")); trying tmpfs"
    mount -t tmpfs -o size=512m none "$MNT"
    fs="tmpfs"
fi
mkdir -p "$T"
: > "$MNT/xattr-probe"
"$B/moobfs-attr" set "$MNT/xattr-probe" probe int64 1 || { echo "UDX_TEST_FAIL: user.* extended attributes do not work on $fs"; exit 1; }
rm "$MNT/xattr-probe"
say "filesystem: $fs ($(grep " $MNT " /proc/mounts | cut -d' ' -f3,4))"

# ---- the data set: the project's generator and loader ------------------------------------
"$B/bench-gen" --seed "$SEED" --inodes "$INODES" --profile 16 --out "$W/out/manifest.txt"
mkdir -p "$W/out/w1"
"$B/bench-load" --manifest "$W/out/manifest.txt" --target "$T" --workload W1 --procs 3 --out "$W/out/w1/w1"
[ "$(find "$T" -type f -name '*.md' | wc -l)" -eq "$INODES" ] || bad "W1 did not create $INODES files"
# what the generator does not produce, so that the rules are exercised and not only the values
A=$B/moobfs-attr
x=$T/ns0/extra; mkdir -p "$x/sub"
"$A" set "$x" kind string note                        # a directory is an entry too
"$A" set "$x/sub" score int64 600
for i in 1 2 3 4 5 6 7 8; do : > "$x/f$i"; done
"$A" set "$x/f1" weight double -0.0; "$A" set "$x/f2" weight double 0.0   # ordered by bits: -0.0 first
"$A" set "$x/f1" kind string ""; "$A" set "$x/f2" kind string "décision"
"$A" rawset "$x/f3" kind 01ff                          # invalid UTF-8: not indexed
"$A" rawset "$x/f3" score 0201                         # int64 of one byte: not indexed
"$A" set "$x/f4" kind raw 6e6f7465                     # raw: valid, never indexed
"$A" set "$x/f4" score int64 700
"$A" set "$x/f5" kind int64 5                          # another type under a queried name
"$A" set "$x/f6" kind string note; ln "$x/f6" "$x/f6.link"          # two names: heterogeneous
"$A" set "$x/f7" kind string note; chmod 600 "$x/f7"                # not readable by the domain's group
"$A" set "$x/f8" kind string note; ln -s f8 "$x/f8.symlink"
mkdir "$x/closed"; : > "$x/closed/g"; "$A" set "$x/closed/g" kind string note; chmod 700 "$x/closed"
# search-only, and listable by others alone: the rule of ADR-0025 leaves both out (journal 0232)
mkdir "$x/searchonly"; : > "$x/searchonly/g"; "$A" set "$x/searchonly/g" kind string note; chmod 711 "$x/searchonly"
mkdir "$x/othersonly"; : > "$x/othersonly/g"; "$A" set "$x/othersonly/g" kind string note; chmod 705 "$x/othersonly"
mkfifo "$x/fifo"
chown -R 1001:1001 "$T/ns1"                            # a domain that belongs to an ordinary user

# ---- queries ------------------------------------------------------------------------------
# options|terms; the nine shapes of bench/queries-w3.txt first, then ordering, limit, "missing or"
grep -v '^#' "$SRC/bench/queries-w3.txt" | sed 's/^/|/' > "$W/out/queries.txt"
t_mid=$(grep -v '^#' "$W/out/manifest.txt" | sed -n "$((INODES / 2))p" | sed 's/.* created=t:\([0-9]*\).*/\1/')
cat >> "$W/out/queries.txt" <<EOF
--order created time desc --limit 20|kind eq string note
--order created time asc --limit 20|
--order score int64 asc|
--order score int64 desc --limit 50|pinned eq bool 0 kind prefix string d
--order weight double desc --limit 5|
--order weight double asc|kind exists - -
--order kind string asc --limit 40|score ge int64 900
--order memory_id string desc --limit 10|agent eq string a07
--order pinned bool desc --limit 30|kind eq string task
--limit 7|kind eq string note
--limit 0|kind eq string note
|weight gt? double 0.5
|kind eq string note weight lt? double 0
|score exists? - - kind eq? string fact
|tag1 prefix? string v0 pinned eq bool true
--order created time desc --limit 15|weight ge? double 1.0
|created lt time $t_mid kind eq string log
|created ge time $t_mid agent prefix string a1 score le int64 300 pinned eq bool false
|memory_id prefix string 0
|weight eq double 0
|weight le double -0.0
|weight lt double 0
|kind eq string décision
|kind exists - -
|kind prefix int64 5
|score gt int64 -1 score lt int64 1
|nosuchattr exists - -
|nosuchattr eq? string x
EOF
nq=$(wc -l < "$W/out/queries.txt" | tr -d ' ')

# compare LABEL: every query on every domain, udx-query against the oracle, byte for byte.
# The oracle walks the files and so sees the index's own files in "missing or" answers; those
# lines are removed from its output, nothing else is. Sets cmp_bad, cmp_n, cmp_lines.
compare() {
    cmp_bad=0; cmp_n=0; cmp_lines=0
    for d in "$T"/*/; do
        d=${d%/}
        [ -e "$d/.udx.db" ] || { [ "${quiet:-0}" = 1 ] || echo "  no database in $d"; cmp_bad=$((cmp_bad + 1)); continue; }
        while IFS='|' read -r opts terms; do
            # shellcheck disable=SC2086
            "$B/moobfs-index" --domain $opts "$d" $terms 2>&1 | grep -v "$TAB\\.udx\\.db" > "$W/out/oracle.txt" || true
            # shellcheck disable=SC2086
            "$B/udx-query" $opts "$d" $terms > "$W/out/udx.txt" 2>&1 || true
            cmp_n=$((cmp_n + 1))
            cmp_lines=$((cmp_lines + $(wc -l < "$W/out/oracle.txt")))
            if ! cmp -s "$W/out/oracle.txt" "$W/out/udx.txt"; then
                cmp_bad=$((cmp_bad + 1))
                if [ "${quiet:-0}" = 0 ] && [ "$cmp_bad" -le 3 ]; then
                    echo "  differs: ${d#"$T"/} [$opts] $terms"
                    diff "$W/out/oracle.txt" "$W/out/udx.txt" | head -6 | sed 's/^/    /'
                fi
            fi
        done < "$W/out/queries.txt"
    done
}
check() {   # check LABEL: the comparison must be exact
    compare
    if [ "$cmp_bad" -eq 0 ]; then
        echo "UDX_COMPARE_OK $1: $cmp_n comparisons ($nq query shapes x $((cmp_n / nq)) domains), $cmp_lines result lines, all equal"
    else
        bad "$1: $cmp_bad of $cmp_n comparisons differ"
    fi
}
now_ms() { echo $(($(date +%s%N) / 1000000)); }
# settle: a marker written after everything else must become visible, then the test compares.
# Returns the wait in milliseconds in settle_ms (process start-up of the probe included).
nsettle=0
settle() {
    nsettle=$((nsettle + 1))
    last=$(for d in "$T"/*/; do echo "${d%/}"; done | tail -1)
    : > "$last/zz-settle"
    t0=$(now_ms)
    "$A" set "$last/zz-settle" settle int64 "$nsettle"
    while [ -z "$("$B/udx-query" "$last" settle eq int64 "$nsettle" 2>/dev/null)" ]; do
        [ $(($(now_ms) - t0)) -lt 60000 ] || { bad "the watcher did not catch up in 60 s"; break; }
        sleep 0.01
    done
    settle_ms=$(($(now_ms) - t0))
    sleep 0.3   # the marker's batch may still be committing other domains
}
start_watch() {
    "$B/udx-watch" "$T" 1 > "$W/out/watch.out" 2> "$W/out/watch.err" &
    watch_pid=$!
    i=0
    until grep -q UDX_WATCH_READY "$W/out/watch.out"; do
        i=$((i + 1)); [ "$i" -lt 600 ] || { bad "udx-watch did not start: $(cat "$W/out/watch.err")"; return; }
        kill -0 "$watch_pid" 2>/dev/null || { bad "udx-watch exited: $(cat "$W/out/watch.err")"; return; }
        sleep 0.1
    done
    say "$(cat "$W/out/watch.out")"
}
stop_watch() {
    kill "$watch_pid" && wait "$watch_pid" || true
    watch_pid=""
    say "$(tail -1 "$W/out/watch.err")"
}
# mutations N SEED: prints N shell commands that change the tree, chosen by a seeded generator
# that follows its own effects (a deleted or renamed file is not used under its old name).
mutations() {
    (cd "$T" && find . -type f -name '*.md' | sed 's|^\./||' | sort) > "$W/out/files.txt"
    awk -v n="$1" -v seed="$2" -v A="$A" -v T="$T" '
    function dir(p) { sub(/\/[^\/]*$/, "", p); return p }
    function ns(p) { sub(/\/.*$/, "", p); return p }
    # MINSTD, so that a seed gives the same changes with any awk (srand() in mawk does not)
    function rnd() { state = (state * 16807) % 2147483647; return state / 2147483647 }
    BEGIN { state = seed * 7919 + 1; for (w = 0; w < 5; w++) rnd(); split("note decision fact task summary log", kinds, " ") }
    { f[++nf] = $0 }
    END {
        for (i = 1; i <= n; i++) {
            op = int(rnd() * 16); j = 1 + int(rnd() * nf); p = f[j]; P = T "/" p
            if (op == 0) print A, "set", P, "score int64", int(rnd() * 1001)
            else if (op == 1) print A, "set", P, "kind string", kinds[1 + int(rnd() * 6)]
            else if (op == 2) print A, "rm", P, "pinned 2>/dev/null || true"
            else if (op == 3) print A, "set", P, "weight double", (rnd() - 0.5) * 4
            else if (op == 4) {
                q = dir(p) "/new" seed "_" i ".md"
                print ": >", T "/" q, "&&", A, "set", T "/" q, "kind string fact &&", A, "set", T "/" q, "score int64", int(rnd() * 1001), "&&", A, "set", T "/" q, "created time", 1760000000000000000 + i
                f[++nf] = q
            } else if (op == 5) { print "rm", P; f[j] = f[nf]; nf-- }
            else if (op == 6) {
                q = ns(p) "/r" (i % 3) "/ren" seed "_" i ".md"
                print "mkdir -p", T "/" dir(q), "&& mv", P, T "/" q; f[j] = q
            } else if (op == 7) {
                q = "ns" int(rnd() * 8) "/x" (i % 2) "/moved" seed "_" i ".md"
                print "mkdir -p", T "/" dir(q), "&& mv", P, T "/" q; f[j] = q
            } else if (op == 8) print "chmod", (rnd() < 0.5 ? "600" : "644"), P
            else if (op == 9) print A, "rawset", P, "kind 01ff"
            else if (op == 10) {
                q = ns(p) "/d" seed "_" i "/sub/deep.md"
                print "mkdir -p", T "/" dir(q), "&& : >", T "/" q, "&&", A, "set", T "/" q, "kind string summary &&", A, "set", T "/" dir(q), "kind string note"
                f[++nf] = q
            } else if (op == 11) {
                print "ln", P, P ".lnk" i (rnd() < 0.5 ? " && rm " P ".lnk" i : "")
            } else if (op == 12) {
                print ": >", P ".tmp &&", A, "set", P ".tmp kind string task &&", A, "set", P ".tmp score int64", int(rnd() * 1001), "&& mv", P ".tmp", P
            } else if (op == 13 && i % 5 == 0) {
                d = dir(p); if (d !~ /\//) { i--; continue }
                q = ns(p) "/mvdir" seed "_" i
                print "mv", T "/" d, T "/" q
                for (k = 1; k <= nf; k++) if (index(f[k], d "/") == 1) f[k] = q substr(f[k], length(d) + 1)
            } else if (op == 14 && i % 5 == 0) {
                d = dir(p); if (d !~ /\//) { i--; continue }
                print "chmod", (rnd() < 0.5 ? "700" : "755"), T "/" d
            } else if (op == 15 && i % 10 == 0) {
                q = "nsnew" seed "_" i
                print "mkdir -p", T "/" q "/a00 && : >", T "/" q "/a00/first.md &&", A, "set", T "/" q "/a00/first.md kind string note"
                f[++nf] = q "/a00/first.md"
            } else i--
        }
    }' "$W/out/files.txt"
}
run_mutations() {   # run_mutations N SEED
    mutations "$1" "$2" > "$W/out/mutations-$2.sh"
    if ! sh -e "$W/out/mutations-$2.sh" > "$W/out/mutations-$2.log" 2>&1; then
        bad "a change of the mutation script failed: $(tail -2 "$W/out/mutations-$2.log" | tr '\n' ' ')"
    fi
    say "applied $(wc -l < "$W/out/mutations-$2.sh" | tr -d ' ') changes (seed $2): $(awk '{ k = $1; if ($1 ~ /moobfs-attr$/) k = "attr-" $2; else if ($0 ~ /\.tmp/) k = "replace"; else if ($0 ~ /nsnew[0-9_]*\/a00\/first/) k = "new-domain"; else if ($0 ~ /deep\.md/) k = "new-dir"; else if ($0 ~ /^mv /) k = "rename-dir"; else if ($0 ~ / mv /) k = "rename"; else if ($0 ~ /^: >/) k = "create"; else if (k == "rm") k = "delete"; else if (k == "ln") k = "link"; c[k]++ } END { for (k in c) printf "%s=%d ", k, c[k] }' "$W/out/mutations-$2.sh")"
}

# ---- 1. udx-index against the oracle -------------------------------------------------------
"$B/udx-index" "$T" 1 > "$W/out/index.txt" || bad "udx-index failed"
say "$(cat "$W/out/index.txt")"
[ "$(stat -c '%a %u' "$T/ns0/.udx.db")" = "600 0" ] || bad "ns0/.udx.db is $(stat -c '%a %u' "$T/ns0/.udx.db"), expected 600 0"
[ "$(stat -c '%a %u' "$T/ns1/.udx.db")" = "600 1001" ] || bad "ns1/.udx.db is $(stat -c '%a %u' "$T/ns1/.udx.db"), expected 600 1001"
check "after udx-index"
"$B/udx-index" "$T" 1 > "$W/out/reindex.txt" || bad "the second udx-index failed"
awk '{ exit !($9 == 2 * $2) }' "$W/out/reindex.txt" || bad "a second udx-index wrote rows on an unchanged tree: $(cat "$W/out/reindex.txt")"
say "second pass, nothing changed (two meta rows per domain): $(cat "$W/out/reindex.txt")"
# the plan of every W3 shape, printed and not measured. The attribute table must never be read
# whole; walking entry in inode order and probing attr by its key is the planner's choice for a
# term that many entries satisfy (Q9), since the output is ordered by inode anyway.
n=0
grep -v '^#' "$SRC/bench/queries-w3.txt" | while read -r q; do
    n=$((n + 1))
    # shellcheck disable=SC2086
    plan=$(sqlite3 "file:$T/ns3/.udx.db?mode=ro" "EXPLAIN QUERY PLAN $("$B/udx-query" --sql "$T/ns3" $q)" | grep -v 'QUERY PLAN' | sed 's/^[ |`-]*//' | tr '\n' ';')
    echo "udx-test: plan Q$n: $plan"
    case $plan in *"SCAN a"*|*"SCAN m"*|*"SCAN o"*) echo "UDX_FAIL: Q$n reads the whole attribute table" ;; esac
done > "$W/out/plans.txt"
cat "$W/out/plans.txt"
if grep -q UDX_FAIL "$W/out/plans.txt"; then bad "a W3 shape reads the whole attribute table"; fi
# the same statements are what the harness's SQLite client would time on the benchmark host;
# here only that it accepts them (two records of 32 bytes per shape), no timing is reported
# shellcheck disable=SC2086
grep -v '^#' "$SRC/bench/queries-w3.txt" | while read -r q; do "$B/udx-query" --sql "$T/ns3" $q; done > "$W/out/w3.sql"
mkdir -p "$W/out/sqlclient"
"$B/bench-sqlclient" --db "$T/ns3/.udx.db" --sql "$W/out/w3.sql" --out "$W/out/sqlclient/w3" --repeat 2 || bad "bench-sqlclient rejected the statements of udx-query --sql"
[ "$(wc -c < "$W/out/sqlclient/w3.0.rec")" -eq $((18 * 32)) ] || bad "bench-sqlclient record count"
say "bench-sqlclient accepted the nine statements of udx-query --sql (procedure check only)"

# ---- 2. who can ask ------------------------------------------------------------------------
asuser() { u=$1; shift; setpriv --reuid "$u" --regid "$u" --clear-groups "$@"; }
asuser 1001 "$B/udx-query" "$T/ns1" kind eq string note > "$W/out/owner.txt" 2>&1 || bad "the domain owner cannot query: $(head -1 "$W/out/owner.txt")"
"$B/moobfs-index" --uid 1001 --domain "$T/ns1" kind eq string note > "$W/out/owner-oracle.txt"
cmp -s "$W/out/owner.txt" "$W/out/owner-oracle.txt" && [ -s "$W/out/owner.txt" ] || bad "owner's answer differs from the oracle as that user"
if asuser 1002 "$B/udx-query" "$T/ns1" kind eq string note > "$W/out/other.txt" 2>&1; then
    bad "another user read the owner's index"
fi
say "authorization: owner (uid 1001) gets $(wc -l < "$W/out/owner.txt" | tr -d ' ') lines, equal to the oracle as uid 1001; uid 1002 is refused ($(head -1 "$W/out/other.txt")); the oracle as uid 1002 lists $("$B/moobfs-index" --uid 1002 --domain "$T/ns1" kind eq string note | wc -l | tr -d ' ') lines, because the directory is 0755"

# ---- 3. udx-watch: bursts, random changes ---------------------------------------------------
if "$B/udx-query" --require-live "$T/ns0" kind eq string note > /dev/null 2>&1; then bad "--require-live passed with no watcher"; fi
say "no watcher: $("$B/udx-query" --status "$T/ns0" || true)"
start_watch
"$B/udx-query" --require-live "$T/ns0" kind eq string note > /dev/null || bad "--require-live failed with the watcher running"
say "watcher running: $("$B/udx-query" --status "$T/ns0")"
# a burst: W4 rewrites score, then sets and removes a scratch attribute, on every memory
mkdir -p "$W/out/w4"
"$B/bench-load" --manifest "$W/out/manifest.txt" --target "$T" --workload W4 --procs 2 --out "$W/out/w4/w4"
settle; say "W4 burst ($((INODES * 3)) attribute writes): marker visible after $settle_ms ms"
check "after the W4 burst"
run_mutations "$CHANGES" "$SEED"
settle; say "marker visible after $settle_ms ms"
check "after $CHANGES random changes"
run_mutations "$CHANGES" $((SEED + 1))
settle; say "marker visible after $settle_ms ms"
check "after $CHANGES more random changes"

# a write is not visible at once: how many immediate queries miss it, and how long until it shows
f=$(cd "$T" && find . -type f -name 'm*.md' | sed 's|^\./||' | sort | head -1)
d=$T/${f%%/*}; miss=0; : > "$W/out/lag.txt"
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    v=$((7000000 + i)); t0=$(date +%s%N)
    "$A" set "$T/$f" probe int64 "$v"
    [ -n "$("$B/udx-query" "$d" probe eq int64 "$v")" ] || miss=$((miss + 1))
    while [ -z "$("$B/udx-query" "$d" probe eq int64 "$v")" ]; do
        [ $((($(date +%s%N) - t0) / 1000000)) -lt 10000 ] || { bad "a single write was not indexed in 10 s"; break; }
        sleep 0.002
    done
    echo $((($(date +%s%N) - t0) / 1000000)) >> "$W/out/lag.txt"
done
say "read-your-writes: $miss of 20 queries issued right after the write did not see it; visible after $(sort -n "$W/out/lag.txt" | sed -n '1p;10p;20p' | tr '\n' ' ')ms (min, median, max; includes starting two processes; settle 10 ms; not a measurement)"

# removing a whole domain must not be blocked by the index inside it
gone=$(for dd in "$T"/nsnew*/; do [ -d "$dd" ] && echo "${dd%/}"; done | head -1)
if [ -n "$gone" ]; then
    rm -rf "$gone" || bad "rm -rf of a domain failed"
    sleep 1
    [ ! -e "$gone" ] || bad "a removed domain came back"
    say "removed the domain ${gone#"$T"/} with the watcher running"
fi
# a domain renamed (its database travels with it) and a directory moved from one domain to another
mv "$T/ns2" "$T/ns2-renamed"
moved=$(for dd in "$T"/ns3/a*/; do [ -d "$dd" ] && echo "${dd%/}"; done | head -1)
mv "$moved" "$T/ns4/from-ns3"
say "renamed the domain ns2 to ns2-renamed and moved ${moved#"$T"/} ($(find "$T/ns4/from-ns3" -type f | wc -l | tr -d ' ') files) to ns4/from-ns3"
# a domain changes owner: the root watcher re-evaluates it and hands the index to the new owner
chown -R 1002:1002 "$T/ns5"
settle; check "after removing a domain, renaming a domain, moving a directory across domains and giving a domain away"
[ "$(stat -c '%a %u' "$T/ns5/.udx.db")" = "600 1002" ] || bad "ns5/.udx.db is $(stat -c '%a %u' "$T/ns5/.udx.db") after chown, expected 600 1002"
asuser 1002 "$B/udx-query" "$T/ns5" kind eq string note > "$W/out/newowner.txt" 2>&1 || bad "the new owner cannot query: $(head -1 "$W/out/newowner.txt")"
"$B/moobfs-index" --uid 1002 --domain "$T/ns5" kind eq string note > "$W/out/newowner-oracle.txt"
cmp -s "$W/out/newowner.txt" "$W/out/newowner-oracle.txt" && [ -s "$W/out/newowner.txt" ] || bad "the new owner's answer differs from the oracle as that user"
say "ns5 given to uid 1002 with the watcher running: database is $(stat -c '%a %u' "$T/ns5/.udx.db"), uid 1002 gets $(wc -l < "$W/out/newowner.txt" | tr -d ' ') lines, equal to the oracle as uid 1002"

# ---- 4. events lost: a queue of 16 events and a watcher that is not scheduled ---------------
stop_watch
old_queue=$(cat /proc/sys/fs/inotify/max_queued_events)
if echo 16 > /proc/sys/fs/inotify/max_queued_events 2>/dev/null; then
    start_watch
    echo "$old_queue" > /proc/sys/fs/inotify/max_queued_events
    kill -STOP "$watch_pid"
    run_mutations 200 $((SEED + 2))
    say "watcher stopped with SIGSTOP, 200 changes not indexed, and a reader is told: $("$B/udx-query" --status "$T/ns0" || true)"
    quiet=1; compare; quiet=0; say "while it is stopped $cmp_bad of $cmp_n comparisons differ"
    [ "$cmp_bad" -gt 0 ] || bad "expected stale answers while the watcher is stopped"
    kill -CONT "$watch_pid"
    settle
    say "after the overflow: $("$B/udx-query" --status "$T/ns0" || true)"
    "$B/udx-query" --status "$T/ns0" | grep -q 'overflows=[1-9]' || bad "the overflow was not recorded in the database"
    check "after an inotify queue overflow and the full pass it forces"
    stop_watch
    tail -1 "$W/out/watch.err" | grep -q 'overflows=[1-9]' || bad "the watcher did not count the overflow"
else
    bad "cannot lower fs.inotify.max_queued_events: the overflow path was not tested"
fi

# ---- 5. no watcher: nothing updates the index, and a reader can tell -------------------------
run_mutations 100 $((SEED + 3))
quiet=1; compare; quiet=0
[ "$cmp_bad" -gt 0 ] || bad "expected stale answers with no watcher"
say "no watcher, 100 changes: $cmp_bad of $cmp_n comparisons differ, as they must; status: $("$B/udx-query" --status "$T/ns0" || true)"
if "$B/udx-query" --require-live "$T/ns0" kind eq string note > /dev/null 2>&1; then bad "--require-live passed with no watcher"; fi
start_watch
check "after restarting the watcher (its first pass repairs what was missed)"

# ---- 6. a known gap, shown and not counted: a hard link made outside the watched tree --------
quiet=1
victim=$("$B/moobfs-index" --domain "$T/ns0" kind exists - - | grep "$TAB"'a[0-9]*/m[0-9]*\.md$' | head -1 | cut -f2)
ln "$T/ns0/$victim" "$MNT/outside-link"
sleep 1
compare
say "KNOWN_GAP: ns0/$victim got a second name outside the watched tree; no event reaches the watcher; $cmp_bad of $cmp_n comparisons differ (the oracle and the layer's rule drop a file with two names, the index still lists it)"
gap=$cmp_bad
rm "$MNT/outside-link"
stop_watch; start_watch
quiet=0
check "after removing the outside link and a full pass"
stop_watch

# ---- 7. one watcher per user: no privileged component ----------------------------------------
# uid 1001 indexes and watches its own domain (depth 0: the root given is the domain) and asks
# as itself; the oracle runs as the same user. Root made files in that domain during the changes
# above; what uid 1001 cannot read is outside both answers.
asuser 1001 "$B/udx-index" "$T/ns1" 0 > "$W/out/user-index.txt" 2>&1 || bad "udx-index as uid 1001 failed: $(cat "$W/out/user-index.txt")"
setpriv --reuid 1001 --regid 1001 --clear-groups "$B/udx-watch" "$T/ns1" 0 > "$W/out/user-watch.out" 2> "$W/out/user-watch.err" &
user_pid=$!
i=0; until grep -q UDX_WATCH_READY "$W/out/user-watch.out"; do i=$((i + 1)); [ "$i" -lt 100 ] || break; sleep 0.1; done
mine=$(asuser 1001 "$B/moobfs-index" --domain "$T/ns1" kind exists - - | grep "$TAB"'a[0-9]*/m[0-9]*\.md$' | head -1 | cut -f2)
asuser 1001 "$A" set "$T/ns1/$mine" kind string decision
asuser 1001 "$A" set "$T/ns1/$mine" score int64 999
asuser 1001 sh -c ': > "$1" && "$2" set "$1" kind string note' sh "$T/ns1/own-new.md" "$A"
i=0; until [ -n "$(asuser 1001 "$B/udx-query" --require-live "$T/ns1" score eq int64 999 kind eq string decision 2>/dev/null | grep "$TAB$mine\$")" ]; do
    i=$((i + 1)); [ "$i" -lt 500 ] || { bad "the unprivileged watcher did not index a change"; break; }; sleep 0.01
done
sleep 0.3
ubad=0; un=0
while IFS='|' read -r opts terms; do
    # shellcheck disable=SC2086
    "$B/moobfs-index" --uid 1001 --domain $opts "$T/ns1" $terms 2>/dev/null | grep -v "$TAB\\.udx\\.db" > "$W/out/oracle.txt" || true
    # shellcheck disable=SC2086
    asuser 1001 "$B/udx-query" $opts "$T/ns1" $terms > "$W/out/udx.txt" 2>&1 || true
    un=$((un + 1)); cmp -s "$W/out/oracle.txt" "$W/out/udx.txt" || ubad=$((ubad + 1))
done < "$W/out/queries.txt"
kill "$user_pid" && wait "$user_pid" || true
if [ "$ubad" -eq 0 ]; then
    echo "UDX_COMPARE_OK one watcher per user: udx-index, udx-watch and udx-query all as uid 1001 on its own domain, $un comparisons with the oracle as uid 1001, all equal"
else
    bad "one watcher per user: $ubad of $un comparisons differ"
fi
say "as uid 1001: $(cat "$W/out/user-watch.out") / $(tail -1 "$W/out/user-watch.err")"

summary="udx-index and udx-watch equal to moobfs-index --domain on $nq query shapes per domain, on $fs; $INODES memories, $((CHANGES * 2 + 300)) random changes, a W4 burst, an inotify overflow, a watcher restart (known gap shown: $gap comparisons differ after a hard link from outside)"
finish
