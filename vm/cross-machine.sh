#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Cross-machine check (journal 0280): data written on one machine, read by the modules on another.
# This script is the writing side. Run it in a privileged x86-64 Debian container (it loop-mounts
# the images it makes), from the repository root:
#   docker run --rm --privileged --platform linux/amd64 -v "$PWD:/work" -w /work \
#     debian:trixie-slim sh vm/cross-machine.sh build/cross
# It writes into OUT (refused if OUT exists):
#   lower-ext4.img, lower-btrfs.img, lower-xfs.img  lower trees with typed attributes, written by
#                                                    this machine's moobfs-attr
#   guest/queries.txt, guest/queries-v1.txt          the queries the reading side repeats (QUERY2
#                                                    per domain as uid 1001; v1 mount-wide as root)
#   guest/<fs>/<tag>.txt, guest/<fs>/v1-<tag>.txt    this machine's oracle (moobfs-index) for each
#   guest/<fs>/attrs.txt                             every file's attributes (moobfs-attr list)
#   guest/m1.img, guest/m1.sha256, m1-fsck.json      an M1 image (mkfs.moobfs), its sources' hashes
#                                                    and fsck.moobfs's report
#   SHA256SUMS                                       the hashes of everything above
# The reading side is MOOB_CROSS=OUT with MOOB_ONLY=crossmachine in vm/run.sh (vm/in-container.sh
# and the crossmachine step of vm/suite.sh). Nothing here runs in make vm-test.
set -eu
OUT=${1:?usage: vm/cross-machine.sh OUT}
[ ! -e "$OUT" ] || { echo "cross-machine: $OUT exists; not overwriting" >&2; exit 1; }
if ! command -v gcc >/dev/null; then
    apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        gcc make libc6-dev e2fsprogs btrfs-progs xfsprogs python3 >/dev/null
fi
B=$(pwd)/build/cross-writer
rm -rf "$B"
make -s tools attr-tools BUILD="$B" >/dev/null
mkdir -p "$OUT/guest"
MNT=$(mktemp -d)
trap 'umount "$MNT" 2>/dev/null || true; rmdir "$MNT"' EXIT
uname -m > "$OUT/writer-machine.txt"
"$B/moobfs" --version >> "$OUT/writer-machine.txt" 2>/dev/null || true

set_attr() { "$B/moobfs-attr" set "$@" > /dev/null; }

populate() {   # the tree under $1: the shapes of the suite's Q2b phase plus every type
    d=$1/da; mkdir -m 0755 "$d"
    long=$(head -c 100 /dev/zero | tr '\0' x)
    for i in $(seq 1 30); do
        f=$d/o$i.md; echo "o $i" > "$f"
        if [ "$i" -ge 29 ]; then t="int64"; else t="time"; fi
        set_attr "$f" created $t $(( (i % 10) * 1000 ))
        [ $((i % 2)) -eq 1 ] && set_attr "$f" kind string note
        if [ "$i" -le 10 ]; then ti="$long$((40 - i))"; else ti="t$i"; fi
        set_attr "$f" title string "$ti"
        chown 1001:1001 "$f"; chmod 0600 "$f"
    done
    chown 1001:1001 "$d"; chmod 0700 "$d"   # private to uid 1001, as the suite's domain da
    d=$1/dt; mkdir -m 0755 "$d"
    i=0
    for v in "-1e300 -9223372036854775808" "-2.5 -1" "-0.0 0" "0 1" "1e-300 255" "3.5 256" \
             "1e300 65536" "inf 4294967296" "-inf 9223372036854775807" "2.25 -256"; do
        i=$((i + 1)); f=$d/e$i.md; echo "e $i" > "$f"
        set_attr "$f" weight double "${v% *}"
        set_attr "$f" score int64 "${v#* }"
        set_attr "$f" at time "$(( i * 1000000007 - 5000000000 ))"
        set_attr "$f" ok bool $((i % 2))
        set_attr "$f" blob raw "$(printf '%02x0102030405060708' "$i")"
        set_attr "$f" name string "é-$i-ünï"
        chown 1001:1001 "$f"; chmod 0644 "$f"
    done
}

# tag|domain|kernel options|oracle options|terms; run as uid 1001 on both sides
cat > "$OUT/guest/queries.txt" <<'EOF'
note|da|||kind eq string note
asc|da|--order created time asc|--order created time asc|
desc|da|--order created time desc|--order created time desc|
int64|da|--order created int64 desc|--order created int64 desc|
top5|da|--order created time desc --limit 5|--order created time desc --limit 5|kind eq string note
pages|da|--order title string asc --limit 3 --pages|--order title string asc|
range|da|--order created time asc --pages --limit 2|--order created time asc|created ge time 7000
wasc|dt|--order weight double asc|--order weight double asc|
wdesc|dt|--order weight double desc|--order weight double desc|
wrange|dt|||weight ge double -2.5 weight lt double 1e300
wzero|dt|||weight eq double 0
sasc|dt|--order score int64 asc|--order score int64 asc|
sneg|dt|||score lt int64 0
sbig|dt|||score ge int64 256
tasc|dt|--order at time asc|--order at time asc|
tgt|dt|||at gt time 0
btrue|dt|||ok eq bool 1
sprefix|dt|||name prefix string é-1
EOF
cat > "$OUT/guest/queries-v1.txt" <<'EOF'
note|kind eq string note
created|created ge time 3000
weight|weight lt double 3.5
score|score ge int64 -1
EOF

for fs in ext4 btrfs xfs; do
    img=$OUT/lower-$fs.img
    case $fs in
        ext4) truncate -s 64M "$img"; mke2fs -q -F -t ext4 -b 4096 -L moob-lower "$img" ;;
        btrfs) truncate -s 256M "$img"; mkfs.btrfs -q -f -L moob-lower "$img" ;;
        xfs) truncate -s 300M "$img"; mkfs.xfs -q -f -L moob-lower "$img" ;;
    esac
    mount -o loop "$img" "$MNT"
    populate "$MNT"
    mkdir -p "$OUT/guest/$fs"
    while IFS="|" read -r tag dom _ oopt terms; do
        # shellcheck disable=SC2086   # options and terms are word lists
        "$B/moobfs-index" --uid 1001 --domain $oopt "$MNT/$dom" $terms > "$OUT/guest/$fs/$tag.txt"
    done < "$OUT/guest/queries.txt"
    while IFS='|' read -r tag terms; do   # v1 QUERY, mount-wide, as root, without domains
        # shellcheck disable=SC2086
        "$B/moobfs-index" "$MNT" $terms > "$OUT/guest/$fs/v1-$tag.txt"
    done < "$OUT/guest/queries-v1.txt"
    (cd "$MNT" && find da dt -type f | sort | while read -r f; do
        "$B/moobfs-attr" list "$f" | sort | awk -v f="$f" '{ print f "\t" $0 }'
    done) > "$OUT/guest/$fs/attrs.txt"
    umount "$MNT"
done

# the M1 image: five sources, two of them identical (shared extents)
src=$(mktemp -d)
for i in 1 2 3 4; do head -c $((i * 3000 + 7)) /dev/urandom > "$src/s$i"; done
cp "$src/s1" "$src/s5"
"$B/mkfs.moobfs" "$OUT/guest/m1.img" a="$src/s1" b="$src/s2" c="$src/s3" d="$src/s4" e="$src/s5"
"$B/fsck.moobfs" "$OUT/guest/m1.img" > "$OUT/m1-fsck.json"
(cd "$src" && for n in a:s1 b:s2 c:s3 d:s4 e:s5; do
    printf '%s  %s\n' "$(sha256sum "${n#*:}" | cut -d' ' -f1)" "${n%%:*}"
done) > "$OUT/guest/m1.sha256"
rm -rf "$src"
(cd "$OUT" && find . -type f ! -name SHA256SUMS | sort | xargs sha256sum) > "$OUT/SHA256SUMS"
echo "CROSS_WRITER_DONE: $(head -1 "$OUT/writer-machine.txt"), $(wc -l < "$OUT/guest/queries.txt") queries on ext4, btrfs and xfs, M1 image of 5 files"
