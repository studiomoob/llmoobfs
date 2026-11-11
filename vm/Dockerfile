# vm/run.sh passes MOOB_BASE: Debian 13 for the 6.12 kernels, sid for 7.3.3 (journal 0275)
ARG MOOB_BASE=debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
FROM $MOOB_BASE
ARG MOOB_DEBIAN_KERNEL=6.12.111
# MOOB_ARCH=s390x: Debian's s390x build of 6.12.111 and QEMU's s390x system emulator, for the
# exploratory big-endian run of journal 0280. The amd64 default installs what it always did.
ARG MOOB_ARCH=amd64
# MOOB_UBUNTU_KERNEL=7.0.0-43: Ubuntu 26.04's generic kernel, its modules (btrfs, XFS and dm-* are
# in linux-modules, not -extra) and headers, on the ubuntu:26.04 base that vm/run.sh passes
# (journal 0281). Python 3.14 there unpacks the zstd modules.
ARG MOOB_UBUNTU_KERNEL=
RUN QEMU=qemu-system-x86 && \
    if [ "$MOOB_ARCH" = s390x ]; then MOOB_DEBIAN_KERNEL="s390x:$MOOB_DEBIAN_KERNEL" QEMU=qemu-system-s390x; fi && \
    if [ -n "$MOOB_UBUNTU_KERNEL" ]; then MOOB_DEBIAN_KERNEL="ubuntu:$MOOB_UBUNTU_KERNEL"; fi && \
    case "$MOOB_DEBIAN_KERNEL" in \
      6.12.111) set -- linux-image-6.12.111+deb13-amd64=6.12.111-1 \
        linux-headers-6.12.111+deb13-amd64=6.12.111-1 ;; \
      6.12.113) set -- linux-image-6.12.113+deb13-amd64=6.12.113-1 \
        linux-headers-6.12.113+deb13-amd64=6.12.113-1 ;; \
      6.12.115) set -- linux-image-6.12.115+deb13.1-amd64=6.12.115-2 \
        linux-headers-6.12.115+deb13.1-amd64=6.12.115-2 ;; \
      7.3.3) set -- linux-image-7.3.3+deb14-amd64=7.3.3-1 \
        linux-headers-7.3.3+deb14-amd64=7.3.3-1 ;; \
      ubuntu:7.0.0-43) set -- linux-image-7.0.0-43-generic=7.0.0-43.43 \
        linux-modules-7.0.0-43-generic=7.0.0-43.43 \
        linux-headers-7.0.0-43-generic=7.0.0-43.43 linux-headers-7.0.0-43=7.0.0-43.43 ;; \
      s390x:6.12.111) set -- linux-image-6.12.111+deb13-s390x=6.12.111-1 \
        linux-headers-6.12.111+deb13-s390x=6.12.111-1 ;; \
      *) echo "Unsupported Debian VM kernel: $MOOB_DEBIAN_KERNEL" >&2; exit 1 ;; \
    esac && \
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    "$@" gcc make libc6-dev \
    busybox-static cpio kmod "$QEMU" python3 ca-certificates e2fsprogs btrfs-progs xfsprogs git sqlite3 libsqlite3-dev dmsetup acl \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
