# SPDX-License-Identifier: GPL-2.0-only
# Image of scripts/static-analysis.sh (security roadmap S5 and S19, journal 0242). The same base
# image as vm/Dockerfile; Debian's headers of the kernel the suite runs on, for the gcc builds
# (W=1, sparse, smatch); the upstream kernel of the KASAN job configured with LLVM=1, for the
# clang analyzer and the kernel's own Coccinelle scripts. Builds on amd64 (CI) and arm64.
# KERNEL_VERSION and KERNEL_SHA256 are copies of the pin of .github/workflows/kasan.yml,
# checked by scripts/check-pins.sh.
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
ARG DEBIAN_KERNEL=6.12.113
ARG KERNEL_VERSION=6.12.112
ARG KERNEL_SHA256=164dc9d1f6c93c61a15e1f071c48379b467f2b17c469cce7223471968208ed03
ARG SMATCH_COMMIT=5189e3fb018b783a26dcb45c02c114c1d1aad290
RUN arch=$(dpkg --print-architecture) && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    "linux-headers-$DEBIAN_KERNEL+deb13-$arch=$DEBIAN_KERNEL-1" \
    sparse=0.6.4-5 coccinelle=1.3.0.deb-1+b2 libpython3.13 ocaml-nox \
    gcc make libc6-dev clang clang-tidy lld llvm flex bison bc libelf-dev libssl-dev \
    python3 perl curl ca-certificates xz-utils git libsqlite3-dev pkg-config \
    && rm -rf /var/lib/apt/lists/* && ln -s "/usr/src/linux-headers-$DEBIAN_KERNEL+deb13-$arch" /opt/kgcc
# smatch has no Debian package: built from a fixed commit of its upstream repository
RUN git init -q /opt/smatch && cd /opt/smatch && \
    git fetch -q --depth 1 https://github.com/error27/smatch.git "$SMATCH_COMMIT" && \
    git checkout -q FETCH_HEAD && [ "$(git rev-parse HEAD)" = "$SMATCH_COMMIT" ] && \
    make -j"$(nproc)" HAVE_LLVM=no smatch && echo "$SMATCH_COMMIT" > COMMIT && \
    rm -rf .git ./*.o
RUN mkdir -p /opt/linux && cd /opt && \
    curl -fsSL -o linux.tar.xz "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz" && \
    echo "$KERNEL_SHA256  linux.tar.xz" | sha256sum -c - && \
    tar -xJf linux.tar.xz -C linux --strip-components=1 && rm linux.tar.xz && \
    cd linux && make -s LLVM=1 defconfig && \
    scripts/config --disable WERROR && make -s LLVM=1 olddefconfig && \
    make -s LLVM=1 -j"$(nproc)" modules_prepare && \
    chmod a+w /opt/linux
# CONFIG_WERROR off: the x86-64 defconfig turns it on, which makes every W=1 warning (the canary's
# and the module's) an error that stops the build before the triage reads it; arm64's does not.
# (spatch run in parallel by coccicheck writes its per-job output files in the tree's top
# directory; the image runs as the caller's user)
ENV KGCC=/opt/kgcc KCLANG=/opt/linux SMATCH=/opt/smatch/smatch
WORKDIR /src
