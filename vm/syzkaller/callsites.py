#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Coverage of the handler functions of security roadmap S3, two ways (ADR-0032, journal 0248).

  callsites.py sites KO PCS [--handlers FILE] [--lines]
      KCOV call sites: every `call __sanitizer_cov_trace_pc` in the module's .text (from
      `objdump -dr`) inside a handler function, and how many of them PCS reaches. PCS lists
      offsets from the start of .text, one hexadecimal number per line, as
      vm/syzkaller/replay.py writes them. --lines adds each missed site's source line
      (`addr2line`). The last line is "CALLSITES covered/total percent".
  callsites.py funccover CSV [--handlers FILE]
      The manager's count, from its /funccover table: the handler functions' PCs with and
      without the .cold parts, and functions and PCs reached per file of layer/.

The handler set is vm/syzkaller/handlers.txt; compiler suffixes (.isra.N, .constprop.N) count
as their function.
"""
import argparse
import csv
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def handlers(path):
    with open(path) as f:
        return [l.strip() for l in f if l.strip() and not l.startswith('#')]


def base_name(sym):
    return re.sub(r'\..*', '', sym)


def call_sites(ko, want):
    """{function: [offset of each call to __sanitizer_cov_trace_pc]} in .text."""
    dis = subprocess.run(['objdump', '-dr', '--no-show-raw-insn', '-j', '.text', ko],
                         capture_output=True, text=True, check=True).stdout
    sites = {f: [] for f in want}
    func = None
    call = None
    for line in dis.splitlines():
        m = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if m:
            func = base_name(m.group(2))
            call = None
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+call', line)
        if m:
            call = int(m.group(1), 16)
            continue
        # the relocation line that follows the call names its target
        if '__sanitizer_cov_trace_pc' in line and call is not None and func in sites:
            sites[func].append(call)
            call = None
    return sites


def cmd_sites(a):
    want = handlers(a.handlers)
    sites = call_sites(a.ko, want)
    if not any(sites.values()):
        raise SystemExit('callsites: no handler KCOV call sites found in the module')
    with open(a.pcs) as f:
        pcs = {int(x, 16) for x in f.read().split()}
    tot = cov = 0
    miss = []
    for fn in want:
        ss = sites[fn]
        c = sum(1 for s in ss if s in pcs)
        tot += len(ss)
        cov += c
        print(f'{fn} {c}/{len(ss)}')
        miss += [(fn, s) for s in ss if s not in pcs]
    if a.lines and miss:
        out = subprocess.run(['addr2line', '-e', a.ko] + [hex(s) for _, s in miss],
                             capture_output=True, text=True, check=True).stdout.splitlines()
        for (fn, s), l in zip(miss, out):
            print(f'MISS {fn} {hex(s)} {l}')
    print(f'CALLSITES {cov}/{tot} {100.0 * cov / tot if tot else 0:.1f}%')


def cmd_funccover(a):
    want = set(handlers(a.handlers))
    c = t = c2 = t2 = 0
    files = {}
    with open(a.csv, newline='') as f:
        for row in csv.reader(f):
            if len(row) < 5 or row[0] != 'moobfs_layer':
                continue
            path, sym = row[1], row[2]
            hit, total = int(row[3]), int(row[4])
            name = os.path.basename(path)
            fc = files.setdefault(name, [0, 0, 0, 0])
            fc[0] += 1 if hit > 0 else 0
            fc[1] += 1
            fc[2] += hit
            fc[3] += total
            if base_name(sym) in want:
                c += hit
                t += total
                if not sym.endswith('.cold'):
                    c2 += hit
                    t2 += total
    for name, (fr, fn, fc, ft) in sorted(files.items()):
        print(f'FILE {name} functions {fr}/{fn} PCs {fc}/{ft} {100.0 * fc / ft if ft else 0:.0f}%')
    if not t2:
        raise SystemExit('callsites: no handler PCs found in the manager report')
    print(f'MANAGER_HANDLERS_ALL {c}/{t} {100.0 * c / t if t else 0:.1f}%')
    print(f'MANAGER_HANDLERS {c2}/{t2} {100.0 * c2 / t2 if t2 else 0:.1f}%')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('sites')
    s.add_argument('ko')
    s.add_argument('pcs')
    s.add_argument('--lines', action='store_true')
    f = sub.add_parser('funccover')
    f.add_argument('csv')
    for p in (s, f):
        p.add_argument('--handlers', default=os.path.join(HERE, 'handlers.txt'))
    a = ap.parse_args()
    if a.cmd == 'sites':
        cmd_sites(a)
    else:
        cmd_funccover(a)
    return 0


if __name__ == '__main__':
    sys.exit(main())
