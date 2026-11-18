#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# vm/kdiag.sh on recorded and made-up dmesg captures (journals 0304 and 0306): which KMSAN and
# KCSAN reports fail a run and which are counted as KMSAN_OUTSIDE or KCSAN_OUTSIDE. Run from the repository root: sh tests/kdiag/run.sh
set -eu
. vm/kdiag.sh
D=tests/kdiag
RE='BUG: KASAN|KASAN: |BUG:|Oops:|^WARNING:'
fails=0
expect() {   # expect <file> <0 or 1> <text the output must contain>
	if out=$(kdiag "$D/$1" "$RE"); then rc=0; else rc=1; fi
	if [ "$rc" = "$2" ] && printf '%s\n' "$out" | grep -q -F -- "$3"; then
		echo "ok: $1"
	else
		echo "FAIL: $1: rc $rc (want $2), output: $out"
		fails=$((fails + 1))
	fi
}
expect kmsan-outside.txt 0 'KMSAN_OUTSIDE: 2 report(s) with neither the use nor the memory in moobfs_layer (1 reached through it)'
expect kmsan-use-in-layer.txt 1 'BUG: KMSAN: uninit-value in moobl_query_page+0x10/0x200 [moobfs_layer] (use in moobfs_layer)'
expect kmsan-memory-from-layer.txt 1 '(memory from moobfs_layer)'
expect kmsan-local-in-layer.txt 1 '(memory from moobfs_layer)'
expect kmsan-slab-from-layer.txt 1 'BUG: KMSAN: uninit-value in btrfs_setxattr+0x1/0x100 (memory from moobfs_layer)'
expect plain-warning.txt 1 'WARNING: CPU: 0'
# issue #195, finding 4: a KCSAN report in virtio during the lowerro freeze check (an outside
# reproducer's capture); the check's kdiag filter (vm/suite.sh) must leave nothing for it to fail on
expect kcsan-lowerro-outside.txt 0 'KCSAN_OUTSIDE: 1 report(s) with no racing access in moobfs_layer (0 reached through it)'
lr_warn=$(kdiag "$D/kcsan-lowerro-outside.txt" '\] WARNING:|^WARNING:|BUG:' | grep -v -E '^(KCSAN|KMSAN)_OUTSIDE:' | head -3)
if [ -z "$lr_warn" ]; then echo "ok: lowerro filter"; else echo "FAIL: lowerro filter: $lr_warn"; fails=$((fails + 1)); fi
[ "$fails" = 0 ] && echo "KDIAG_TEST_PASS" || { echo "KDIAG_TEST_FAIL: $fails"; exit 1; }
