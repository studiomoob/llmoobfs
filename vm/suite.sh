#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# MoobFS layer test suite, shared by the Debian initramfs harness (vm/init) and the
# KASAN/lockdep harness (vm/kasan-guest.sh). The caller sets:
#   BIN        directory with layer, fsx, query, ipc, moobfs-*, replay-log, moobfs_layer.ko
#   TOOLS      directory with git, sqlite3, dmsetup, mke2fs, e2fsck, debugfs, setfacl, getfacl
#   MODS       optional directory with lower-fs modules and an "order" file
#   DEV_EXT4 DEV_BTRFS DEV_XFS   block devices carrying the prepared lower filesystems
#   DEV_SCRATCH DEV_LOG          blank block devices for dm-log-writes and dm-flakey
# and a fail() function. LOWER and LAYER (mount points) default to /lower and /mnt/layer.
# Prints MOOBFS_LAYER_PASS and MOOBFS_ACCEPTANCE_PASS on success.
# MOOB_ONLY (optional) runs only the named steps, for development:
#   ext4 btrfs xfs      everything of run_lower for that lower filesystem
#   demo clients powerloss ioerror reserve
#   casefold            a casefolded lower directory and lower root are refused (issue #95)
#   faults              only under the KASAN harness: the fault-injection block of kasan-guest.sh
#   buildrace           the first query on a domain racing with writes to it (journal 0098)
#   release             built domains released by the budget and by the shrinker (journal 0101)
#   sqlext              the moobfs SQLite virtual table against an ordinary table (journal 0108)
#   permmatrix          QUERY2 against the lower's own permission decision, random modes (0116)
#   permgaps            decisive search, truncation, directory and encoding checks (0172)
#   rootpath            a domain directory renamed while a build waits to start (issue #110)
#   overbudget          builds over the budget while other domains are built (issue #111)
#   memcg               account a retained QUERY2 buffer to the calling memory cgroup (#116)
#   buildcost           bounded unprivileged first-build and post-rename cost probe (#118)
#   buildcostlarge      optional 7500-file scale of the same probe (not in the full suite)
#   builddenial         optional 17000-file cold QUERY2 cost with/without work cap
#   buildclient         optional 6000-file, six-attribute domain shape from client scale
#   buildcostevict      optional two-uid budget probe: the second uid lends the first one's room
#                       back, since that domain is over its share (ADR-0021, ADR-0027)
#   buildlimit          optional QUERY2 work and admission limit regression (#118)
#   buildwork           optional per-mount query-build work policy regression (#118)
#   buildtime           optional cooperative scan-deadline regression (#118)
#   buildmoved          optional cumulative work across a moved-directory rescan (#118)
#   stall               a FUSE mount that never answers, inside a checked domain (journal 0116)
#   lowerro             a read-only, frozen or idmapped lower mount (journal 0117)
#   bigdir              a domain built from a directory larger than one read batch (journal 0120)
#   xattrread           a listed typed value that fails getxattr must fail a build or invalidate
#                       a built domain; an attribute removed between list/get is skipped
#   domainrecs          removed domain directories leave no record and no fallback mark (#113)
#   domainreuse         domain directories and files removed on the lower and their inode numbers
#                       reused: the new ones are not answered from the removed ones' index
#   minacl              minimal access ACLs planted with debugfs: the kernel, moobfs-index,
#                       moobfs-check and the Python scan agree on which ones are extended (0222)
#   ixlock              inodes without an index record are instantiated and evicted without
#                       waiting for the index lock held by a build (issue #117)
#   fairshare           a uid over its share of index_budget lends room to a uid under its own,
#                       and a uid within its share keeps its domain (ADR-0027)
#   landlock            the moobfs CLI under a Landlock sandbox that cannot read
#                       /proc/self/mountinfo answers as without it (issue #167)
#   pylib               the Python library's kernel and scan backends give equal answers (C5);
#                       needs python3 in the guest, so it runs under KASAN and not in vm-test
# A run with MOOB_ONLY ends in MOOBFS_ONLY_PASS, never in the two markers above, so it cannot
# be taken for a complete run.
set -u
# Capture the producer status before displaying its output: POSIX pipelines return
# the last command's status. A failed log read also makes the check fail.
run_logged() (
    log=$1; shift
    rc=0
    "$@" > "$log" || rc=$?
    cat "$log" || return 1
    return "$rc"
)
want() {
    [ -z "${MOOB_ONLY:-}" ] && return 0
    case " $MOOB_ONLY " in *" $1 "*) return 0 ;; esac
    return 1
}
for step in ${MOOB_ONLY:-}; do
    case "$step" in ext4|btrfs|xfs|demo|clients|powerloss|ioerror|reserve|casefold|faults|buildrace|release|sqlext|permmatrix|permgaps|rootpath|overbudget|memcg|buildcost|buildcostlarge|builddenial|buildclient|buildcostevict|buildlimit|buildwork|buildtime|buildmoved|stall|lowerro|bigdir|xattrread|domainrecs|domainreuse|minacl|pylib|ixlock|fairshare|landlock) ;; *) fail "unknown MOOB_ONLY step $step" ;; esac
done
LOWER=${LOWER:-/lower}
LAYER=${LAYER:-/mnt/layer}
mkdir -p "$LOWER" "$LAYER" /tmp
# Phases 2a/2b: memory layer stacked over ext4, btrfs and XFS (virtio disks vda, vdb, vdc).
# shellcheck disable=SC2153   # MODS comes from the environment (vm/init, vm/kasan-guest.sh)
if [ -f "$MODS/order" ]; then while read -r m; do insmod "$MODS/$m" || fail "lower module $m"; done < "$MODS/order"; fi
insmod "$BIN/moobfs_layer.ko" || fail layer-module
mount -t moobfs_layer none "$LAYER" && fail layer-mount-without-lower
mount -t moobfs_layer -o lower=/nonexistent none "$LAYER" && fail layer-mount-bad-lower
run_lower() {
    fs=$1; dev=$2; reflink=$3
    mount -t "$fs" "$dev" "$LOWER" || fail "lower-mount-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "layer-mount-$fs"
    grep -q 'moobfs_layer' /proc/mounts || fail "layer-proc-mounts-$fs"
    mount -t moobfs_layer -o lower="$LAYER" none /mnt && fail "layer-stack-on-itself-$fs"
    "$BIN/layer" "$LAYER" "$LOWER" "$reflink" links || fail "layer-tests-$fs"
    "$BIN/fcntl-flags" "$LAYER/fcntl-flags" || fail "fcntl-flags-$fs"
    rm "$LAYER/fcntl-flags" || fail "fcntl-flags-cleanup-$fs"
    # fadvise, readahead, statx fields, drop_inode, F_SETFL O_NOATIME act as on the lower
    # (issue #115, journal 0224); prints PASSTHROUGH_PASS and removes its files
    "$BIN/passthrough" "$LAYER" "$LOWER" || fail "passthrough-$fs"
    # every refusal of a typed attribute write with its errno, and the limit of 64 per file,
    # paths a syzkaller seed reaches poorly (journal 0141); prints TYPED_LIMITS_PASS
    "$BIN/layer" typed "$LAYER" "$LOWER" || fail "typed-limits-$fs"
    # typed attribute tools: write through the layer, check the lower tree
    echo memo > "$LAYER/typed.md" || fail "typed-create-$fs"
    "$BIN/moobfs-attr" set "$LAYER/typed.md" kind string decision || fail "attr-set-$fs"
    "$BIN/moobfs-attr" set "$LAYER/typed.md" created time 1760000000000000000 || fail "attr-time-$fs"
    "$BIN/moobfs-attr" set "$LAYER/typed.md" pinned bool true || fail "attr-bool-$fs"
    "$BIN/moobfs-attr" set "$LAYER/typed.md" score double 0.5 || fail "attr-double-$fs"
    [ "$("$BIN/moobfs-attr" get "$LAYER/typed.md" kind)" = "$(printf 'kind\tstring\tdecision')" ] || fail "attr-get-$fs"
    [ "$("$BIN/moobfs-attr" list "$LOWER/typed.md" | wc -l)" -eq 4 ] || fail "attr-list-lower-$fs"
    "$BIN/moobfs-attr" set "$LAYER/typed.md" Bad string x 2>/dev/null && fail "attr-badname-accepted-$fs"
    "$BIN/moobfs-check" "$LOWER" > /tmp/check.json || fail "check-clean-$fs"
    grep -q '"problem_count": 0' /tmp/check.json || fail "check-count-$fs"
    # plant an invalid encoding directly on the lower tree: the checker must flag it
    "$BIN/moobfs-attr" rawset "$LOWER/typed.md" broken 0402 || fail "rawset-lower-$fs"
    "$BIN/moobfs-check" "$LOWER" > /tmp/check.json; [ $? -eq 2 ] || fail "check-detects-$fs"
    grep -q 'payload is not a valid value' /tmp/check.json || fail "check-reason-$fs"
    "$BIN/moobfs-attr" get "$LAYER/typed.md" broken >/dev/null; [ $? -eq 2 ] || fail "attr-get-invalid-$fs"
    "$BIN/moobfs-attr" rm "$LAYER/typed.md" broken || fail "attr-rm-$fs"
    "$BIN/moobfs-check" "$LOWER" > /dev/null || fail "check-clean-again-$fs"
    rm "$LAYER/typed.md" || fail "typed-rm-$fs"
    # index and QUERY: the kernel answer must equal the user-space oracle on the lower tree
    same_query() {
        "$BIN/query" "$LAYER" "$@" > /tmp/k.txt || return 1
        "$BIN/moobfs-index" "$LOWER" "$@" > /tmp/o.txt || return 1
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; return 1; }
    }
    mkdir -p "$LAYER/q/sub" || fail "q-mkdir-$fs"
    for i in 1 2 3 4 5; do
        echo "memory $i" > "$LAYER/q/f"$i
        "$BIN/moobfs-attr" set "$LAYER/q/f"$i score int64 $((i * 10)) || fail "q-score-$fs"
    done
    "$BIN/moobfs-attr" set "$LAYER/q/f1" kind string decision || fail "q-kind-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f2" kind string note || fail "q-kind-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f3" kind string decided || fail "q-kind-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f4" kind string note || fail "q-kind-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/sub" kind string folder || fail "q-kind-dir-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f2" pinned bool true || fail "q-pinned-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f5" pinned bool false || fail "q-pinned-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f3" weight double 0.75 || fail "q-double-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f4" weight double -2.5 || fail "q-double-$fs"
    "$BIN/moobfs-attr" set "$LAYER/q/f1" raw raw deadbeef || fail "q-raw-$fs"
    same_query kind eq string decision || fail "query-eq-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'q/f1$' /tmp/k.txt || fail "query-eq-content-$fs"
    same_query kind eq string note score ge int64 40 || fail "query-and-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'q/f4$' /tmp/k.txt || fail "query-and-content-$fs"
    # Index findings of the inspection of 2026-10-13 (journal 0083), in a directory of their own.
    mkdir -p "$LAYER/insp" || fail "insp-mkdir-$fs"
    echo v > "$LAYER/insp/victim" && echo m > "$LAYER/insp/mover" || fail "insp-create-$fs"
    "$BIN/moobfs-attr" set "$LAYER/insp/victim" rk string victim || fail "insp-attr-$fs"
    "$BIN/moobfs-attr" set "$LAYER/insp/mover" rk string mover || fail "insp-attr-$fs"
    # a rename over an indexed file removes the replaced file from the index
    "$BIN/query" "$LAYER" build || fail "insp-build-$fs"   # counters cover built domains only (ADR-0012)
    n0=$("$BIN/query" "$LAYER" info | sed -n 's/.* inodes=\([0-9]*\).*/\1/p')
    mv "$LAYER/insp/mover" "$LAYER/insp/victim" || fail "insp-rename-$fs"
    n1=$("$BIN/query" "$LAYER" info | sed -n 's/.* inodes=\([0-9]*\).*/\1/p')
    [ -n "$n0" ] && [ "$n1" -eq $((n0 - 1)) ] || { echo "inodes $n0 -> $n1"; fail "insp-rename-over-count-$fs"; }
    same_query rk eq string victim || fail "insp-rename-over-$fs"
    [ ! -s /tmp/k.txt ] || fail "insp-rename-over-stale-$fs"
    same_query rk eq string mover || fail "insp-rename-mover-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'insp/victim$' /tmp/k.txt || fail "insp-rename-mover-content-$fs"
    # a typed value replaced by a raw one is no longer found
    "$BIN/moobfs-attr" set "$LAYER/insp/victim" tr string typed || fail "insp-typed-$fs"
    same_query tr eq string typed || fail "insp-typed-query-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] || fail "insp-typed-count-$fs"
    "$BIN/moobfs-attr" set "$LAYER/insp/victim" tr raw c0ffee || fail "insp-raw-$fs"
    same_query tr eq string typed || fail "insp-raw-over-typed-$fs"
    [ ! -s /tmp/k.txt ] || fail "insp-raw-over-typed-stale-$fs"
    # a name longer than the result field is returned cut and marked, not dropped
    deep=$(head -c 120 /dev/zero | tr '\0' d)
    mkdir -p "$LAYER/insp/$deep/$deep/$deep" && echo l > "$LAYER/insp/$deep/$deep/$deep/leaf" || fail "insp-deep-$fs"
    "$BIN/moobfs-attr" set "$LAYER/insp/$deep/$deep/$deep/leaf" lp string deep || fail "insp-deep-attr-$fs"
    "$BIN/query" "$LAYER" lp eq string deep > /tmp/k.txt || fail "insp-deep-query-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q '(truncated)$' /tmp/k.txt || { cat /tmp/k.txt; fail "insp-deep-truncated-$fs"; }
    # moobfs-check --domain-depth lists the files QUERY2 would leave out, with the reason
    mkdir -p "$LAYER/insp/lo/dom/open" "$LAYER/insp/lo/dom/shut" && chmod 755 "$LAYER/insp/lo" "$LAYER/insp/lo/dom" "$LAYER/insp/lo/dom/open" || fail "lo-mkdir-$fs"
    chmod 700 "$LAYER/insp/lo/dom/shut" || fail "lo-chmod-$fs"
    for f in dom/open/in.md dom/shut/hidden.md dom/open/linked.md top.md; do
        echo x > "$LAYER/insp/lo/$f" && chmod 644 "$LAYER/insp/lo/$f" && "$BIN/moobfs-attr" set "$LAYER/insp/lo/$f" lo string v || fail "lo-file-$f-$fs"
    done
    ln "$LAYER/insp/lo/dom/open/linked.md" "$LAYER/insp/lo/dom/open/second-name.md" || fail "lo-link-$fs"
    "$BIN/moobfs-check" --domain-depth 1 "$LOWER/insp/lo" > /tmp/lo.json || { cat /tmp/lo.json; fail "lo-check-status-$fs"; }
    grep -q '"left_out_count": 4' /tmp/lo.json || { cat /tmp/lo.json; fail "lo-count-$fs"; }
    grep -q '"path": "dom/shut/hidden.md", "reason": "directory dom/shut: more private than the domain directory"' /tmp/lo.json || { cat /tmp/lo.json; fail "lo-private-dir-$fs"; }
    grep -q '"path": "dom/open/linked.md", "reason": "has more than one hard link"' /tmp/lo.json || fail "lo-hardlink-$fs"
    grep -q '"path": "top.md", "reason": "above every domain directory"' /tmp/lo.json || fail "lo-above-$fs"
    grep -q '"path": "dom/open/in.md"' /tmp/lo.json && { cat /tmp/lo.json; fail "lo-listed-a-queryable-file-$fs"; }
    "$BIN/moobfs-check" "$LOWER/insp/lo" | grep -q left_out && fail "lo-listed-without-option-$fs"
    rm -rf "$LAYER/insp" || fail "insp-cleanup-$fs"
    same_query lp eq string deep || fail "insp-cleanup-query-$fs"
    [ ! -s /tmp/k.txt ] || fail "insp-cleanup-stale-$fs"
    same_query kind prefix string dec || fail "query-prefix-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 2 ] || fail "query-prefix-count-$fs"
    same_query score gt int64 20 || fail "query-gt-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 3 ] || fail "query-gt-count-$fs"
    same_query score lt int64 0 || fail "query-empty-$fs"
    [ ! -s /tmp/k.txt ] || fail "query-empty-count-$fs"
    # one attribute name holding several types across files: the scan seeks to the first
    # candidate and stops after the last one, so every op must still equal the oracle when
    # entries of other types sit before and after the term's run in the tree
    i=0
    for v in "string b" "string ba" "string bz" "string c" "string a" "int64 5" "int64 -3" "int64 5" "double 1.5" "double -2.25" "bool true" "bool false" "time 100" "time 50"; do
        i=$((i + 1)); : > "$LAYER/q/m"$i
        # shellcheck disable=SC2086
        $BIN/moobfs-attr set $LAYER/q/m$i mix $v || fail "q-mix-set-$i-$fs"
    done
    for t in "eq string b" "prefix string b" "prefix string bz" "prefix string x" "lt string c" "le string ba" "gt string b" "ge string bz" \
             "eq int64 5" "lt int64 5" "le int64 5" "gt int64 -3" "ge int64 -3" "gt int64 100" "lt int64 -10" \
             "eq double 1.5" "lt double 0" "ge double -2.25" "eq bool true" "lt bool true" "gt time 50" "le time 100" "exists - -"; do
        # shellcheck disable=SC2086
        same_query mix $t || fail "query-mix-$t-$fs"
    done
    [ "$("$BIN/query" "$LAYER" mix eq int64 5 | wc -l)" -eq 2 ] || fail "query-mix-eq-count-$fs"
    [ "$("$BIN/query" "$LAYER" mix prefix string b | wc -l)" -eq 3 ] || fail "query-mix-prefix-count-$fs"
    [ "$("$BIN/query" "$LAYER" mix exists - - | wc -l)" -eq 14 ] || fail "query-mix-exists-count-$fs"
    rm -f "$LAYER/q/m"* || fail "q-mix-cleanup-$fs"
    same_query pinned exists - - || fail "query-exists-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 2 ] || fail "query-exists-count-$fs"
    same_query pinned eq bool true || fail "query-bool-$fs"
    same_query weight lt double 0 || fail "query-double-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] && grep -q 'q/f4$' /tmp/k.txt || fail "query-double-content-$fs"
    same_query kind eq string folder || fail "query-dir-$fs"
    grep -q 'q/sub$' /tmp/k.txt || fail "query-dir-content-$fs"
    "$BIN/query" "$LAYER" raw exists - - > /tmp/k.txt || fail "query-raw-$fs"
    [ ! -s /tmp/k.txt ] || fail "query-raw-not-indexed-$fs"
    "$BIN/query" "$LAYER" invalid || fail "query-invalid-$fs"
    "$BIN/query" "$LAYER" build || fail "query-build-$fs"
    "$BIN/query" "$LAYER" info > /tmp/info.txt || fail "query-info-$fs"
    grep -q "lower=$fs" /tmp/info.txt || fail "query-info-lower-$fs"
    grep -q 'entries=1[0-9] inodes=6' /tmp/info.txt || { cat /tmp/info.txt; fail "query-info-counts-$fs"; }
    # updates: rename, directory rename, unlink and removexattr are reflected
    mv "$LAYER/q/f1" "$LAYER/q/g1" || fail "q-rename-$fs"
    same_query kind eq string decision || fail "query-renamed-$fs"
    grep -q 'q/g1$' /tmp/k.txt || fail "query-renamed-path-$fs"
    mv "$LAYER/q" "$LAYER/r" || fail "q-dir-rename-$fs"
    same_query kind eq string decision || fail "query-dir-renamed-$fs"
    grep -q 'r/g1$' /tmp/k.txt || fail "query-dir-renamed-path-$fs"
    rm "$LAYER/r/f2" || fail "q-unlink-$fs"
    same_query kind eq string note || fail "query-unlinked-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 1 ] || fail "query-unlinked-count-$fs"
    "$BIN/moobfs-attr" rm "$LAYER/r/f4" kind || fail "q-removexattr-$fs"
    same_query kind eq string note || fail "query-removexattr-$fs"
    [ ! -s /tmp/k.txt ] || fail "query-removexattr-count-$fs"
    # remount rebuilds the index from the lower tree
    umount "$LAYER" || fail "q-umount-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "q-remount-$fs"
    same_query score ge int64 10 || fail "query-after-remount-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 4 ] || fail "query-after-remount-count-$fs"
    same_query kind eq string decision || fail "query-after-remount-eq-$fs"
    grep -q 'r/g1$' /tmp/k.txt || fail "query-after-remount-path-$fs"
    # moobfs CLI (roadmap C4): without domains the mount root answers the mount-wide QUERY
    "$BIN/moobfs" query --ino "$LAYER" score ge int64 10 > /tmp/k.txt || fail "cli-v1-$fs"
    "$BIN/moobfs-index" "$LOWER" score ge int64 10 > /tmp/o.txt || fail "cli-v1-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || { cat /tmp/k.txt /tmp/o.txt; fail "cli-v1-mismatch-$fs"; }
    [ "$(wc -l < /tmp/k.txt)" -eq 4 ] || fail "cli-v1-count-$fs"
    "$BIN/moobfs" query "$LAYER/r" score ge int64 10 > /dev/null 2> /tmp/e.txt; [ $? -eq 69 ] || fail "cli-v1-subdir-status-$fs"
    grep -q 'domains are off' /tmp/e.txt || { cat /tmp/e.txt; fail "cli-v1-subdir-message-$fs"; }
    umount "$LAYER" || fail "q-umount2-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0,noindex none "$LAYER" || fail "q-noindex-mount-$fs"
    "$BIN/query" "$LAYER" score ge int64 10 > /tmp/noindex.txt 2>&1 && fail "query-noindex-answered-$fs"
    grep -q 'Operation not supported' /tmp/noindex.txt || { cat /tmp/noindex.txt; fail "query-noindex-errno-$fs"; }
    [ "$("$BIN/query" "$LAYER" q2errno kind exists x x)" = "q2 95" ] || fail "query2-noindex-errno-$fs"   # EOPNOTSUPP
    "$BIN/moobfs" query "$LAYER" score ge int64 10 > /dev/null 2> /tmp/e.txt; [ $? -eq 69 ] || fail "cli-noindex-status-$fs"
    grep -q 'index of this mount is off' /tmp/e.txt || { cat /tmp/e.txt; fail "cli-noindex-message-$fs"; }
    f=$("$BIN/query" "$LAYER" info | sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p')
    [ -n "$f" ] && [ $(( f & 2 )) -ne 0 ] || fail "query-noindex-flag-$fs"   # MOOB_FLAG_INDEX_OFF
    umount "$LAYER" || fail "q-umount3-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0,index_budget=1M none "$LAYER" || fail "q-budget-mount-$fs"
    same_query score ge int64 10 || fail "query-budget-$fs"
    umount "$LAYER" || fail "q-umount4-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "q-remount-final-$fs"
    # a v1 QUERY from an ordinary user with a bad request is refused before anything is built
    # (journal 0105): before, with domain_depth=0, it built the whole index first
    [ "$("$BIN/query" --uid 1001 "$LAYER" v1bad)" = "q1 22" ] || fail "v1-bad-request-$fs"
    [ "$("$BIN/query" "$LAYER" info | sed -n 's/^domains_built=\([0-9]*\) .*/\1/p')" = 0 ] || fail "v1-bad-request-built-$fs"
    # a typed attribute on a write-only file, set by its owner: the lower accepts it, so must the
    # layer (review item 7, journal 0120)
    : > "$LAYER/wo.md" && chown 1001:1001 "$LAYER/wo.md" && chmod 0200 "$LAYER/wo.md" || fail "wo-setup-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER" xset "$LAYER/wo.md" kind decision)" = "xset 0" ] || fail "typed-attr-write-only-refused-$fs"
    [ "$("$BIN/moobfs-attr" get "$LOWER/wo.md" kind)" = "$(printf 'kind\tstring\tdecision')" ] || fail "wo-lower-$fs"
    rm -f "$LAYER/wo.md" || fail "wo-cleanup-$fs"
    # remount (journal 0106): read-only refuses writes and read-write restores them; an option
    # given on remount through mount(2) is ignored, as overlayfs does, and through the new mount
    # API (fsconfig) refused when it changes the mount, accepted when it repeats the current
    # value, as libmount does on every remount (util-linux 2.41, journal 0118)
    mount -o remount,ro "$LAYER" || fail "remount-ro-$fs"
    touch "$LAYER/ro-probe" 2> /dev/null && fail "remount-ro-writable-$fs"
    mount -o remount,rw "$LAYER" || fail "remount-rw-$fs"
    touch "$LAYER/ro-probe" && rm "$LAYER/ro-probe" || fail "remount-rw-not-writable-$fs"
    "$BIN/reconf" "$LAYER" "$LOWER" 0 || fail "remount-fsconfig-$fs"
    grep " $LAYER " /proc/mounts | grep -q "index_budget" && fail "remount-option-changed-$fs"
    rm -rf "$LAYER/r" || fail "q-cleanup-$fs"
    # isolation (M3 phase 3b): two users, two namespaces; QUERY as each user must equal the
    # oracle run as that user, and must not reveal the other namespace, not even in counts
    mkdir -p "$LAYER/ns/a" "$LAYER/ns/b" "$LAYER/ns/shared" || fail "iso-mkdir-$fs"
    for d in a b shared; do
        for i in 1 2 3; do
            echo "secret $d $i" > "$LAYER/ns/$d/m"$i.md
            chmod 0600 "$LAYER/ns/$d/m"$i.md || fail "iso-mode-$fs"
            "$BIN/moobfs-attr" set "$LAYER/ns/$d/m"$i.md kind string note || fail "iso-attr-$fs"
            "$BIN/moobfs-attr" set "$LAYER/ns/$d/m"$i.md namespace string $d || fail "iso-ns-$fs"
        done
    done
    chown -R 1001:1001 "$LAYER/ns/a" && chmod 0700 "$LAYER/ns/a" || fail "iso-chown-a-$fs"
    chown -R 1002:1002 "$LAYER/ns/b" && chmod 0700 "$LAYER/ns/b" || fail "iso-chown-b-$fs"
    chmod -R a+rX "$LAYER/ns/shared" && chmod 0755 "$LAYER/ns" "$LAYER/ns/shared" || fail "iso-shared-$fs"
    chmod 0755 "$LAYER" || fail "iso-root-mode-$fs"   # QUERY needs an open descriptor of the root
    for u in 1001 1002; do
        "$BIN/query" --uid $u "$LAYER" kind eq string note > /tmp/k.txt || fail "iso-query-$u-$fs"
        "$BIN/moobfs-index" --uid $u "$LOWER" kind eq string note > /tmp/o.txt || fail "iso-oracle-$u-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "iso-mismatch-$u-$fs"; }
        [ "$(wc -l < /tmp/k.txt)" -eq 6 ] || fail "iso-count-$u-$fs"
        other=$([ $u = 1001 ] && echo b || echo a)
        grep -q "ns/$other/" /tmp/k.txt && fail "iso-leak-$u-$fs"
        grep -q "ns/shared/" /tmp/k.txt || fail "iso-shared-missing-$u-$fs"
    done
    "$BIN/query" "$LAYER" kind eq string note > /tmp/k.txt || fail "iso-query-root-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 9 ] || fail "iso-root-count-$fs"
    # many candidates, few authorized: results and counts must stay exact beyond one chunk
    mkdir -p "$LAYER/ns/bulk" && chmod 0700 "$LAYER/ns/bulk" || fail "iso-bulk-$fs"
    for i in $(seq 1 600); do : > "$LAYER/ns/bulk/f$i"; done
    "$BIN/moobfs-attr" set "$LAYER/ns/bulk/f1" kind string note >/dev/null || fail "iso-bulk-attr-$fs"
    for i in $(seq 2 600); do setfattr -n user.moob.kind -v "$(printf '\001note')" "$LAYER/ns/bulk/f$i" 2>/dev/null || "$BIN/moobfs-attr" set "$LAYER/ns/bulk/f$i" kind string note || fail "iso-bulk-attr-$fs"; done
    chown -R 1002:1002 "$LAYER/ns/bulk" || fail "iso-bulk-chown-$fs"
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "iso-bulk-query-$fs"
    [ "$(wc -l < /tmp/k.txt)" -eq 6 ] || fail "iso-bulk-count-$fs"
    "$BIN/moobfs-index" --uid 1001 "$LOWER" kind eq string note > /tmp/o.txt || fail "iso-bulk-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || fail "iso-bulk-mismatch-$fs"
    rm -rf "$LAYER/ns/bulk" || fail "iso-bulk-rm-$fs"
    # changing the attribute never grants access; changing permissions does, immediately
    "$BIN/moobfs-attr" set "$LAYER/ns/b/m1.md" namespace string a || fail "iso-reattr-$fs"
    "$BIN/query" --uid 1001 "$LAYER" namespace eq string a > /tmp/k.txt || fail "iso-query-reattr-$fs"
    grep -q "ns/b/" /tmp/k.txt && fail "iso-attr-grants-$fs"
    chmod 0755 "$LAYER/ns/b" && chmod a+r "$LAYER/ns/b/m1.md" || fail "iso-chmod-$fs"
    "$BIN/query" --uid 1001 "$LAYER" namespace eq string a > /tmp/k.txt || fail "iso-query-chmod-$fs"
    grep -q "ns/b/m1.md" /tmp/k.txt || fail "iso-perm-not-seen-$fs"
    grep -q "ns/b/m2.md" /tmp/k.txt && fail "iso-perm-overreach-$fs"
    umount "$LAYER" || fail "iso-umount-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "iso-remount-$fs"
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "iso-query-remount-$fs"
    grep -q "ns/b/m2.md" /tmp/k.txt && fail "iso-leak-remount-$fs"
    # ACLs: QUERY must agree with open() under the same uid when ACLs grant or deny
    chmod 0700 "$LAYER/ns/b" && chmod 0600 "$LAYER/ns/b/m1.md" || fail "acl-reset-$fs"
    "$TOOLS/setfacl" -m u:1001:x "$LAYER/ns/b" && "$TOOLS/setfacl" -m u:1001:r "$LAYER/ns/b/m2.md" || fail "acl-set-$fs"
    "$BIN/query" --uid 1001 "$LAYER" open ns/b/m2.md | grep -q ': ok' || fail "acl-open-grant-$fs"
    # search alone on ns/b: 1001 opens m2.md by name but cannot list ns/b, so no answer names
    # it (journal 0215); read and search on ns/b let it be answered
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "acl-query-$fs"
    grep -q 'ns/b/m2.md' /tmp/k.txt && fail "acl-search-only-leak-$fs"
    "$TOOLS/setfacl" -m u:1001:rx "$LAYER/ns/b" || fail "acl-set-rx-$fs"
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "acl-query-rx-$fs"
    grep -q 'ns/b/m2.md' /tmp/k.txt || fail "acl-grant-not-seen-$fs"
    grep -q 'ns/b/m1.md' /tmp/k.txt && fail "acl-overreach-$fs"
    "$TOOLS/setfacl" -m u:1001:- "$LAYER/ns/shared/m1.md" || fail "acl-deny-$fs"
    "$BIN/query" --uid 1001 "$LAYER" open ns/shared/m1.md | grep -q 'Permission denied' || fail "acl-open-deny-$fs"
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "acl-query2-$fs"
    grep -q 'ns/shared/m1.md' /tmp/k.txt && fail "acl-deny-leak-$fs"
    "$TOOLS/setfacl" -b "$LAYER/ns/b" "$LAYER/ns/b/m2.md" "$LAYER/ns/shared/m1.md" || fail "acl-clear-$fs"
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "acl-query3-$fs"
    grep -q 'ns/b/m2.md' /tmp/k.txt && fail "acl-clear-leak-$fs"
    grep -q 'ns/shared/m1.md' /tmp/k.txt || fail "acl-clear-missing-$fs"
    # identity (spec-m3 §2): same id across namespaces is normal, on two inodes in one namespace is an error
    for f in ns/a/m1.md ns/shared/m1.md; do "$BIN/moobfs-attr" set "$LAYER/$f" memory_id string id-1 || fail "id-set-$fs"; done
    "$BIN/moobfs-attr" set "$LAYER/ns/a/m1.md" namespace string a && "$BIN/moobfs-attr" set "$LAYER/ns/shared/m1.md" namespace string shared || fail "id-ns-$fs"
    "$BIN/moobfs-check" "$LOWER" > /tmp/check.json || { cat /tmp/check.json; fail "id-check-normal-$fs"; }
    grep -q '"same_id_other_namespace": 1' /tmp/check.json || fail "id-cross-count-$fs"
    ln "$LAYER/ns/a/m1.md" "$LAYER/ns/a/link1.md" 2>/dev/null && { "$BIN/moobfs-check" "$LOWER" > /tmp/check.json || fail "id-hardlink-flagged-$fs"; rm "$LAYER/ns/a/link1.md"; }
    "$BIN/moobfs-attr" set "$LAYER/ns/a/m2.md" memory_id string id-1 && "$BIN/moobfs-attr" set "$LAYER/ns/a/m2.md" namespace string a || fail "id-dup-set-$fs"
    "$BIN/moobfs-check" "$LOWER" > /tmp/check.json; [ $? -eq 2 ] || fail "id-dup-not-detected-$fs"
    grep -q 'duplicate (namespace, memory_id)' /tmp/check.json || fail "id-dup-reason-$fs"
    "$BIN/moobfs-attr" rm "$LAYER/ns/a/m2.md" memory_id || fail "id-dup-rm-$fs"
    # QUERY v2 (docs/spec-query-v2.md, phase Q2a): first-level directories are domains.
    # The tree is built under domain_depth=0 so that no permission change happens after the
    # domain mount; the kernel answer is compared with the oracle's independent homogeneity rule.
    mkdir -p "$LAYER/da/sub" "$LAYER/db" "$LAYER/dshared" || fail "q2-mkdir-$fs"
    for d in da db dshared da/sub; do
        for i in 1 2 3; do
            [ $d = da/sub ] && [ $i -gt 1 ] && continue
            echo "q2 $d $i" > "$LAYER/$d/m"$i.md
            "$BIN/moobfs-attr" set "$LAYER/$d/m"$i.md kind string note >/dev/null || fail "q2-attr-$fs"
        done
    done
    chmod 0600 "$LAYER/da/m"*.md "$LAYER/da/sub/m"*.md "$LAYER/db/m"*.md && chmod 0644 "$LAYER/dshared/m"*.md || fail "q2-mode-$fs"
    chown -R 1001:1001 "$LAYER/da" && chmod 0700 "$LAYER/da" "$LAYER/da/sub" || fail "q2-chown-a-$fs"
    chown -R 1002:1002 "$LAYER/db" && chmod 0700 "$LAYER/db" || fail "q2-chown-b-$fs"
    chmod 0755 "$LAYER/dshared" || fail "q2-shared-$fs"
    umount "$LAYER" || fail "q2-umount-$fs"
    mount -t moobfs_layer -o lower="$LOWER" none "$LAYER" || fail "q2-mount-$fs"
    "$BIN/query" "$LAYER" build || fail "q2-build-$fs"
    "$BIN/query" "$LAYER" info > /tmp/info.txt || fail "q2-info-$fs"
    grep -q 'domain_depth=1 ' /tmp/info.txt || { cat /tmp/info.txt; fail "q2-info-depth-$fs"; }
    grep -q 'fallback_domains=0' /tmp/info.txt || { cat /tmp/info.txt; fail "q2-info-fallback0-$fs"; }
    f=$(sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p' /tmp/info.txt)
    [ $(( f & 16 )) -ne 0 ] || { cat /tmp/info.txt; fail "q2-info-flag-$fs"; }   # MOOB_FLAG_DOMAINS
    hetero() { "$BIN/query" "$LAYER" info | sed -n 's/.*heterogeneous=\([0-9]*\).*/\1/p'; }
    q2cmp() {   # uid domain expected-count tag
        "$BIN/query" --uid "$1" "$LAYER/""$2" q2 kind eq string note > /tmp/k.txt || fail "q2-query-$4-$fs"
        "$BIN/moobfs-index" --domain "$LOWER/""$2" kind eq string note > /tmp/o.txt || fail "q2-oracle-$4-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "q2-mismatch-$4-$fs"; }
        [ "$(wc -l < /tmp/k.txt)" -eq "$3" ] || { cat /tmp/k.txt; fail "q2-count-$4-$fs"; }
    }
    q2cmp 1001 da 4 own-a
    q2cmp 1002 db 3 own-b
    q2cmp 1001 dshared 3 shared
    grep -q 'sub/m1.md' /tmp/k.txt && fail "q2-shared-path-$fs"
    "$BIN/query" --uid 1001 "$LAYER/da" q2 kind eq string note | grep -q "	sub/m1.md$" || fail "q2-relative-path-$fs"
    # isolation (test 5) and descriptors that are not domains
    [ "$("$BIN/query" --uid 1001 "$LAYER/db" q2errno kind eq string note)" = "q2 13" ] || fail "q2-isolation-$fs"
    [ "$("$BIN/query" "$LAYER/da/sub" q2errno kind eq string note)" = "q2 25" ] || fail "q2-enotty-sub-$fs"
    [ "$("$BIN/query" "$LAYER" q2errno kind eq string note)" = "q2 25" ] || fail "q2-enotty-root-$fs"
    # moobfs CLI (roadmap C4): as two users, equal to the oracle, paged, ordered and limited
    clicmp() {   # uid domain tag [cli options]: the CLI's answer equals the oracle's
        u=$1; d=$2; tag=$3; shift 3
        "$BIN/asuid" "$u" "$BIN/moobfs" query --ino "$@" "$LAYER/$d" kind eq string note > /tmp/k.txt || fail "cli-query-$tag-$fs"
        "$BIN/moobfs-index" --domain "$LOWER/$d" kind eq string note > /tmp/o.txt || fail "cli-oracle-$tag-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "cli:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "cli-mismatch-$tag-$fs"; }
    }
    clicmp 1001 da own-a
    clicmp 1002 db own-b
    clicmp 1001 dshared shared-a
    clicmp 1002 dshared shared-b
    clicmp 1001 da paged --page 1
    "$BIN/asuid" 1001 "$BIN/moobfs" query --ino --page 1 --order kind string desc "$LAYER/da" kind eq string note > /tmp/k.txt || fail "cli-order-$fs"
    "$BIN/moobfs-index" --domain --order kind string desc "$LOWER/da" kind eq string note > /tmp/o.txt || fail "cli-order-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt && [ "$(wc -l < /tmp/k.txt)" -eq 4 ] || { cat /tmp/k.txt /tmp/o.txt; fail "cli-order-mismatch-$fs"; }
    "$BIN/asuid" 1001 "$BIN/moobfs" query --ino --page 1 --limit 2 --order kind string asc "$LAYER/da" kind eq string note > /tmp/k.txt || fail "cli-limit-$fs"
    "$BIN/moobfs-index" --domain --order kind string asc --limit 2 "$LOWER/da" kind eq string note > /tmp/o.txt || fail "cli-limit-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt && [ "$(wc -l < /tmp/k.txt)" -eq 2 ] || { cat /tmp/k.txt /tmp/o.txt; fail "cli-limit-mismatch-$fs"; }
    "$BIN/asuid" 1001 "$BIN/moobfs" query --json "$LAYER/da" kind eq string note > /tmp/k.json || fail "cli-json-$fs"
    grep -q '"complete": true, "count": 4,' /tmp/k.json && grep -q '"path": "sub/m1.md"' /tmp/k.json || { cat /tmp/k.json; fail "cli-json-content-$fs"; }
    # explicit errors: another user's domain, a directory that is not a domain, no layer
    "$BIN/asuid" 1001 "$BIN/moobfs" query "$LAYER/db" kind eq string note > /tmp/k.txt 2> /tmp/e.txt; [ $? -eq 77 ] || fail "cli-eacces-status-$fs"
    [ ! -s /tmp/k.txt ] && grep -q 'permission denied' /tmp/e.txt || { cat /tmp/k.txt /tmp/e.txt; fail "cli-eacces-message-$fs"; }
    "$BIN/moobfs" query "$LAYER" kind eq string note > /dev/null 2> /tmp/e.txt; [ $? -eq 69 ] || fail "cli-root-status-$fs"
    grep -q 'give a domain directory (1 level below' /tmp/e.txt || { cat /tmp/e.txt; fail "cli-root-message-$fs"; }
    "$BIN/moobfs" query /tmp kind eq string note > /dev/null 2> /tmp/e.txt; [ $? -eq 69 ] || fail "cli-nolayer-status-$fs"
    grep -q 'is not on a moobfs_layer mount' /tmp/e.txt || { cat /tmp/e.txt; fail "cli-nolayer-message-$fs"; }
    "$BIN/moobfs" query "$LAYER/da" kind zz string note > /dev/null 2>&1; [ $? -eq 65 ] || fail "cli-bad-terms-$fs"
    # info: counters for root only; check runs moobfs-check
    "$BIN/asuid" 1001 "$BIN/moobfs" info "$LAYER/da" > /tmp/k.txt 2>&1 || { cat /tmp/k.txt; fail "cli-info-user-$fs"; }
    grep -q 'directories 1 level below' /tmp/k.txt && grep -q 'administrators only' /tmp/k.txt || { cat /tmp/k.txt; fail "cli-info-user-content-$fs"; }
    "$BIN/moobfs" info --json "$LAYER" > /tmp/k.json || fail "cli-info-json-$fs"
    grep -q '"domain_depth": 1,' /tmp/k.json && grep -q '"counters": {' /tmp/k.json && grep -q "\"lower_fs\": \"$fs\"" /tmp/k.json || { cat /tmp/k.json; fail "cli-info-json-content-$fs"; }
    "$BIN/moobfs" check "$LOWER" > /tmp/k.json || { cat /tmp/k.json; fail "cli-check-$fs"; }
    grep -q '"problems"' /tmp/k.json || { cat /tmp/k.json; fail "cli-check-output-$fs"; }
    echo "CLI_PASS: $fs; two users equal to the oracle, paging, order, limit, JSON, info, check and explicit errors"
    "$BIN/query" "$LAYER/dshared" q2invalid | grep -q QUERY2_INVALID_REQUESTS_REFUSED || fail "q2-invalid-$fs"
    # v1 is restricted to CAP_SYS_ADMIN while domains are on (spec §4.5)
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt 2>/dev/null && fail "q2-v1-open-to-user-$fs"
    "$BIN/query" "$LAYER" kind eq string note > /tmp/k.txt || fail "q2-v1-root-$fs"
    grep -q 'db/m1.md' /tmp/k.txt || fail "q2-v1-root-content-$fs"
    # homogeneity (test 3): a stricter mode removes a file at the next call; reverting restores it
    h0=$(hetero)
    chmod 0640 "$LAYER/dshared/m1.md" || fail "q2-chmod-file-$fs"
    q2cmp 1001 dshared 2 hetero-mode
    grep -q 'm1.md' /tmp/k.txt && fail "q2-hetero-leak-$fs"
    [ "$(hetero)" -eq $((h0 + 1)) ] || fail "q2-hetero-count-$fs"
    chmod 0644 "$LAYER/dshared/m1.md" || fail "q2-chmod-back-$fs"
    q2cmp 1001 dshared 3 hetero-back
    [ "$(hetero)" -eq "$h0" ] || fail "q2-hetero-count-back-$fs"
    # an extended access ACL excludes a file unless the mode alone makes it world readable
    "$TOOLS/setfacl" -m u:1002:r "$LAYER/dshared/m2.md" || fail "q2-acl-set-$fs"
    q2cmp 1001 dshared 2 acl
    "$TOOLS/setfacl" -b "$LAYER/dshared/m2.md" && chmod 0644 "$LAYER/dshared/m2.md" || fail "q2-acl-clear-$fs"
    q2cmp 1001 dshared 3 acl-back
    # hard links make a file heterogeneous (another name may sit in another domain)
    ln "$LAYER/dshared/m3.md" "$LAYER/dshared/l3.md" || fail "q2-link-$fs"
    q2cmp 1001 dshared 2 nlink
    rm "$LAYER/dshared/l3.md" || fail "q2-unlink-$fs"
    # back to one link the file may stay excluded (fail-safe) until its next mode change
    chmod 0644 "$LAYER/dshared/m3.md" || fail "q2-chmod-relink-$fs"
    q2cmp 1001 dshared 3 relinked
    # a directory permission change inside a domain switches it to per-result checks
    chmod 0600 "$LAYER/da/sub" || fail "q2-chmod-dir-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=1' || fail "q2-fallback-$fs"
    q2cmp 1001 da 3 fallback
    chmod 0700 "$LAYER/da/sub" || fail "q2-chmod-dir-back-$fs"
    q2cmp 1001 da 4 fallback-back
    # renames: a directory may not leave its domain or depth (EXDEV); a file changes partition
    [ "$("$BIN/query" "$LAYER" rename "$LAYER/da/sub" "$LAYER/dshared/sub")" = "rename 18" ] || fail "q2-exdev-domain-$fs"
    [ "$("$BIN/query" "$LAYER" rename "$LAYER/da/sub" "$LAYER/sub")" = "rename 18" ] || fail "q2-exdev-depth-$fs"
    [ "$("$BIN/query" "$LAYER" rename "$LAYER/da/sub" "$LAYER/da/sub2")" = "rename 0" ] || fail "q2-rename-inside-$fs"
    mv "$LAYER/da/sub2" "$LAYER/da/sub" || fail "q2-rename-back-$fs"
    mv "$LAYER/dshared/m3.md" "$LAYER/da/m9.md" && chown 1001:1001 "$LAYER/da/m9.md" && chmod 0600 "$LAYER/da/m9.md" || fail "q2-move-file-$fs"
    q2cmp 1001 da 5 moved-in
    q2cmp 1001 dshared 2 moved-out
    # V6 (journal 0155): RENAME_WHITEOUT would create a device node; the layer refuses it
    echo w > "$LAYER/dshared/wo.src" || fail "v6-file-$fs"
    [ "$("$BIN/query" "$LAYER" whiteout "$LAYER/dshared/wo.src" "$LAYER/dshared/wo.dst")" = "whiteout 22" ] || fail "v6-rename-whiteout-not-refused-$fs"
    rm "$LAYER/dshared/wo.src" || fail "v6-clean-$fs"
    # T3 (journal 0155): an extended ACL on the domain directory itself. dacl is 0750 and 1001's,
    # its file 0640: without the ACL only 1001 and its group traverse and read it. The ACL lets
    # 1002 traverse, so the file must leave the homogeneous answers, or 1002 would receive it.
    # Set before the remount, so that the build decides, not a hook (the hook sets the fallback).
    mkdir -m 0750 "$LAYER/dacl" && echo t3 > "$LAYER/dacl/f.md" && chmod 0640 "$LAYER/dacl/f.md" || fail "t3-mk-$fs"
    "$BIN/moobfs-attr" set "$LAYER/dacl/f.md" kind string note >/dev/null || fail "t3-attr-$fs"
    chown -R 1001:1001 "$LAYER/dacl" && "$TOOLS/setfacl" -m u:1002:rx "$LAYER/dacl" || fail "t3-acl-$fs"
    # a remount rebuilds the same partitions and clears the fallback
    umount "$LAYER" && mount -t moobfs_layer -o lower="$LOWER" none "$LAYER" || fail "q2-remount-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || fail "q2-fallback-cleared-$fs"
    q2cmp 1001 da 5 remount-a
    q2cmp 1002 db 3 remount-b
    q2cmp 1002 dshared 2 remount-shared
    "$BIN/query" --uid 1002 "$LAYER" open dacl/f.md | grep -q 'Permission denied' || fail "t3-precondition-$fs"
    "$BIN/query" --uid 1002 "$LAYER/dacl" q2 kind eq string note > /tmp/k.txt || fail "t3-query-$fs"
    [ ! -s /tmp/k.txt ] || { cat /tmp/k.txt; fail "t3-acl-on-domain-directory-leaks-$fs"; }
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || fail "t3-fallback-$fs"
    # T4 (journal 0155): writes under a private directory inside a built domain. What a write or
    # a rename puts below dshared/priv (0700, root's) must stay out of 1001's homogeneous
    # answers, decided by the write paths, while the domain stays out of fallback mode.
    # modes from umask, not chmod: a chmod of a directory in the domain would set the fallback
    ( umask 077 && mkdir "$LAYER/dshared/priv" ) || fail "t4-mkdir-$fs"
    ( umask 022 && echo t4 > "$LAYER/dshared/priv/w.md" ) || fail "t4-file-$fs"
    "$BIN/moobfs-attr" set "$LAYER/dshared/priv/w.md" kind string note >/dev/null || fail "t4-attr-$fs"
    mv "$LAYER/dshared/m1.md" "$LAYER/dshared/priv/r.md" || fail "t4-rename-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || fail "t4-fallback-$fs"
    "$BIN/query" --uid 1001 "$LAYER/dshared" q2 kind eq string note > /tmp/k.txt || fail "t4-query-$fs"
    grep -q 'priv/' /tmp/k.txt && { cat /tmp/k.txt; fail "t4-private-directory-leaks-$fs"; }
    mv "$LAYER/dshared/priv/r.md" "$LAYER/dshared/m1.md" && rm -rf "$LAYER/dshared/priv" "$LAYER/dacl" || fail "t4-clean-$fs"
    echo "HOMOGENEITY_GAPS_PASS: $fs, ACL on the domain directory (T3), writes under a private directory (T4)"
    # QUERY2 ordering and cursor (phase Q2b, spec test 7): 30 files in domain da with a time
    # attribute (ties included), two of them with an int64 under the same name, and string
    # titles, ten of them longer than the cursor's inline space
    long=$(head -c 100 /dev/zero | tr '\0' x)
    for i in $(seq 1 30); do
        f=$LAYER/da/o$i.md; echo "o $i" > "$f"
        if [ "$i" -ge 29 ]; then t="int64"; else t="time"; fi
        "$BIN/moobfs-attr" set "$f" created $t $(( (i % 10) * 1000 )) >/dev/null || fail "q2b-attr-$fs"
        [ $((i % 2)) -eq 1 ] && { "$BIN/moobfs-attr" set "$f" kind string note >/dev/null || fail "q2b-kind-$fs"; }
        if [ "$i" -le 10 ]; then ti="$long$((40 - i))"; else ti="t$i"; fi
        "$BIN/moobfs-attr" set "$f" title string "$ti" >/dev/null || fail "q2b-title-$fs"
        chown 1001:1001 "$f" && chmod 0600 "$f" || fail "q2b-mode-$fs"
    done
    q2ord() {   # tag expected-count query-options... -- oracle-options...
        tag=$1; want=$2; shift 2
        kq=""; while [ "$1" != "--" ]; do kq="$kq $1"; shift; done; shift
        # shellcheck disable=SC2086
        $BIN/query --uid 1001 $LAYER/da q2 $kq > /tmp/k.txt || fail "q2b-query-$tag-$fs"
        # shellcheck disable=SC2086
        $BIN/moobfs-index --domain "$@" > /tmp/o.txt || fail "q2b-oracle-$tag-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "q2b-mismatch-$tag-$fs"; }
        [ "$(wc -l < /tmp/k.txt)" -eq "$want" ] || { cat /tmp/k.txt; fail "q2b-count-$tag-$fs"; }
    }
    q2ord asc 28 --order created time asc -- --order created time asc "$LOWER/da"
    q2ord desc 28 --order created time desc -- --order created time desc "$LOWER/da"
    q2ord int64 2 --order created int64 desc -- --order created int64 desc "$LOWER/da"
    q2ord top5 5 --order created time desc --limit 5 kind eq string note -- --order created time desc --limit 5 "$LOWER/da" kind eq string note
    q2ord pages-desc 14 --order created time desc --limit 4 --pages kind eq string note -- --order created time desc "$LOWER/da" kind eq string note
    q2ord pages-asc 28 --order created time asc --limit 1 --pages -- --order created time asc "$LOWER/da"
    q2ord pages-long 30 --order title string asc --limit 3 --pages -- --order title string asc "$LOWER/da"
    q2ord pages-long-desc 30 --order title string desc --limit 4 --pages -- --order title string desc "$LOWER/da"
    q2ord range 8 --order created time asc --pages --limit 2 created ge time 7000 -- --order created time asc "$LOWER/da" created ge time 7000
    q2ord unordered-pages 20 --limit 3 --pages kind eq string note -- "$LOWER/da" kind eq string note
    # cursors: the same query continues; another query, a remount or a fallback refuses it
    "$BIN/query" --uid 1001 "$LAYER/da" q2 --limit 3 --cursor-out /tmp/c1 kind eq string note > /dev/null 2>&1 || fail "q2b-cursor-out-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --limit 3 --cursor-in /tmp/c1 kind eq string note)" = "q2 0" ] || fail "q2b-cursor-same-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --limit 9 --cursor-in /tmp/c1 kind eq string note)" = "q2 0" ] || fail "q2b-cursor-other-limit-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --cursor-in /tmp/c1 kind exists x x)" = "q2 116" ] || fail "q2b-cursor-other-query-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --cursor-in /tmp/c1 --order created time asc kind eq string note)" = "q2 116" ] || fail "q2b-cursor-other-order-$fs"
    # T2 (journal 0155): the MAC alone must refuse a cursor whose fields changed while its query
    # hash (offset 16 of struct moobl_cursor) stayed valid: the pad (5), the high byte of the
    # inode (15) and the last inline value byte (119), all zero in this cursor
    for off in 5 15 119; do
        cp /tmp/c1 /tmp/ct && printf '\377' | dd of=/tmp/ct bs=1 seek=$off conv=notrunc 2>/dev/null || fail "t2-patch-$fs"
        cmp -s /tmp/c1 /tmp/ct && fail "t2-patch-changed-nothing-$off-$fs"
        [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --limit 3 --cursor-in /tmp/ct kind eq string note)" = "q2 116" ] || fail "t2-tampered-cursor-accepted-at-$off-$fs"
    done
    chmod 0700 "$LAYER/da/sub" || fail "q2b-fallback-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --limit 3 --cursor-in /tmp/c1 kind eq string note)" = "q2 116" ] || fail "q2b-cursor-fallback-$fs"
    q2ord fallback-pages 14 --order created time desc --limit 4 --pages kind eq string note -- --order created time desc "$LOWER/da" kind eq string note
    "$BIN/query" --uid 1001 "$LAYER/da" q2 --limit 3 --cursor-out /tmp/c1 kind eq string note > /dev/null 2>&1 || fail "q2b-cursor-out2-$fs"
    umount "$LAYER" && mount -t moobfs_layer -o lower="$LOWER" none "$LAYER" || fail "q2b-remount-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --limit 3 --cursor-in /tmp/c1 kind eq string note)" = "q2 116" ] || fail "q2b-cursor-remount-$fs"
    q2ord remount-pages 30 --order title string desc --limit 7 --pages -- --order title string desc "$LOWER/da"
    # results reach user space only after query_sem is released (finding F7, journal 0105): an
    # unmapped results buffer gets EFAULT, and the semaphore is free afterwards, since a rebuild,
    # which takes it for writing, completes (a directory renamed in the domain forces one)
    [ "$("$BIN/query" --uid 1001 "$LAYER/da" q2errno --bad-results kind eq string note)" = "q2 14" ] || fail "q2-efault-$fs"
    rb0=$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')
    mkdir "$LAYER/da/efd" && mv "$LAYER/da/efd" "$LAYER/da/efd2" || fail "q2-efault-rename-$fs"
    timeout 30 "$BIN/query" --uid 1001 "$LAYER/da" q2 kind eq string note > /dev/null || fail "q2-efault-then-query-$fs"
    [ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -gt "$rb0" ] || fail "q2-efault-no-rebuild-$fs (the test proved nothing)"
    rmdir "$LAYER/da/efd2" || fail "q2-efault-cleanup-$fs"
    echo "QUERY2_ORDER_PASS: $fs"
    # leapfrog intersection of equality terms (phase Q2c): answers equal the oracle, paging
    # works, and the plan is really taken: with a non-equality first term whose values fall
    # as inodes rise, a plain scan would answer in falling inode order, the intersection in
    # rising inode order
    for i in $(seq 1 30); do
        f=$LAYER/da/o$i.md
        "$BIN/moobfs-attr" set "$f" ns string n$((i % 3)) >/dev/null &&
        "$BIN/moobfs-attr" set "$f" agent int64 $((i % 5)) >/dev/null &&
        "$BIN/moobfs-attr" set "$f" rank string r$((100 - i)) >/dev/null || fail "q2c-attr-$fs"
    done
    q2ord leap2 2 ns eq string n1 agent eq int64 2 -- "$LOWER/da" ns eq string n1 agent eq int64 2
    q2ord leap3 1 ns eq string n1 agent eq int64 2 kind eq string note -- "$LOWER/da" ns eq string n1 agent eq int64 2 kind eq string note
    q2ord leap-mixed 2 rank exists x x ns eq string n1 agent eq int64 2 -- "$LOWER/da" rank exists x x ns eq string n1 agent eq int64 2
    q2ord leap-range 1 ns eq string n1 created ge time 7000 agent eq int64 2 -- "$LOWER/da" ns eq string n1 created ge time 7000 agent eq int64 2
    q2ord leap-none 0 ns eq string n1 agent eq int64 7 -- "$LOWER/da" ns eq string n1 agent eq int64 7
    q2ord leap-type 0 ns eq string n1 agent eq string 2 -- "$LOWER/da" ns eq string n1 agent eq string 2
    q2ord leap-pages 5 --limit 2 --pages ns eq string n1 kind eq string note -- "$LOWER/da" ns eq string n1 kind eq string note
    q2ord leap-pages1 5 --limit 1 --pages kind eq string note ns eq string n1 -- "$LOWER/da" kind eq string note ns eq string n1
    "$BIN/query" --uid 1001 "$LAYER/da" q2 --raw rank exists x x ns eq string n1 kind eq string note | cut -f1 > /tmp/raw.txt || fail "q2c-raw-$fs"
    [ "$(wc -l < /tmp/raw.txt)" -eq 5 ] || fail "q2c-raw-count-$fs"
    sort -n -c /tmp/raw.txt || { cat /tmp/raw.txt; fail "q2c-plan-not-leapfrog-$fs"; }
    q2ord single-eq 10 rank exists x x ns eq string n1 -- "$LOWER/da" rank exists x x ns eq string n1
    # "missing or ..." terms and batches (phase Q2d): an expiry on every fourth file.
    # Operators such as gt? are unquoted words: globbing stays off until the block ends.
    set -f
    for i in 4 8 12 16 20 24 28; do
        "$BIN/moobfs-attr" set "$LAYER/da/o"$i.md expires time $((i * 1000)) >/dev/null || fail "q2d-attr-$fs"
    done
    q2ord miss-gt 9 ns eq string n1 expires gt? time 10000 -- "$LOWER/da" ns eq string n1 expires gt? time 10000
    q2ord miss-le 8 ns eq string n1 expires le? time 4000 -- "$LOWER/da" ns eq string n1 expires le? time 4000
    q2ord miss-first 9 expires gt? time 10000 ns eq string n1 -- "$LOWER/da" expires gt? time 10000 ns eq string n1
    q2ord miss-eq 10 ns eq string n1 kind eq? string note -- "$LOWER/da" ns eq string n1 kind eq? string note
    q2ord miss-type 0 ns eq string n1 agent eq? string 2 -- "$LOWER/da" ns eq string n1 agent eq? string 2
    q2ord miss-ordered 26 --order created time desc expires gt? time 10000 -- --order created time desc "$LOWER/da" expires gt? time 10000
    q2ord miss-pages 9 --limit 2 --pages ns eq string n1 expires gt? time 10000 -- "$LOWER/da" ns eq string n1 expires gt? time 10000
    q2batchcmp() {   # tag: three requests, one per namespace value, each equal to the oracle
        "$BIN/query" --uid 1001 "$LAYER/da" q2batch -- ns eq string n0 -- ns eq string n1 expires gt? time 10000 -- ns eq string n2 > /tmp/kb.txt || fail "q2d-batch-$1-$fs"
        k=0
        for q in "ns eq string n0" "ns eq string n1 expires gt? time 10000" "ns eq string n2"; do
            grep "^q$k	" /tmp/kb.txt | cut -f2- > /tmp/k.txt
            # shellcheck disable=SC2086
            $BIN/moobfs-index --domain $LOWER/da $q > /tmp/o.txt || fail "q2d-batch-oracle-$1-$fs"
            cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/kb.txt; echo "oracle $k:"; cat /tmp/o.txt; fail "q2d-batch-mismatch-$1-$k-$fs"; }
            k=$((k + 1))
        done
        [ "$(wc -l < /tmp/kb.txt)" -eq 29 ] || { cat /tmp/kb.txt; fail "q2d-batch-count-$1-$fs"; }
    }
    q2batchcmp plain
    "$BIN/batch-errors" "$LAYER/da" || fail "q2-batch-errors-plain-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/db" q2batcherrno -- kind exists x x)" = "q2batch 13" ] || fail "q2d-batch-isolation-$fs"
    [ "$("$BIN/query" "$LAYER/da/sub" q2batcherrno -- kind exists x x)" = "q2batch 25" ] || fail "q2d-batch-enotty-$fs"
    [ "$("$BIN/query" "$LAYER/da" q2batcherrno -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x -- kind exists x x)" = "q2batch 22" ] || fail "q2d-batch-nine-$fs"
    [ "$("$BIN/query" "$LAYER/da" q2batcherrno -- kind exists x x -- kind eq? string note)" = "q2batch 22" ] || fail "q2d-batch-one-invalid-$fs"
    echo "QUERY2_MISSING_BATCH_PASS: $fs"
    set +f
    chmod 0700 "$LAYER/da/sub" || fail "q2c-fallback-$fs"
    q2ord leap-fallback 5 --limit 2 --pages ns eq string n1 kind eq string note -- "$LOWER/da" ns eq string n1 kind eq string note
    set -f; q2batchcmp fallback; set +f
    "$BIN/batch-errors" "$LAYER/da" || fail "q2-batch-errors-fallback-$fs"
    echo "QUERY2_LEAPFROG_PASS: $fs"
    # QUERY2 under concurrency (phase Q2e, spec test 8): mode changes, renames, unlink and
    # recreate, files moving to another domain and back, and chmod of the domain directory
    # itself (which switches the domain to fallback and closes it for a moment), racing for
    # 5 s with paged QUERY2 as uid 1001 over the plain, equality and ordered plans. Answers
    # must stay well formed and every result openable unless changed after the answer; the
    # final answer must equal the oracle, paged or not.
    mkdir -p "$LAYER/dr" "$LAYER/dr2" && chmod 0755 "$LAYER/dr" "$LAYER/dr2" || fail "q2e-mkdir-$fs"
    q2e_new() {   # $1: file index
        f=$LAYER/dr/r$1.md
        echo "r$1" > "$f" && "$BIN/moobfs-attr" set "$f" kind string note >/dev/null &&
            "$BIN/moobfs-attr" set "$f" ns string r >/dev/null &&
            "$BIN/moobfs-attr" set "$f" created time $(( $1 * 1000 )) >/dev/null && chmod 0644 "$f"
    }
    for i in $(seq 1 40); do q2e_new "$i" || fail "q2e-attr-$fs"; done
    umount "$LAYER" && mount -t moobfs_layer -o lower="$LOWER" none "$LAYER" || fail "q2e-remount-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || fail "q2e-fallback-start-$fs"
    : > /tmp/q2e.go
    ( n=0; while [ -e /tmp/q2e.go ]; do
          i=$(( n % 40 + 1 )); n=$(( n + 1 ))
          chmod 0600 "$LAYER/dr/r"$i.md 2>/dev/null; chmod 0644 "$LAYER/dr/r"$i.md 2>/dev/null
          mv "$LAYER/dr/r"$i.md "$LAYER/dr/t"$i.md 2>/dev/null && mv "$LAYER/dr/t"$i.md "$LAYER/dr/r"$i.md 2>/dev/null
          rm -f "$LAYER/dr/r"$i.md; q2e_new $i 2>/dev/null
          [ $(( n % 5 )) -eq 0 ] && { mv "$LAYER/dr/r"$i.md "$LAYER/dr2/r"$i.md 2>/dev/null; mv "$LAYER/dr2/r"$i.md "$LAYER/dr/r"$i.md 2>/dev/null; }
          [ $(( n % 7 )) -eq 0 ] && { chmod 0711 "$LAYER/dr"; chmod 0755 "$LAYER/dr"; }
      done ) &
    racer=$!
    # two processes on one open file: one finds its result buffer lent (journal 0139)
    MOOB_RACE2_FORK=1 run_logged /tmp/q2e.txt "$BIN/query" --uid 1001 "$LAYER/dr" race2 5 || { rm -f /tmp/q2e.go; wait $racer; fail "q2e-race-$fs"; }
    rm -f /tmp/q2e.go; wait $racer
    grep -q '^race2: [1-9]' /tmp/q2e.txt || fail "q2e-no-iterations-$fs"
    # a rename or move cut by the end of the race may leave t<i>.md behind or r<i>.md in dr2
    chmod 0755 "$LAYER/dr" && rm -f "$LAYER/dr/t"*.md "$LAYER/dr2/"*.md || fail "q2e-tidy-$fs"
    for i in $(seq 1 40); do [ -e "$LAYER/dr/r$i.md" ] || q2e_new "$i" || fail "q2e-restore-$fs"; done
    chmod 0644 "$LAYER/dr/"*.md || fail "q2e-final-mode-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' && fail "q2e-no-fallback-$fs"   # the race must have switched it
    "$BIN/query" --uid 1001 "$LAYER/dr" q2 kind eq string note > /tmp/k.txt || fail "q2e-final-query-$fs"
    "$BIN/moobfs-index" --domain "$LOWER/dr" kind eq string note > /tmp/o.txt || fail "q2e-final-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || { diff /tmp/k.txt /tmp/o.txt; fail "q2e-final-mismatch-$fs"; }
    [ "$(wc -l < /tmp/k.txt)" -eq 40 ] || fail "q2e-final-count-$fs"
    "$BIN/query" --uid 1001 "$LAYER/dr" q2 --limit 3 --pages kind eq string note > /tmp/k2.txt || fail "q2e-final-pages-$fs"
    cmp -s /tmp/k.txt /tmp/k2.txt || fail "q2e-final-pages-mismatch-$fs"
    rm -rf "$LAYER/dr" "$LAYER/dr2" || fail "q2e-cleanup-$fs"
    echo "QUERY2_RACE_PASS: $fs"
    echo "kind eq string note" > /tmp/q2q.txt
    "$BIN/bench-load" --workload W3 --target "$LAYER/da" --root "$LAYER/da" --query2 1 --uid 1001 --queries /tmp/q2q.txt --repeat 3 --out /tmp/q2w3 || fail "q2-bench-w3-$fs"
    rm -rf "$LAYER/da" "$LAYER/db" "$LAYER/dshared" || fail "q2-cleanup-$fs"
    umount "$LAYER" && mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "q2-restore-$fs"
    echo "QUERY2_PASS: $fs"
    # mount contract: idmapped mounts and user-namespace mounts are refused by the VFS
    "$BIN/idmap" idmap "$LAYER" || fail "contract-idmap-$fs"
    mkdir -p /tmp/userns-mnt && "$BIN/idmap" userns "$LOWER" /tmp/userns-mnt || fail "contract-userns-$fs"
    # concurrency: chmod, rename and unlink+recreate racing with QUERY as uid 1001 for 5 s;
    # answers must stay well formed, every result openable unless changed after the answer,
    # and the final answer must equal the oracle
    mkdir -p "$LAYER/ns/race" && chmod 0755 "$LAYER/ns/race" || fail "race-mkdir-$fs"
    for i in $(seq 1 40); do echo r"$i" > "$LAYER/ns/race/r$i.md"; "$BIN/moobfs-attr" set "$LAYER/ns/race/r$i.md" kind string note >/dev/null || fail "race-attr-$fs"; done
    chmod 0644 "$LAYER/ns/race/"*.md || fail "race-mode-$fs"
    : > /tmp/race.go
    ( n=0; while [ -e /tmp/race.go ]; do
          i=$(( n % 40 + 1 )); n=$(( n + 1 ))
          chmod 0600 "$LAYER/ns/race/r"$i.md 2>/dev/null; chmod 0644 "$LAYER/ns/race/r"$i.md 2>/dev/null
          mv "$LAYER/ns/race/r"$i.md "$LAYER/ns/race/t"$i.md 2>/dev/null && mv "$LAYER/ns/race/t"$i.md "$LAYER/ns/race/r"$i.md 2>/dev/null
          rm -f "$LAYER/ns/race/r"$i.md; echo r$i > "$LAYER/ns/race/r"$i.md; "$BIN/moobfs-attr" set "$LAYER/ns/race/r"$i.md kind string note >/dev/null 2>&1
          [ $(( n % 7 )) -eq 0 ] && { mv "$LAYER/ns/race" "$LAYER/ns/race2" 2>/dev/null; mv "$LAYER/ns/race2" "$LAYER/ns/race" 2>/dev/null; }
      done ) &
    racer=$!
    run_logged /tmp/race.txt "$BIN/query" --uid 1001 "$LAYER" race 5 || { rm -f /tmp/race.go; wait $racer; fail "race-query-$fs"; }
    rm -f /tmp/race.go; wait $racer
    grep -q '^race: [1-9]' /tmp/race.txt || fail "race-no-iterations-$fs"
    chmod 0644 "$LAYER/ns/race/"*.md || fail "race-final-mode-$fs"
    "$BIN/query" --uid 1001 "$LAYER" kind eq string note > /tmp/k.txt || fail "race-final-query-$fs"
    "$BIN/moobfs-index" --uid 1001 "$LOWER" kind eq string note > /tmp/o.txt || fail "race-final-oracle-$fs"
    cmp -s /tmp/k.txt /tmp/o.txt || { diff /tmp/k.txt /tmp/o.txt; fail "race-final-mismatch-$fs"; }
    rm -rf "$LAYER/ns" || fail "iso-cleanup-$fs"
    echo "ISOLATION_PASS: $fs"
    # M4 harness over the layer (correctness, no numbers): W1 creates a profile-16 dataset,
    # W2 reads it, every W3 shape agrees with the oracle, W3 and W4 record one entry per call
    mkdir -p "$LAYER/bench" && chmod 0755 "$LAYER/bench" || fail "bench-mkdir-$fs"
    "$BIN/bench-gen" --seed 11 --inodes 300 --profile 16 --out /tmp/bench-m.txt || fail "bench-gen-$fs"
    "$BIN/bench-load" --manifest /tmp/bench-m.txt --target "$LAYER/bench" --workload W1 --procs 2 --out /tmp/bench-w1 || fail "bench-w1-$fs"
    [ "$(find "$LAYER/bench" -type f -name '*.md' | wc -l)" -eq 300 ] || fail "bench-w1-count-$fs"
    "$BIN/moobfs-check" "$LOWER/bench" > /tmp/bench-check.json || { cat /tmp/bench-check.json; fail "bench-check-$fs"; }
    bench_attrs=$(grep -v '^#' /tmp/bench-m.txt | awk -F'\t' '{ n += split($5, a, " ") } END { print n }')
    grep -q "\"typed_attributes\": $bench_attrs" /tmp/bench-check.json || fail "bench-check-attrs-$fs"
    "$BIN/bench-load" --manifest /tmp/bench-m.txt --target "$LAYER/bench" --workload W2 --procs 2 --out /tmp/bench-w2 || fail "bench-w2-$fs"
    grep -v '^#' "$BIN/bench-queries-w3.txt" > /tmp/bench-q.txt || fail "bench-queries-$fs"
    nq=0
    while read -r qline; do
        # shellcheck disable=SC2086
        same_query $qline || fail "bench-w3-equiv-$nq-$fs"
        nq=$((nq + 1))
    done < /tmp/bench-q.txt
    [ "$nq" -eq 9 ] || fail "bench-w3-shapes-$fs"
    "$BIN/bench-load" --queries /tmp/bench-q.txt --target "$LAYER/bench" --root "$LAYER" --workload W3 --out /tmp/bench-w3 || fail "bench-w3-$fs"
    # SQLite baseline next to the files: plan gate (SEARCH, never SCAN) and three-way equivalence
    rm -f /tmp/bench-base.db*
    SQLITE=$TOOLS/sqlite3 sh "$BIN/bench-sqlbase.sh" init /tmp/bench-base.db || fail "bench-sqlbase-init-$fs"
    SQLITE=$TOOLS/sqlite3 sh "$BIN/bench-sqlbase.sh" load /tmp/bench-base.db /tmp/bench-m.txt "$LAYER/bench" || fail "bench-sqlbase-load-$fs"
    run_logged /tmp/bench-equiv.txt env SQLITE="$TOOLS/sqlite3" SQLBASE="$BIN/bench-sqlbase.sh" BIN="$BIN" sh "$BIN/bench-equiv.sh" "$BIN/bench-queries-w3.txt" /tmp/bench-base.db "$LOWER/bench" "$LAYER" || fail "bench-equiv-$fs"
    tail -1 /tmp/bench-equiv.txt | grep -q '^EQUIV_PASS: 9 shapes (kernel' || fail "bench-equiv-marker-$fs"
    tail -1 /tmp/bench-equiv.txt
    "$BIN/bench-load" --manifest /tmp/bench-m.txt --target "$LAYER/bench" --workload W4 --out /tmp/bench-w4 || fail "bench-w4-$fs"
    [ "$(cat /tmp/bench-w1.*.rec /tmp/bench-w2.*.rec /tmp/bench-w3.*.rec /tmp/bench-w4.*.rec | wc -c)" -eq $(( (300 + 300 + 9 + 600) * 32 )) ] || fail "bench-records-$fs"
    # W8: mixed reads, attribute updates and queries from 10 processes; every call recorded
    "$BIN/bench-load" --manifest /tmp/bench-m.txt --target "$LAYER/bench" --root "$LAYER" --queries /tmp/bench-q.txt --workload W8 --procs 10 --out /tmp/bench-w8 || fail "bench-w8-$fs"
    [ "$(cat /tmp/bench-w8.*.rec | wc -c)" -ge $(( 300 * 32 )) ] || fail "bench-w8-records-$fs"
    # W5: three mutators renaming and recreating every memory while process 0 cycles the queries;
    # afterwards the tree must be whole and every W3 shape must still equal the oracle
    "$BIN/bench-load" --manifest /tmp/bench-m.txt --target "$LAYER/bench" --root "$LAYER" --queries /tmp/bench-q.txt --workload W5 --procs 4 --out /tmp/bench-w5 || fail "bench-w5-$fs"
    [ "$(find "$LAYER/bench" -type f -name '*.md' | wc -l)" -eq 300 ] || fail "bench-w5-count-$fs"
    find "$LAYER/bench" -name '*.away' | grep -q . && fail "bench-w5-leftover-$fs"
    "$BIN/moobfs-check" "$LOWER/bench" > /tmp/bench-check.json || { cat /tmp/bench-check.json; fail "bench-w5-check-$fs"; }
    while read -r qline; do
        # shellcheck disable=SC2086
        same_query $qline || fail "bench-w5-equiv-$fs"
    done < /tmp/bench-q.txt
    # W6 (cold: page cache dropped) and W7 (warm): remount and first QUERY; the layer must be mounted again afterwards
    "$BIN/bench-load" --workload W6 --target "$LAYER" --lower "$LOWER" --mount-opts domain_depth=0 --queries /tmp/bench-q.txt --out /tmp/bench-w6 --repeat 2 || fail "bench-w6-$fs"
    "$BIN/bench-load" --workload W7 --target "$LAYER" --lower "$LOWER" --mount-opts domain_depth=0 --queries /tmp/bench-q.txt --out /tmp/bench-w7 --repeat 2 || fail "bench-w7-$fs"
    [ "$(cat /tmp/bench-w6.0.rec /tmp/bench-w7.0.rec | wc -c)" -eq $(( 4 * 32 )) ] || fail "bench-mount-records-$fs"
    grep -q "moobfs_layer $LAYER\| $LAYER " /proc/mounts || fail "bench-remounted-$fs"
    # after the remount: index on, paths current, index complete (flag bits 0x2, 0x4 and 0x8 clear;
    # root also gets MOOB_FLAG_COUNTERS 0x20, ADR-0011)
    f=$("$BIN/query" "$LAYER" info | sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p')
    [ -n "$f" ] && [ $(( f & 0xe )) -eq 0 ] || fail "bench-remount-flags-$fs"
    rm -rf "$LAYER/bench" /tmp/bench-* || fail "bench-cleanup-$fs"
    # the campaign orchestrator, one repetition at a tiny scale: procedure only, never numbers
    env BIN="$BIN" SQLITE="$TOOLS/sqlite3" sh "$BIN/bench-run.sh" --out /tmp/bench-run --scale 60 --profile 16 --reps 1 --procs 2 --layer "C-$fs=$LAYER:$LOWER" > /tmp/bench-run.log 2>&1 || { tail -20 /tmp/bench-run.log; fail "bench-run-$fs"; }
    grep -q '^BENCH_RUN_DONE\|BENCH_RUN_DONE' /tmp/bench-run.log || fail "bench-run-done-$fs"
    [ -s /tmp/bench-run/environment.txt ] && [ -s "/tmp/bench-run/rep1/C-$fs/w3.0.rec" ] && [ -s "/tmp/bench-run/rep1/C-$fs/equiv.txt" ] || fail "bench-run-artifacts-$fs"
    rm -rf /tmp/bench-run /tmp/bench-run.log "$LAYER/bench"
    echo "BENCH_LAYER_PASS: $fs"
    mkdir -p "$LAYER/fsx"
    "$BIN/fsx" -q -N 5000 -S 1 "$LAYER/fsx/file1" || fail "fsx-1-$fs"
    "$BIN/fsx" -q -N 3000 -S 2 -r 512 -w 512 "$LAYER/fsx/file2" || fail "fsx-2-$fs"
    "$BIN/fsx" -q -N 1000 -S 3 -l 2000000 "$LAYER/fsx/file3" || fail "fsx-large-$fs"
    cmp "$LAYER/fsx/file1" "$LOWER/fsx/file1" || fail "fsx-lower-mismatch-$fs"
    rm -rf "$LAYER/fsx" || fail "layer-cleanup-$fs"
    sync
    umount "$LAYER" || fail "layer-unmount-$fs"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0,nolinks none "$LAYER" || fail "layer-remount-$fs"
    "$BIN/layer" "$LAYER" "$LOWER" "$reflink" nolinks || fail "layer-nolinks-tests-$fs"
    umount "$LAYER" || fail "layer-second-unmount-$fs"
    # The lower as hostile input (security roadmap S4, ADR-0011 promise 5): trees planted directly
    # on the lower with every rule of the attribute encoding broken, names the layer must skip and
    # files beyond the per-file limit. The mount must succeed, the index must equal the user-space
    # oracle, changes made behind the mount must not crash or hang, and after a remount the index
    # must equal the oracle again. HOSTILE_SEEDS sets how many trees (default 3).
    hsame() {   # tag, then a term: inode sets of the kernel and of the oracle must be equal
        tag=$1; shift
        "$BIN/query" "$LAYER" "$@" | cut -f1 | sort -u > /tmp/k.txt || fail "hostile-query-$tag-$fs"
        "$BIN/moobfs-index" "$LOWER/hostile" "$@" | cut -f1 | sort -u > /tmp/o.txt || fail "hostile-oracle-$tag-$fs"
        cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "hostile-mismatch-$tag-$fs"; }
    }
    hall() {
        for n in k0 k1 k2 k3 k4 k5; do hsame "$1-$n" $n exists - -; done
        hsame "$1-str" k0 eq string a; hsame "$1-pfx" k1 prefix string b; hsame "$1-int" k2 ge int64 3
        hsame "$1-bool" k3 eq bool true; hsame "$1-and" k4 exists - - k5 exists - -
    }
    hseed=1
    while [ $hseed -le "${HOSTILE_SEEDS:-3}" ]; do
        rm -rf "$LOWER/hostile"; mkdir "$LOWER/hostile" || fail "hostile-mkdir-$fs"
        "$BIN/hostile" plant "$LOWER/hostile" $hseed 300 > /tmp/h.txt || { cat /tmp/h.txt; fail "hostile-plant-$hseed-$fs"; }
        mount -t moobfs_layer -o lower="$LOWER/hostile",domain_depth=0 none "$LAYER" || fail "hostile-mount-$hseed-$fs"
        hall "s$hseed"
        [ -s /tmp/k.txt ] || fail "hostile-empty-answer-$hseed-$fs"   # the last query must have matched something
        "$BIN/hostile" probe "$LAYER" > /dev/null || fail "hostile-probe-$hseed-$fs"
        "$BIN/moobfs-check" "$LOWER/hostile" > /dev/null; [ $? -eq 2 ] || fail "hostile-check-$hseed-$fs"
        "$BIN/hostile" mutate "$LOWER/hostile" $hseed 300 > /dev/null || fail "hostile-mutate-$hseed-$fs"
        "$BIN/hostile" probe "$LAYER" > /dev/null || fail "hostile-probe-stale-$hseed-$fs"
        for n in k0 k1 k2 k3 k4 k5; do "$BIN/query" "$LAYER" $n exists - - > /dev/null 2>&1; done   # stale by contract; must return
        umount "$LAYER" || fail "hostile-umount-$hseed-$fs"
        mount -t moobfs_layer -o lower="$LOWER/hostile",domain_depth=0 none "$LAYER" || fail "hostile-remount-$hseed-$fs"
        hall "r$hseed"
        umount "$LAYER" || fail "hostile-umount2-$hseed-$fs"
        # with domains on, each first-level directory answers like the oracle of that domain
        mount -t moobfs_layer -o lower="$LOWER/hostile" none "$LAYER" || fail "hostile-domain-mount-$hseed-$fs"
        for d in d0 d1 d2 d3 d4; do
            [ -d "$LOWER/hostile/$d" ] || continue
            "$BIN/query" "$LAYER/$d" q2 k0 exists - - | cut -f1 | sort -u > /tmp/k.txt || fail "hostile-q2-$d-$hseed-$fs"
            "$BIN/moobfs-index" --domain "$LOWER/hostile/$d" k0 exists - - | cut -f1 | sort -u > /tmp/o.txt || fail "hostile-q2-oracle-$d-$hseed-$fs"
            cmp -s /tmp/k.txt /tmp/o.txt || { echo "kernel:"; cat /tmp/k.txt; echo "oracle:"; cat /tmp/o.txt; fail "hostile-q2-mismatch-$d-$hseed-$fs"; }
        done
        umount "$LAYER" || fail "hostile-domain-umount-$hseed-$fs"
        hseed=$((hseed + 1))
    done
    # the format marker on the lower root: only a decimal version this module knows is accepted
    rm -rf "$LOWER/hostile"; mkdir "$LOWER/hostile" || fail "hostile-marker-mkdir-$fs"
    for m in 2 999999999 abc 1x " 1" -1; do
        "$BIN/hostile" marker "$LOWER/hostile" "$m" || fail "hostile-marker-set-$fs"
        mount -t moobfs_layer -o lower="$LOWER/hostile" none "$LAYER" 2>/dev/null && fail "hostile-marker-accepted-$m-$fs"
    done
    for n in 0 10 17 4000; do
        "$BIN/hostile" marker "$LOWER/hostile" --size $n || fail "hostile-marker-size-set-$fs"
        mount -t moobfs_layer -o lower="$LOWER/hostile" none "$LAYER" 2>/dev/null && fail "hostile-marker-size-accepted-$n-$fs"
    done
    "$BIN/hostile" marker "$LOWER/hostile" 1 || fail "hostile-marker-valid-set-$fs"
    mount -t moobfs_layer -o lower="$LOWER/hostile" none "$LAYER" || fail "hostile-marker-valid-$fs"
    umount "$LAYER" || fail "hostile-marker-umount-$fs"
    rm -rf "$LOWER/hostile"
    if [ "$fs" = ext4 ]; then
        # moobfs CLI (roadmap C4): a build over the work allowance (E2BIG) is a temporary refusal
        # that names the option; 1100 names against index_build_work=1024, with no layer mounted
        # while the tree is written
        mkdir -p "$LOWER/cliwork/big" || fail "cli-work-mkdir"
        i=0; while [ $i -lt 1100 ]; do : > "$LOWER/cliwork/big/f$i" || fail "cli-work-files"; i=$((i + 1)); done
        mount -t moobfs_layer -o lower="$LOWER/cliwork",index_build_work=1024 none "$LAYER" || fail "cli-work-mount"
        "$BIN/asuid" 1001 "$BIN/moobfs" query "$LAYER/big" kind eq string note > /dev/null 2> /tmp/e.txt; [ $? -eq 75 ] || { cat /tmp/e.txt; fail "cli-e2big-status"; }
        grep -q 'index_build_work' /tmp/e.txt || { cat /tmp/e.txt; fail "cli-e2big-message"; }
        umount "$LAYER" && rm -rf "$LOWER/cliwork" || fail "cli-work-cleanup"
        echo "CLI_E2BIG_PASS"
    fi
    # A btrfs subvolume inside the tree numbers its files from the start again, so two files
    # could share one index record: the layer refuses to enter it and the scan leaves it out.
    if [ "$fs" = btrfs ]; then
        "$BIN/layer" subvol "$LOWER" nested || fail "subvol-create"
        echo inside > "$LOWER/nested/f" || fail "subvol-file"
        "$BIN/moobfs-attr" set "$LOWER/nested/f" sv string inside || fail "subvol-attr"
        mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "subvol-mount"
        [ -e "$LAYER/nested" ] && fail "subvol-entered"
        "$BIN/layer" svcheck "$LAYER" nested || fail "subvol-check"
        "$BIN/query" "$LAYER" sv eq string inside > /tmp/k.txt || fail "subvol-query"
        [ ! -s /tmp/k.txt ] || { cat /tmp/k.txt; fail "subvol-indexed"; }
        umount "$LAYER" || fail "subvol-umount"
        rm "$LOWER/nested/f" && rmdir "$LOWER/nested" || fail "subvol-remove"
        echo "NESTED_SUBVOLUME_REFUSED"
    fi
    echo "HOSTILE_LOWER_PASS: $fs, ${HOSTILE_SEEDS:-3} trees"
    umount "$LOWER" || fail "lower-unmount-$fs"
    echo "LAYER_LOWER_PASS: $fs reflink=$reflink"
}
want ext4 && run_lower ext4 "$DEV_EXT4" no
want btrfs && run_lower btrfs "$DEV_BTRFS" yes
want xfs && run_lower xfs "$DEV_XFS" yes

if want demo; then
# Readable demonstration, quoted in the README: typed attributes written through the layer,
# QUERY2 on domain directories under the default mount options, results filtered by permission.
# Lines starting with "DEMO| " are each command as run (with $BIN on PATH) and its output.
demo() {
    printf 'DEMO| # %s\n' "$*"
    # shellcheck disable=SC2030   # PATH changes only inside this subshell, on purpose
    ( PATH=$BIN:$PATH; eval "$*" ) > /tmp/demo.out 2>&1; rc=$?
    sed 's/^/DEMO| /' /tmp/demo.out
    return $rc
}
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail demo-lower-mount
mkdir "$LOWER/alice" "$LOWER/bob" "$LOWER/team" || fail demo-mkdir
echo 'Cache: keep SQLite next to the files' > "$LOWER/alice/cache.md" || fail demo-write
echo 'Deploys are frozen on Fridays' > "$LOWER/bob/deploy.md" || fail demo-write
echo 'C code follows the kernel style' > "$LOWER/team/style.md" || fail demo-write
echo 'Notes for the salary review' > "$LOWER/team/salary.md" || fail demo-write
chown -R 1001:1001 "$LOWER/alice" && chmod 0700 "$LOWER/alice" && chmod 0600 "$LOWER/alice/cache.md" || fail demo-alice
chown -R 1002:1002 "$LOWER/bob" && chmod 0700 "$LOWER/bob" && chmod 0600 "$LOWER/bob/deploy.md" || fail demo-bob
chmod 0755 "$LOWER/team" && chmod 0644 "$LOWER/team/style.md" || fail demo-team
chown 1002:1002 "$LOWER/team/salary.md" && chmod 0600 "$LOWER/team/salary.md" || fail demo-salary
echo "DEMO| # (prepared on the lower ext4: alice/ uid 1001 0700, bob/ uid 1002 0700, team/ 0755)"
demo "mount -t moobfs_layer -o lower=$LOWER none $LAYER" || fail demo-mount
for f in alice/cache.md bob/deploy.md team/style.md team/salary.md; do
    demo "moobfs-attr set $LAYER/$f kind string decision" || fail "demo-attr-$f"
done
demo "moobfs-attr list $LOWER/alice/cache.md" || fail demo-attr-lower
grep -q "^kind	string	decision$" /tmp/demo.out || fail demo-attr-lower-content
demo "stat -c '%A %u %n' $LAYER/team/*" || fail demo-stat
demo "query --uid 1001 $LAYER/alice q2 kind eq string decision" || fail demo-q2-alice
[ "$(wc -l < /tmp/demo.out)" -eq 1 ] && grep -q '	cache.md$' /tmp/demo.out || fail demo-q2-alice-content
demo "query --uid 1001 $LAYER/team q2 kind eq string decision" || fail demo-q2-team
[ "$(wc -l < /tmp/demo.out)" -eq 1 ] && grep -q '	style.md$' /tmp/demo.out || fail demo-q2-team-content
demo "query --uid 1001 $LAYER/bob q2 kind eq string decision" && fail demo-q2-bob-answered
grep -q 'Permission denied' /tmp/demo.out || fail demo-q2-bob-errno
demo "query --uid 1001 $LAYER kind eq string decision" && fail demo-v1-user-answered
grep -q 'Operation not permitted' /tmp/demo.out || fail demo-v1-user-errno
demo "query $LAYER kind eq string decision" || fail demo-v1-root
[ "$(wc -l < /tmp/demo.out)" -eq 4 ] || fail demo-v1-root-count
demo "query $LAYER build" || fail demo-build
demo "query $LAYER info" || fail demo-info
grep -q 'domain_depth=1 heterogeneous=1 fallback_domains=0' /tmp/demo.out || fail demo-info-counts
# ADR-0011 F1: the index counters sum every user's memories; root gets them, a user gets zeros
grep -q 'counters=shown' /tmp/demo.out || fail demo-info-root-counters
demo "query --uid 1001 $LAYER info" || fail demo-info-user
grep -q 'domain_depth=1 heterogeneous=0 fallback_domains=0 counters=hidden' /tmp/demo.out || fail demo-info-user-hidden
grep -q ' used=0 entries=0 inodes=0$' /tmp/demo.out || fail demo-info-user-counters
umount "$LAYER" || fail demo-umount
rm -rf "$LOWER/alice" "$LOWER/bob" "$LOWER/team" /tmp/demo.out || fail demo-cleanup
umount "$LOWER" || fail demo-lower-umount
echo "DEMO_PASS: QUERY2 per domain, v1 refused to a user, heterogeneous file left out"
fi

# Phase 2e: acceptance with clients that do not know MoobFS (git, SQLite), an own memory
# client, power loss under dm-log-writes and I/O errors under dm-flakey.
# shellcheck disable=SC2031   # sets PATH afresh; the subshell change in demo() is meant to be lost
export PATH="$TOOLS":/bin:/usr/bin:/sbin:/usr/sbin
export HOME=/tmp GIT_CONFIG_NOSYSTEM=1 GIT_EXEC_PATH=/usr/lib/git-core GIT_TEMPLATE_DIR=/nonexistent
export DM_DISABLE_UDEV=1
remember() {   # remember <mount> <relative path> <kind> <score> : the client convention of spec-m2 §6
    tmp="$1/.tmp.$$"
    printf '# %s\n\nmemory %s\n' "$2" "$2" > "$tmp" || return 1
    "$BIN/moobfs-attr" set "$tmp" kind string "$3" || return 1
    "$BIN/moobfs-attr" set "$tmp" score int64 "$4" || return 1
    "$BIN/moobfs-attr" set "$tmp" created time 1760000000000000000 || return 1
    mkdir -p "$(dirname "$1/$2")" || return 1
    sync "$tmp" 2>/dev/null || sync
    mv "$tmp" "$1/$2" || return 1
    sync    # a rename is durable only after the parent directory is synced; clients must fsync it
}
SCRATCH=$DEV_SCRATCH; LOGDEV=$DEV_LOG
if want clients; then
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail acc-lower-mount
mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail acc-layer-mount
# git: a repository living on the layer, with memories as tracked files
mkdir -p "$LAYER/repo" && cd "$LAYER/repo" || fail acc-repo-dir
git init -q . 2>/dev/null || fail acc-git-init
for i in 1 2 3; do
    remember "$LAYER/repo" "notes/n$i.md" note $((i * 7)) || fail "acc-remember-$i"
    git add -A || fail "acc-git-add-$i"
    git -c user.name=moob -c user.email=moob@test commit -q -m "memory $i" || fail "acc-git-commit-$i"
done
[ "$(git log --oneline | wc -l)" -eq 3 ] || fail acc-git-log
[ -z "$(git status --porcelain)" ] || fail acc-git-status
git fsck --strict --no-progress 2>&1 | grep -v '^$' && fail acc-git-fsck
git cat-file -p HEAD:notes/n2.md | grep -q 'memory notes/n2.md' || fail acc-git-content
cd /
"$BIN/query" "$LAYER" kind eq string note score gt int64 10 > /tmp/k.txt || fail acc-query
[ "$(wc -l < /tmp/k.txt)" -eq 2 ] || fail acc-query-count
# SQLite in WAL mode: a writer and a reader at the same time, then integrity_check.
# Run as a controlled experiment: directly on the lower fs (control), on the layer without a
# concurrent reader, and on the layer with one. Counts are printed before any verdict.
sqlite_run() {   # sqlite_run <writer db path> <reader db path or -> <mode wal|delete> <label> [vfs]
    DB=$1; RDB=$2; MODE=$3
    VFS=${5:-}
    # concurrent CLI processes wait for fcntl locks instead of failing
    if [ -n "$VFS" ]; then sq() { sqlite3 -vfs "$VFS" -cmd ".timeout 10000" "$@"; }; else sq() { sqlite3 -cmd ".timeout 10000" "$@"; }; fi
    rm -f "$DB" "$DB-wal" "$DB-shm" "$DB-journal"
    sq "$DB" "PRAGMA journal_mode=$MODE; CREATE TABLE memories(id INTEGER PRIMARY KEY, path TEXT, score INT);" | grep -qi "$MODE" || fail "sqlite-mode-$4"
    ( for i in $(seq 1 200); do sq "$DB" "INSERT INTO memories(path, score) VALUES('p$i', $i);" || { echo "writer failed at $i ($4)"; exit 1; }; done ) &
    writer=$!
    reads=0
    if [ "$RDB" != - ]; then
        while kill -0 $writer 2>/dev/null; do sq "$RDB" 'SELECT count(*) FROM memories;' > /dev/null || echo "reader error ($4)"; reads=$((reads + 1)); done
    fi
    wait $writer || echo "writer exit code $? ($4)"
    count=$(sq "$DB" 'SELECT count(*) FROM memories;')
    integrity=$(sq "$DB" 'PRAGMA integrity_check;')
    missing=$(sq "$DB" "WITH RECURSIVE s(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM s WHERE x<200) SELECT group_concat(x) FROM s WHERE x NOT IN (SELECT score FROM memories);")
    echo "SQLITE_RESULT $4: rows=$count integrity=$integrity reads=$reads missing=[$missing]"
    [ "$count" = 200 ]
}
sqlite_run "$LOWER/c1.db" "$LOWER/c1.db" wal lower-wal; r1=$?
sqlite_run "$LAYER/a1.db" - wal layer-wal-alone; r2=$?
sqlite_run "$LAYER/m1.db" "$LAYER/m1.db" wal layer-wal; r3=$?
sqlite_run "$LAYER/m2.db" "$LAYER/m2.db" delete layer-rollback; r4=$?
sqlite_run "$LAYER/m3.db" "$LAYER/m3.db" wal layer-wal-unix-excl unix-excl; r5=$?
sqlite_run "$LAYER/m5.db" "$LAYER/m5.db" wal layer-wal-again; r6=$?
echo "SQLITE_VARIANTS: lower=$r1 alone=$r2 layer=$r3 rollback=$r4 excl=$r5 again=$r6"
[ $r1 -eq 0 ] || fail acc-sqlite-control
[ $r2 -eq 0 ] || fail acc-sqlite-layer-alone
[ $r3 -eq 0 ] && [ $r6 -eq 0 ] || fail acc-sqlite-layer-reader
echo "SQLITE_CONCURRENT_OK"
"$BIN/moobfs-check" "$LOWER" > /dev/null || fail acc-check
umount "$LAYER" || fail acc-layer-umount
umount "$LOWER" || fail acc-lower-umount
echo "ACCEPTANCE_CLIENTS_PASS"
fi
if want powerloss; then
# Power loss: format and use a device under dm-log-writes, then replay the log to each mark
# on a zeroed disk and check that every memory completed before the mark is whole.
dd if=/dev/zero of="$SCRATCH" bs=1M count=64 2>/dev/null || fail lw-zero
"$TOOLS/dmsetup" create lw --table "0 $(blockdev --getsz "$SCRATCH") log-writes $SCRATCH $LOGDEV" || fail lw-create
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 /dev/mapper/lw || fail lw-mkfs
"$TOOLS/dmsetup" message lw 0 mark mkfs || fail lw-mark-mkfs
mount -t ext4 /dev/mapper/lw "$LOWER" || fail lw-lower-mount
mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail lw-layer-mount
for i in 1 2 3 4; do
    remember "$LAYER" "mem/m$i.md" decision $i || fail "lw-remember-$i"
    "$TOOLS/dmsetup" message lw 0 mark "m$i" || fail "lw-mark-$i"
done
# syncfs on the layer, with no fsync of the file, must make it durable in the lower filesystem
mkdir -p "$LAYER/mem" && printf 'kept by syncfs\n' > "$LAYER/mem/syncfs.md" || fail lw-syncfs-write
"$BIN/layer" syncfs "$LAYER" || fail lw-syncfs
"$TOOLS/dmsetup" message lw 0 mark sf || fail lw-mark-sf
umount "$LAYER" || fail lw-layer-umount
umount "$LOWER" || fail lw-lower-umount
"$TOOLS/dmsetup" message lw 0 mark end || fail lw-mark-end
"$TOOLS/dmsetup" remove --retry lw || fail lw-remove
check_mark() {   # replay up to a mark; memories 1..$2 must be whole, later ones absent
    dd if=/dev/zero of="$SCRATCH" bs=1M count=64 2>/dev/null || return 1
    "$BIN/replay-log" --log "$LOGDEV" --replay "$SCRATCH" --end-mark "$1" || return 1
    mount -t ext4 "$SCRATCH" "$LOWER" && umount "$LOWER" || return 1      # journal recovery, as after a reboot
    "$TOOLS/e2fsck" -fn "$SCRATCH" > /tmp/fsck.txt 2>&1 || { cat /tmp/fsck.txt; return 1; }
    mount -t ext4 -o ro "$SCRATCH" "$LOWER" || return 1
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || return 1
    n=0
    for i in 1 2 3 4; do
        if [ "$i" -le "$2" ]; then
            grep -q "memory mem/m$i.md" "$LAYER/mem/m$i.md" || return 1
            [ "$("$BIN/moobfs-attr" get "$LAYER/mem/m"$i.md score)" = "$(printf 'score\tint64\t%s' $i)" ] || return 1
        else
            [ ! -e "$LAYER/mem/m$i.md" ] || return 1
        fi
    done
    if [ "${3:-}" = syncfs ]; then grep -q 'kept by syncfs' "$LAYER/mem/syncfs.md" || return 1; fi
    "$BIN/query" "$LAYER" kind eq string decision > /tmp/k.txt || return 1
    [ "$(wc -l < /tmp/k.txt)" -eq "$2" ] || return 1
    for leftover in "$LOWER/.tmp."*; do [ -e "$leftover" ] && echo "note: a temporary file survived the crash at $1 (allowed)"; break; done
    umount "$LAYER" && umount "$LOWER"
}
check_mark mkfs 0 || fail lw-check-mkfs
check_mark m1 1 || fail lw-check-m1
check_mark m2 2 || fail lw-check-m2
check_mark m4 4 || fail lw-check-m4
check_mark sf 4 syncfs || fail lw-check-syncfs
check_mark end 4 syncfs || fail lw-check-end
echo "POWER_LOSS_REPLAY_PASS: marks mkfs m1 m2 m4 sf end"
fi
if want ioerror; then
# I/O errors: the lower device starts erroring every write; errors must reach the caller,
# nothing may crash, and the tree must come back clean once the device is healthy again.
dd if=/dev/zero of="$SCRATCH" bs=1M count=64 2>/dev/null || fail fl-zero
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 "$SCRATCH" || fail fl-mkfs
SZ=$(blockdev --getsz "$SCRATCH")
"$TOOLS/dmsetup" create fl --table "0 $SZ linear $SCRATCH 0" || fail fl-create
mount -t ext4 /dev/mapper/fl "$LOWER" || fail fl-lower-mount
mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail fl-layer-mount
remember "$LAYER" "ok.md" note 1 || fail fl-remember-ok
sync
"$TOOLS/dmsetup" suspend fl && dmsetup reload fl --table "0 $SZ flakey $SCRATCH 0 0 1 1 error_writes" && dmsetup resume fl || fail fl-reload-down
remember "$LAYER" "lost.md" note 2 2>/dev/null && sync && echo "note: writes were absorbed by the page cache before the device reported errors"
sync 2>/dev/null
"$TOOLS/dmsetup" suspend --noflush --nolockfs fl && dmsetup reload fl --table "0 $SZ linear $SCRATCH 0" && dmsetup resume fl || fail fl-reload-up
umount "$LAYER" 2>/dev/null || true
umount "$LOWER" 2>/dev/null || true
"$TOOLS/dmsetup" remove --retry fl || fail fl-remove
"$TOOLS/e2fsck" -fy "$SCRATCH" > /tmp/fsck.txt 2>&1; rc=$?; [ $rc -le 1 ] || { cat /tmp/fsck.txt; fail fl-fsck; }
mount -t ext4 "$SCRATCH" "$LOWER" || fail fl-remount
mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail fl-layer-remount
grep -q 'memory ok.md' "$LAYER/ok.md" || fail fl-ok-content
"$BIN/moobfs-check" "$LOWER" > /dev/null || fail fl-check
umount "$LAYER" && umount "$LOWER" || fail fl-final-umount
echo "IO_ERROR_INJECTION_PASS"
fi
if want reserve; then
# Reserved space: an ordinary user writing through the layer cannot spend the blocks ext4
# reserves. First with a reserve owner other than root, where only CAP_SYS_RESOURCE in the
# mounter credentials could spend it (lowered, as overlayfs, journal 0069); then with the default
# reserve owner, uid 0, which ext4 grants by the fsuid of the writing task: writes run with the
# writer's fsuid, not the mounter's (finding F4, journal 0132).
for opts in resuid=4242 defaults; do
    "$TOOLS/mke2fs" -q -F -t ext4 -b 4096 "$SCRATCH" || fail "rs-mkfs-$opts"
    mount -t ext4 -o "$opts" "$SCRATCH" "$LOWER" || fail "rs-lower-mount-$opts"
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail "rs-layer-mount-$opts"
    "$BIN/layer" reserve "$LAYER" "$LOWER" || fail "rs-reserve-$opts"
    umount "$LAYER" && umount "$LOWER" || fail "rs-final-umount-$opts"
done
fi
if want casefold; then
# A casefolded lower directory (issue #95, journal 0136): the layer's names are case-sensitive, so
# it refuses a casefolded directory and every name in it, and a casefolded lower root, as overlayfs
# refuses layers whose casefolding differs. Skipped when the kernel has no casefolding.
if "$TOOLS/mke2fs" -q -F -t ext4 -b 4096 -O casefold "$SCRATCH" 2>/dev/null &&
   mount -t ext4 "$SCRATCH" "$LOWER" 2>/dev/null; then
    "$BIN/layer" cfsetup "$LOWER" || fail cf-setup
    mount -t moobfs_layer -o lower="$LOWER",domain_depth=0 none "$LAYER" || fail cf-layer-mount
    "$BIN/layer" cfcheck "$LAYER" "$LOWER" || fail cf-check
    "$BIN/query" "$LAYER" build > /dev/null || fail cf-build
    umount "$LAYER" || fail cf-layer-umount
    if mount -t moobfs_layer -o lower="$LOWER/ci" none "$LAYER" 2>/dev/null; then
        umount "$LAYER"; fail cf-root-accepted
    fi
    umount "$LOWER" || fail cf-umount
    echo "CASEFOLD_REFUSED_PASS"
else
    echo "CASEFOLD_SKIPPED: no casefolding in this kernel or mke2fs"
fi
fi
# Tracepoints of the index (journal 0102): tr_on clears the buffer and enables the named events
# of the moobfs system, tr_count counts one event in the buffer. It also turns the ring buffer
# on: tests/query-build-moved.c and tests/deadscan.c leave tracing_on at 0.
TR=/sys/kernel/tracing
tr_on() {
    [ -e $TR/trace ] || mount -t tracefs nodev $TR || return 1
    echo 1 > $TR/tracing_on || return 1
    echo > $TR/trace || return 1
    for e in "$@"; do echo 1 > $TR/events/moobfs/"$e/enable" || return 1; done
}
tr_off() { for e in "$@"; do echo 0 > $TR/events/moobfs/"$e/enable"; done; }
tr_count() { grep -c " $1: " $TR/trace; }
if want buildrace; then
# ADR-0012: the index of a domain is built by the first query on it, with the domain still
# being written. tests/buildrace.c writes, renames and unlinks inside each domain while the
# first query builds it; whatever the interleaving, every domain must then equal the oracle,
# and again after the mount-wide query has built what is left.
for fs in ext4 btrfs xfs; do
    case $fs in ext4) dev=$DEV_EXT4 ;; btrfs) dev=$DEV_BTRFS ;; xfs) dev=$DEV_XFS ;; esac
    mount -t $fs "$dev" "$LOWER" || fail "br-lower-mount-$fs"
    rm -rf "$LOWER/br" && mkdir -m 755 "$LOWER/br" || fail "br-mkdir-$fs"
    mount -t moobfs_layer -o lower="$LOWER/br",domain_depth=1 none "$LAYER" || fail "br-mount-$fs"
    for d in $(seq 0 23); do
        dd=$LAYER/$(printf 'r%03d' "$d")
        mkdir -p "$dd/s/t" "$dd/u" || fail "br-domain-$fs"
        for i in $(seq 0 59); do
            f=$dd/$(printf 'f%04d' "$i"); : > "$f"
            [ $((i % 3)) -eq 0 ] || "$BIN/moobfs-attr" set "$f" kind string k$((i % 4)) > /dev/null || fail "br-attr-$fs"
        done
        echo t > "$dd/s/t/inner" && "$BIN/moobfs-attr" set "$dd/s/t/inner" kind string k1 > /dev/null || fail "br-inner-$fs"
    done
    umount "$LAYER" || fail "br-umount-$fs"
    mount -t moobfs_layer -o lower="$LOWER/br",domain_depth=1 none "$LAYER" || fail "br-remount-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'domains_built=0 ' || { "$BIN/query" "$LAYER" info; fail "br-built-at-mount-$fs"; }
    # the scan sleeps between reading a file and storing it, so that writes do land in the
    # window the counter check protects; without the delay this test passes with the check
    # removed. Not longer than this: at 300 us nearly every reading saw a write and no build
    # ever finished (EAGAIN for ever), which is the starvation journal 0098 records as a limit.
    tr_on moobfs_scan_reread || fail "br-trace-on-$fs"
    echo 40 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail "br-delay-$fs"
    run_logged /tmp/br.txt "$BIN/buildrace" "$LAYER" 24 60; rc=$?
    echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us
    [ $rc -eq 0 ] || fail "br-run-$fs"
    grep -q '^BUILDRACE_DONE' /tmp/br.txt || fail "br-done-$fs"
    retries=$(tr_count moobfs_scan_reread)
    tr_off moobfs_scan_reread
    [ "$retries" -gt 0 ] || fail "br-no-write-landed-in-the-window-$fs (the test proved nothing)"
    br_cmp() {
        for p in "$LOWER/br/"*; do
            d=${p##*/}
            for q in "kind exists - -" "kind eq string k1" "score ge int64 50"; do
                # $q holds one condition as four words; it is split on purpose
                # shellcheck disable=SC2086
                "$BIN/query" "$LAYER/$d" q2 $q | cut -f2 | sort > /tmp/br.k || return 1
                # shellcheck disable=SC2086
                "$BIN/moobfs-index" --domain "$LOWER/br/$d" $q | cut -f2 | sort > /tmp/br.o || return 1
                cmp -s /tmp/br.k /tmp/br.o || { echo "domain $d, $q: kernel $(wc -l < /tmp/br.k) oracle $(wc -l < /tmp/br.o)"; diff /tmp/br.k /tmp/br.o | grep '^[-+<>][^-+]' | head -6; return 1; }
            done
        done
    }
    br_cmp || fail "br-index-differs-from-oracle-$fs"
    # A directory moved below a private one, inside the domain: its files stop being reachable
    # by whoever can only traverse the domain, and must leave the domain's answers. The rename
    # makes the domain stale and the next query builds it again with the new placement.
    mkdir -p "$LAYER/r000/open/box" "$LAYER/r000/priv" && chmod 0700 "$LAYER/r000/priv" || fail "br-priv-mkdir-$fs"
    echo x > "$LAYER/r000/open/box/secret" && "$BIN/moobfs-attr" set "$LAYER/r000/open/box/secret" kind string moved > /dev/null || fail "br-priv-attr-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/r000" q2 kind eq string moved | wc -l)" -eq 1 ] || fail "br-priv-before-$fs"
    mv "$LAYER/r000/open/box" "$LAYER/r000/priv/box" || fail "br-priv-mv-$fs"
    [ "$("$BIN/query" --uid 1001 "$LAYER/r000" q2 kind eq string moved | wc -l)" -eq 0 ] || fail "br-moved-under-private-directory-still-answered-$fs"
    [ "$("$BIN/query" "$LAYER" kind eq string moved | wc -l)" -eq 1 ] || fail "br-priv-still-indexed-$fs"   # the mount-wide query sees it
    rm -rf "$LAYER/r000/priv" "$LAYER/r000/open" || fail "br-priv-rm-$fs"
    "$BIN/query" "$LAYER" build || fail "br-build-all-$fs"
    "$BIN/query" "$LAYER" info | grep -q 'domains_built=25 ' || { "$BIN/query" "$LAYER" info; fail "br-built-count-$fs"; }
    br_cmp || fail "br-index-differs-after-build-all-$fs"
    f=$("$BIN/query" "$LAYER" info | sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p')
    [ $(( f & 8 )) -eq 0 ] || fail "br-index-incomplete-$fs"
    # A chmod to 0600 while the first build of a new domain reads the file (journal 0152): the
    # reading is repeated, and the file's own homogeneity must be decided again with it, or the
    # file stays in the answers to a caller that can no longer read it. The scan sleeps one
    # second per file so that the chmod, 0.3 s in, lands inside the reading.
    tr_on moobfs_scan_reread || fail "br-chmod-trace-on-$fs"
    echo 1000000 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail "br-chmod-delay-$fs"
    "$BIN/chmodrace" "$LAYER/chmodrace" 300000 > /tmp/br-chmod.txt; rc=$?
    echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us
    cr_retries=$(tr_count moobfs_scan_reread)
    tr_off moobfs_scan_reread
    cat /tmp/br-chmod.txt
    [ $rc -ne 1 ] || fail "br-chmod-during-build-still-answered-$fs"
    [ $rc -eq 0 ] || fail "br-chmod-run-$fs"
    [ "$cr_retries" -gt 0 ] || fail "br-chmod-not-in-the-window-$fs (the test proved nothing)"
    umount "$LAYER" && rm -rf "$LOWER/br" && umount "$LOWER" || fail "br-cleanup-$fs"
    echo "BUILD_RACE_PASS: $fs, $(cut -d' ' -f2- /tmp/br.txt) readings_repeated=$retries; every domain equal to the oracle; chmod during a build: absent"
done
fi
if want release; then
# ADR-0012, second half: a built domain is released when a build needs its room in the index
# budget, least recently queried first, and by the shrinker under memory pressure, except the
# domains used in the last index_idle seconds. Every answer after a release, and after the
# rebuild it causes, must equal the oracle.
rl_info() { "$BIN/query" "$LAYER" info | sed -n "s/.*$1=\([0-9]*\).*/\1/p" | head -1; }
rl_cmp() {
    for d in "$@"; do
        "$BIN/query" "$LAYER/$d" q2 kind exists - - | cut -f2 | sort > /tmp/rl.k || return 1
        "$BIN/moobfs-index" --domain "$LOWER/rl/$d" kind exists - - | cut -f2 | sort > /tmp/rl.o || return 1
        [ -s /tmp/rl.o ] && cmp -s /tmp/rl.k /tmp/rl.o || { echo "domain $d: kernel $(wc -l < /tmp/rl.k) oracle $(wc -l < /tmp/rl.o)"; return 1; }
    done
}
for fs in ext4 btrfs xfs; do
    case $fs in ext4) dev=$DEV_EXT4 ;; btrfs) dev=$DEV_BTRFS ;; xfs) dev=$DEV_XFS ;; esac
    mount -t $fs "$dev" "$LOWER" || fail "rl-lower-mount-$fs"
    rm -rf "$LOWER/rl" && mkdir -m 755 "$LOWER/rl" || fail "rl-mkdir-$fs"
    mount -t moobfs_layer -o lower="$LOWER/rl" none "$LAYER" || fail "rl-mount-$fs"
    for d in d0 d1 d2 d3; do
        mkdir -m 755 "$LAYER/$d" || fail "rl-domain-$fs"
        "$BIN/bench-gen" --seed 7 --inodes 500 --profile 16 --out /tmp/rl-m.txt || fail "rl-gen-$fs"
        "$BIN/bench-load" --manifest /tmp/rl-m.txt --target "$LAYER/$d" --workload W1 --procs 2 --out /tmp/rl-w1 > /dev/null || fail "rl-w1-$fs"
    done
    # for the cursor check below: twelve files with values longer than the cursor's inline space
    mkdir -m 755 "$LAYER/d0/cur" || fail "rl-cur-dir-$fs"
    long=$(head -c 100 /dev/zero | tr '\0' x)
    for i in $(seq 10 21); do
        f=$LAYER/d0/cur/c$i; : > "$f"
        "$BIN/moobfs-attr" set "$f" rlcur string "$long$i" > /dev/null &&
            "$BIN/moobfs-attr" set "$f" rlgrp string g > /dev/null &&
            "$BIN/moobfs-attr" set "$f" rltag string t > /dev/null || fail "rl-cur-attr-$fs"
    done
    umount "$LAYER" || fail "rl-umount-$fs"

    # the budget: sized from what one domain takes, so that two fit and three do not
    mount -t moobfs_layer -o lower="$LOWER/rl" none "$LAYER" || fail "rl-measure-mount-$fs"
    rl_cmp d0 || fail "rl-measure-$fs"
    one=$(rl_info used)
    umount "$LAYER" || fail "rl-measure-umount-$fs"
    budget=$(( one * 5 / 2 ))
    [ $budget -ge 1048576 ] || fail "rl-domains-too-small-for-the-1M-minimum-budget-$fs (one domain: $one bytes)"
    mount -t moobfs_layer -o lower="$LOWER/rl",index_budget=$budget none "$LAYER" || fail "rl-budget-mount-$fs"
    tr_on moobfs_domain_build moobfs_domain_release || fail "rl-trace-on-$fs"
    rl_cmp d0 d1 d2 d3 || fail "rl-budget-answer-differs-$fs"
    # the tracepoints saw it: four builds that succeeded, at least two of them after releasing
    [ "$(grep ' moobfs_domain_build: ' $TR/trace | grep -c ' err=0 ')" -eq 4 ] || { cat $TR/trace; fail "rl-trace-builds-$fs"; }
    [ "$(grep ' moobfs_domain_release: ' $TR/trace | grep -c ' by=budget ')" -ge 2 ] || { cat $TR/trace; fail "rl-trace-budget-releases-$fs"; }
    [ "$(rl_info domain_releases)" -ge 2 ] || { "$BIN/query" "$LAYER" info; fail "rl-budget-nothing-released-$fs"; }
    [ "$(rl_info domains_built)" -le 2 ] || { "$BIN/query" "$LAYER" info; fail "rl-budget-too-many-built-$fs"; }
    [ "$(rl_info used)" -le $budget ] || fail "rl-budget-exceeded-$fs"
    rl_cmp d0 || fail "rl-budget-rebuild-differs-$fs"
    # the least recently queried goes first: once d0 and d1 are the last two queried, both stay
    rl_cmp d1 || fail "rl-budget-lru-query-$fs"
    rl_cmp d0 || fail "rl-budget-lru-query2-$fs"
    b0=$(rl_info domain_builds)
    rl_cmp d0 d1 || fail "rl-budget-lru-again-$fs"
    [ "$(rl_info domain_builds)" -eq "$b0" ] || fail "rl-budget-recent-domain-was-released-$fs"
    # F2 (ADR-0016): with d0 and d1 built, writes into d0 overflow the budget. A failed update
    # makes d0 be built again by its next query, which must answer with every file, never
    # without the ones whose update failed; the room comes from releasing d1.
    tr_on moobfs_index_incomplete || fail "rl-fill-trace-on-$fs"
    n=$(( one * 9 / 10 / 1000 ))
    long=$(head -c 1000 /dev/zero | tr '\0' y)
    mkdir -m 755 "$LAYER/d0/fill" || fail "rl-fill-dir-$fs"
    for i in $(seq 1 $n); do
        f=$LAYER/d0/fill/f$i; : > "$f" && "$BIN/moobfs-attr" set "$f" rlfill string "$long" > /dev/null || fail "rl-fill-attr-$fs"
    done
    grep -q ' moobfs_index_incomplete: ' $TR/trace || fail "rl-fill-never-overflowed-$fs (the test proved nothing)"
    "$BIN/query" "$LAYER/d0" q2 rlfill exists - - | cut -f2 | sort > /tmp/rl.k || fail "rl-fill-query-$fs"
    "$BIN/moobfs-index" --domain "$LOWER/rl/d0" rlfill exists - - | cut -f2 | sort > /tmp/rl.o || fail "rl-fill-oracle-$fs"
    [ "$(wc -l < /tmp/rl.o)" -eq $n ] && cmp -s /tmp/rl.k /tmp/rl.o ||
        { echo "fill: kernel $(wc -l < /tmp/rl.k) oracle $(wc -l < /tmp/rl.o) written $n"; fail "rl-fill-answer-differs-$fs"; }
    [ "$(rl_info used)" -le $budget ] || fail "rl-fill-budget-exceeded-$fs"
    rm -r "$LAYER/d0/fill" || fail "rl-fill-rm-$fs"
    echo "RELEASE_FILL: $fs, $n writes overflowed the budget; d0 built again with all of them"
    # the mount-wide query needs every domain at once and does not release any: it fails
    "$BIN/query" "$LAYER" build 2> /dev/null && fail "rl-budget-mount-wide-query-did-not-fail-$fs"
    f=$("$BIN/query" "$LAYER" info | sed -n 's/.* flags=\([0-9a-fx]*\) .*/\1/p')
    [ $(( f & 8 )) -eq 0 ] || fail "rl-budget-index-incomplete-$fs"
    umount "$LAYER" || fail "rl-budget-umount-$fs"

    # the shrinker, with no idle floor: drop_caches=2 asks every shrinker for everything
    mount -t moobfs_layer -o lower="$LOWER/rl",index_idle=0 none "$LAYER" || fail "rl-shrink-mount-$fs"
    grep -q "index_idle=0" /proc/mounts || fail "rl-shrink-option-not-shown-$fs"
    rl_cmp d0 d1 d2 d3 || fail "rl-shrink-before-$fs"
    [ "$(rl_info domains_built)" -eq 4 ] || fail "rl-shrink-built-$fs"
    echo > $TR/trace
    sync; echo 2 > /proc/sys/vm/drop_caches
    [ "$(rl_info domains_built)" -eq 0 ] || { "$BIN/query" "$LAYER" info; fail "rl-shrinker-released-nothing-$fs"; }
    [ "$(grep ' moobfs_domain_release: ' $TR/trace | grep -c ' by=shrinker ')" -eq 4 ] || { cat $TR/trace; fail "rl-trace-shrinker-releases-$fs"; }
    [ "$(rl_info domain_releases)" -eq 4 ] || fail "rl-shrink-count-$fs"
    rl_cmp d0 d1 d2 d3 || fail "rl-shrink-rebuild-differs-$fs"
    # a cursor taken before a release resumes after the rebuild (spec §9): the first page, a
    # release, the remaining pages; together they must be the whole answer, in its order
    rl_page() {
        "$BIN/query" "$LAYER/d0" q2 --raw --limit 5 --cursor-out /tmp/rl.c "$@" > /tmp/rl.p || return 1
        r0=$(rl_info domain_releases)
        sync; echo 2 > /proc/sys/vm/drop_caches
        [ "$(rl_info domain_releases)" -gt "$r0" ] || { echo "nothing was released"; return 1; }
        "$BIN/query" "$LAYER/d0" q2 --raw --limit 5 --pages --cursor-in /tmp/rl.c "$@" >> /tmp/rl.p || return 1
        "$BIN/query" "$LAYER/d0" q2 --raw "$@" > /tmp/rl.f || return 1
        [ "$(wc -l < /tmp/rl.f)" -eq 12 ] && cmp -s /tmp/rl.p /tmp/rl.f || { cat /tmp/rl.p; echo ---; cat /tmp/rl.f; return 1; }
    }
    rl_page --order rlcur string asc || fail "rl-cursor-long-asc-$fs"
    rl_page --order rlcur string desc || fail "rl-cursor-long-desc-$fs"
    rl_page rlgrp eq string g || fail "rl-cursor-unordered-$fs"
    rl_page rlgrp eq string g rltag eq string t || fail "rl-cursor-intersection-$fs"
    tr_off moobfs_domain_build moobfs_domain_release
    # tracefs is root's: the counts these events carry must not reach other users (ADR-0011, F1)
    [ "$(stat -c %a $TR)" = 700 ] || fail "rl-tracefs-not-root-only-$fs ($(stat -c %a $TR))"
    umount "$LAYER" || fail "rl-shrink-umount-$fs"

    # with the default floor (60 s) the domains just queried stay
    mount -t moobfs_layer -o lower="$LOWER/rl" none "$LAYER" || fail "rl-idle-mount-$fs"
    rl_cmp d0 d1 d2 d3 || fail "rl-idle-before-$fs"
    sync; echo 2 > /proc/sys/vm/drop_caches
    [ "$(rl_info domains_built)" -eq 4 ] && [ "$(rl_info domain_releases)" -eq 0 ] || { "$BIN/query" "$LAYER" info; fail "rl-idle-floor-not-kept-$fs"; }
    umount "$LAYER" || fail "rl-idle-umount-$fs"

    # releases racing with builds and writes: the shrinker runs in a loop during buildrace
    rm -rf "$LOWER/rl/"* && mount -t moobfs_layer -o lower="$LOWER/rl",index_idle=0 none "$LAYER" || fail "rl-race-mount-$fs"
    for d in $(seq 0 7); do
        dd=$LAYER/$(printf 'r%03d' "$d")
        mkdir -p "$dd/s/t" "$dd/u" || fail "rl-race-domain-$fs"
        for i in $(seq 0 59); do
            f=$dd/$(printf 'f%04d' "$i"); : > "$f"
            "$BIN/moobfs-attr" set "$f" kind string k$((i % 4)) > /dev/null || fail "rl-race-attr-$fs"
        done
    done
    rm -f /tmp/rl.stop
    ( while [ ! -e /tmp/rl.stop ]; do echo 2 > /proc/sys/vm/drop_caches; done ) &
    # one buildrace lasts well under a second; five, with pauses in which the loop releases
    # what they built, so that later rounds build again what was released while being written
    rc=0
    for k in 1 2 3 4 5; do
        "$BIN/buildrace" "$LAYER" 8 60 > /tmp/rl-br.txt || { rc=1; break; }
        sleep 0.3
    done
    touch /tmp/rl.stop; wait
    [ $rc -eq 0 ] || { cat /tmp/rl-br.txt; fail "rl-race-run-$fs"; }
    rl_race_releases=$(rl_info domain_releases)
    [ "$rl_race_releases" -gt 0 ] || fail "rl-race-no-release-happened-$fs (the test proved nothing)"
    for d in $(seq 0 7); do
        d=$(printf 'r%03d' "$d")
        "$BIN/query" "$LAYER/$d" q2 kind exists - - | cut -f2 | sort > /tmp/rl.k || fail "rl-race-query-$fs"
        "$BIN/moobfs-index" --domain "$LOWER/rl/$d" kind exists - - | cut -f2 | sort > /tmp/rl.o || fail "rl-race-oracle-$fs"
        cmp -s /tmp/rl.k /tmp/rl.o || fail "rl-race-differs-$d-$fs"
    done
    "$BIN/permission-gaps" release "$LAYER" "$LOWER/rl" || fail "rl-unprivileged-$fs"
    umount "$LAYER" && rm -rf "$LOWER/rl" && umount "$LOWER" || fail "rl-cleanup-$fs"
    echo "RELEASE_PASS: $fs, one domain $one bytes, budget $budget; shrinker, idle floor and cursors across a release; releases during buildrace: $rl_race_releases; every answer equal to the oracle"
done
fi
if want lowerro; then
# The layer takes write access on the lower mount for every change it makes there (journal 0117):
# a read-only lower mount refuses them as it would refuse the caller directly, and the freeze of
# the lower filesystem holds them back instead of reaching ext4 frozen (which warns). An
# idmapped lower mount is refused at mount.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "lr-lower-mount"
rm -rf "$LOWER/lr" && mkdir -m 755 "$LOWER/lr" "$LOWER/lr/d" "$LOWER/lr/gone" || fail "lr-mkdir"
echo keep > "$LOWER/lr/d/f" && chmod 644 "$LOWER/lr/d/f" || fail "lr-file"
lr_state() { (cd "$LOWER/lr" && find . | sort && stat -c '%a %s %n' d/f && "$BIN/moobfs-attr" list d/f); }
lr_state > /tmp/lr.before 2>&1
mkdir -p /tmp/moob-ro
mount --bind "$LOWER/lr" /tmp/moob-ro && mount -o remount,bind,ro /tmp/moob-ro || fail "lr-ro-bind"
touch /tmp/moob-ro/x 2>/dev/null && fail "lr-ro-bind-is-writable (the test would prove nothing)"
mount -t moobfs_layer -o lower=/tmp/moob-ro none "$LAYER" || fail "lr-ro-layer-mount"
cat "$LAYER/d/f" > /dev/null || fail "lr-ro-read"
for op in "touch $LAYER/d/new" "mkdir $LAYER/d/sub" "ln -s f $LAYER/d/l" "mv $LAYER/d/f $LAYER/d/g" \
          "rm $LAYER/d/f" "rmdir $LAYER/gone" "chmod 600 $LAYER/d/f" "truncate -s 0 $LAYER/d/f" \
          "$BIN/moobfs-attr set $LAYER/d/f kind string note"; do
    # shellcheck disable=SC2086
    if $op 2> /tmp/lr.err; then fail "lr-ro-accepted: $op"; fi
    grep -qi 'read-only' /tmp/lr.err || { cat /tmp/lr.err; fail "lr-ro-not-erofs: $op"; }
done
umount "$LAYER" && umount /tmp/moob-ro || fail "lr-ro-umount"
lr_state > /tmp/lr.after 2>&1
cmp -s /tmp/lr.before /tmp/lr.after || { diff /tmp/lr.before /tmp/lr.after; fail "lr-ro-lower-changed"; }
# frozen lower: each change waits for the thaw, with no kernel warning meanwhile
mount -t moobfs_layer -o lower="$LOWER/lr" none "$LAYER" || fail "lr-fz-layer-mount"
echo "moobfs-suite: lowerro freeze" > /dev/kmsg
fsfreeze --freeze "$LOWER" || fail "lr-fz-freeze"
rm -f /tmp/lr.done.*
(touch "$LAYER/d/new" && echo ok > /tmp/lr.done.create) &
(chmod 600 "$LAYER/d/f" && echo ok > /tmp/lr.done.setattr) &
("$BIN/moobfs-attr" set "$LAYER/gone" kind string note > /dev/null && echo ok > /tmp/lr.done.xattr) &
sleep 2
lr_early=$(ls /tmp/lr.done.* 2>/dev/null)
lr_warn=$(dmesg | sed -n '/moobfs-suite: lowerro freeze/,$p' | grep -E '\] WARNING:|^WARNING:|BUG:' | head -3)
fsfreeze --unfreeze "$LOWER" || fail "lr-fz-thaw"
wait
[ -z "$lr_warn" ] || { dmesg | sed -n '/moobfs-suite: lowerro freeze/,$p' | grep -E -A15 '\] WARNING:|^WARNING:' | head -40; fail "lr-fz-kernel-warning"; }
[ -z "$lr_early" ] || fail "lr-fz-change-completed-while-frozen: $lr_early"
for k in create setattr xattr; do [ -f /tmp/lr.done.$k ] || fail "lr-fz-$k-failed-after-thaw"; done
[ -f "$LOWER/lr/d/new" ] && [ "$(stat -c %a "$LOWER/lr/d/f")" = 600 ] || fail "lr-fz-lower-not-changed-after-thaw"
umount "$LAYER" || fail "lr-fz-umount"
# idmapped lower: refused (needs util-linux mount with X-mount.idmap, 2.39 or later)
if mount -V 2>/dev/null | grep -q util-linux && mount --bind -o X-mount.idmap=b:0:10000:65536 "$LOWER/lr" /tmp/moob-ro 2>/dev/null; then
    if mount -t moobfs_layer -o lower=/tmp/moob-ro none "$LAYER" 2> /tmp/lr.err; then umount "$LAYER"; fail "lr-idmapped-lower-accepted"; fi
    # the new mount API hands the message to mount(8); mount(2) logs it in the kernel log
    { cat /tmp/lr.err; dmesg | tail -20; } | grep -q 'lower cannot be an idmapped mount' || { cat /tmp/lr.err; fail "lr-idmapped-no-message"; }
    umount /tmp/moob-ro || fail "lr-idmap-umount"
    lr_idmap=refused
else
    lr_idmap="not tested (no idmapped bind mount here)"
fi
rm -rf "$LOWER/lr" && umount "$LOWER" || fail "lr-cleanup"
echo "LOWERRO_PASS: read-only lower mount refused 9 changes with EROFS, lower unchanged; 3 changes waited for the thaw of a frozen lower, no kernel warning; idmapped lower: $lr_idmap"
fi
if want sqlext; then
# The moobfs SQLite virtual table (ADR-0013, journal 0108): every statement is run on the virtual
# table over a domain and on an ordinary table filled from the lower filesystem; the rows must be
# equal. The query plans are printed, to see what SQLite hands to the module. The COLLATE NOCASE
# statements return rows only when the condition stays SQLite's: the kernel compares bytes
# (journal 0120).
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "sx-lower-mount"
rm -rf "$LOWER/sx" && mkdir -m 755 "$LOWER/sx" || fail "sx-mkdir"
mount -t moobfs_layer -o lower="$LOWER/sx" none "$LAYER" || fail "sx-mount"
mkdir -m 755 "$LAYER/dm" "$LAYER/dm/sub" "$LAYER/dp" || fail "sx-domains"
for i in $(seq 0 119); do
    if [ $((i % 4)) -eq 0 ]; then f=$LAYER/dm/sub/f$i; else f=$LAYER/dm/f$i; fi
    : > "$f" || fail "sx-file"
    case $((i % 3)) in 0) k=note ;; 1) k=decision ;; *) k=fact ;; esac
    [ "$i" -eq 7 ] || "$BIN/moobfs-attr" set "$f" kind string $k > /dev/null || fail "sx-kind"
    "$BIN/moobfs-attr" set "$f" agent string a$((i % 5)) > /dev/null || fail "sx-agent"
    "$BIN/moobfs-attr" set "$f" created time $(( (i * 37 % 120) * 1000 )) > /dev/null || fail "sx-created"
    [ $((i % 10)) -eq 0 ] || "$BIN/moobfs-attr" set "$f" score int64 $((i % 17)) > /dev/null || fail "sx-score"
    "$BIN/moobfs-attr" set "$f" weight double "$(( i - 60 )).25" > /dev/null || fail "sx-weight"
    "$BIN/moobfs-attr" set "$f" flag bool $((i % 2)) > /dev/null || fail "sx-flag"
    if [ $((i % 6)) -eq 0 ]; then t="título $i"; else t="title $i"; fi
    "$BIN/moobfs-attr" set "$f" title string "$t" > /dev/null || fail "sx-title"
done
sxcols="kind string|agent string|created time|score int64|weight double|flag bool|title string"
sx() {  # sx <sql>...: compare vt and ref
    IFS='|'
    # shellcheck disable=SC2086
    set -- $sxcols -- "$@"
    unset IFS
    "$BIN/sqlq" compare "$LAYER/dm" "$LOWER/sx/dm" "$@"
}
sx "SELECT rowid, path, kind, agent, created, score, weight, flag, title FROM {T}" \
   "SELECT path FROM {T} WHERE kind = 'note'" \
   "SELECT path FROM {T} WHERE kind IN ('note', 'decision')" \
   "SELECT path FROM {T} WHERE kind IN ('note', 'decision') AND agent = 'a1'" \
   "SELECT path, score FROM {T} WHERE agent IN ('a1', 'a3') AND score >= 5" \
   "SELECT path FROM {T} WHERE kind = 'fact' OR agent = 'a2'" \
   "SELECT path FROM {T} WHERE (kind = 'note' AND score > 10) OR (agent = 'a4' AND flag = 1)" \
   "SELECT path FROM {T} WHERE score IS NULL" \
   "SELECT path, score FROM {T} WHERE score IS NOT NULL AND score < 3" \
   "SELECT path FROM {T} WHERE score != 5" \
   "SELECT path, title FROM {T} WHERE title LIKE 'tí%'" \
   "SELECT path, title FROM {T} WHERE title >= 'título' AND title < 'títulp'" \
   "SELECT path, created FROM {T} ORDER BY created DESC LIMIT 7" \
   "SELECT path, created FROM {T} WHERE kind = 'decision' ORDER BY created LIMIT 5" \
   "SELECT path, created FROM {T} WHERE kind IN ('note', 'fact') ORDER BY created DESC, path LIMIT 10" \
   "SELECT path, score FROM {T} ORDER BY score, path LIMIT 12" \
   "SELECT path, created FROM {T} ORDER BY created LIMIT 50 OFFSET 30" \
   "SELECT count(*) FROM {T}" \
   "SELECT kind, count(*) FROM {T} GROUP BY kind ORDER BY kind" \
   "SELECT agent, max(created), count(score) FROM {T} WHERE score > 3 GROUP BY agent ORDER BY agent" \
   "SELECT path, weight FROM {T} WHERE weight BETWEEN -2.5 AND 3.5 ORDER BY weight" \
   "SELECT path FROM {T} WHERE score = '5'" \
   "SELECT path FROM {T} WHERE score = 5.0" \
   "SELECT path FROM {T} WHERE kind = 'note' AND kind = 'fact'" \
   "SELECT path FROM {T} WHERE flag = 1 AND kind = 'note' AND agent = 'a3' AND score >= 2 AND created > 5000" \
   "SELECT a.path, b.path FROM {T} a JOIN {T} b ON a.agent = b.agent WHERE b.path = 'f1' AND a.kind = 'note'" \
   "SELECT path FROM {T} WHERE kind = 'NOTE' COLLATE NOCASE" \
   "SELECT path FROM {T} WHERE kind COLLATE NOCASE IN ('Note', 'FACT') AND agent = 'a2'" \
   "SELECT path, title FROM {T} WHERE title >= 'TITLE 5' COLLATE NOCASE AND title < 'TITLE 6' COLLATE NOCASE" \
   > /tmp/sx.txt 2>&1 || { cat /tmp/sx.txt; fail "sx-differs"; }
grep -q SQLQ_COMPARE_DONE /tmp/sx.txt || { cat /tmp/sx.txt; fail "sx-incomplete"; }
# with created declared required, the newest N are the kernel's ordered scan with its limit
sxcols="kind string|created time required|agent string|score int64"
sx "SELECT path, created FROM {T} ORDER BY created DESC LIMIT 7" \
   "SELECT path, created FROM {T} WHERE kind = 'decision' ORDER BY created LIMIT 5" \
   "SELECT path, created FROM {T} WHERE agent = 'a2' AND score >= 4 ORDER BY created DESC LIMIT 3" \
   "SELECT path, created FROM {T} ORDER BY created LIMIT 50 OFFSET 30" \
   > /tmp/sx2.txt 2>&1 || { cat /tmp/sx2.txt; fail "sx-required-differs"; }
grep -E '^(query|  plan):' /tmp/sx2.txt | sed 's/^/  /'
grep -q 'TEMP B-TREE' /tmp/sx2.txt && { cat /tmp/sx2.txt; fail "sx-required-order-not-the-kernels"; }
cat /tmp/sx2.txt >> /tmp/sx.txt
grep -E '^(query|  plan):' /tmp/sx.txt | sed 's/^/  /'
sx_or=$(grep -c 'MULTI-INDEX OR' /tmp/sx.txt)
# a file of another user, alone in its domain: not in any answer, neither for its owner's
# neighbours nor for root (it fails the homogeneity rule of its domain)
: > "$LAYER/dp/secret" && "$BIN/moobfs-attr" set "$LAYER/dp/secret" kind string note > /dev/null &&
    : > "$LAYER/dp/open" && "$BIN/moobfs-attr" set "$LAYER/dp/open" kind string note > /dev/null &&
    chown 1002:1002 "$LAYER/dp/secret" && chmod 600 "$LAYER/dp/secret" || fail "sx-private"
"$BIN/sqlq" --uid 1001 run "$LAYER/dp" "kind string" -- "SELECT path FROM vt" > /tmp/sxp.txt || fail "sx-uid-run"
grep -q secret /tmp/sxp.txt && fail "sx-private-file-returned"
grep -q open /tmp/sxp.txt || fail "sx-public-file-missing"
n_root=$("$BIN/sqlq" run "$LAYER/dm" "kind string" -- "SELECT count(*) FROM vt")
n_user=$("$BIN/sqlq" --uid 1001 run "$LAYER/dm" "kind string" -- "SELECT count(*) FROM vt")
[ "$n_root" = "$n_user" ] && [ "$n_root" -eq 119 ] || fail "sx-uid-count ($n_root, $n_user)"
umount "$LAYER" && rm -rf "$LOWER/sx" && umount "$LOWER" || fail "sx-cleanup"
echo "SQLEXT_PASS: $(grep -c SQLQ_SAME /tmp/sx.txt) statements equal to the ordinary table; MULTI-INDEX OR in $sx_or plans; private file never returned"
fi
if want permmatrix; then
# Journal 0116: a domain answered from its partition (no per-result checks) must return a file
# only to those who can read it on the lower, whatever the modes, owners and groups. The
# oracle is the lower kernel (getxattr with the identity's credentials), not a copy of the
# homogeneity rule; tests/permmatrix.c also counts the cases the rule before 0116 got wrong,
# so the run proves the matrix reached them. Journal 0215: the oracle also lists each directory
# on the way, since a search-only one (0711) lets getxattr through but hides its names, and the
# matrix is checked again in fallback mode (per-result checks) after a chmod through the layer;
# the cases the rule before 0215 (search alone for a directory) got wrong are counted too.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "pm-lower-mount"
pm_wrong=0
pm_prev=0
for seed in 1 2 3 4 5 6 7 8 9 10 11 12; do
    rm -rf "$LOWER/pmx" && mkdir -m 755 "$LOWER/pmx" || fail "pm-mkdir-$seed"
    "$BIN/permmatrix" plant "$LOWER/pmx" $seed || fail "pm-plant-$seed"
    mount -t moobfs_layer -o lower="$LOWER/pmx",domain_depth=1 none "$LAYER" || fail "pm-mount-$seed"
    "$BIN/permmatrix" check "$LAYER" "$LOWER/pmx" > /tmp/pm.txt; rc=$?
    cat /tmp/pm.txt
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || { "$BIN/query" "$LAYER" info; fail "pm-fallback-$seed (the partition was not what answered)"; }
    [ $rc -eq 0 ] || fail "pm-mismatch-$seed"
    pm_wrong=$((pm_wrong + $(sed -n 's/.* old_rule_wrong=\([0-9]*\).*/\1/p' /tmp/pm.txt)))
    pm_prev=$((pm_prev + $(sed -n 's/.* prev_rule_wrong=\([0-9]*\).*/\1/p' /tmp/pm.txt)))
    chmod "$(stat -c %a "$LOWER/pmx/pm")" "$LAYER/pm" || fail "pm-chmod-$seed"
    "$BIN/permmatrix" check "$LAYER" "$LOWER/pmx" > /tmp/pm.txt; rc=$?
    cat /tmp/pm.txt
    "$BIN/query" "$LAYER" info | grep -q 'fallback_domains=1' || { "$BIN/query" "$LAYER" info; fail "pm-no-fallback-$seed (per-result checks were not what answered)"; }
    [ $rc -eq 0 ] || fail "pm-mismatch-fallback-$seed"
    umount "$LAYER" || fail "pm-umount-$seed"
done
[ $pm_wrong -gt 0 ] || fail "pm-no-case-of-the-old-rule (the matrix proved nothing)"
[ $pm_prev -gt 0 ] || fail "pm-no-case-of-the-rule-before-0215 (the matrix proved nothing)"
# The matrix's fallback round meets only files the rule already left out. A directory made
# search-only through the layer after the build leaves its files in the partition and puts the
# domain in fallback mode: the per-result check must ask read of that directory too (0215).
rm -rf "$LOWER/pmx" && mkdir -m 755 "$LOWER/pmx" || fail "pm-so-mkdir"
mount -t moobfs_layer -o lower="$LOWER/pmx",domain_depth=1 none "$LAYER" || fail "pm-so-mount"
mkdir -m 755 "$LAYER/d" "$LAYER/d/s" && : > "$LAYER/d/s/f" && chmod 644 "$LAYER/d/s/f" || fail "pm-so-files"
"$BIN/moobfs-attr" set "$LAYER/d/s/f" kind string so > /dev/null || fail "pm-so-attr"
"$BIN/query" --uid 1001 "$LAYER/d" q2 kind exists - - | grep -q 's/f' || fail "pm-so-before"
chmod 0711 "$LAYER/d/s" || fail "pm-so-chmod"
"$BIN/query" "$LAYER" info | grep -q 'fallback_domains=1' || { "$BIN/query" "$LAYER" info; fail "pm-so-no-fallback"; }
"$BIN/query" --uid 1001 "$LAYER/d" q2 kind exists - - > /tmp/pm-so.txt || fail "pm-so-query"
grep -q 's/f' /tmp/pm-so.txt && fail "pm-so-leak (a search-only directory's file named per result)"
"$BIN/query" "$LAYER/d" q2 kind exists - - | grep -q 's/f' || fail "pm-so-owner-missing"
umount "$LAYER" || fail "pm-so-umount"
# The two other places the rule is applied, with no permission change and so no fallback: a
# search-only directory created through the layer in a built domain (place_dentry_above), and
# one renamed during the domain's first build (dir_moved_prepare). Its file stays out of the
# partition: uid 1001 does not get it and it is counted heterogeneous.
pm_hetero() { "$BIN/query" "$LAYER" info | sed -n 's/.*heterogeneous=\([0-9]*\).*/\1/p'; }
rm -rf "$LOWER/pmx" && mkdir -m 755 "$LOWER/pmx" || fail "pm-cr-mkdir"
mount -t moobfs_layer -o lower="$LOWER/pmx",domain_depth=1 none "$LAYER" || fail "pm-cr-mount"
# no mkdir -m nor chmod through the layer: busybox mkdir -m chmods, and a chmod of a directory
# puts the domain in fallback mode
(umask 022; mkdir "$LAYER/d" && : > "$LAYER/d/top") && "$BIN/moobfs-attr" set "$LAYER/d/top" kind string so > /dev/null || fail "pm-cr-domain"
"$BIN/query" --uid 1001 "$LAYER/d" q2 kind exists - - | grep -q top || fail "pm-cr-build"
h0=$(pm_hetero)
(umask 066; mkdir "$LAYER/d/s") && [ "$(stat -c %a "$LAYER/d/s")" = 711 ] || fail "pm-cr-mkdir-0711"
(umask 022; : > "$LAYER/d/s/f") && "$BIN/moobfs-attr" set "$LAYER/d/s/f" kind string so > /dev/null || fail "pm-cr-file"
[ "$(stat -c %a "$LAYER/d/s/f")" = 644 ] || fail "pm-cr-file-mode"
"$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || { "$BIN/query" "$LAYER" info; fail "pm-cr-fallback (the partition was not what answered)"; }
"$BIN/query" --uid 1001 "$LAYER/d" q2 kind exists - - > /tmp/pm-cr.txt || fail "pm-cr-query"
grep -q 's/f' /tmp/pm-cr.txt && fail "pm-cr-leak (a file created under a search-only directory answered)"
[ "$(pm_hetero)" -gt "$h0" ] || fail "pm-cr-not-heterogeneous"
umount "$LAYER" || fail "pm-cr-umount"
rm -rf "$LOWER/pmx" && mkdir -m 755 "$LOWER/pmx" "$LOWER/pmx/d" && mkdir -m 711 "$LOWER/pmx/d/s" || fail "pm-mv-mkdir"
"$BIN/query-build-limit" plant6 "$LOWER/pmx/d/s" 600 || fail "pm-mv-plant"
mount -t moobfs_layer -o lower="$LOWER/pmx",domain_depth=1 none "$LAYER" || fail "pm-mv-mount"
[ -e /sys/kernel/tracing/trace ] || mount -t tracefs nodev /sys/kernel/tracing || fail "pm-mv-tracefs"
echo 100 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail "pm-mv-delay-on"
"$BIN/query-build-moved" "$LAYER/d" any; pm_rc=$?
echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail "pm-mv-delay-off"
[ "$pm_rc" -eq 0 ] || fail "pm-mv-no-move-during-build"
"$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || { "$BIN/query" "$LAYER" info; fail "pm-mv-fallback (the partition was not what answered)"; }
"$BIN/query" --uid 1001 "$LAYER/d" q2 kind exists - - > /tmp/pm-mv.txt || fail "pm-mv-query"
grep -q 't/' /tmp/pm-mv.txt && fail "pm-mv-leak (a directory moved during the build answered)"
[ "$(pm_hetero)" -ge 600 ] || fail "pm-mv-not-heterogeneous"
umount "$LAYER" || fail "pm-mv-umount"
rm -rf "$LOWER/pmx" && umount "$LOWER" || fail "pm-cleanup"
echo "PERMMATRIX_STEP_PASS: 12 seeds, partition and per-result checks, no answer the lower would refuse; $pm_wrong answers the rule before journal 0116 gave wrongly, $pm_prev the rule before 0215"
fi
if want permgaps; then
for fs in ext4 btrfs xfs; do
    case $fs in ext4) dev=$DEV_EXT4 ;; btrfs) dev=$DEV_BTRFS ;; xfs) dev=$DEV_XFS ;; esac
    mount -t "$fs" "$dev" "$LOWER" || fail "pg-lower-$fs"
    rm -rf "$LOWER/pg" && mkdir -m 755 "$LOWER/pg" || fail "pg-directory-$fs"
    "$BIN/permission-gaps" plant "$LOWER/pg" || fail "pg-plant-$fs"
    mount -t moobfs_layer -o lower="$LOWER/pg" none "$LAYER" || fail "pg-mount-$fs"
    "$BIN/permission-gaps" check "$LAYER" "$LOWER/pg" q2 || fail "pg-query2-$fs"
    umount "$LAYER" || fail "pg-unmount-$fs"
    mount -t moobfs_layer -o lower="$LOWER/pg",domain_depth=0 none "$LAYER" || fail "pg-v1-mount-$fs"
    "$BIN/permission-gaps" check "$LAYER" "$LOWER/pg" v1 || fail "pg-v1-$fs"
    umount "$LAYER" && rm -rf "$LOWER/pg" && umount "$LOWER" || fail "pg-cleanup-$fs"
    echo "PERMISSION_GAPS_PASS: $fs (T5/T6/T8, independent T10 fixtures)"
done
fi
if want rootpath; then
# Issue #110 (R2-5 of journal 0155): QUERY2 takes the name of the domain directory before the
# build records the generation of renames. tests/rootpath-race.c holds the domain's build mutex
# with a mount-wide QUERY, makes a QUERY2 wait for it, and renames the domain directory
# meanwhile; the mount-wide QUERY after that must find every attributed file under the new
# name. The files are planted on the lower and the caches dropped, so that the build is slow
# and the layer has no inode of them: the answer comes from the names the build recorded.
rp_files=${ROOTPATH_FILES:-10000}; rp_every=200
rp_expect=$(( (rp_files + rp_every - 1) / rp_every ))
for fs in ext4 btrfs xfs; do
    case $fs in ext4) dev=$DEV_EXT4 ;; btrfs) dev=$DEV_BTRFS ;; xfs) dev=$DEV_XFS ;; esac
    mount -t "$fs" "$dev" "$LOWER" || fail "rp-lower-$fs"
    rm -rf "$LOWER/rp" && mkdir -p "$LOWER/rp/d" || fail "rp-directory-$fs"
    "$BIN/rootpath-race" plant "$LOWER/rp/d" "$rp_files" "$rp_every" || fail "rp-plant-$fs"
    sync && echo 3 > /proc/sys/vm/drop_caches
    mount -t moobfs_layer -o lower="$LOWER/rp",domain_depth=1 none "$LAYER" || fail "rp-mount-$fs"
    rp_ok=0
    for attempt in 1 2 3; do
        run_logged /tmp/rp.txt "$BIN/rootpath-race" run "$LAYER" d e "$rp_expect"; rc=$?
        [ $rc -eq 0 ] && { rp_ok=1; break; }
        [ $rc -eq 3 ] || fail "rootpath-race-$fs"
        echo "rootpath: $fs attempt $attempt did not reach the interleaving"
        # the interleaving was not reached: put the name back and start from a cold mount
        umount "$LAYER" || fail "rp-unmount-$fs"
        [ -d "$LOWER/rp/e" ] && { mv "$LOWER/rp/e" "$LOWER/rp/d" || fail "rp-rename-back-$fs"; }
        sync && echo 3 > /proc/sys/vm/drop_caches
        mount -t moobfs_layer -o lower="$LOWER/rp",domain_depth=1 none "$LAYER" || fail "rp-remount-$fs"
    done
    [ $rp_ok -eq 1 ] || fail "rootpath-race-inconclusive-$fs"
    umount "$LAYER" && rm -rf "$LOWER/rp" && umount "$LOWER" || fail "rp-cleanup-$fs"
    echo "ROOTPATH_PASS: $fs"
done
fi
if want overbudget; then
# Issue #111 (R2-7 of journal 0155): a build over the budget releases the domain queried least
# recently and tries again. The flag that told it so was mount-wide and cleared by every build,
# so a build of another domain in between made it fail with ENOMEM instead. Domains x and y fit
# the budget alone, not together; tests/overbudget-race.c queries them in turn while churners
# rebuild four domains without attributes without pause. No query of x or y may fail. The race
# is not widened: the test counts over many turns.
for fs in ext4 btrfs xfs; do
    case $fs in ext4) dev=$DEV_EXT4 ;; btrfs) dev=$DEV_BTRFS ;; xfs) dev=$DEV_XFS ;; esac
    mount -t "$fs" "$dev" "$LOWER" || fail "ob-lower-$fs"
    rm -rf "$LOWER/ob" && mkdir -p "$LOWER/ob/x" "$LOWER/ob/y" || fail "ob-directory-$fs"
    "$BIN/overbudget-race" plant "$LOWER/ob/x" 650 && "$BIN/overbudget-race" plant "$LOWER/ob/y" 650 || fail "ob-plant-$fs"
    for k in 0 1 2 3; do
        # no attribute: a build of z<k> clears the old flag but charges nothing, so it never
        # releases a domain itself; the only releases are those of x and y
        mkdir -p "$LOWER/ob/z$k/s" && : > "$LOWER/ob/z$k/f" || fail "ob-small-$fs"
    done
    mount -t moobfs_layer -o lower="$LOWER/ob",domain_depth=1 none "$LAYER" || fail "ob-mount-$fs"
    ob_one=$("$BIN/overbudget-race" used "$LAYER" x) || fail "ob-measure-$fs"
    umount "$LAYER" || fail "ob-unmount-$fs"
    ob_budget=$((ob_one * 3 / 2 + 65536)); [ "$ob_budget" -ge 1048576 ] || fail "ob-domain-too-small-$fs ($ob_one)"
    mount -t moobfs_layer -o lower="$LOWER/ob",domain_depth=1,index_budget=$ob_budget none "$LAYER" || fail "ob-budget-mount-$fs"
    tr_on moobfs_domain_build moobfs_domain_release || fail "ob-trace-$fs"
    run_logged /tmp/ob.txt "$BIN/overbudget-race" run "$LAYER" 10 4; rc=$?
    tr_off moobfs_domain_build moobfs_domain_release
    if [ $rc -ne 0 ]; then
        # the builds of x and y that failed with ENOMEM, and what was released around them
        ob_x=$(stat -c %i "$LOWER/ob/x"); ob_y=$(stat -c %i "$LOWER/ob/y")
        grep -E "domain=($ob_x|$ob_y) err=-12" $TR/trace | head -5
        echo "ob: builds $(tr_count moobfs_domain_build), releases $(tr_count moobfs_domain_release)"
        grep " moobfs_domain_release: " $TR/trace | tail -5
        fail "overbudget-race-$fs"
    fi
    umount "$LAYER" && rm -rf "$LOWER/ob" && umount "$LOWER" || fail "ob-cleanup-$fs"
    echo "OVERBUDGET_PASS: $fs, one domain $ob_one bytes, budget $ob_budget"
done
fi
if want memcg; then
# The Debian reference guest enables CONFIG_MEMCG; the pinned KASAN kernel does
# not. In the latter, exercise the other suite steps but report this one as
# skipped instead of pretending that an uncharged allocation was measured.
cgroot=$(mktemp -d /tmp/moob-cgroup.XXXXXX) || fail memcg-tempdir
if ! mount -t cgroup2 none "$cgroot"; then
    [ "${MOOB_EXPECT_MEMCG:-0}" = 1 ] && fail memcg-cgroup2-mount
    rmdir "$cgroot" || fail memcg-skip-cleanup
    echo "MEMCG_SKIPPED: cgroup v2 unavailable in this guest"
elif ! grep -qw memory "$cgroot/cgroup.controllers"; then
    [ "${MOOB_EXPECT_MEMCG:-0}" = 1 ] && fail memcg-controller-unavailable
    umount "$cgroot" && rmdir "$cgroot" || fail memcg-skip-cleanup
    echo "MEMCG_SKIPPED: kernel built without the memory controller"
else
    echo +memory > "$cgroot/cgroup.subtree_control" || fail memcg-enable
    mount -t ext4 "$DEV_EXT4" "$LOWER" || fail memcg-lower-mount
    mkdir -m 755 "$LOWER/mc" "$LOWER/mc/empty" "$LOWER/mc/index" || fail memcg-domain-create
    "$BIN/overbudget-race" plant "$LOWER/mc/index" 650 || fail memcg-index-plant
    mount -t moobfs_layer -o lower="$LOWER/mc",domain_depth=1 none "$LAYER" || fail memcg-layer-mount
    cg="$cgroot/buffer-$$"
    mkdir "$cg" && [ -f "$cg/memory.current" ] || fail memcg-buffer-cgroup
    run_logged /tmp/memcg-buffer.txt "$BIN/memcg-query" buffer "$cg" "$LAYER/empty" || fail memcg-query-uncharged
    rmdir "$cg" || fail memcg-buffer-cgroup-cleanup
    cg="$cgroot/index-$$"
    mkdir "$cg" && [ -f "$cg/memory.stat" ] || fail memcg-index-cgroup
    run_logged /tmp/memcg-index.txt "$BIN/memcg-query" index "$cg" "$LAYER/index" || fail memcg-index-uncharged
    rmdir "$cg" && umount "$cgroot" && rmdir "$cgroot" || fail memcg-cgroup-cleanup
    umount "$LAYER" && rm -rf "$LOWER/mc" && umount "$LOWER" || fail memcg-layer-cleanup
    echo "MEMCG_ACCOUNT_PASS: retained QUERY2 buffer and built index charged to caller cgroup"
fi
fi
if want stall && [ ! -c /dev/fuse ]; then
    echo "STALL_SKIPPED: this kernel has no /dev/fuse (the KASAN harness has FUSE built in)"
elif want stall; then
# Journal 0116: in a domain with per-result checks (here after a chmod inside it), a FUSE mount
# over one of its directories whose daemon never answers (tests/fusestall.c) must neither hold
# the query of the domain (the check does not leave the mount) nor, through query_sem, the
# queries of other users once a rebuild queues for writing.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "st-lower-mount"
rm -rf "$LOWER/stx" && mkdir -m 755 "$LOWER/stx" || fail "st-mkdir"
mount -t moobfs_layer -o lower="$LOWER/stx",domain_depth=1 none "$LAYER" || fail "st-mount"
mkdir -p "$LAYER/D/a" "$LAYER/E" && : > "$LAYER/D/a/f" && : > "$LAYER/D/top" && : > "$LAYER/E/g" || fail "st-files"
for f in $LAYER/D/a/f $LAYER/D/top $LAYER/E/g; do "$BIN/moobfs-attr" set "$f" kind string st > /dev/null || fail "st-attr"; done
chown -R 1001:1001 "$LAYER/D" && chown -R 1002:1002 "$LAYER/E" || fail "st-chown"
[ "$("$BIN/query" --uid 1001 "$LAYER/D" q2 kind exists - - | wc -l)" -eq 2 ] || fail "st-before"
[ "$("$BIN/query" --uid 1002 "$LAYER/E" q2 kind exists - - | wc -l)" -eq 1 ] || fail "st-before-e"
chmod 0711 "$LAYER/D/a" || fail "st-chmod"   # a permission change inside D: D is checked per result
# (the chown -R above already put D and E in fallback; the chmod makes sure of D)
"$BIN/query" "$LAYER" info | grep -q 'fallback_domains=[1-9]' || { "$BIN/query" "$LAYER" info; fail "st-no-fallback"; }
rm -f /tmp/st.ready
"$BIN/fusestall" "$LAYER/D/a" /tmp/st.ready & st_pid=$!
for i in 1 2 3 4 5 6 7 8 9 10; do [ -e /tmp/st.ready ] && break; sleep 1; done
[ -e /tmp/st.ready ] || { kill $st_pid; fail "st-fuse-not-ready"; }
st_cleanup() { kill $st_pid 2>/dev/null; wait 2>/dev/null; umount -l "$LAYER/D/a" 2>/dev/null; }
# Three queries while the FUSE mount is there: (1) one of D, (2) after a directory rename in D,
# one that rebuilds D and so takes query_sem for writing, (3) one of E by another user. The
# answer of (3) is checked first: it fails if a query of D waiting on FUSE kept query_sem
# (the rebuild then waits for it and (3) behind the rebuild); (1) and (2) fail next if the
# check of a/f entered the FUSE mount at all.
( timeout 20 "$BIN/query" --uid 1001 "$LAYER/D" q2 kind exists - - > /tmp/st1.txt; echo $? > /tmp/st1.rc ) &
sleep 2
mkdir "$LAYER/D/b" && mv "$LAYER/D/b" "$LAYER/D/c" || { st_cleanup; fail "st-rename"; }
( timeout 20 "$BIN/query" --uid 1001 "$LAYER/D" q2 kind exists - - > /tmp/st2.txt; echo $? > /tmp/st2.rc ) &
sleep 2
( timeout 20 "$BIN/query" --uid 1002 "$LAYER/E" q2 kind exists - - > /tmp/st3.txt; echo $? > /tmp/st3.rc ) &
sleep 3
[ -s /tmp/st3.rc ] && [ "$(cat /tmp/st3.rc)" = 0 ] && grep -q '	g$' /tmp/st3.txt || { st_cleanup; fail "st-other-user-held-up-by-a-query-waiting-on-fuse"; }
for k in 1 2; do
    [ -s /tmp/st$k.rc ] || { st_cleanup; fail "st-domain-query-$k-waits-on-the-fuse-mount"; }
    [ "$(cat /tmp/st$k.rc)" = 0 ] && grep -q '	top$' /tmp/st$k.txt && ! grep -q 'a/f' /tmp/st$k.txt || { cat /tmp/st$k.txt; st_cleanup; fail "st-domain-answer-$k"; }
done
st_cleanup
umount "$LAYER" && rm -rf "$LOWER/stx" && umount "$LOWER" || fail "st-cleanup"
echo "STALL_PASS: a FUSE mount that never answers holds neither the checked domain nor the other users"
fi
if want bigdir; then
# Journal 0120: a build reads a directory in batches of 1024 names (MOOBL_SCAN_BATCH) instead of
# holding its whole listing. A domain whose one directory holds 3000 entries, planted on the lower
# and built by the first query, must equal the oracle: no name lost or doubled between batches.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "bd-lower-mount"
rm -rf "$LOWER/bdx" && mkdir -m 755 "$LOWER/bdx" && mkdir -p "$LOWER/bdx/big/sub" || fail "bd-mkdir"
i=0
while [ $i -lt 3000 ]; do
    f=$LOWER/bdx/big/$(printf 'memory-with-a-long-name-%04d' $i)
    : > "$f" || fail "bd-file"
    [ $((i % 6)) -eq 0 ] || "$BIN/moobfs-attr" set "$f" kind string k$((i % 3)) > /dev/null || fail "bd-attr"
    i=$((i + 1))
done
: > "$LOWER/bdx/big/sub/inner" && "$BIN/moobfs-attr" set "$LOWER/bdx/big/sub/inner" kind string k1 > /dev/null || fail "bd-inner"
mount -t moobfs_layer -o lower="$LOWER/bdx",domain_depth=1 none "$LAYER" || fail "bd-mount"
for q in "kind exists - -" "kind eq string k1"; do
    # shellcheck disable=SC2086
    $BIN/query $LAYER/big q2 $q | cut -f2 | sort > /tmp/bd.k || fail "bd-query"
    # shellcheck disable=SC2086
    $BIN/moobfs-index --domain $LOWER/bdx/big $q | cut -f2 | sort > /tmp/bd.o || fail "bd-oracle"
    cmp -s /tmp/bd.k /tmp/bd.o || { echo "kernel $(wc -l < /tmp/bd.k) oracle $(wc -l < /tmp/bd.o)"; diff /tmp/bd.k /tmp/bd.o | head -5; fail "bd-differs-from-oracle"; }
done
[ "$(wc -l < /tmp/bd.k)" -eq 1001 ] || fail "bd-count ($(wc -l < /tmp/bd.k))"
umount "$LAYER" && rm -rf "$LOWER/bdx" && umount "$LOWER" || fail "bd-cleanup"
echo "BIGDIR_PASS: a 3000-entry directory built in batches, equal to the oracle"
fi
if want buildcost || { [ -n "${MOOB_ONLY:-}" ] && want buildcostlarge; }; then
# Exploratory issue 118 probe in a disposable VM: one domain with 1000 files,
# one first build after drop_caches, then seven directory renames with a query
# over the already-built index after each rebuild.
# Counters and the build tracepoint make a missed invalidation or an accidental
# extra build visible. The opt-in large scale uses 7500 files; it never runs as
# part of the full suite. The probe reports only bounded per-ioctl observations.
bc_files=1000
if [ -n "${MOOB_ONLY:-}" ] && want buildcostlarge; then bc_files=7500; fi
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bc-lower-mount
mkdir -p "$LOWER/bc/d/s" || fail bc-directory
"$BIN/overbudget-race" plant "$LOWER/bc/d/s" "$bc_files" || fail bc-plant
sync && umount "$LOWER" || fail bc-lower-unmount-before-cold
echo 3 > /proc/sys/vm/drop_caches || fail bc-drop-caches
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bc-lower-remount
mount -t moobfs_layer -o lower="$LOWER/bc",domain_depth=1 none "$LAYER" || fail bc-layer-mount
bc_builds() { "$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p'; }
tr_on moobfs_domain_build || fail bc-trace-on
bc_before=$(bc_builds)
"$BIN/query-build-cost" "$LAYER/d" 1001 first-after-remount || fail bc-first
[ "$(bc_builds)" -eq $((bc_before + 1)) ] || fail bc-first-build-count
"$BIN/query-build-cost" "$LAYER/d" 1001 first-warm || fail bc-warm
[ "$(bc_builds)" -eq $((bc_before + 1)) ] || fail bc-warm-built-again
for bc_rep in 1 2 3 4 5 6 7; do
    if [ $((bc_rep % 2)) -eq 1 ]; then
        mv "$LAYER/d/s" "$LAYER/d/t" || fail bc-rename
    else
        mv "$LAYER/d/t" "$LAYER/d/s" || fail bc-rename
    fi
    "$BIN/query-build-cost" "$LAYER/d" 1001 "rename-$bc_rep" || fail bc-rebuild
    [ "$(bc_builds)" -eq $((bc_before + 1 + bc_rep)) ] || fail bc-rename-build-count
    "$BIN/query-build-cost" "$LAYER/d" 1001 "warm-$bc_rep" || fail bc-after-warm
    [ "$(bc_builds)" -eq $((bc_before + 1 + bc_rep)) ] || fail bc-warm-built-again
done
bc_traced=$(tr_count moobfs_domain_build)
[ "$bc_traced" -eq 8 ] || fail "bc-trace-build-count-$bc_traced"
grep ' moobfs_domain_build: ' "$TR/trace" | sed 's/^.* moobfs_domain_build: /QUERY_BUILD_TRACE /'
tr_off moobfs_domain_build
umount "$LAYER" && rm -rf "$LOWER/bc" && umount "$LOWER" || fail bc-cleanup
bc_raw_before=$(awk '{print $3}' /sys/block/vda/stat)
dd if="$DEV_EXT4" of=/dev/null bs=4096 skip=10000 count=1 >/dev/null 2>&1 || fail bc-block-counter-read
bc_raw_after=$(awk '{print $3}' /sys/block/vda/stat)
[ "$bc_raw_after" -gt "$bc_raw_before" ] || fail bc-block-counter-no-increase
echo "BUILD_COST_DEVICE_CONTROL: raw device read advanced sectors by $((bc_raw_after - bc_raw_before))"
echo "BUILD_COST_OBSERVED: files=$bc_files one first build, seven rename rebuilds, and no warm-query rebuilds"
fi
if [ -n "${MOOB_ONLY:-}" ] && want buildcostevict; then
# Two private domains belong to different users. Size the shared budget from
# one measured domain so either fits alone but not together. Under ADR-0021 uid 1002's QUERY2
# failed with ENOMEM; since ADR-0027 uid 1001's domain is over its share (half the budget) and
# uid 1002, under its own, releases it and builds. The fairshare step checks the rest.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bce-lower-mount
mkdir -p "$LOWER/bce/a/s" "$LOWER/bce/b/s" || fail bce-directories
"$BIN/overbudget-race" plant "$LOWER/bce/a/s" 1000 || fail bce-plant-a
"$BIN/overbudget-race" plant "$LOWER/bce/b/s" 1000 || fail bce-plant-b
chown 1001:1001 "$LOWER/bce/a" && chown 1002:1002 "$LOWER/bce/b" &&
    chmod 700 "$LOWER/bce/a" "$LOWER/bce/b" || fail bce-private-domains
mount -t moobfs_layer -o lower="$LOWER/bce",domain_depth=1 none "$LAYER" || fail bce-measure-mount
"$BIN/query-build-cost" "$LAYER/a" 1001 measure-a || fail bce-measure-a
bce_one=$("$BIN/query" "$LAYER" info | sed -n 's/.* used=\([0-9]*\).*/\1/p')
umount "$LAYER" || fail bce-measure-unmount
[ -n "$bce_one" ] || fail bce-missing-used-counter
bce_budget=$((bce_one * 3 / 2 + 65536))
[ "$bce_budget" -ge 1048576 ] || fail bce-budget-below-minimum
mount -t moobfs_layer -o "lower=$LOWER/bce,domain_depth=1,index_budget=$bce_budget" none "$LAYER" || fail bce-budget-mount
tr_on moobfs_domain_build moobfs_domain_release || fail bce-trace-on
bce_builds() { "$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p'; }
bce_releases() { "$BIN/query" "$LAYER" info | sed -n 's/.*domain_releases=\([0-9]*\).*/\1/p'; }
bce_before=$(bce_builds)
"$BIN/query-build-cost" "$LAYER/a" 1001 first-user || fail bce-first-user
[ "$(bce_builds)" -eq $((bce_before + 1)) ] || fail bce-first-build
"$BIN/query-build-cost" "$LAYER/b" 1002 second-user || fail bce-second-user
[ "$(bce_builds)" -eq $((bce_before + 2)) ] || fail bce-second-not-built
[ "$(bce_releases)" -eq 1 ] || fail bce-no-lend
[ "$(grep ' moobfs_domain_release: ' "$TR/trace" | grep -c ' by=budget ')" -eq 1 ] || fail bce-budget-trace
tr_off moobfs_domain_build moobfs_domain_release
umount "$LAYER" && rm -rf "$LOWER/bce" && umount "$LOWER" || fail bce-cleanup
echo "BUILD_COST_CROSS_USER_LENT: uid 1001's domain was over its share; uid 1002 released it and built; budget=$bce_budget"
fi
if want fairshare; then
# ADR-0027. On ext4, uid 1002 owns a private 2000-file domain b and uid 1001 a 600-file domain a.
# The budget is b's bytes plus half of a's: either fits alone, both do not, and the share (half
# the budget) is above a's bytes and below b's. 1002 builds b alone (one uid may use the whole
# budget); 1001's a then does not fit, 1001 holds nothing and 1002 is over its share, so 1001
# releases b and builds. 1002's next query does not fit: 1001 holds less than its share, so b is
# refused with ENOMEM and a stays built. 1001's next query does not build again.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail fs-lower-mount
mkdir -p "$LOWER/fs/a/s" "$LOWER/fs/b/s" || fail fs-directories
"$BIN/overbudget-race" plant "$LOWER/fs/a/s" 600 || fail fs-plant-a
"$BIN/overbudget-race" plant "$LOWER/fs/b/s" 2000 || fail fs-plant-b
chown -R 1001:1001 "$LOWER/fs/a" && chown -R 1002:1002 "$LOWER/fs/b" &&
    chmod 700 "$LOWER/fs/a" "$LOWER/fs/b" || fail fs-private-domains
fs_used() { "$BIN/query" "$LAYER" info | sed -n 's/.* used=\([0-9]*\).*/\1/p'; }
fs_builds() { "$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p'; }
fs_releases() { "$BIN/query" "$LAYER" info | sed -n 's/.*domain_releases=\([0-9]*\).*/\1/p'; }
mount -t moobfs_layer -o lower="$LOWER/fs",domain_depth=1 none "$LAYER" || fail fs-measure-mount
"$BIN/query-build-cost" "$LAYER/a" 1001 measure-a > /dev/null || fail fs-measure-a
fs_a=$(fs_used)
"$BIN/query-build-cost" "$LAYER/b" 1002 measure-b > /dev/null || fail fs-measure-b
fs_b=$(($(fs_used) - fs_a))
umount "$LAYER" || fail fs-measure-unmount
fs_budget=$((fs_b + fs_a / 2))
[ "$fs_budget" -ge 1048576 ] && [ $((fs_budget / 2)) -gt "$fs_a" ] && [ $((fs_budget / 2)) -lt "$fs_b" ] ||
    fail "fs-shape a=$fs_a b=$fs_b budget=$fs_budget"
mount -t moobfs_layer -o "lower=$LOWER/fs,domain_depth=1,index_budget=$fs_budget" none "$LAYER" || fail fs-budget-mount
tr_on moobfs_domain_release || fail fs-trace-on
fs_before=$(fs_builds)
"$BIN/query-build-cost" "$LAYER/b" 1002 b-alone > /dev/null || fail fs-b-alone
[ "$(fs_builds)" -eq $((fs_before + 1)) ] && [ "$(fs_releases)" -eq 0 ] || fail fs-b-alone-counters
"$BIN/query-build-cost" "$LAYER/a" 1001 a-lent > /dev/null || fail fs-a-not-lent
[ "$(fs_builds)" -eq $((fs_before + 2)) ] && [ "$(fs_releases)" -eq 1 ] || fail fs-a-lent-counters
"$BIN/query-build-cost" "$LAYER/b" 1002 b-refused expect-enomem > /dev/null || fail fs-b-not-refused
[ "$(fs_builds)" -eq $((fs_before + 2)) ] && [ "$(fs_releases)" -eq 1 ] || fail fs-a-released
"$BIN/query-build-cost" "$LAYER/a" 1001 a-kept > /dev/null || fail fs-a-again
[ "$(fs_builds)" -eq $((fs_before + 2)) ] || fail fs-a-rebuilt
[ "$(grep ' moobfs_domain_release: ' "$TR/trace" | grep -c ' by=budget ')" -eq 1 ] || fail fs-budget-trace
tr_off moobfs_domain_release
umount "$LAYER" && rm -rf "$LOWER/fs" && umount "$LOWER" || fail fs-cleanup
echo "FAIRSHARE_PASS: a=$fs_a b=$fs_b budget=$fs_budget; b alone, a lent b's room, b refused while a within its share"
fi
if [ -n "${MOOB_ONLY:-}" ] && want builddenial; then
# Compare the same cold 17,000-file QUERY2 with the pre-limit and limited
# modules. The helper accepts only a complete empty answer or E2BIG with
# untouched output, and reports process CPU/read bytes plus device sectors.
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 -N 40000 "$DEV_EXT4" || fail bdn-format
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bdn-lower-mount
mkdir -p "$LOWER/bdn/d/s" || fail bdn-directory
"$BIN/query-build-limit" plant "$LOWER/bdn/d/s" 17000 || fail bdn-plant
sync && umount "$LOWER" || fail bdn-lower-unmount-before-cold
echo 3 > /proc/sys/vm/drop_caches || fail bdn-drop-caches
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bdn-lower-remount
mount -t moobfs_layer -o lower="$LOWER/bdn",domain_depth=1 none "$LAYER" || fail bdn-layer-mount
bdn_result=$("$BIN/query-build-denial" "$LAYER/d" 1001) || fail bdn-query
echo "$bdn_result"
case "$bdn_result" in
    *"outcome=full "*) bdn_expected_builds=1 ;;
    *"outcome=denied "*) bdn_expected_builds=0 ;;
    *) fail bdn-outcome ;;
esac
[ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq "$bdn_expected_builds" ] || fail bdn-build-count
umount "$LAYER" && rm -rf "$LOWER/bdn" && umount "$LOWER" || fail bdn-cleanup
bdn_raw_before=$(awk '{print $3}' /sys/block/vda/stat)
dd if="$DEV_EXT4" of=/dev/null bs=4096 skip=10000 count=1 >/dev/null 2>&1 || fail bdn-block-counter-read
bdn_raw_after=$(awk '{print $3}' /sys/block/vda/stat)
[ "$bdn_raw_after" -gt "$bdn_raw_before" ] || fail bdn-block-counter-no-increase
echo "BUILD_DENIAL_DEVICE_CONTROL: raw device read advanced sectors by $((bdn_raw_after - bdn_raw_before))"
echo "BUILD_DENIAL_OBSERVED: 17000 empty files, one cold query, outcome=$bdn_result"
fi
if [ -n "${MOOB_ONLY:-}" ] && want buildclient; then
# Journal 0068 measured 60,000 client pages in ten similarly sized domains,
# with five or six typed attributes per file. This tests one 6,000-file,
# six-attribute domain against the proposed per-build work cap in a VM.
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 -N 20000 "$DEV_EXT4" || fail bcl-format
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bcl-lower-mount
mkdir -p "$LOWER/bcl/d/s" || fail bcl-directory
"$BIN/query-build-limit" plant6 "$LOWER/bcl/d/s" 6000 || fail bcl-plant
sync && umount "$LOWER" || fail bcl-lower-unmount-before-cold
echo 3 > /proc/sys/vm/drop_caches || fail bcl-drop-caches
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bcl-lower-remount
mount -t moobfs_layer -o lower="$LOWER/bcl",domain_depth=1 none "$LAYER" || fail bcl-layer-mount
"$BIN/query-build-limit" "$LAYER/d" 1001 ok || fail bcl-query
bcl_info=$("$BIN/query" "$LAYER" info) || fail bcl-info
echo "$bcl_info"
echo "$bcl_info" | grep -q 'domain_builds=1 ' || fail bcl-build-count
echo "$bcl_info" | grep -q 'entries=36000 inodes=6000' || fail bcl-index-counts
umount "$LAYER" && rm -rf "$LOWER/bcl" && umount "$LOWER" || fail bcl-cleanup
echo 'BUILD_CLIENT_SHAPE_PASS: 6000 files with six typed attributes, one cold QUERY2 build'
fi
if [ -n "${MOOB_ONLY:-}" ] && want buildlimit; then
# Issue 118: a too-large build is discarded, while a separate small domain
# demonstrates per-uid and mount-wide admission. This slow step is opt-in.
# The 17,000-file over-limit fixture needs more inodes than the default ext4
# format of the disposable device provides.
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 -N 40000 "$DEV_EXT4" || fail bl-format
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bl-lower-mount
mkdir -p "$LOWER/blwork/d/s" "$LOWER/blrate/d/s" || fail bl-mkdir
"$BIN/query-build-limit" plant "$LOWER/blwork/d/s" 17000 || fail bl-plant-work
"$BIN/overbudget-race" plant "$LOWER/blrate/d/s" 1 || fail bl-plant-rate
mount -t moobfs_layer -o lower="$LOWER/blwork",domain_depth=1 none "$LAYER" || fail bl-work-mount
"$BIN/query-build-limit" "$LAYER/d" 1001 work || fail bl-work-errno-or-output
[ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq 0 ] || fail bl-work-left-built-domain
umount "$LAYER" || fail bl-work-unmount
mount -t moobfs_layer -o lower="$LOWER/blrate",domain_depth=1 none "$LAYER" || fail bl-rate-mount
bl_builds() { "$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p'; }
bl_rename() {
    if [ $((bl_i % 2)) -eq 0 ]; then
        mv "$LAYER/d/s" "$LAYER/d/t"
    else
        mv "$LAYER/d/t" "$LAYER/d/s"
    fi
}
bl_i=1
while [ "$bl_i" -le 16 ]; do
    [ "$bl_i" -eq 1 ] || bl_rename || fail bl-uid-rename
    "$BIN/query-build-limit" "$LAYER/d" 1001 ok || fail bl-uid-admitted
    bl_i=$((bl_i + 1))
done
[ "$(bl_builds)" -eq 16 ] || fail bl-uid-build-count
bl_rename || fail bl-uid-limit-rename
"$BIN/query-build-limit" "$LAYER/d" 1001 quota || fail bl-uid-quota
"$BIN/query-build-limit" "$LAYER/d" 1002 ok || fail bl-other-uid-admitted
"$BIN/query-build-limit" "$LAYER/d" 1001 ok || fail bl-fresh-bypasses-admission
[ "$(bl_builds)" -eq 17 ] || fail bl-other-uid-build-count
umount "$LAYER" || fail bl-rate-unmount
mount -t moobfs_layer -o lower="$LOWER/blrate",domain_depth=1 none "$LAYER" || fail bl-mount-cap-mount
bl_i=1
while [ "$bl_i" -le 64 ]; do
    if [ "$bl_i" -gt 1 ]; then bl_rename || fail bl-mount-rename; fi
    bl_uid=$((1001 + (bl_i - 1) / 16))
    "$BIN/query-build-limit" "$LAYER/d" "$bl_uid" ok || fail bl-mount-admitted
    bl_i=$((bl_i + 1))
done
[ "$(bl_builds)" -eq 64 ] || fail bl-mount-build-count
bl_rename || fail bl-mount-quota-rename
"$BIN/query-build-limit" "$LAYER/d" 1005 quota || fail bl-mount-quota
# CAP_SYS_ADMIN is outside the admission (moobl_index_admin(), ADR-0026): root builds the
# domain the mount-wide limit refused, and a rename leaves it stale again for the recovery
[ "$("$BIN/query" "$LAYER/d" q2errno kind exists - -)" = "q2 0" ] || fail bl-admin-not-exempt
[ "$(bl_builds)" -eq 65 ] || fail bl-admin-build-count
bl_i=$((bl_i + 1))
bl_rename || fail bl-admin-rename
sleep 61
"$BIN/query-build-limit" "$LAYER/d" 1005 ok || fail bl-window-recovery
[ "$(bl_builds)" -eq 66 ] || fail bl-recovery-build-count
umount "$LAYER" && rm -rf "$LOWER/blwork" "$LOWER/blrate" &&
    umount "$LOWER" || fail bl-cleanup
for fs in btrfs xfs; do
    case $fs in btrfs) dev=$DEV_BTRFS ;; xfs) dev=$DEV_XFS ;; esac
    mount -t "$fs" "$dev" "$LOWER" || fail "bl-lower-mount-$fs"
    mkdir -p "$LOWER/blwork/d/s" || fail "bl-mkdir-$fs"
    "$BIN/query-build-limit" plant "$LOWER/blwork/d/s" 17000 || fail "bl-plant-$fs"
    mount -t moobfs_layer -o lower="$LOWER/blwork",domain_depth=1 none "$LAYER" || fail "bl-work-mount-$fs"
    "$BIN/query-build-limit" "$LAYER/d" 1001 work || fail "bl-work-errno-or-output-$fs"
    [ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq 0 ] || fail "bl-work-left-built-domain-$fs"
    umount "$LAYER" && rm -rf "$LOWER/blwork" && umount "$LOWER" || fail "bl-cleanup-$fs"
done
echo "BUILD_LIMIT_PASS: work denied on ext4, btrfs and XFS without published output; uid and mount admission recover; CAP_SYS_ADMIN outside the admission"
fi
if [ -n "${MOOB_ONLY:-}" ] && want buildwork; then
# ADR-0020: the default still denies the measured attributed domain; an
# explicit bounded mount choice admits it. Invalid choices must not mount.
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 -N 40000 "$DEV_EXT4" || fail bw-format
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bw-lower-mount
mkdir -p "$LOWER/bw/d/s" || fail bw-mkdir
"$BIN/query-build-limit" plant6 "$LOWER/bw/d/s" 17000 || fail bw-plant
for bw_value in 0 1023 65537 -1 nope; do
    if mount -t moobfs_layer -o "lower=$LOWER/bw,index_build_work=$bw_value" none "$LAYER" 2>/dev/null; then
        umount "$LAYER" || fail bw-invalid-unmount
        fail "bw-invalid-accepted-$bw_value"
    fi
done
mount -t moobfs_layer -o lower="$LOWER/bw" none "$LAYER" || fail bw-default-mount
grep " $LAYER " /proc/mounts | grep -q 'index_build_work' && fail bw-default-shown
"$BIN/query-build-limit" "$LAYER/d" 1001 work || fail bw-default-denial
umount "$LAYER" || fail bw-default-unmount
mount -t moobfs_layer -o "lower=$LOWER/bw,index_build_work=1024" none "$LAYER" || fail bw-min-mount
"$BIN/query-build-limit" "$LAYER/d" 1001 work || fail bw-min-denial
umount "$LAYER" || fail bw-min-unmount
mount -t moobfs_layer -o "lower=$LOWER/bw,index_build_work=65536" none "$LAYER" || fail bw-max-mount
grep " $LAYER " /proc/mounts | grep -q 'index_build_work=65536' || fail bw-max-not-shown
"$BIN/reconf" "$LAYER" "$LOWER/bw" 1 65536 || fail bw-remount-contract
"$BIN/query-build-limit" "$LAYER/d" 1001 ok || fail bw-max-full-answer
[ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq 1 ] || fail bw-max-build-count
umount "$LAYER" && rm -rf "$LOWER/bw" && umount "$LOWER" || fail bw-cleanup
echo 'BUILD_WORK_OPTION_PASS: invalid values refused; default and minimum denied; 65536 completed 17000 attributed files; remount contract preserved'
fi
if [ -n "${MOOB_ONLY:-}" ] && want buildtime; then
# Existing test-only scan delay makes the cooperative ten-second deadline
# reachable without relying on a particular lower device's speed.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bt-lower-mount
mkdir -p "$LOWER/bt/d/s" || fail bt-mkdir
"$BIN/overbudget-race" plant "$LOWER/bt/d/s" 60 || fail bt-plant
mount -t moobfs_layer -o lower="$LOWER/bt",domain_depth=1 none "$LAYER" || fail bt-mount
echo 200000 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail bt-delay-on
"$BIN/query-build-limit" "$LAYER/d" 1001 timeout || fail bt-timeout-errno-or-output
echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail bt-delay-off
"$BIN/query-build-limit" "$LAYER/d" 1001 ok || fail bt-recovery
[ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq 1 ] || fail bt-build-count
umount "$LAYER" && rm -rf "$LOWER/bt" && umount "$LOWER" || fail bt-cleanup
echo "BUILD_TIME_PASS: timed-out scan did not publish results and the next query rebuilt"
fi
if [ -n "${MOOB_ONLY:-}" ] && want buildmoved; then
# Function tracing synchronizes a same-domain directory rename with the
# first scan. Each of the 9,000 names fits alone; scanning them twice must
# exceed the cumulative 32,768-unit allowance.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail bm-lower-mount
mkdir -p "$LOWER/bm/d/s" || fail bm-mkdir
"$BIN/query-build-limit" plant "$LOWER/bm/d/s" 9000 || fail bm-plant
: > "$LOWER/bm/d/visible" || fail bm-visible
"$BIN/moobfs-attr" set "$LOWER/bm/d/visible" kind string note || fail bm-attr
mount -t moobfs_layer -o lower="$LOWER/bm",domain_depth=1 none "$LAYER" || fail bm-mount
[ -e /sys/kernel/tracing/trace ] || mount -t tracefs nodev /sys/kernel/tracing || fail bm-tracefs
echo 100 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail bm-delay-on
"$BIN/query-build-moved" "$LAYER/d"; bm_rc=$?
echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us || fail bm-delay-off
[ "$bm_rc" -eq 0 ] || fail bm-moved-scan-work-cap
[ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq 0 ] || fail bm-partial-build
"$BIN/query-build-limit" "$LAYER/d" 1001 ok || fail bm-rebuild-after-denial
[ "$("$BIN/query" "$LAYER" info | sed -n 's/.*domain_builds=\([0-9]*\).*/\1/p')" -eq 1 ] || fail bm-rebuild-count
umount "$LAYER" && rm -rf "$LOWER/bm" && umount "$LOWER" || fail bm-cleanup
echo "BUILD_MOVED_PASS: one allowance across original and moved-directory scans"
fi
if want xattrread; then
# The test-only knob returns one error after listxattr finds a typed attribute. A failed
# first build must return EIO and the next query must rebuild it. A failed adoption into
# a built domain must invalidate it so that the next query finds the moved file.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "xr-lower-mount"
mkdir "$LOWER/xattr-read-0150" || fail "xr-root"
mkdir "$LOWER/xattr-read-0150/source" "$LOWER/xattr-read-0150/incoming" \
      "$LOWER/xattr-read-0150/target" "$LOWER/xattr-read-0150/memory" \
      "$LOWER/xattr-read-0150/removed" || fail "xr-domains"
: > "$LOWER/xattr-read-0150/source/file" || fail "xr-file"
: > "$LOWER/xattr-read-0150/incoming/file" || fail "xr-incoming-file"
: > "$LOWER/xattr-read-0150/memory/file" || fail "xr-memory-file"
: > "$LOWER/xattr-read-0150/removed/file" || fail "xr-removed-file"
"$BIN/moobfs-attr" set "$LOWER/xattr-read-0150/source/file" kind string note || fail "xr-attr"
"$BIN/moobfs-attr" set "$LOWER/xattr-read-0150/incoming/file" kind string note || fail "xr-incoming-attr"
"$BIN/moobfs-attr" set "$LOWER/xattr-read-0150/memory/file" kind string note || fail "xr-memory-attr"
"$BIN/moobfs-attr" set "$LOWER/xattr-read-0150/removed/file" kind string note || fail "xr-removed-attr"
mount -t moobfs_layer -o lower="$LOWER/xattr-read-0150",domain_depth=1 none "$LAYER" || fail "xr-layer-mount"
knob=/sys/module/moobfs_layer/parameters/scan_getxattr_errno_once
echo 5 > "$knob" || fail "xr-arm-eio"
got=$("$BIN/query" "$LAYER/source" q2errno kind eq string note) || fail "xr-build-query"
[ "$got" = 'q2 5' ] || { echo "XR_BUILD_EIO: $got"; fail "xr-build-eio"; }
[ "$("$BIN/query" "$LAYER/source" q2 kind eq string note | cut -f2)" = file ] || fail "xr-retry"
echo 12 > "$knob" || fail "xr-arm-enomem"
[ "$("$BIN/query" "$LAYER/memory" q2errno kind eq string note)" = 'q2 12' ] || fail "xr-build-enomem"
[ "$("$BIN/query" "$LAYER/memory" q2 kind eq string note | cut -f2)" = file ] || fail "xr-enomem-retry"
[ "$("$BIN/query" "$LAYER/target" q2errno kind eq string note)" = 'q2 0' ] || fail "xr-target-build"
echo 5 > "$knob" || fail "xr-arm-adopt-eio"
mv "$LAYER/incoming/file" "$LAYER/target/link" || fail "xr-rename"
"$BIN/query" "$LAYER/target" q2 kind eq string note > /tmp/xr-adopt.txt || fail "xr-adopt-query"
[ "$(cut -f2 /tmp/xr-adopt.txt)" = link ] || { cat /tmp/xr-adopt.txt; fail "xr-adopt-rebuild"; }
# ENODATA means that the listed name disappeared before its value was fetched; it is
# permitted to skip that value. The test knob simulates that race without another writer.
echo 61 > "$knob" || fail "xr-arm-enodata"
[ "$("$BIN/query" "$LAYER/removed" q2errno kind eq string note)" = 'q2 0' ] || fail "xr-enodata"
[ "$(cat "$knob")" = 0 ] || fail "xr-disarmed"
umount "$LAYER" && umount "$LOWER" || fail "xr-unmount"
echo "XATTRREAD_PASS: EIO/ENOMEM build errors, retry, adoption rebuild and ENODATA race"
fi
if want domainrecs; then
# Issue #113 (journal 0210): a domain record and a fallback mark used to stay until unmount, so
# creating, querying and removing domain directories in a loop grew the index outside its budget.
# btrfs does not reuse inode numbers (one new record per directory before the fix); ext4 does, so
# there a new directory takes over a removed one's record and must answer with its own files.
# A QUERY2 or a chmod through a descriptor of a removed directory must not record it again.
dr_param=/sys/module/moobfs_layer/parameters/domain_records
[ -r "$dr_param" ] || fail "dr-no-counter (module without the domain_records test counter)"
dr_fallback() { "$BIN/query" "$LAYER" info | sed -n 's/.*fallback_domains=\([0-9]*\).*/\1/p'; }
for dr_fs in btrfs ext4; do
    if [ "$dr_fs" = btrfs ]; then dr_dev=$DEV_BTRFS; else dr_dev=$DEV_EXT4; fi
    mount -t "$dr_fs" "$dr_dev" "$LOWER" || fail "dr-lower-mount-$dr_fs"
    rm -rf "$LOWER/dr" && mkdir -m 755 "$LOWER/dr" || fail "dr-mkdir-$dr_fs"
    mount -t moobfs_layer -o lower="$LOWER/dr",domain_depth=1 none "$LAYER" || fail "dr-mount-$dr_fs"
    dr_base=$(cat "$dr_param")
    i=0
    while [ $i -lt 200 ]; do
        mkdir "$LAYER/d" && : > "$LAYER/d/f" || fail "dr-make-$dr_fs-$i"
        "$BIN/moobfs-attr" set "$LAYER/d/f" kind string "v$i" || fail "dr-attr-$dr_fs-$i"
        [ "$("$BIN/query" "$LAYER/d" q2 kind eq string "v$i" | cut -f2)" = f ] || fail "dr-answer-$dr_fs-$i"
        rm "$LAYER/d/f" && rmdir "$LAYER/d" || fail "dr-remove-$dr_fs-$i"
        i=$((i + 1))
    done
    dr_after=$(cat "$dr_param")
    [ $((dr_after - dr_base)) -le 1 ] || fail "dr-records-$dr_fs: $dr_base before, $dr_after after 200 domains"
    dr_fb0=$(dr_fallback)
    i=0
    while [ $i -lt 50 ]; do
        mkdir "$LAYER/c" && chmod 750 "$LAYER/c" && rmdir "$LAYER/c" || fail "dr-chmod-$dr_fs-$i"
        i=$((i + 1))
    done
    [ "$(dr_fallback)" = "$dr_fb0" ] || fail "dr-fallback-$dr_fs: $dr_fb0 before, $(dr_fallback) after 50 domains"
    mkdir "$LAYER/z" && : > "$LAYER/z/f" && "$BIN/moobfs-attr" set "$LAYER/z/f" kind string z || fail "dr-dead-make-$dr_fs"
    exec 3<"$LAYER/z"
    rm "$LAYER/z/f" && rmdir "$LAYER/z" || fail "dr-dead-remove-$dr_fs"
    dr_mid=$(cat "$dr_param")
    [ "$("$BIN/query" /proc/self/fd/3 q2errno kind eq string z)" = "q2 2" ] || fail "dr-dead-query-$dr_fs (want ENOENT)"
    chmod 700 /proc/self/fd/3 2>/dev/null
    exec 3<&-
    [ "$(cat "$dr_param")" -le "$dr_mid" ] || fail "dr-dead-record-$dr_fs"
    dr_fb1=$(dr_fallback)
    [ "$dr_fb1" = "$dr_fb0" ] || fail "dr-dead-fallback-$dr_fs"
    umount "$LAYER" && rm -rf "$LOWER/dr" && umount "$LOWER" || fail "dr-cleanup-$dr_fs"
    echo "DOMAINRECS_$dr_fs: records $dr_base -> $dr_after after 200 domains; fallback marks $dr_fb0 -> $dr_fb1 after 50 and the removed directory"
done
echo "DOMAINRECS_PASS: removed domain directories leave no record and no fallback mark; a removed directory's descriptor gets ENOENT"
fi
if want domainreuse; then
# GHSA-756j-cgwq-gvwm (journal 0216): domain records and file records are keyed by inode number.
# A tree removed on the lower, behind the layer, and new inodes given the same numbers used to be
# answered from the removed ones' entries, to their new owner. The records now also keep the
# lower inode's i_generation. ext4 and XFS reuse numbers; btrfs does not, so there the step only
# checks that nothing else changes. Three cases: a reused domain directory queried first with
# QUERY2, a reused domain directory queried first with the mount-wide v1 QUERY (the rebuild of
# domain_ensure_all), and a reused file number given a new file in another, built domain.
du_entries() { "$BIN/query" "$LAYER" info | sed -n 's/.* entries=\([0-9]*\).*/\1/p'; }
# du_free <path>: removes a tree on the lower and waits until its inodes are free. The layer's
# dentries hold the lower ones until reclaimed, and one drop_caches=2 left them allocated in a
# fresh guest (likely drop_slab() stopping before the parent of a file was unused); a few rounds
# free them. XFS then frees unlinked inodes in its background inactivation, which statfs pushes.
du_free() {
    rm -rf "$1" && sync || fail "du-remove-$du_fs"
    for k in 1 2 3 4; do echo 2 > /proc/sys/vm/drop_caches; done
    stat -f "$LOWER" > /dev/null && sleep 1
}
# du_dir <prefix> <ino>: new 0700 directories on the lower until one gets ino (the others stay);
# sets du_new, or du_new='' when the filesystem gave it to none of 64
du_dir() {
    du_new='' i=0
    while [ $i -lt 64 ]; do
        mkdir -m 700 "$LOWER/du/$1$i" || fail "du-dir-$du_fs-$1$i"
        if [ "$(stat -c %i "$LOWER/du/$1$i")" = "$2" ]; then du_new=$1$i; return; fi
        i=$((i + 1))
    done
}
# du_alice <dir> <file>: a 0700 domain of uid 1001 with one file, built by a QUERY2 of uid 1001.
# XFS frees a whole inode chunk once all its inodes are free, and gives new inodes a new chunk: a
# file made in the directory, next to it, and moved out keeps the chunk and the numbers free.
du_alice() {
    mkdir -m 700 "$LOWER/du/$1" && chown 1001:1001 "$LOWER/du/$1" || fail "du-alice-$du_fs-$1"
    : > "$LOWER/du/$1/keep" && mv "$LOWER/du/$1/keep" "$LOWER/du/keep-$1" || fail "du-keep-$du_fs-$1"
    : > "$LAYER/$1/$2" && chown 1001:1001 "$LAYER/$1/$2" && chmod 600 "$LAYER/$1/$2" &&
        "$BIN/moobfs-attr" set "$LAYER/$1/$2" kind string decision &&
        "$BIN/moobfs-attr" set "$LAYER/$1/$2" secret string s3cret || fail "du-file-$du_fs-$1"
    [ "$("$BIN/query" --uid 1001 "$LAYER/$1" q2 kind eq string decision | cut -f2)" = "$2" ] ||
        fail "du-build-$du_fs-$1"
}
for du_fs in ext4 xfs btrfs; do
    case $du_fs in ext4) du_dev=$DEV_EXT4 ;; xfs) du_dev=$DEV_XFS ;; btrfs) du_dev=$DEV_BTRFS ;; esac
    mount -t "$du_fs" "$du_dev" "$LOWER" || fail "du-lower-mount-$du_fs"
    rm -rf "$LOWER/du" && mkdir -m 755 "$LOWER/du" || fail "du-mkdir-$du_fs"
    mount -t moobfs_layer -o lower="$LOWER/du",domain_depth=1 none "$LAYER" || fail "du-mount-$du_fs"
    du_note=''
    # 1. a reused domain directory, QUERY2 first
    du_alice alice plan.md
    du_ino=$(stat -c %i "$LOWER/du/alice")
    du_free "$LOWER/du/alice"
    du_dir bob "$du_ino"
    if [ -n "$du_new" ]; then
        chown 1002:1002 "$LOWER/du/$du_new" || fail "du-chown-$du_fs"
        du_got=$("$BIN/query" --uid 1002 "$LAYER/$du_new" q2 kind eq string decision) || fail "du-query-$du_fs"
        [ -z "$du_got" ] || fail "du-stale-$du_fs: $du_new (inode $du_ino) answered $du_got"
        : > "$LAYER/$du_new/own.md" && chown 1002:1002 "$LAYER/$du_new/own.md" &&
            "$BIN/moobfs-attr" set "$LAYER/$du_new/own.md" kind string decision || fail "du-own-$du_fs"
        [ "$("$BIN/query" --uid 1002 "$LAYER/$du_new" q2 kind eq string decision | cut -f2)" = own.md ] ||
            fail "du-own-answer-$du_fs"
        du_note="QUERY2-first: inode $du_ino reused by $du_new; "
    elif [ "$du_fs" != btrfs ]; then
        fail "du-no-reuse-$du_fs (inode $du_ino not reused in 64 directories)"
    fi
    # 2. a reused domain directory, the mount-wide v1 QUERY first: it must build the new domain
    # again by itself, so the removed file's two entries leave the index
    du_alice carol notes.md
    du_ino=$(stat -c %i "$LOWER/du/carol")
    du_e0=$(du_entries)
    du_free "$LOWER/du/carol"
    du_dir dave "$du_ino"
    if [ -n "$du_new" ]; then
        chown 1002:1002 "$LOWER/du/$du_new" || fail "du-v1-chown-$du_fs"
        "$BIN/query" "$LAYER" secret eq string s3cret > /tmp/du-v1.txt || fail "du-v1-$du_fs"
        [ -s /tmp/du-v1.txt ] && fail "du-v1-stale-$du_fs: $(cat /tmp/du-v1.txt)"
        du_e1=$(du_entries)
        [ "$du_e1" -eq $((du_e0 - 2)) ] ||
            fail "du-v1-entries-$du_fs: $du_e0 entries with carol's file, $du_e1 after the v1 QUERY on $du_new"
        [ -z "$("$BIN/query" --uid 1002 "$LAYER/$du_new" q2 secret eq string s3cret)" ] ||
            fail "du-v1-q2-$du_fs"
        du_note="${du_note}v1-first: inode $du_ino reused by $du_new; "
    elif [ "$du_fs" != btrfs ]; then
        fail "du-v1-no-reuse-$du_fs (inode $du_ino not reused in 64 directories)"
    fi
    # 3. a reused file number: erin's file removed on the lower, a new file of uid 1002 in frank's
    # built domain given its number and one attribute through the layer; erin's other value must
    # not move into frank's partition
    mkdir -m 700 "$LOWER/du/frank" && chown 1002:1002 "$LOWER/du/frank" || fail "du-frank-$du_fs"
    [ -z "$("$BIN/query" --uid 1002 "$LAYER/frank" q2 kind eq string mine)" ] || fail "du-frank-build-$du_fs"
    du_alice erin secret.md
    du_fino=$(stat -c %i "$LOWER/du/erin/secret.md")
    du_free "$LOWER/du/erin"
    du_new='' i=0
    while [ $i -lt 64 ]; do
        : > "$LAYER/frank/x$i" || fail "du-x-$du_fs-$i"
        if [ "$(stat -c %i "$LAYER/frank/x$i")" = "$du_fino" ]; then du_new=x$i; break; fi
        i=$((i + 1))
    done
    if [ -n "$du_new" ]; then
        chown 1002:1002 "$LAYER/frank/$du_new" &&
            "$BIN/moobfs-attr" set "$LAYER/frank/$du_new" kind string mine || fail "du-x-attr-$du_fs"
        du_got=$("$BIN/query" --uid 1002 "$LAYER/frank" q2 secret eq string s3cret) || fail "du-x-query-$du_fs"
        [ -z "$du_got" ] || fail "du-file-stale-$du_fs: frank/$du_new (inode $du_fino) answered $du_got"
        [ "$("$BIN/query" --uid 1002 "$LAYER/frank" q2 kind eq string mine | cut -f2)" = "$du_new" ] ||
            fail "du-x-own-$du_fs"
        du_note="${du_note}file: inode $du_fino reused by frank/$du_new"
    else
        du_note="${du_note}file: inode $du_fino not reused in 64 files"
        [ "$du_fs" = ext4 ] && fail "du-file-no-reuse-ext4 (inode $du_fino not reused in 64 files)"
    fi
    umount "$LAYER" && rm -rf "$LOWER/du" && umount "$LOWER" || fail "du-cleanup-$du_fs"
    echo "DOMAINREUSE_$du_fs: $du_note"
done
echo "DOMAINREUSE_PASS: reused domain directory and file inode numbers are not answered from the removed ones' index"
fi
if want minacl; then
# Journal 0222: an access ACL is extended when it may decide otherwise than the mode bits: a
# named or mask entry, or three entries that differ from the bits (posix_acl_equiv_mode() and
# the bits). ext4 never keeps a minimal ACL written through the VFS (posix_acl_update_mode()
# drops it), so debugfs plants two on the unmounted filesystem: "same" (u=rw,g=r,o=r on 0644)
# and "other0" (u=rw,g=r,o=- on 0644: for uid 1001 the ACL refuses what the bits grant).
# "named" has a named-user entry, "plain" no ACL. The kernel must answer plain and same to uid
# 1001 and nothing it cannot read; moobfs-index --domain, moobfs-check and the Python scan
# must give the same partition.
ma_acl() {   # perm of the owner, group and other entries: version 2, then {tag, perm, id}
    # The arguments are single octal digits that complete the escapes \00N of the format.
    # shellcheck disable=SC2059
    printf "\002\000\000\000\001\000\00$1\000\377\377\377\377\004\000\00$2\000\377\377\377\377\040\000\00$3\000\377\377\377\377"
}
"$TOOLS/mke2fs" -q -F -t ext4 -b 4096 "$DEV_SCRATCH" || fail ma-mkfs
mount -t ext4 "$DEV_SCRATCH" "$LOWER" || fail ma-lower-mount
mkdir "$LOWER/d" && chmod 755 "$LOWER/d" || fail ma-mkdir
for f in plain same other0 named; do
    echo "$f" > "$LOWER/d/$f" && chmod 644 "$LOWER/d/$f" || fail "ma-file-$f"
    "$BIN/moobfs-attr" set "$LOWER/d/$f" kind string note > /dev/null || fail "ma-attr-$f"
done
"$TOOLS/setfacl" -m u:1002:r "$LOWER/d/named" || fail ma-setfacl
umount "$LOWER" || fail ma-lower-umount
ma_acl 6 4 4 > /tmp/ma-same.acl && ma_acl 6 4 0 > /tmp/ma-other0.acl || fail ma-acl-files
for f in same other0; do
    "$TOOLS/debugfs" -w -R "ea_set -f /tmp/ma-$f.acl /d/$f system.posix_acl_access" "$DEV_SCRATCH" 2> /dev/null || fail "ma-plant-$f"
    "$TOOLS/debugfs" -R "ea_list /d/$f" "$DEV_SCRATCH" 2> /dev/null | grep -q 'system.posix_acl_access (16)' || fail "ma-planted-$f"
done
mount -t ext4 "$DEV_SCRATCH" "$LOWER" || fail ma-lower-remount
[ "$(stat -c %a "$LOWER/d/same") $(stat -c %a "$LOWER/d/other0")" = "644 644" ] || fail ma-modes
[ "$("$BIN/query" --uid 1001 "$LOWER/d" open same)" = "open same: ok" ] || fail ma-same-unreadable
"$BIN/query" --uid 1001 "$LOWER/d" open other0 | grep -q 'Permission denied' || fail "ma-other0-readable (the planted ACL is not in force)"
mount -t moobfs_layer -o lower="$LOWER",domain_depth=1 none "$LAYER" || fail ma-mount
"$BIN/query" --uid 1001 "$LAYER/d" q2 kind eq string note > /tmp/ma-k.txt || fail ma-query
cat /tmp/ma-k.txt
"$BIN/query" "$LAYER" info | grep -q 'fallback_domains=0' || fail "ma-fallback (the partition was not what answered)"
"$BIN/moobfs-index" --uid 1001 "$LOWER/d" kind eq string note > /tmp/ma-r.txt || fail ma-readable
while read -r _ p; do
    grep -q "	$p$" /tmp/ma-r.txt || fail "ma-leak: $p answered to uid 1001, which cannot read it"
done < /tmp/ma-k.txt
[ "$(cut -f2 /tmp/ma-k.txt | sort | tr '\n' ' ')" = "plain same " ] || fail "ma-kernel-partition"
"$BIN/moobfs-index" --domain "$LOWER/d" kind eq string note > /tmp/ma-o.txt || fail ma-oracle
cmp -s /tmp/ma-k.txt /tmp/ma-o.txt || { echo "kernel:"; cat /tmp/ma-k.txt; echo "moobfs-index:"; cat /tmp/ma-o.txt; fail ma-oracle-mismatch; }
"$BIN/moobfs-check" --domain-depth 1 "$LOWER" > /tmp/ma.json || { cat /tmp/ma.json; fail ma-check-status; }
for f in other0 named; do
    grep -q "\"path\": \"d/$f\", \"reason\": \"has an extended access ACL\"" /tmp/ma.json || { cat /tmp/ma.json; fail "ma-check-$f"; }
done
grep -q '"path": "d/same"\|"path": "d/plain"' /tmp/ma.json && { cat /tmp/ma.json; fail ma-check-listed-a-queryable-file; }
ma_py=not-run
if command -v python3 > /dev/null 2>&1; then
    python3 -B -c 'import sys; sys.path.insert(0, sys.argv[1]); from moobfs import scan
print(" ".join(sorted(p for p, _, _ in scan.candidates(sys.argv[2]))))' "${REPO:-.}/python/src" "$LOWER/d" > /tmp/ma-py.txt || fail ma-python
    [ "$(cat /tmp/ma-py.txt)" = "plain same" ] || { cat /tmp/ma-py.txt; fail ma-python-mismatch; }
    ma_py=equal
fi
umount "$LAYER" && umount "$LOWER" || fail ma-cleanup
echo "MINACL_PASS: kernel answered plain and same, left out other0 (minimal ACL other than the mode) and named; moobfs-index and moobfs-check equal; python scan: $ma_py"
fi
if want ixlock; then
# Issue #117 (journal 0223): instantiating or evicting a layer inode took the index lock for
# writing even when the inode had no record. A build holds that lock for 2.5 s per file
# (scan_hold_us, 3 files per domain), and the measurement starts 1 s into it, so a path that
# takes the lock waits at least the 1.5 s left of the first hold (a few evictions in a row may
# then get the lock between two holds). During the build of domain s1, tests/ixlock.c
# instantiates the 8 files of domain plain, which have no record; during the build of s2 it
# evicts them with drop_caches. Neither may wait for the build, and each build must still be
# running when its measurement ends, or nothing was measured under the lock.
il_hold=/sys/module/moobfs_layer/parameters/scan_hold_us
[ -w "$il_hold" ] || fail "il-no-param (module without the scan_hold_us test parameter)"
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail il-lower-mount
rm -rf "$LOWER/il" && mkdir -p "$LOWER/il/s1" "$LOWER/il/s2" "$LOWER/il/plain" || fail il-mkdir
"$BIN/overbudget-race" plant "$LOWER/il/s1" 3 && "$BIN/overbudget-race" plant "$LOWER/il/s2" 3 || fail il-plant
i=0
while [ $i -lt 8 ]; do : > "$LOWER/il/plain/f0000$i" || fail "il-plain-$i"; i=$((i + 1)); done
mount -t moobfs_layer -o lower="$LOWER/il",domain_depth=1 none "$LAYER" || fail il-mount
echo 2500000 > "$il_hold" || fail il-hold-on
# il_under <domain> <ixlock arguments>: runs ixlock while the build of <domain> holds the lock
il_under() {
    il_d=$1; shift
    "$BIN/query" "$LAYER/$il_d" q2 kind eq string x > /tmp/il-q.txt 2>&1 &
    il_q=$!
    sleep 1
    run_logged /tmp/il.txt "$BIN/ixlock" "$@" || { echo 0 > "$il_hold"; wait $il_q; fail "il-helper-$il_d"; }
    kill -0 $il_q 2>/dev/null; il_alive=$?
    wait $il_q || { cat /tmp/il-q.txt; echo 0 > "$il_hold"; fail "il-build-query-$il_d"; }
}
il_under s1 stat "$LAYER/plain" 8
il_stat=$(sed -n 's/.*stat_ms=\([0-9]*\).*/\1/p' /tmp/il.txt); il_alive1=$il_alive
il_under s2 drop
il_drop=$(sed -n 's/.*drop_ms=\([0-9]*\).*/\1/p' /tmp/il.txt); il_alive2=$il_alive
echo 0 > "$il_hold" || fail il-hold-off
[ -n "$il_stat" ] && [ -n "$il_drop" ] || fail il-parse
[ "$il_stat" -lt 1000 ] || fail "il-instantiate-waited ($il_stat ms for 8 files without a record)"
[ "$il_drop" -lt 1000 ] || fail "il-evict-waited ($il_drop ms to evict them)"
[ $il_alive1 -eq 0 ] && [ $il_alive2 -eq 0 ] || fail "il-window (a build ended before its measurement did)"
"$BIN/query" "$LAYER" info | grep -q 'domain_builds=2' || fail il-build-count
umount "$LAYER" && rm -rf "$LOWER/il" && umount "$LOWER" || fail il-cleanup
echo "IXLOCK_PASS: 8 inodes without a record instantiated in $il_stat ms and evicted (drop_caches) in $il_drop ms while builds held the index lock"
fi
if want landlock; then
# Issue #167 (journal 0230): the moobfs CLI read /proc/self/mountinfo before every query and
# info, so a Landlock sandbox without READ_FILE on /proc stopped it although QUERY2 would have
# answered. Under tests/landlock_run.c (directory listing beneath one tree, file reads beneath a
# subdirectory and the directory of the command), as uid 1001, the CLI must give the answers it
# gives without the sandbox: QUERY2 on a domain, the mount-wide QUERY on the root of a mount
# without domains, and info on the mount root, whose build work and idle options (shown only in
# the mount record) are then unknown. The programs are copied to a directory uid 1001 can reach
# (the KASAN harness keeps BIN under the runner's home, journal 0198).
ll_bin=/tmp/ll-bin
rm -rf "$ll_bin" && mkdir -m 755 "$ll_bin" && cp "$BIN/moobfs" "$BIN/query" "$BIN/landlock_run" "$ll_bin/" || fail ll-bin
ll() { "$BIN/asuid" 1001 "$ll_bin/landlock_run" "$@"; }
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail ll-lower-mount
rm -rf "$LOWER/ll" && mkdir -p "$LOWER/ll/d1/dom/a" "$LOWER/ll/d1/dom/b" "$LOWER/ll/d0/r" || fail ll-mkdir
for f in d1/dom/a/m1 d1/dom/a/m2 d1/dom/b/m1 d0/m1 d0/r/m2; do
    echo "$f" > "$LOWER/ll/$f" && "$BIN/moobfs-attr" set "$LOWER/ll/$f" kind string note > /dev/null || fail "ll-file-$f"
done
chown -R 1001:1001 "$LOWER/ll" || fail ll-chown
mount -t moobfs_layer -o lower="$LOWER/ll/d1",domain_depth=1 none "$LAYER" || fail ll-mount
ll "$LAYER/dom" "$LAYER/dom/a" "$ll_bin/query" /proc/self open mountinfo > /tmp/ll.txt 2> /tmp/ll-e.txt; rc=$?
if [ $rc -eq 2 ] && grep -q 'landlock unavailable' /tmp/ll-e.txt; then
    umount "$LAYER" && rm -rf "$LOWER/ll" && umount "$LOWER" || fail ll-cleanup
    echo "LANDLOCK_SKIPPED: this kernel has no Landlock"
else
[ $rc -eq 3 ] && grep -q 'open mountinfo: Permission denied' /tmp/ll.txt || { cat /tmp/ll.txt /tmp/ll-e.txt; fail "ll-sandbox (mountinfo readable under landlock_run)"; }
# QUERY2 on a domain: the same answer as without the sandbox and as the oracle
"$BIN/asuid" 1001 "$ll_bin/moobfs" query --ino "$LAYER/dom" kind eq string note > /tmp/ll-o.txt || fail ll-query-free
ll "$LAYER/dom" "$LAYER/dom/a" "$ll_bin/moobfs" query --ino "$LAYER/dom" kind eq string note > /tmp/ll-k.txt 2> /tmp/ll-e.txt || { cat /tmp/ll-e.txt; fail ll-query; }
cmp -s /tmp/ll-k.txt /tmp/ll-o.txt && [ "$(wc -l < /tmp/ll-k.txt)" -eq 3 ] || { cat /tmp/ll-k.txt /tmp/ll-o.txt; fail ll-query-mismatch; }
"$BIN/moobfs-index" --domain "$LOWER/ll/d1/dom" kind eq string note > /tmp/ll-r.txt || fail ll-oracle
cmp -s /tmp/ll-k.txt /tmp/ll-r.txt || { cat /tmp/ll-k.txt /tmp/ll-r.txt; fail ll-oracle-mismatch; }
ll "$LAYER/dom" "$LAYER/dom/a" "$ll_bin/moobfs" query --json "$LAYER/dom" kind eq string note > /tmp/ll-k.json 2> /dev/null || fail ll-json
grep -q '"ioctl": "QUERY2", "complete": true, "count": 3,' /tmp/ll-k.json || { cat /tmp/ll-k.json; fail ll-json-content; }
# a directory that is not a domain and a path that is not on a layer: the same exit status
ll "$LAYER/dom" "$LAYER/dom/a" "$ll_bin/moobfs" query "$LAYER/dom/a" kind eq string note > /dev/null 2> /tmp/ll-e.txt; [ $? -eq 69 ] || { cat /tmp/ll-e.txt; fail ll-notdomain-status; }
grep -q 'domain depth is unknown' /tmp/ll-e.txt || { cat /tmp/ll-e.txt; fail ll-notdomain-message; }
ll "$LAYER/dom" "$LAYER/dom/a" "$ll_bin/moobfs" query /tmp kind eq string note > /dev/null 2> /tmp/ll-e.txt; [ $? -eq 69 ] || { cat /tmp/ll-e.txt; fail ll-nolayer-status; }
grep -q 'is not on a moobfs_layer mount' /tmp/ll-e.txt || { cat /tmp/ll-e.txt; fail ll-nolayer-message; }
# info on the mount root, and from a domain directory below it
"$BIN/asuid" 1001 "$ll_bin/moobfs" info --json "$LAYER" > /tmp/ll-o.json || fail ll-info-free
ll "$LAYER" "$LAYER/dom/a" "$ll_bin/moobfs" info --json "$LAYER" > /tmp/ll-k.json 2> /tmp/ll-e.txt || { cat /tmp/ll-e.txt; fail ll-info; }
sed 's/"index_build_work": [0-9]*, "index_idle": [0-9]*,/"index_build_work": null, "index_idle": null,/' /tmp/ll-o.json | cmp -s - /tmp/ll-k.json || { cat /tmp/ll-o.json /tmp/ll-k.json; fail ll-info-mismatch; }
ll "$LAYER" "$LAYER/dom/a" "$ll_bin/moobfs" info "$LAYER/dom" > /tmp/ll-k.txt 2> /tmp/ll-e.txt || { cat /tmp/ll-e.txt; fail ll-info-domain; }
grep -q "^mount: *$LAYER\$" /tmp/ll-k.txt && grep -q 'directories 1 level below' /tmp/ll-k.txt && grep -q 'build work: *unknown' /tmp/ll-k.txt || { cat /tmp/ll-k.txt; fail ll-info-domain-content; }
umount "$LAYER" || fail ll-umount
# no domains: the mount root still answers the mount-wide QUERY; GET_INFO gives the depth
mount -t moobfs_layer -o lower="$LOWER/ll/d0",domain_depth=0 none "$LAYER" || fail ll-mount0
"$BIN/asuid" 1001 "$ll_bin/moobfs" query --ino "$LAYER" kind eq string note > /tmp/ll-o.txt || fail ll-v1-free
ll "$LAYER" "$LAYER" "$ll_bin/moobfs" query --ino "$LAYER" kind eq string note > /tmp/ll-k.txt 2> /tmp/ll-e.txt || { cat /tmp/ll-e.txt; fail ll-v1; }
cmp -s /tmp/ll-k.txt /tmp/ll-o.txt && [ "$(wc -l < /tmp/ll-k.txt)" -eq 2 ] || { cat /tmp/ll-k.txt /tmp/ll-o.txt; fail ll-v1-mismatch; }
ll "$LAYER" "$LAYER" "$ll_bin/moobfs" query --json "$LAYER" kind eq string note 2> /dev/null | grep -q '"ioctl": "QUERY", "complete": true, "count": 2,' || fail ll-v1-ioctl
ll "$LAYER" "$LAYER" "$ll_bin/moobfs" query "$LAYER/r" kind eq string note > /dev/null 2>&1; [ $? -eq 69 ] || fail ll-v1-subdir-status
umount "$LAYER" && rm -rf "$LOWER/ll" && umount "$LOWER" || fail ll-cleanup
echo "LANDLOCK_PASS: under Landlock without /proc/self/mountinfo the moobfs CLI gave the same QUERY2 and mount-wide QUERY answers and the same info as without it"
fi
rm -rf "$ll_bin"
fi
if want pylib && ! command -v python3 > /dev/null 2>&1; then
    echo "PYLIB_SKIPPED: no python3 in this guest (the KASAN harness boots the host's root)"
elif want pylib; then
# Roadmap C5 (journal 0204): python/tests/kernel_equal.py builds a random corpus through the
# layer and compares QUERY2 and QUERY2_BATCH with the library's scan of the same directory and
# of the lower one, as root, as the domain's owner and as a team member; another uid is refused
# by both backends; a domain over index_build_work=1024 raises QueryRefused or is answered by
# the scan. -B: the repository may be read-only.
mount -t ext4 "$DEV_EXT4" "$LOWER" || fail "py-lower-mount"
rm -rf "$LOWER/py" "$LOWER/py-refusal" && mkdir -m 755 "$LOWER/py" || fail "py-mkdir"
mount -t moobfs_layer -o lower="$LOWER/py",domain_depth=1 none "$LAYER" || fail "py-mount"
python3 -B "${REPO:-.}/python/tests/kernel_equal.py" "$LAYER" "$LOWER/py" > /tmp/pylib.txt 2>&1; rc=$?
cat /tmp/pylib.txt
grep -q '^MOOBFS_PYLIB_PASS:' /tmp/pylib.txt && [ $rc -eq 0 ] || fail "pylib (python/tests/kernel_equal.py)"
umount "$LAYER" && rm -rf "$LOWER/py" "$LOWER/py-refusal" && umount "$LOWER" || fail "py-cleanup"
rmdir "$LAYER-refusal" 2>/dev/null || true
fi
rmmod moobfs_layer || fail layer-unload
"$BIN/moobfs" query /tmp kind eq string note > /dev/null 2> /tmp/e.txt; [ $? -eq 69 ] || fail cli-unloaded-status
grep -q 'module is not loaded' /tmp/e.txt || { cat /tmp/e.txt; fail cli-unloaded-message; }
# under the KASAN harness kdiag (vm/kdiag.sh) leaves out KCSAN reports with no racing access in
# the layer; the initramfs harness has no KCSAN and keeps the plain grep
if command -v kdiag > /dev/null 2>&1; then
    dmesg > /tmp/kdiag-layer.txt
    kdiag /tmp/kdiag-layer.txt 'BUG:|Oops:|^WARNING:|Kernel panic|KASAN:' || fail kernel-diagnostic-layer
elif dmesg | grep -E 'BUG:|Oops:|\] WARNING:|^WARNING:|Kernel panic|KASAN:'; then fail kernel-diagnostic-layer; fi
if [ -n "${MOOB_ONLY:-}" ]; then
    echo "MOOBFS_ONLY_PASS: steps $MOOB_ONLY (not a complete run)"
else
echo "MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey"
echo "MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; typed attributes and tools; index and QUERY equal to the oracle; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload"
fi
