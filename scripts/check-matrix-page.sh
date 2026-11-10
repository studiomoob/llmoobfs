#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Keeps docs/matrix-and-limits.md from drifting (journal 0282): the releases between its
# "supported-kernels" markers must be exactly those of scripts/supported-kernels, the upstream
# releases between its "kasan-pins" markers exactly the two pins of the KASAN workflow, every
# relative link must name a file of the tree, and the page must stay short enough to print on one
# page. Run from the repository root; no network.
set -eu
page=docs/matrix-and-limits.md
max_lines=70
fail=0
bad() { echo "MATRIX PAGE: $*"; fail=1; }
block() { awk -v m="$1" '$0 == "<!-- /" m " -->" { on = 0 } on { print } $0 == "<!-- " m " -->" { on = 1 }' "$page"; }

for m in supported-kernels kasan-pins; do
	if ! grep -q "^<!-- $m -->\$" "$page" || ! grep -q "^<!-- /$m -->\$" "$page"; then
		bad "no $m markers"
	fi
done

listed=$(block supported-kernels | grep -o '`[0-9][0-9.]*+[^`]*`' | tr -d '`' | sort)
want=$(grep -v '^#' scripts/supported-kernels | grep -v '^ *$' | sort)
[ "$listed" = "$want" ] || bad "supported releases differ from scripts/supported-kernels: page [$(printf %s "$listed" | tr "\n" " ")] list [$(printf %s "$want" | tr "\n" " ")]"

yaml() { sed -n "s/^ *$1: *//p" .github/workflows/kasan.yml; }
pins=$(block kasan-pins | grep -o 'upstream [0-9][0-9.]*[0-9]' | sed 's/^upstream //' | sort)
wantpins=$(printf '%s\n%s\n' "$(yaml KERNEL_VERSION)" "$(yaml KERNEL73_VERSION)" | sort)
[ "$pins" = "$wantpins" ] || bad "KASAN pins differ from kasan.yml: page [$(printf %s "$pins" | tr "\n" " ")] workflow [$(printf %s "$wantpins" | tr "\n" " ")]"

# relative links, resolved from docs/ (fragments are not checked)
for l in $(grep -o '](\([^)]*\))' "$page" | sed 's/^](//; s/)$//; s/#.*//'); do
	case "$l" in http://*|https://*|'') continue ;; esac
	[ -e "docs/$l" ] || bad "broken link: $l"
done

n=$(wc -l < "$page")
[ "$n" -le "$max_lines" ] || bad "$n lines, more than the $max_lines that fit on one printed page"

[ "$fail" -eq 0 ] && echo "MATRIX_PAGE_OK"
exit "$fail"
