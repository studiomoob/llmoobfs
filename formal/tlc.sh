#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Runs TLC, the TLA+ model checker, from a pinned tla2tools.jar (security roadmap S21).
#
#   formal/tlc.sh fetch            download and verify the jar only
#   formal/tlc.sh run CFG          check formal/IndexLoad.tla with formal/CFG; TLC's exit code
#   formal/tlc.sh check            every configuration of formal/README.md against its
#                                  expectation: PASS ones must pass, FAIL ones must find a
#                                  violation of an invariant. Exit 1 on any mismatch.
#
# The jar is the official v1.7.4 release asset (2024-08-05, the latest release GitHub does not
# mark as prerelease on 2026-11-04). Its SHA-256 was computed from the downloaded asset on
# 2026-11-04; a jar with another hash is refused. The cache directory is $TLA_CACHE, default
# $XDG_CACHE_HOME/moobfs-tla or ~/.cache/moobfs-tla, never the repository. Nothing is
# overwritten or removed: an existing jar is only verified, a download goes to a temporary name
# and is renamed only when no jar is there.
set -eu

TLA_VERSION=v1.7.4
TLA_URL="https://github.com/tlaplus/tlaplus/releases/download/$TLA_VERSION/tla2tools.jar"
TLA_SHA256=936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88

# Configurations and what TLC must answer for each (formal/README.md, "Configurations").
TLA_PASS=${TLA_PASS:-"IndexLoad IndexLoadSafety IndexLoadNoLinks"}
TLA_FAIL=${TLA_FAIL:-"IndexLoadNoRetry IndexLoadPre0152 IndexLoadPreR22 IndexLoadPreF1 IndexLoadPreF2"}

here=$(cd "$(dirname "$0")" && pwd)
cache=${TLA_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/moobfs-tla}
jar="$cache/tla2tools-$TLA_VERSION.jar"

sha256() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

fetch() {
	mkdir -p "$cache"
	if [ ! -f "$jar" ]; then
		tmp="$jar.part.$$"
		curl -sSfL --retry 3 -o "$tmp" "$TLA_URL" || { rm -f "$tmp"; exit 1; }
		mv -n "$tmp" "$jar"
		rm -f "$tmp"	# left only if another run put a jar there first
	fi
	got=$(sha256 "$jar")
	if [ "$got" != "$TLA_SHA256" ]; then
		echo "tlc.sh: $jar has SHA-256 $got, expected $TLA_SHA256; refused (remove it to download again)" >&2
		exit 1
	fi
}

# TLC's exit codes (tlc2.output.EC.ExitStatus): 0 no error, 12 safety violation,
# 13 liveness violation, 11 deadlock, 75 and above errors of the model or of TLC itself.
run() {
	cfg=$1
	meta=$(mktemp -d "${TMPDIR:-/tmp}/moobfs-tlc.XXXXXX")
	status=0
	(cd "$here" && java -XX:+UseParallelGC -cp "$jar" tlc2.TLC -workers "${TLA_WORKERS:-auto}" \
		-metadir "$meta" -config "$cfg.cfg" IndexLoad.tla) || status=$?
	rm -rf "$meta"
	return "$status"
}

check() {
	bad=0
	for c in $TLA_PASS; do
		echo "=== $c: must find no violation"
		st=0
		run "$c" || st=$?
		if [ "$st" -eq 0 ]; then
			echo "TLA_CHECK_PASS $c"
		else
			echo "TLA_CHECK_MISMATCH $c: TLC exit $st, expected 0"
			bad=1
		fi
	done
	for c in $TLA_FAIL; do
		echo "=== $c: negative control, must violate an invariant"
		st=0
		run "$c" || st=$?
		if [ "$st" -eq 12 ]; then
			echo "TLA_CHECK_PASS $c (violation found, as required)"
		else
			echo "TLA_CHECK_MISMATCH $c: TLC exit $st, expected 12 (invariant violated)"
			bad=1
		fi
	done
	return "$bad"
}

case "${1:-}" in
fetch) fetch ;;
run) [ $# -eq 2 ] || { echo "usage: $0 run CFG" >&2; exit 2; }; fetch; run "$2" ;;
check) fetch; check ;;
*) echo "usage: $0 fetch | run CFG | check" >&2; exit 2 ;;
esac
