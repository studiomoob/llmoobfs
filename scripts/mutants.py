#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Mutation testing of the homogeneity rule (security roadmap S14, journal 0242).

  mutants.py list                    the mutants of tests/mutation/homogeneity.mut
  mutants.py apply ID DIR            apply one mutant to the tree copy DIR (its layer/)
  mutants.py check                   inside the image of `make static`: every mutant applies to
                                     exactly one place, keeps the line count, builds against
                                     the kernel headers, and changes the object code (a mutant
                                     whose object equals the original's is equivalent: fail)
  mutants.py run [ID...]             each mutant in a disposable VM (`make vm-test` with its
                                     MOOB_ONLY steps) from a copy of HEAD; a mutant is killed
                                     when the suite fails with MOOBFS_VM_FAIL after the module
                                     loaded. Writes build/mutation/report.txt; exit 1 if one
                                     survives. Needs Docker and, in practice, KVM.

`make mutants-check` runs `check` in the static-analysis image; `run` is for the test host.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CATALOGUE = os.path.join(ROOT, 'tests', 'mutation', 'homogeneity.mut')


class Mutant:
    def __init__(self, ident, title):
        self.ident, self.title = ident, title
        self.file, self.steps, self.old, self.new = 'layer/index.c', [], [], []

    def apply(self, tree):
        path = os.path.join(tree, self.file)
        with open(path, encoding='utf-8') as f:
            text = f.read()
        old, new = '\n'.join(self.old) + '\n', '\n'.join(self.new) + '\n'
        count = text.count(old)
        if count != 1:
            raise ValueError(f'{self.ident}: the "-" lines occur {count} times in {self.file}')
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text.replace(old, new))


def load(path=CATALOGUE):
    mutants, cur = [], None
    with open(path, encoding='utf-8') as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip('\n')
            where = f'{path}:{n}'
            if line.startswith('#') or not line.strip():
                continue
            if line.startswith('@ '):
                m = re.match(r'^@ (M\d+) (.+)$', line)
                if not m:
                    raise ValueError(f'{where}: expected "@ Mnn function: text"')
                cur = Mutant(m.group(1), m.group(2))
                mutants.append(cur)
            elif cur is None:
                raise ValueError(f'{where}: text before the first mutant')
            elif line.startswith('steps: '):
                cur.steps = line[len('steps: '):].split()
            elif line.startswith('file: '):
                cur.file = line[len('file: '):].strip()
            elif line.startswith('- '):
                cur.old.append(line[2:])
            elif line.startswith('+ '):
                cur.new.append(line[2:])
            else:
                raise ValueError(f'{where}: unexpected line')
    ids = [m.ident for m in mutants]
    for m in mutants:
        if ids.count(m.ident) != 1:
            raise ValueError(f'{m.ident}: duplicate id')
        if not m.old or len(m.old) != len(m.new):
            raise ValueError(f'{m.ident}: needs "-" lines and as many "+" lines')
        if m.old == m.new:
            raise ValueError(f'{m.ident}: the replacement is the original')
        if not m.steps:
            raise ValueError(f'{m.ident}: no steps line')
    return mutants


def copy_sources(dst):
    for d in ('layer', 'include', 'common'):
        shutil.copytree(os.path.join(ROOT, d), os.path.join(dst, d),
                        ignore=shutil.ignore_patterns('*.o', '*.ko', '*.mod', '*.mod.c', '.*.cmd',
                                                      'Module.symvers', 'modules.order'))


def build_object(tree, kdir, name):
    """Builds the module in tree and returns the debug-stripped bytes of layer/<name>."""
    layer = os.path.join(tree, 'layer')
    r = subprocess.run(['make', '-C', kdir, f'M={layer}', f'-j{os.cpu_count() or 1}',
                        f'KCFLAGS=-ffile-prefix-map={tree}/=', 'modules'],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
    if r.returncode:
        return None, r.stdout
    obj = os.path.join(layer, name)
    stripped = obj + '.stripped'
    subprocess.run(['objcopy', '--strip-debug', obj, stripped], check=True)
    with open(stripped, 'rb') as f:
        return f.read(), r.stdout


def check(mutants):
    kdir = os.environ.get('KGCC')
    if not kdir:
        print('check runs inside the image of make static (KGCC unset): make mutants-check')
        return 2
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        # one fixed path for the original and every mutant, so the objects differ only where
        # the code does (file names are kept in the objects by WARN_ON)
        tree = os.path.join(tmp, 'm')
        copy_sources(tree)
        orig = {}
        for m in mutants:
            name = os.path.basename(m.file)[:-2] + '.o'
            if name not in orig:
                orig[name], log = build_object(tree, kdir, name)
                if orig[name] is None:
                    print(log)
                    print(f'MUTANTS_FAIL: the unmutated {m.file} does not build')
                    return 1
        for m in mutants:
            src = os.path.join(tree, m.file)
            pristine = os.path.join(ROOT, m.file)
            shutil.copyfile(pristine, src)
            try:
                m.apply(tree)
            except ValueError as e:
                print(f'MUTANT_BAD {e}')
                bad += 1
                continue
            name = os.path.basename(m.file)[:-2] + '.o'
            obj, log = build_object(tree, kdir, name)
            if obj is None:
                print(log)
                print(f'MUTANT_BAD {m.ident}: does not build')
                bad += 1
            elif obj == orig[name]:
                print(f'MUTANT_BAD {m.ident}: same object code as the original (equivalent)')
                bad += 1
            else:
                print(f'MUTANT_OK {m.ident} steps={",".join(m.steps)}: {m.title}')
            shutil.copyfile(pristine, src)
    if bad:
        print(f'MUTANTS_FAIL bad={bad} of {len(mutants)}')
        return 1
    print(f'MUTANTS_CHECK_OK mutants={len(mutants)}')
    return 0


def vm_marker(console):
    try:
        with open(console, encoding='utf-8', errors='replace') as f:
            for line in f:
                if line.startswith('MOOBFS_VM_FAIL:'):
                    return line.split(':', 1)[1].strip()
    except OSError:
        pass
    return None


def run(mutants, wanted):
    out = os.path.join(ROOT, 'build', 'mutation')
    os.makedirs(out, exist_ok=True)
    chosen = [m for m in mutants if not wanted or m.ident in wanted]
    report, survived = [], 0
    for m in chosen:
        tree = os.path.join(out, m.ident)
        shutil.rmtree(tree, ignore_errors=True)
        os.makedirs(tree)
        # a copy of HEAD, never the working tree: the mutant must not reach the sources
        archive = subprocess.run(['git', '-C', ROOT, 'archive', 'HEAD'], stdout=subprocess.PIPE,
                                 check=True).stdout
        subprocess.run(['tar', '-x', '-C', tree], input=archive, check=True)
        m.apply(tree)
        env = dict(os.environ, MOOB_ONLY=' '.join(m.steps))
        log = os.path.join(out, f'{m.ident}.log')
        with open(log, 'w', encoding='utf-8') as f:
            rc = subprocess.run(['sh', 'vm/run.sh'], cwd=tree, env=env, stdout=f,
                                stderr=subprocess.STDOUT, check=False).returncode
        marker = vm_marker(os.path.join(tree, 'build', 'vm', 'console.log'))
        if rc and marker and marker not in ('module', 'loop'):
            verdict = f'KILLED by {marker}'
        elif rc:
            verdict = 'ERROR (the run failed before a suite marker; see the log)'
            survived += 1
        else:
            verdict = 'SURVIVED'
            survived += 1
        line = f'{m.ident} steps={",".join(m.steps)} {verdict}: {m.title}'
        print(line, flush=True)
        report.append(line)
        console = os.path.join(tree, 'build', 'vm', 'console.log')
        if os.path.exists(console):
            shutil.copyfile(console, os.path.join(out, f'{m.ident}-console.log'))
        shutil.rmtree(tree, ignore_errors=True)
    with open(os.path.join(out, 'report.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(report) + '\n')
    print(f'MUTANTS_RUN killed={len(chosen) - survived} survived_or_error={survived} of {len(chosen)}')
    return 1 if survived else 0


def main(argv):
    if len(argv) < 2 or argv[1] not in ('list', 'apply', 'check', 'run'):
        print(__doc__)
        return 2
    mutants = load()
    if argv[1] == 'list':
        for m in mutants:
            print(f'{m.ident} steps={",".join(m.steps)}: {m.title}')
        return 0
    if argv[1] == 'apply':
        if len(argv) != 4:
            print('usage: mutants.py apply ID DIR')
            return 2
        for m in mutants:
            if m.ident == argv[2]:
                m.apply(argv[3])
                return 0
        print(f'no mutant {argv[2]}')
        return 2
    if argv[1] == 'check':
        return check(mutants)
    return run(mutants, set(argv[2:]))


if __name__ == '__main__':
    sys.exit(main(sys.argv))
