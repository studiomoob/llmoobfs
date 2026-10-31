#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Triage of the logs of scripts/static-analysis.sh (security roadmap S5 and S19, journal 0242).

Each finding becomes a key "tool|path|message|source": the path relative to the repository (or
"linux:" and the path in the kernel tree), the message with line numbers replaced by N, and the
text of the reported source line, so that a key survives edits that only move lines. A finding
reported inside a kernel header is attached to the last note of its trace that is in the
repository ("via path: source"), so that one baseline entry for a list helper does not hide a
new finding elsewhere in the layer.

Fails (exit 1) when a finding of layer/ or kernel/ is not in the baseline, when a baseline entry
is no longer reported by a checker that ran, or when a checker reports none of its expected
canary findings. Every finding is written to <out>/findings.txt, the input for a baseline update.
"""
import argparse
import os
import platform
import re
import sys

WARN = re.compile(r'^(?P<path>[^\s:]+):(?P<line>\d+):(?:\d+:)? (?P<sev>warning|error): (?P<msg>.*)$')
NOTE = re.compile(r'^(?P<path>[^\s:]+):(?P<line>\d+):(?:\d+:)? note: ')
SMATCH = re.compile(r'^(?P<path>[^\s:]+):(?P<line>\d+) (?P<func>\S+)\(\) (?P<sev>warn|error): (?P<msg>.*)$')
COCCI = re.compile(r'^(?P<path>[^\s:]+):(?P<line>\d+):\d+-\d+: (?P<msg>.*)$')
MODULES = {'layer': 'layer', 'kernel': 'kernel', 'include': 'include', 'common': 'common',
           'canary': 'tests/static/canary'}


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--work', required=True, help='directory with one source copy per checker')
    ap.add_argument('--kernel', required=True, help='kernel tree the "./" paths are relative to')
    ap.add_argument('--out', required=True, help='directory of the <tool>-<module>.log files')
    ap.add_argument('--baseline', required=True)
    ap.add_argument('--expected', required=True)
    ap.add_argument('tools', nargs='+')
    return ap.parse_args()


def read_entries(path):
    """Entries of a baseline-like file; each block of entries must follow a comment."""
    entries, commented, problems = [], False, []
    with open(path, encoding='utf-8') as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip('\n')
            if not line.strip():
                commented = False
            elif line.startswith('#'):
                commented = True
            elif not commented:
                problems.append(f'{path}:{n}: entry without a justification comment above it')
            else:
                entries.append(line)
    return entries, problems


class Locator:
    def __init__(self, work, kernel, tool):
        self.root = os.path.join(work, tool)
        self.kernel = kernel

    def place(self, path):
        """(display path, file to read, in repository) for a path printed by a checker."""
        if path.startswith(self.root + '/'):
            rel = path[len(self.root) + 1:]
            top, _, rest = rel.partition('/')
            return MODULES.get(top, top) + '/' + rest, path, top in MODULES
        if path.startswith('./'):
            rel = path[2:]
            return 'linux:' + rel, os.path.join(self.kernel, rel), False
        if path.startswith('/'):
            m = re.match(r'^/usr/src/linux-headers-[^/]+/(.*)$', path) or \
                re.match(r'^' + re.escape(self.kernel) + r'/(.*)$', path)
            return 'linux:' + (m.group(1) if m else path), path, False
        return path, path, False

    @staticmethod
    def source(fname, line):
        try:
            with open(fname, encoding='utf-8', errors='replace') as f:
                for n, text in enumerate(f, 1):
                    if n == line:
                        return ' '.join(text.split())
        except OSError:
            pass
        return '?'


def norm(msg):
    msg = re.sub(r'\bline \d+', 'line N', msg.strip())
    return msg.replace('|', '/')


def findings(tool, log, loc):
    """Keys of one log, each with the module it came from."""
    out = []
    with open(log, encoding='utf-8', errors='replace') as f:
        lines = [x.rstrip('\n') for x in f]
    for i, line in enumerate(lines):
        m = SMATCH.match(line) if tool == 'smatch' else None
        if not m and tool == 'coccinelle':
            m = COCCI.match(line)
            if m and m.group('msg').startswith('warning: line'):
                m = None	# a remark about the .cocci script itself, not a finding
        if not m:
            m = WARN.match(line)
        if not m:
            continue
        shown, fname, mine = loc.place(m.group('path'))
        src = loc.source(fname, int(m.group('line')))
        if not mine:
            # attach a finding inside a kernel header to the last repository frame of its trace
            via = None
            for nxt in lines[i + 1:]:
                if WARN.match(nxt):
                    break
                n = NOTE.match(nxt)
                if n:
                    s2, f2, m2 = loc.place(n.group('path'))
                    if m2:
                        via = f'{s2}: {loc.source(f2, int(n.group("line")))}'
            if via:
                src = f'{src} via {via}'
                mine = True
        if not mine:
            continue	# a header warning with no frame of ours: the kernel's, not the module's
        if tool in ('sparse', 'smatch') and re.search(r'\[-W[^]]*\]$', m.group('msg')):
            continue	# the compiler's own warning in a C=2 build, kept under w1 already
        out.append(f'{tool}|{shown}|{norm(m.group("msg"))}|{src}')
    return out


def main():
    a = parse_args()
    baseline, problems = read_entries(a.baseline)
    # an entry prefixed '@ARCH ' (amd64 or arm64) is reported on that architecture only: it is
    # accepted on every architecture, and stale only where it is expected
    arch = {'x86_64': 'amd64', 'aarch64': 'arm64'}.get(platform.machine(), platform.machine())
    only = {}
    for i, e in enumerate(baseline):
        m = re.match(r'@(\S+) (.*)$', e)
        if m:
            baseline[i] = m.group(2)
            only[m.group(2)] = m.group(1)
    expected, p2 = read_entries(a.expected)
    problems += p2
    seen, canary = set(), {}
    ran = set()
    for checker in a.tools:
        loc = Locator(a.work, a.kernel, checker)
        # the clang checker's W=1 build is kept as a checker of its own, clang-w1
        for tool in [checker] + (['clang-w1'] if checker == 'clang' else []):
            ran.add(tool)
            canary[tool] = set()
            for mod in ('layer', 'kernel', 'canary'):
                log = os.path.join(a.out, f'{tool}-{mod}.log')
                if not os.path.exists(log):
                    problems.append(f'missing log {log}')
                    continue
                for key in findings(tool, log, loc):
                    if key.split('|')[1].startswith('tests/static/canary/'):
                        canary[tool].add(key)
                    else:
                        seen.add(key)
    with open(os.path.join(a.out, 'findings.txt'), 'w', encoding='utf-8') as f:
        for key in sorted(seen) + sorted(k for t in canary.values() for k in t):
            f.write(key + '\n')
    new = sorted(seen - set(baseline))
    stale = sorted(k for k in set(baseline) - seen
                   if k.split('|')[0] in ran and only.get(k, arch) == arch)
    missing = []
    for e in expected:
        tool, _, needle = e.partition('|')
        if tool in ran and not any(needle in k for k in canary.get(tool, ())):
            missing.append(e)
    for tool in sorted(ran):
        n = sum(1 for k in seen if k.startswith(tool + '|'))
        print(f'STATIC_TOOL {tool}: findings={n} canary={len(canary.get(tool, ()))}')
    for k in new:
        print(f'NEW (fix it, or justify it in {a.baseline}): {k}')
    for k in stale:
        print(f'STALE (no longer reported; remove it from {a.baseline}): {k}')
    for k in missing:
        print(f'CANARY_MISSED (the checker did not report its known defect): {k}')
    for p in problems:
        print(f'PROBLEM: {p}')
    if new or stale or missing or problems:
        print('STATIC_FAIL')
        return 1
    print(f'STATIC_OK tools={len(ran)} findings={len(seen)}, all in the baseline')
    return 0


if __name__ == '__main__':
    sys.exit(main())
