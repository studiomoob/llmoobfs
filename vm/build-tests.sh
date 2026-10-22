#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Compiles every test program that runs inside the VMs into the given directory (issue #72).
# The one list of test programs: vm/in-container.sh (make vm-test), vm/kasan-local.sh and the
# KASAN, KCSAN and arm64 workflows all call this script. A new program is added here only.
# Run from the repository root: sh vm/build-tests.sh <output dir>
set -eu
[ $# -eq 1 ] || { echo "usage: vm/build-tests.sh <output dir>" >&2; exit 64; }
out=$1
mkdir -p "$out"
cc=${CC:-gcc}
strict="-static -O2 -Wall -Wextra -Werror"
# shellcheck disable=SC2086   # $strict holds several compiler flags
t() { name=$1; shift; $cc $strict "$@" -o "$out/$name"; }

t layer      tests/layer.c
t fcntl-flags tests/fcntl-flags.c
t mounted    tests/mounted.c
t failnth    -Iinclude tests/failnth.c
t renamepath-fault -Iinclude tests/renamepath-fault.c
t deadscan   -Iinclude tests/deadscan.c
t rootpath-race -Iinclude tests/rootpath-race.c
t overbudget-race -Iinclude tests/overbudget-race.c
t memcg-query -Iinclude tests/memcg-query.c
t hostile    tests/hostile.c
t buildrace  -Iinclude tests/buildrace.c -lpthread
t chmodrace  -Iinclude tests/chmodrace.c
t query      -Iinclude tests/query.c common/attr.c
t batch-errors -Iinclude tests/batch-errors.c
t ipc        tests/ipc.c
t reconf     tests/reconf.c
t permmatrix -Iinclude tests/permmatrix.c
t permission-gaps -Iinclude tests/permission-gaps.c
t fusestall  tests/fusestall.c
t sqlq       -Iinclude -DSQLITE_CORE tests/sqlq.c sqlite/moobfs_vtab.c common/attr.c -lsqlite3 -lm -ldl -lpthread
t idmap      tests/idmap.c
# third-party programs keep their own warnings, so they are built without -Wall -Werror
$cc -static -O2 -Itests/third_party/fsx tests/third_party/fsx/fsx.c -o "$out/fsx"
$cc -static -O2 tests/third_party/log-writes/replay-log.c tests/third_party/log-writes/log-writes.c -o "$out/replay-log"
