#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# apt-get update and install, with retries and, on Ubuntu, one fallback mirror (journal 0285).
# On 2026-11-11 Canonical's nameservers for ubuntu.com stopped answering over IPv4, so
# archive.ubuntu.com and azure.archive.ubuntu.com could not be resolved from GitHub's IPv4-only
# runners. The fallback, mirrors.edge.kernel.org, is served under another domain with other
# nameservers and carries the archive and the security pocket; apt checks the archive's
# signatures (Signed-By the image's Ubuntu keyring), so the mirror is not trusted for content.
# Usage, as root: sh vm/apt-install.sh [apt-get install options] package...
# MOOB_APT_FALLBACK overrides the fallback mirror. Prints which mirror served the install.
set -eu
FALLBACK=${MOOB_APT_FALLBACK:-http://mirrors.edge.kernel.org/ubuntu}
# apt's own retries of each download, with a shorter wait on a dead connection (apt.conf(5))
cat > /etc/apt/apt.conf.d/80moob-retries <<'EOF'
Acquire::Retries "5";
Acquire::http::Timeout "20";
Acquire::https::Timeout "20";
EOF
attempt() {
	# apt-get update succeeds with a warning when an index cannot be fetched; -o makes that an error
	apt-get update -o APT::Update::Error-Mode=any &&
		DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "$@"
}
if attempt "$@"; then
	echo "apt-install: installed from the image's own sources"
	exit 0
fi
# shellcheck disable=SC1091
. /etc/os-release
if [ "${ID:-}" != ubuntu ] || [ -z "${VERSION_CODENAME:-}" ]; then
	echo "apt-install: failed, and there is no fallback mirror for ${ID:-this system}" >&2
	exit 1
fi
echo "apt-install: the image's own sources failed; retrying once from $FALLBACK (archive and security)" >&2
mkdir -p /etc/apt/moob-disabled
for f in /etc/apt/sources.list /etc/apt/sources.list.d/*; do
	[ -e "$f" ] && mv "$f" /etc/apt/moob-disabled/
done
cat > /etc/apt/sources.list.d/moob-fallback.sources <<EOF
Types: deb
URIs: $FALLBACK
Suites: $VERSION_CODENAME $VERSION_CODENAME-updates $VERSION_CODENAME-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
attempt "$@"
echo "apt-install: installed from the fallback mirror $FALLBACK"
