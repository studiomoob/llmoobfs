# SPDX-License-Identifier: GPL-2.0-only
# Image of vm/fstests-ubuntu.sh (journal 0281): Ubuntu 26.04 with its 7.0.0-43-generic kernel,
# modules and headers, virtme-ng and fstests v2026.09.22 built, so that vm/fstests-local.sh can
# boot Ubuntu's own kernel (MOOB_KVER) on a host that runs another distribution. The guest's root
# is this image's root. Same base digest and kernel pins as vm/run.sh and vm/Dockerfile.
FROM ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    linux-image-7.0.0-43-generic=7.0.0-43.43 linux-modules-7.0.0-43-generic=7.0.0-43.43 \
    linux-headers-7.0.0-43-generic=7.0.0-43.43 linux-headers-7.0.0-43=7.0.0-43.43 \
    virtme-ng busybox-static qemu-system-x86 kmod udev ca-certificates git make gcc libc6-dev \
    automake libtool libtool-bin gawk indent libacl1-dev libaio-dev libattr1-dev libcap-dev \
    libgdbm-dev liburing-dev uuid-dev uuid-runtime xfslibs-dev attr acl bc dbench dump quota \
    psmisc fio e2fsprogs btrfs-progs xfsprogs python3 perl util-linux sudo \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 --branch v2026.09.22 https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git /fstests \
    && make -C /fstests -j4 >/dev/null
WORKDIR /work
