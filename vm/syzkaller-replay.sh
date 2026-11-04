#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Replays syzkaller programs one at a time with per-call KCOV in disposable guests and counts
# the KCOV call sites of the S3 handler functions they reach (ADR-0032, journal 0248).
#
#   SYZ=<syzkaller checkout> sh vm/syzkaller-replay.sh <out dir> <corpus.db or directory>...
#
# Uses what vm/syzkaller-local.sh built: the kernel, the image (with this tree's module, from
# its image step) and the checkout's syz-execprog. Every corpus.db is unpacked and every
# directory copied into <out>/progs, so a corpus and the seeds can be replayed together. Each
# guest runs each program once (syz-execprog -cover, sandbox none, one proc, no threads) and the
# module's .text PCs are mapped to the `call __sanitizer_cov_trace_pc` sites of
# vm/syzkaller/handlers.txt (vm/syzkaller/callsites.py). Results per program are kept in a cache
# as they come (<out>/cache unless REPLAY_CACHE names another), so a stopped replay resumes and
# a grown corpus replays only its new programs.
#
# REPLAY_GUESTS lists host CPUs per guest ("4,5 6,7": two guests of 2 vCPUs pinned there);
# REPLAY_DEADLINE (epoch seconds) stops starting programs after that time; REPLAY_PORT is
# the first host port forwarded to the guests' SSH (default 10100, one more per guest), so two
# campaigns can share a host; SYZ_CACHE as for vm/syzkaller-local.sh. Results:
# <out>/replay.txt (counts), <out>/callsites.txt (per function, missed sites with their source
# line, last line "CALLSITES covered/total percent"). Exit status 0 when every program was
# replayed. Run from the repository root on the test host.
set -eu
OUT=${1:?out dir}
shift
[ $# -ge 1 ] || { echo "syzkaller-replay: no corpus.db or directory of programs" >&2; exit 1; }
[ -f vm/syzkaller/replay.py ] || { echo "syzkaller-replay: run from the repository root" >&2; exit 1; }
R=$(pwd)
SYZ=${SYZ:?syzkaller checkout}
KERNEL_VERSION=$(sed -n 's/^ *KERNEL_VERSION: *//p' .github/workflows/kasan.yml)
C=${SYZ_CACHE:-$HOME/.cache/moobfs-syz}
K=$C/linux-$KERNEL_VERSION
IMG=$C/image
KO=$R/layer/moobfs_layer.ko
for f in "$K/arch/x86/boot/bzImage" "$IMG/trixie.img" "$KO" "$SYZ/bin/syz-db" "$SYZ/bin/linux_amd64/syz-execprog"; do
    [ -f "$f" ] || { echo "syzkaller-replay: $f missing; run vm/syzkaller-local.sh's build steps" >&2; exit 1; }
done
mkdir -p "$OUT"
rm -rf "$OUT/progs" && mkdir -p "$OUT/progs"
for src in "$@"; do
    if [ -d "$src" ]; then
        cp "$src"/* "$OUT/progs/"
    else
        "$SYZ/bin/syz-db" unpack "$src" "$OUT/progs" > /dev/null
    fi
done
rc=0
python3 vm/syzkaller/replay.py --kernel "$K/arch/x86/boot/bzImage" --image "$IMG/trixie.img" \
    --key "$IMG/trixie.id_rsa" --bin "$SYZ/bin/linux_amd64" --ko "$KO" --progs "$OUT/progs" \
    --out "$OUT" --cache "${REPLAY_CACHE:-$OUT/cache}" --guests "${REPLAY_GUESTS:-}" \
    --deadline "${REPLAY_DEADLINE:-0}" --port "${REPLAY_PORT:-10100}" || rc=$?
python3 vm/syzkaller/callsites.py sites "$KO" "$OUT/union.txt" --lines > "$OUT/callsites.txt"
tail -n 1 "$OUT/callsites.txt"
exit "$rc"
