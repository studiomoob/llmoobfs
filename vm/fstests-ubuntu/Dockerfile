# SPDX-License-Identifier: GPL-2.0-only
# Image of vm/fstests-ubuntu.sh (journal 0281): Ubuntu 26.04 with its 7.0.0-43-generic kernel,
# modules and headers, virtme-ng and fstests' build dependencies (build context: vm/), so that vm/fstests-local.sh can
# boot Ubuntu's own kernel (MOOB_KVER) on a host that runs another distribution. The guest's root
# is this image's root. fstests itself is built at run time from the checkout vm/fstests-ubuntu.sh
# is given. Same base digest and kernel pins as vm/run.sh and vm/Dockerfile.
FROM ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    linux-image-7.0.0-43-generic=7.0.0-43.43 linux-modules-7.0.0-43-generic=7.0.0-43.43 \
    linux-headers-7.0.0-43-generic=7.0.0-43.43 linux-headers-7.0.0-43=7.0.0-43.43 \
    busybox-static qemu-system-x86 kmod udev ca-certificates git make gcc libc6-dev \
    automake libtool libtool-bin gawk indent libacl1-dev libaio-dev libattr1-dev libcap-dev \
    libgdbm-dev liburing-dev uuid-dev uuid-runtime xfslibs-dev attr acl bc dbench dump quota \
    psmisc fio e2fsprogs btrfs-progs xfsprogs python3 perl util-linux sudo zstd \
    && rm -rf /var/lib/apt/lists/*
# fstests' configure needs pkg-config's macros (PKG_CHECK_MODULES), which the Debian test host had
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    autoconf pkgconf python3-venv && rm -rf /var/lib/apt/lists/*
# zstd: virtme-ng unpacks Ubuntu's .ko.zst modules for its initramfs with the zstd command.
# virtme-ng 1.42 by hash, as the KASAN harness; Ubuntu's 1.40 refuses --rwdir on a bind mount
# ("path must be defined inside a valid overlay")
COPY kasan-requirements.txt /tmp/kasan-requirements.txt
RUN python3 -m venv /opt/vng && /opt/vng/bin/pip install -q --require-hashes -r /tmp/kasan-requirements.txt
ENV PATH=/opt/vng/bin:$PATH
# fstests' expected outputs are those of GNU coreutils, and Ubuntu 26.04 ships the Rust ones by
# default (messages end in "(os error 1)", touch cannot parse dates past 2038): generic/193, 597
# and 634 failed on them. iproute2 brings up lo, which the lock tests' client and server use
# (generic/131, 571). Journal 0281.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    --allow-remove-essential coreutils-from-gnu coreutils-from-uutils- iproute2 \
    && rm -rf /var/lib/apt/lists/* \
    && ln --version | grep -q "GNU coreutils"
WORKDIR /work
