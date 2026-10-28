#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Fails when a claim of docs/retired-claims.txt appears in a document that states the project's
# current state (journal 0126; AGENTS.md, "Keep every public surface in step"). History (journals,
# ADRs, dated research and reviews) is not checked.
set -eu
files="README.md README.pt-BR.md SECURITY.md CONTRIBUTING.md ROADMAP.md
docs/faq.md docs/help-wanted.md docs/testing.md docs/when-not-to-use.md docs/compatibility.md
docs/architecture-overview.md docs/development.md docs/tools.md docs/accessibility.md
docs/public-surfaces.md docs/status.md docs/status.pt-BR.md docs/operations.md
docs/container-integration.md docs/security-operations.md GOVERNANCE.md
site/*.html site/pt/*.html"
found=0
while IFS= read -r claim; do
	case "$claim" in ''|'#'*) continue ;; esac
	# shellcheck disable=SC2086
	if grep -n -i -F -- "$claim" $files 2>/dev/null | grep -v 'retired-claims' ; then
		echo "retired claim still stated: \"$claim\""
		found=1
	fi
done < docs/retired-claims.txt
[ "$found" -eq 0 ] && echo "RETIRED_CLAIMS_OK"
exit "$found"
