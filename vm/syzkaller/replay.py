#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Per-call KCOV replay of syzkaller programs in disposable guests (ADR-0032, journal 0248).

Called by vm/syzkaller-replay.sh, which names the kernel, image and binaries. Each guest boots
the syzkaller kernel on the image with `snapshot=on` (nothing is written back), checks that the
module it loaded is KO byte for byte, and runs every program once with
`syz-execprog -cover` on a fresh tmpfs. The PCs that fall in the module's .text are kept as
offsets from its start, one file per program in the cache, named after the program's SHA-1 and
written as soon as the program ends: a run that is stopped (deadline, power cut) starts again
where it was, and a later run over a grown corpus replays only the new programs. The cache is
keyed by the module and the kernel image, so a rebuilt module starts afresh.

Outputs in OUT: union.txt (offsets reached by any program), replay.txt (counts) and one
console log per guest.
"""
import argparse
import hashlib
import os
import queue
import shlex
import subprocess
import sys
import threading
import time

GUEST_SCRIPT = r'''
set -u
lo=$1; hi=$2
cat > /root/prog
mkdir -p /root/w && mount -t tmpfs -o size=512m tmpfs /root/w && cd /root/w || exit 3
timeout {timeout} /root/syz-execprog -executor=/root/syz-executor -sandbox=none -procs=1 \
    -repeat=1 -threaded=false -cover -coverfile=c /root/prog > /root/last.log 2>&1
echo "EXIT $?"
for c in c_prog*; do [ -f "$c" ] && cat "$c"; done |
    awk -v lo="$lo" -v hi="$hi" 'length($1) == length(lo) && $1 >= lo && $1 < hi {{ print $1 }}' | sort -u
cd / && umount -l /root/w
echo REPLAY_OK
'''


def sha(path, n=None):
    h = hashlib.sha256() if n else hashlib.sha1()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()[:n] if n else h.hexdigest()


def text_size(ko):
    out = subprocess.run(['objdump', '-h', '-j', '.text', ko], capture_output=True, text=True,
                         check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3 and p[1] == '.text':
            return int(p[2], 16)
    raise SystemExit('replay: no .text in ' + ko)


class Guest:
    def __init__(self, a, idx, cpus):
        self.a, self.idx, self.cpus = a, idx, cpus
        self.port = a.port + idx
        self.proc = None
        self.lo = self.hi = None
        self.ssh = ['ssh', '-p', str(self.port), '-i', a.key, '-o', 'StrictHostKeyChecking=no',
                    '-o', 'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR',
                    '-o', 'ConnectTimeout=5', '-o', 'BatchMode=yes', 'root@127.0.0.1']

    def log(self, msg):
        print(f'replay[{self.idx}] {time.strftime("%H:%M:%S")} {msg}', flush=True)

    def run(self, cmd, inp=None, timeout=120):
        return subprocess.run(self.ssh + [cmd], input=inp, capture_output=True, timeout=timeout)

    def boot(self):
        a = self.a
        ncpu = len(self.cpus.split(',')) if self.cpus else 2
        qemu = ['qemu-system-x86_64', '-m', str(a.mem), '-smp', str(ncpu), '-enable-kvm',
                '-cpu', 'host', '-display', 'none', '-no-reboot',
                '-serial', 'file:' + os.path.join(a.out, f'console-{self.idx}.log'),
                '-drive', f'file={a.image},format=raw,snapshot=on', '-kernel', a.kernel,
                '-append', 'root=/dev/sda console=ttyS0 net.ifnames=0 panic=1',
                '-netdev', f'user,id=n0,hostfwd=tcp:127.0.0.1:{self.port}-:22',
                '-device', 'e1000,netdev=n0']
        if self.cpus:
            qemu = ['taskset', '-c', self.cpus] + qemu
        self.proc = subprocess.Popen(qemu, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        end = time.time() + 300
        while time.time() < end:
            if self.proc.poll() is not None:
                break
            try:
                if self.run('true', timeout=20).returncode == 0:
                    break
            except subprocess.TimeoutExpired:
                pass
            time.sleep(5)
        else:
            self.stop()
            return False
        if self.proc.poll() is not None:
            return False
        bins = [os.path.join(a.bin, 'syz-execprog'), os.path.join(a.bin, 'syz-executor')]
        for b in bins:
            with open(b, 'rb') as f:
                r = self.run(f'cat > /root/{os.path.basename(b)} && chmod 755 /root/{os.path.basename(b)}',
                             inp=f.read())
            if r.returncode != 0:
                self.stop()
                return False
        r = self.run('sha256sum /moob/moobfs_layer.ko; cat /sys/module/moobfs_layer/sections/.text')
        out = r.stdout.decode().split()
        if r.returncode != 0 or len(out) < 3:
            self.log('module not loaded in the guest')
            self.stop()
            return False
        if out[0] != a.ko_sha:
            self.log('the image holds another module; run the image build step')
            self.stop()
            return False
        base = int(out[2], 16)
        self.lo, self.hi = base, base + a.text_size
        self.log(f'up, .text at {hex(base)}')
        return True

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.proc = None

    def one(self, prog):
        a = self.a
        script = GUEST_SCRIPT.format(timeout=a.timeout)
        cmd = f'sh -c {shlex.quote(script)} sh 0x{self.lo:016x} 0x{self.hi:016x}'
        with open(prog, 'rb') as f:
            data = f.read()
        try:
            r = self.run(cmd, inp=data, timeout=a.timeout + 60)
        except subprocess.TimeoutExpired:
            return None
        lines = r.stdout.decode(errors='replace').split()
        if (r.returncode != 0 or not lines or lines[-1] != 'REPLAY_OK'
                or len(lines) < 3 or lines[:2] != ['EXIT', '0']):
            return None
        return sorted({int(x, 16) - self.lo for x in lines if x.startswith('0x')})


def worker_run(g, q, a, stats, lock):
    up = False
    while True:
        if a.deadline and time.time() > a.deadline:
            break
        try:
            name, path, key = q.get_nowait()
        except queue.Empty:
            break
        result = None
        for attempt in range(2):
            if not up:
                up = g.boot()
                if not up:
                    g.log('boot failed')
                    time.sleep(10)
                    continue
            result = g.one(path)
            if result is not None:
                break
            g.log(f'{name}: no answer (attempt {attempt + 1}), guest restarted')
            g.stop()
            up = False
        with lock:
            if result is None:
                stats['failed'].append(name)
            else:
                tmp = os.path.join(a.cache, key + '.tmp')
                with open(tmp, 'w') as f:
                    f.write(''.join(f'{x:x}\n' for x in result))
                os.replace(tmp, os.path.join(a.cache, key))
                stats['run'] += 1
                if stats['run'] % 50 == 0:
                    g.log(f'{stats["run"]} programs replayed')
    g.stop()


def worker(g, q, a, stats, lock):
    try:
        worker_run(g, q, a, stats, lock)
    except Exception as exc:
        g.log(f'worker failed: {exc}')
        with lock:
            stats['failed'].append(f'guest-{g.idx}: {exc}')
    finally:
        g.stop()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--kernel', required=True, help='bzImage of the syzkaller kernel')
    ap.add_argument('--image', required=True)
    ap.add_argument('--key', required=True, help="the image's ssh key")
    ap.add_argument('--bin', required=True, help='bin/linux_amd64 of the syzkaller checkout')
    ap.add_argument('--ko', required=True, help='the module in the image (layer/moobfs_layer.ko)')
    ap.add_argument('--progs', required=True, help='directory of programs')
    ap.add_argument('--out', required=True)
    ap.add_argument('--cache', help='default OUT/cache')
    ap.add_argument('--guests', default='', help='host CPUs per guest, e.g. "4,5 6,7"; one unpinned guest by default')
    ap.add_argument('--port', type=int, default=10100)
    ap.add_argument('--mem', type=int, default=2560)
    ap.add_argument('--timeout', type=int, default=60, help='seconds per program')
    ap.add_argument('--deadline', type=float, default=0, help='epoch seconds after which no program starts')
    a = ap.parse_args()
    a.ko_sha = sha(a.ko, 64)
    a.text_size = text_size(a.ko)
    a.cache = os.path.join(a.cache or os.path.join(a.out, 'cache'), a.ko_sha[:16] + '-' + sha(a.kernel, 16))
    os.makedirs(a.cache, exist_ok=True)
    os.makedirs(a.out, exist_ok=True)

    progs = sorted(os.listdir(a.progs))
    q = queue.Queue()
    keys = {}
    queued = set()
    for name in progs:
        path = os.path.join(a.progs, name)
        key = sha(path)
        keys[name] = key
        if not os.path.exists(os.path.join(a.cache, key)) and key not in queued:
            q.put((name, path, key))
            queued.add(key)
    todo = q.qsize()
    print(f'replay: {len(progs)} programs, {len(progs) - todo} in the cache, {todo} to run', flush=True)
    stats = {'run': 0, 'failed': []}
    lock = threading.Lock()
    cpus = a.guests.split() or ['']
    threads = [threading.Thread(target=worker, args=(Guest(a, i, c), q, a, stats, lock))
               for i, c in enumerate(cpus)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    union = set()
    have = 0
    for name in progs:
        p = os.path.join(a.cache, keys[name])
        if os.path.exists(p):
            have += 1
            with open(p) as f:
                union.update(int(x, 16) for x in f.read().split())
    with open(os.path.join(a.out, 'union.txt'), 'w') as f:
        f.write(''.join(f'{x:x}\n' for x in sorted(union)))
    left = q.qsize()
    summary = (f'programs={len(progs)} replayed={have} run_now={stats["run"]} '
               f'failed={len(stats["failed"])} not_reached={left} module_text_pcs={len(union)}\n')
    with open(os.path.join(a.out, 'replay.txt'), 'w') as f:
        f.write(summary)
        for n in stats['failed']:
            f.write(f'failed {n}\n')
    sys.stdout.write('replay: ' + summary)
    return 0 if have == len(progs) else 1


if __name__ == '__main__':
    sys.exit(main())
