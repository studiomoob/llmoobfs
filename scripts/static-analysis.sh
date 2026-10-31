#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Kernel static analysis of the two modules (security roadmap S5 and S19, journal 0242): the
# W=1 build, sparse, smatch, Coccinelle (the kernel's coccicheck), the clang static analyzer
# (the kernel's clang-analyzer target) and kernel-doc, over layer/, kernel/ and the UAPI header.
# `make static` runs it in the image of tests/static/Dockerfile (tools and kernel trees pinned
# there), the same command locally and in .github/workflows/static.yml. Each finding is compared
# with tests/static/baseline.txt, where every accepted finding has its justification:
# scripts/static-triage.py fails on a finding not listed there and on a listed one no longer
# reported. A canary module (tests/static/canary) with one known defect per checker must make
# every checker report it, so a checker that stopped working cannot pass silently.
# MOOB_STATIC_TOOLS picks checkers (default: all). Output: build/static/.
set -eu
cd "$(dirname "$0")/.."
TOOLS=${MOOB_STATIC_TOOLS:-w1 sparse smatch coccinelle clang kerneldoc}

if [ "${MOOB_STATIC_IN:-}" != 1 ]; then
	docker build -t moobfs-static -f tests/static/Dockerfile tests/static
	mkdir -p build/static
	# as the caller's user, so that build/static holds no root-owned files
	exec docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e MOOB_STATIC_IN=1 \
		-e MOOB_STATIC_TOOLS="$TOOLS" -v "$PWD:/src:ro" -v "$PWD/build/static:/out" \
		moobfs-static sh /src/scripts/static-analysis.sh
fi

# Inside the image: KGCC is Debian's header tree for the kernel the suite runs on, KCLANG the
# upstream tree configured with LLVM=1 (the analyzer and coccicheck need a full tree).
: "${KGCC:?}" "${KCLANG:?}" "${SMATCH:?}"
OUT=${MOOB_STATIC_OUT:-/out}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
JOBS=$(nproc)
rm -f "$OUT"/*.log
{
	echo "arch: $(dpkg --print-architecture)"
	echo "gcc headers: $KGCC"
	echo "clang tree: $KCLANG ($(make -s -C "$KCLANG" kernelversion))"
	gcc --version | head -n 1
	clang --version | head -n 1
	sparse --version
	"$SMATCH" --version 2>&1 | head -n 1
	spatch --version | head -n 1
	echo "smatch commit: $(cat "$(dirname "$SMATCH")/COMMIT")"
} > "$OUT/versions.txt"
cat "$OUT/versions.txt"

# A fresh copy of the sources for each checker, so that no object of one build leaks into the
# next; the modules include ../include and ../common.
copy() {
	rm -rf "${W:?}/${1:?}"
	mkdir -p "$W/$1"
	cp -R layer kernel include common "$W/$1/"
	cp -R tests/static/canary "$W/$1/canary"
	find "$W/$1" \( -name '*.o' -o -name '*.ko' -o -name '*.mod' -o -name '*.mod.c' -o -name '.*.cmd' \
		-o -name Module.symvers -o -name modules.order \) -delete
}

# run <tool> <module> <make arguments...>: the build must succeed; the log keeps the findings.
# (sh has no local variables: the names differ from the loop's)
run() {
	r_log=$OUT/$1-$2.log
	echo "== $1 $2"
	r_what="$1 on $2"
	shift 2
	if ! "$@" > "$r_log" 2>&1; then
		cat "$r_log"
		echo "STATIC_FAIL: $r_what did not complete"
		exit 1
	fi
}

for tool in $TOOLS; do
	copy "$tool"
	for mod in layer kernel canary; do
		d=$W/$tool/$mod
		case $tool in
		w1) run "$tool" "$mod" make -C "$KGCC" M="$d" W=1 -j"$JOBS" modules ;;
		sparse) run "$tool" "$mod" make -C "$KGCC" M="$d" C=2 CHECK=sparse -j"$JOBS" modules ;;
		smatch) run "$tool" "$mod" make -C "$KGCC" M="$d" C=2 CHECK="$SMATCH -p=kernel" -j"$JOBS" modules ;;
		coccinelle) run "$tool" "$mod" make -C "$KCLANG" M="$d" coccicheck MODE=report J="$JOBS" ;;
		clang)
			# compile_commands.json comes from the .cmd files of a clang build, here with W=1
			# (clang's own warnings, kept as clang-w1); modpost cannot resolve the kernel's
			# symbols without vmlinux, which the analyzer does not need
			run clang-w1 "$mod" make -C "$KCLANG" LLVM=1 W=1 M="$d" KBUILD_MODPOST_WARN=1 -j"$JOBS" modules
			run "$tool" "$mod" make -C "$KCLANG" LLVM=1 M="$d" clang-analyzer
			;;
		kerneldoc)
			case $mod in
			layer) set -- "$d"/*.c "$d"/*.h ;;
			kernel) set -- "$d"/*.c "$W/$tool"/include/*.h ;;
			canary) set -- "$d"/*.c ;;
			esac
			run "$tool" "$mod" "$KCLANG/scripts/kernel-doc" -Wall -none "$@"
			;;
		*) echo "unknown tool: $tool"; exit 2 ;;
		esac
	done
done

# shellcheck disable=SC2086 # one argument per checker
python3 scripts/static-triage.py --work "$W" --kernel "$KCLANG" --out "$OUT" \
	--baseline tests/static/baseline.txt --expected tests/static/canary-expected.txt $TOOLS
