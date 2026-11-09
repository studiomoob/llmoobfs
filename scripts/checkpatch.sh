#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs the kernel's checkpatch.pl (--no-tree, file mode) over the tracked sources in
# layer/ and kernel/. The script and its data files come from a fixed stable tag and are
# verified by SHA-256 before use. Required in CI since journal 0050 (the tree was brought to
# zero warnings); checkpatch.pl exits 1 on any warning or error. Needs curl and perl.
set -eu
cd "$(dirname "$0")/.."

TAG=v6.12.116
DIR=build/checkpatch-$TAG
# sha256 of each file at $TAG (same bytes from git.kernel.org and from the GitHub mirror)
SUMS='fa54defb8bc0f8ad4dd34885f6f486243d4e2f71d32d76f8768aa5376f3f188a  checkpatch.pl
edfc6c79164193e509e8f4770b1479790961eb2f040c5865d5305ae5d438c64c  spelling.txt
e7ccfd007cb45570d8a2960b9f711f3e722647585860923aadc27cc291e065e3  const_structs.checkpatch'

if command -v sha256sum >/dev/null 2>&1; then
	sha256() { sha256sum -c -; }
else
	sha256() { shasum -a 256 -c -; }
fi

mkdir -p "$DIR"
if ! (cd "$DIR" && echo "$SUMS" | sha256 >/dev/null 2>&1); then
	for f in checkpatch.pl spelling.txt const_structs.checkpatch; do
		curl -fsSL -o "$DIR/$f" \
			"https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/scripts/$f?h=$TAG" ||
		curl -fsSL -o "$DIR/$f" \
			"https://raw.githubusercontent.com/gregkh/linux/$TAG/scripts/$f"
	done
	(cd "$DIR" && echo "$SUMS" | sha256)
fi

# Tracked paths contain no spaces, so word splitting of the list is intended.
# shellcheck disable=SC2046
perl "$DIR/checkpatch.pl" --no-tree --terse --show-types --file \
	$(git ls-files 'layer/*.[ch]' 'kernel/*.[ch]' ':!layer/moobl_compat.h')
# The kernel-version keys of ADR-0034 live in this one header, in the usual
# "LINUX_VERSION_CODE >= KERNEL_VERSION(...)" form; checkpatch's advice against them (written for
# in-tree code) is waived there and only there, so no other file can key on the version.
perl "$DIR/checkpatch.pl" --no-tree --terse --show-types \
	--ignore LINUX_VERSION_CODE,CONSTANT_COMPARISON --file layer/moobl_compat.h
