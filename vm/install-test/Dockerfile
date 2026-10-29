FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    qemu-system-x86 qemu-utils openssh-client python3 curl ca-certificates \
    build-essential debhelper dh-dkms ovmf python3-virt-firmware openssl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
