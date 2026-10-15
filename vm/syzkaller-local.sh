#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Fuzzes the layer with syzkaller (security roadmap S3, journal 0109) on a Debian host with KVM.
#
#   SYZ=<syzkaller checkout> GO=<go binary dir> sh vm/syzkaller-local.sh <seconds> [step...]
#
# Steps, all by default: kernel (the pinned kernel of the KASAN job, with
# vm/kasan.config and vm/syzkaller/kernel.config, in ~/.cache/moobfs-syz), module, image (a
# Debian image from syzkaller's tools/create-image.sh, prepared by vm/syzkaller/image-setup.sh;
# needs sudo and debootstrap), descriptions (vm/syzkaller/moobfs.txt into the checkout, constants
# extracted, syzkaller rebuilt), run (syz-manager for <seconds>, then a summary). Results in
# build/syzkaller/: manager.log, crashes/, summary.txt. Run from the repository root.
set -eu
DUR=${1:?seconds}
shift
STEPS=${*:-kernel module image descriptions run}
R=$(pwd)
SYZ=${SYZ:?syzkaller checkout}
GO=${GO:?directory of the go binary}
PATH=$GO:$PATH:/usr/sbin:/sbin; export PATH
KERNEL_VERSION=$(sed -n 's/^ *KERNEL_VERSION: *//p' .github/workflows/kasan.yml)
KERNEL_SHA256=$(sed -n 's/^ *KERNEL_SHA256: *//p' .github/workflows/kasan.yml)
C=$HOME/.cache/moobfs-syz
K=$C/linux-$KERNEL_VERSION
IMG=$C/image
B=$R/build/syzkaller
mkdir -p "$C" "$B"
want() { case " $STEPS " in *" $1 "*) return 0 ;; esac; return 1; }

if want kernel; then
    if [ ! -d "$K" ]; then
        curl -fsSL -o "$C/linux.tar.xz" "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$KERNEL_VERSION.tar.xz"
        echo "$KERNEL_SHA256  $C/linux.tar.xz" | sha256sum -c -
        mkdir -p "$K.tmp" && tar -xJf "$C/linux.tar.xz" -C "$K.tmp" --strip-components=1
        mv "$K.tmp" "$K" && rm -f "$C/linux.tar.xz"
    fi
    cat vm/kasan.config vm/syzkaller/kernel.config > "$B/fragment.config"
    if ! cmp -s "$B/fragment.config" "$K/.moobfs-syz.config" || [ ! -f "$K/Module.symvers" ]; then
        (cd "$K" && make -s defconfig && make -s kvm_guest.config &&
            scripts/kconfig/merge_config.sh -m .config "$B/fragment.config" > /dev/null &&
            make -s olddefconfig && make -s -j"$(nproc)" bzImage modules)
        cp "$B/fragment.config" "$K/.moobfs-syz.config"
    fi
    # the layer's headers where syz-extract looks for them
    cp include/moobfs_ioctl.h include/moobfs_attr.h "$K/include/uapi/linux/"
fi

if want module; then
    make layer KDIR="$K" > "$B/module.log" 2>&1
    make attr-tools BUILD="$B/tools" CFLAGS="-O2 -g -static" > "$B/tools.log" 2>&1
fi

if want image; then
    if [ ! -f "$IMG/trixie.img" ]; then
        mkdir -p "$IMG"
        (cd "$IMG" && sh "$SYZ/tools/create-image.sh" --distribution trixie > "$B/image.log" 2>&1)
    fi
    sudo sh vm/syzkaller/image-setup.sh "$IMG/trixie.img" layer/moobfs_layer.ko
fi

if want descriptions; then
    cp vm/syzkaller/moobfs.txt "$SYZ/sys/linux/moobfs.txt"
    (cd "$SYZ" && make bin/syz-extract > /dev/null &&
        bin/syz-extract -os linux -arch amd64 -sourcedir "$K" -builddir "$K" moobfs.txt &&
        make descriptions > "$B/syz-build.log" 2>&1 && make >> "$B/syz-build.log" 2>&1)
    cp "$SYZ/sys/linux/moobfs.txt.const" vm/syzkaller/moobfs.txt.const
fi

if want run; then
    # seed programs that mount the layer and reach its calls (vm/syzkaller/seeds/); with
    # KEEP_CORPUS=1 the corpus of the previous run is kept and the seeds are added to it
    rm -rf "$B/corpus.d" && mkdir -p "$B/corpus.d"
    if [ "${KEEP_CORPUS:-0}" = 1 ] && [ -f "$B/workdir/corpus.db" ]; then
        "$SYZ/bin/syz-db" unpack "$B/workdir/corpus.db" "$B/corpus.d" > /dev/null
    fi
    cp vm/syzkaller/seeds/* "$B/corpus.d/"
    rm -rf "$B/workdir" && mkdir -p "$B/workdir"
    "$SYZ/bin/syz-db" pack "$B/corpus.d" "$B/workdir/corpus.db"
    cat > "$B/manager.cfg" <<EOF
{
	"target": "linux/amd64",
	"http": "127.0.0.1:56741",
	"workdir": "$B/workdir",
	"kernel_obj": "$K",
	"module_obj": ["$R/layer"],
	"image": "$IMG/trixie.img",
	"sshkey": "$IMG/trixie.id_rsa",
	"syzkaller": "$SYZ",
	"procs": 4,
	"sandbox": "none",
	"type": "qemu",
	"vm": { "count": 3, "kernel": "$K/arch/x86/boot/bzImage", "cpu": 2, "mem": 2048 },
	"enable_syscalls": [
		"openat\$moobfs_root", "openat\$moobfs_dom", "openat\$moobfs_file",
		"mount\$moobfs_layer", "umount2\$moobfs", "ioctl\$MOOB_IOC_QUERY", "ioctl\$MOOB_IOC_GET_INFO",
		"ioctl\$MOOB_IOC_QUERY2", "ioctl\$MOOB_IOC_QUERY2_BATCH",
		"fsetxattr\$moob", "setxattr\$moob", "fremovexattr\$moob", "fgetxattr\$moob",
		"creat", "close", "read", "pread64", "write", "pwrite64", "lseek", "chdir", "fchdir",
		"mkdir", "mkdirat", "rmdir", "unlink", "unlinkat", "rename", "renameat2", "link", "linkat",
		"symlink", "readlink", "getdents64", "fstat", "newfstatat", "statx", "fchmod", "fchmodat",
		"fchown", "fchownat", "truncate", "ftruncate", "fallocate", "fsync", "fdatasync",
		"syncfs", "mmap", "munmap", "copy_file_range", "sendfile", "listxattr", "flistxattr",
		"ioctl\$FICLONE", "ioctl\$FS_IOC_GETFLAGS", "ioctl\$FS_IOC_SETFLAGS", "utimensat",
		"ioctl\$FS_IOC_FIEMAP", "statfs", "fstatfs"
	]
}
EOF
    # the manager serves coverage only while it runs: per-function coverage a minute before the end
    # (force=1: one PC of 14 587 had no module of its own in the first run, and without it the
    # page refuses to answer)
    ( sleep $((DUR > 120 ? DUR - 60 : DUR / 2)); curl -fs "http://127.0.0.1:56741/funccover?force=1" > "$B/funccover.csv" ) &
    timeout --signal=INT "$DUR" "$SYZ/bin/syz-manager" -config "$B/manager.cfg" > "$B/manager.log" 2>&1 || true
    wait
    {
        echo "duration_s=$DUR"
        grep -o 'corpus: [0-9]*\|coverage: [0-9]*\|exec total: [0-9]*\|crashes: [0-9]*' "$B/manager.log" | tail -4
        ls "$B/workdir/crashes" 2> /dev/null | wc -l | sed 's/^/crash_dirs=/'
        for d in "$B"/workdir/crashes/*/; do [ -f "$d/description" ] && sed 's/^/crash: /' "$d/description"; done
        [ -s "$B/funccover.csv" ] && grep -c 'layer/' "$B/funccover.csv" | sed 's/^/layer_functions_listed=/' 
    } > "$B/summary.txt"
    cat "$B/summary.txt"
fi
