# SPDX-License-Identifier: GPL-2.0-only
"""Stages the lower-filesystem modules of a distribution kernel for the VM's initramfs.

    python3 vm/stage-modules.py RELEASE MODS_DIR LOOP_KO

Writes MODS_DIR/<name>.ko for ext4, btrfs, XFS, virtio_blk, dm-mod, dm-log-writes, dm-flakey and
crc32c with their modules.dep chains, in load order, and MODS_DIR/order; LOOP_KO is loop.ko.
Debian compresses modules with xz, Ubuntu with zstd (Python 3.14's compression.zstd); a module
the kernel has built in (modules.builtin, as ext4, virtio_blk, loop and dm-mod on Ubuntu's
7.0.0-43-generic, journal 0281) is skipped, and one that is neither a file nor built in is an
error. Moved out of vm/in-container.sh in journal 0281; Debian's chain is staged as before.
"""
import lzma
import sys
from pathlib import Path

base = Path('/lib/modules') / sys.argv[1]
out = Path(sys.argv[2])
loop_out = Path(sys.argv[3])
SUFFIXES = ('.ko.xz', '.ko.zst', '.ko')
builtin_file = base / 'modules.builtin'
builtin = set(builtin_file.read_text().split()) if builtin_file.exists() else set()


def unpack(path):
    data = path.read_bytes()
    if path.name.endswith('.xz'):
        return lzma.decompress(data)
    if path.name.endswith('.zst'):
        from compression import zstd  # Python 3.14
        return zstd.decompress(data)
    return data


def find(stem):
    """The module file of stem (kernel/fs/ext4/ext4), or None when it is built in."""
    for suffix in SUFFIXES:
        if (base / (stem + suffix)).exists():
            return stem + suffix
    if stem + '.ko' in builtin:
        return None
    sys.exit(f'stage-modules: {stem} is neither a module nor built into {sys.argv[1]}')


def plain(m):
    name = Path(m).name
    for suffix in SUFFIXES:
        if name.endswith(suffix):
            return name[:-len(suffix)] + '.ko'
    return name


dep = {}
for line in (base / 'modules.dep').read_text().splitlines():
    k, _, v = line.partition(':')
    dep[k.strip()] = v.split()
order = []


def visit(m):
    for d in dep.get(m, []):
        visit(d)
    if m not in order:
        order.append(m)


# Filesystems request the crc32c shash through the crypto API; modules.dep does not record that.
# Its module is crc32c_generic on 6.12 and crc32c-cryptoapi on 7.3 (journal 0275); a kernel that
# builds either in needs nothing.
crc = [find(s) for s in ('kernel/crypto/crc32c_generic', 'kernel/crypto/crc32c-cryptoapi')
       if any((base / (s + x)).exists() for x in SUFFIXES) or s + '.ko' in builtin]
if not crc:
    sys.exit(f'stage-modules: no crc32c module in {sys.argv[1]}')
wanted = [crc[0]] + [find(s) for s in (
    'kernel/drivers/block/virtio_blk', 'kernel/fs/ext4/ext4', 'kernel/fs/btrfs/btrfs',
    'kernel/fs/xfs/xfs', 'kernel/drivers/md/dm-mod', 'kernel/drivers/md/dm-log-writes',
    'kernel/drivers/md/dm-flakey')]
skipped = [m for m in wanted if m is None]
for m in wanted:
    if m is not None:
        visit(m)
with open(out / 'order', 'w') as f:
    for m in order:
        (out / plain(m)).write_bytes(unpack(base / m))
        f.write(plain(m) + '\n')
print('lower module chain:', ' '.join(Path(m).name for m in order),
      f'({len(skipped)} built in)' if skipped else '')
loop = find('kernel/drivers/block/loop')
if loop is None:
    print('loop: built in')
else:
    loop_out.write_bytes(unpack(base / loop))
