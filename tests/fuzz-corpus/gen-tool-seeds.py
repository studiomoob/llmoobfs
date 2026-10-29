#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Writes the libFuzzer seeds of tests/fuzz_image.c and tests/fuzz_tools.c (journal 0237).

tests/fuzz-corpus/image: valid MOOBFSR1 images (include/moobfs_format.h) behind a flag byte. With
flag 1 the target writes the checksums itself, so the seeds leave them zero; trailing zero bytes
of the last block are cut because the target pads the image to a whole block.
tests/fuzz-corpus/tools: query terms (byte 0 even, words split at NUL) and POSIX ACL values
(byte 0 odd). Run from the repository root:
    python3 tests/fuzz-corpus/gen-tool-seeds.py
"""
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
BLOCK, OBJ_REC, FILE_REC = 4096, 64, 128


def align(n):
    return (n + BLOCK - 1) // BLOCK * BLOCK


def image(names_to_object, payloads):
    """names_to_object: [(name, object index)]; payloads: [bytes] (each object's data)."""
    files, objects = len(names_to_object), len(payloads)
    otab = BLOCK
    ftab = otab + align(objects * OBJ_REC)
    data = ftab + align(files * FILE_REC)
    offsets, cursor = [], data
    for p in payloads:
        offsets.append(cursor)
        cursor += align(len(p) or 1)
    img = bytearray(cursor)
    struct.pack_into('<8sIIIIQQQQ', img, 0, b'MOOBFSR1', 1, BLOCK, files, objects,
                     otab, ftab, data, cursor)
    for i, p in enumerate(payloads):
        struct.pack_into('<QQQ', img, otab + i * OBJ_REC, i + 1, offsets[i], len(p))
        img[offsets[i]:offsets[i] + len(p)] = p
    for i, (name, obj) in enumerate(names_to_object):
        n = name.encode()
        struct.pack_into('<QQI', img, ftab + i * FILE_REC, i + 2, obj + 1, len(n))
        img[ftab + i * FILE_REC + 24:ftab + i * FILE_REC + 24 + len(n)] = n
    cut = bytes(img).rstrip(b'\0')
    return cut if align(len(cut)) == len(img) else bytes(img)


def acl(mode_bits, entries, err=0xff):
    body = b'\x02\0\0\0' + b''.join(struct.pack('<HHI', t, p, i) for t, p, i in entries)
    return bytes([1, err]) + struct.pack('<H', mode_bits) + body


def terms(*words):
    return b'\0' + b'\0'.join(w.encode() for w in words)


one = image([('note.txt', 0)], [b'hello\n'])
seeds = {
    'image': {
        'one-file-fixed': b'\x01' + one,
        'one-file-raw': b'\x00' + one,
        'two-files-one-payload-fixed': b'\x01' + image([('a', 0), ('b', 0)], [b'shared\n']),
        'two-files-two-payloads-fixed': b'\x01' + image([('a', 0), ('b', 1)], [b'one\n', b'two\n']),
        'empty-payload-fixed': b'\x01' + image([('e', 0)], [b'']),
    },
    'tools': {
        't-string-eq': terms('kind', 'eq', 'string', 'decision'),
        't-int64-gt-missing-ok': terms('rank', 'gt?', 'int64', '-5'),
        't-exists': terms('tag', 'exists', '-', '-'),
        't-double-le': terms('score', 'le', 'double', '1e308'),
        't-bool-and-time': terms('ok', 'eq', 'bool', 'true', 'at', 'ge', 'time', '1700000000'),
        't-four-terms': terms(*(['a', 'prefix', 'string', 'x'] * 4)),
        't-five-terms': terms(*(['a', 'eq', 'int64', '1'] * 5)),
        't-bad-op': terms('a', 'ne', 'string', 'x'),
        't-trailing-word': terms('a', 'eq', 'string', 'x', 'extra'),
        'a-minimal-0644': acl(0o644, [(0x01, 6, 0), (0x04, 4, 0), (0x20, 4, 0)]),
        'a-named-user-mask': acl(0o640, [(0x01, 6, 0), (0x02, 4, 1000), (0x04, 4, 0),
                                         (0x10, 4, 0), (0x20, 0, 0)]),
        'a-unknown-tag': acl(0o600, [(0x40, 7, 0)]),
        'a-bad-version': bytes([1, 0xff, 0xa4, 0x01, 3, 0, 0, 0]),
        'a-enodata': bytes([1, 0, 0xa4, 0x01]),
        'a-eacces': bytes([1, 2, 0xa4, 0x01]),
    },
}

for sub, files in seeds.items():
    out = os.path.join(HERE, sub)
    os.makedirs(out, exist_ok=True)
    for name, data in files.items():
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
    print(f'{sub}: {len(files)} seeds')
