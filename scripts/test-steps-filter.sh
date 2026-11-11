#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Finding 0211-1 (journal 0211; test added in journal 0283): the manual KCSAN and arm64 KASAN
# workflows put their `steps` input inside quotes of the guest's command line, so they first
# refuse a value with anything but lower-case letters, digits and spaces. This runs the filter
# line exactly as each workflow has it, under three locales, with names that must pass and
# values that must not. Run from the repository root: sh scripts/test-steps-filter.sh
# The patterns and values below hold a literal $MOOB_ONLY, $(id) and backquotes on purpose.
# shellcheck disable=SC2016
set -eu
eacute=$(printf '\303\251')   # a lower-case letter outside ASCII
fail() { echo "STEPS_FILTER_FAIL: $*"; exit 1; }
for wf in .github/workflows/kcsan.yml .github/workflows/kasan-arm64.yml; do
    n=$(grep -c 'case "\$MOOB_ONLY" in \*\[!' "$wf" || true)
    [ "$n" -eq 1 ] || fail "$wf: expected one filter line, found $n"
    line=$(grep 'case "\$MOOB_ONLY" in \*\[!' "$wf" | sed 's/^ *//')
    for loc in C en_US.UTF-8 pt_BR.UTF-8; do
        for ok in ext4 'buildrace release' 'xfs ioerror faults' fallbackfault; do
            LC_ALL=$loc MOOB_ONLY=$ok sh -c "$line" 2>/dev/null || fail "$wf ($loc) refused '$ok'"
        done
        for bad in "ext4' ; id #" 'ext4$(id)' 'ext4;id' 'Ext4' "ext4$eacute" 'a-b' 'ext4`id`' "ext4
id" 'a|b' 'a&b' 'a>b' 'ext4*' '../x' 'a"b'; do
            if LC_ALL=$loc MOOB_ONLY=$bad sh -c "$line" 2>/dev/null; then
                fail "$wf ($loc) accepted '$bad'"
            fi
        done
    done
done
echo "STEPS_FILTER_PASS: both workflows refuse 14 hostile values and accept 4 step lists under C, en_US.UTF-8 and pt_BR.UTF-8"
