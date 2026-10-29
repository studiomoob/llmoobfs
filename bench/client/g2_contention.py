#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""G2 latency ceiling, protocol section 14. Run as root on the dedicated Linux host.

ext4 lower, domain_depth=1. alice (uid 1001) has one private domain of 600 memories; bob (uid
1002), the noisy neighbour, four of 2000. The budget is alice's bytes plus one and a half
times one of bob's domains, so each of bob's queries builds a domain and releases another of
his. Phase 1: 200 warm searches of alice's alone. Phase 2: the same while a child process of
bob's queries his domains in turn without pause. Three runs; in each, alice's p99 in phase 2
is at most twice her p99 in phase 1, bob's p99 is at most 10 s, no answer is partial, and
alice's domain is not rebuilt. Exit 0 when every bound holds in every run, 2 otherwise.
Usage: g2_contention.py MODULE.ko
"""
import errno
import os
import resource
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from g1_mounted import ALICE, BOB, as_user, ensure_users, get_info, mount_layer, search, umount
from g2_baseline import owned, plant, prepare
import langgraph_store as store

SMALL, BIG, BOB_DOMAINS, SEARCHES, RUNS = 600, 2000, 4, 200, 3
REFUSED = {errno.ENOMEM, errno.E2BIG, errno.ETIMEDOUT, errno.EDQUOT}
FLT = {"kind": "decision"}
TRACE = "/sys/kernel/tracing"


def pct(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p * len(v)))] if v else float("nan")


def timed_search(mnt, uid, gid, name, flt, limit):
    t0 = time.monotonic()
    try:
        items, _ = search(mnt, uid, gid, [], name, flt, limit)
        out = sorted(i.key for i in items)
        err = None
    except store.MoobQueryError as e:
        out, err = None, e.errno
    return time.monotonic() - t0, out, err


def bob_loop(mnt, bob_gid, out_path):
    """Child: query bob's domains in turn until SIGTERM; one line per query."""
    stop = []
    signal.signal(signal.SIGTERM, lambda *_: stop.append(1))
    with open(out_path, "w") as out:
        i = 0
        while not stop:
            name = f"bob{i % BOB_DOMAINS}"
            dt, items, err = timed_search(mnt, BOB, bob_gid, name, None, 10000)
            n = -1 if items is None else len(items)
            out.write(f"{dt:.6f} {n} {err or 0}\n")
            out.flush()
            i += 1
    os._exit(0)


def trace(on):
    try:
        with open(f"{TRACE}/events/moobfs/moobfs_domain_release/enable", "w") as f:
            f.write("1" if on else "0")
        if on:
            open(f"{TRACE}/trace", "w").close()
    except OSError:
        pass


def alice_releases():
    """Budget releases of alice's domain in the trace (her key is her directory's inode)."""
    try:
        return open(f"{TRACE}/trace").read()
    except OSError:
        return ""


def one_run(run, lower, mnt, opts, alice_gid, bob_gid, alice_ino):
    mount_layer(lower, mnt, opts)
    t, first, err = timed_search(mnt, ALICE, alice_gid, "alice", FLT, 100)
    if err or not first:
        return False, f"alice's first search: {err}"
    calm = []
    for _ in range(SEARCHES):
        dt, items, err = timed_search(mnt, ALICE, alice_gid, "alice", FLT, 100)
        if items != first:
            return False, "alice's uncontended answer changed"
        calm.append(dt)
    before = get_info(mnt)
    trace(True)
    path = f"/tmp/g2c-bob-{run}.txt"
    pid = os.fork()
    if pid == 0:
        bob_loop(mnt, bob_gid, path)
    time.sleep(1.0)	# bob's first build is under way
    busy, partial = [], 0
    for _ in range(SEARCHES):
        dt, items, err = timed_search(mnt, ALICE, alice_gid, "alice", FLT, 100)
        if items != first:
            partial += 1
        busy.append(dt)
    os.kill(pid, signal.SIGTERM)
    os.waitpid(pid, 0)
    after = get_info(mnt)
    tr = alice_releases()
    trace(False)
    umount(mnt)
    bob = [line.split() for line in open(path)]
    bob_t = [float(b[0]) for b in bob]
    bob_bad = sum(1 for b in bob if not (int(b[1]) == BIG or (int(b[1]) == -1 and int(b[2]) in REFUSED)))
    bob_refused = sum(1 for b in bob if int(b[1]) == -1)
    alice_hit = f"domain={alice_ino} by=budget" in tr
    p99c, p99b = pct(calm, 0.99), pct(busy, 0.99)
    ok = (p99b <= 2 * p99c and pct(bob_t, 0.99) <= 10.0 and partial == 0 and bob_bad == 0 and
          not alice_hit and len(bob) > 0)
    print(f"G2_CONTENTION run={run} alice_calm p50={pct(calm, .5) * 1e3:.3f} "
          f"p95={pct(calm, .95) * 1e3:.3f} p99={p99c * 1e3:.3f} ms; "
          f"alice_busy p50={pct(busy, .5) * 1e3:.3f} p95={pct(busy, .95) * 1e3:.3f} "
          f"p99={p99b * 1e3:.3f} ms ratio={p99b / p99c:.2f}; "
          f"bob queries={len(bob)} refused={bob_refused} bad={bob_bad} "
          f"p50={pct(bob_t, .5):.3f} p95={pct(bob_t, .95):.3f} p99={pct(bob_t, .99):.3f} s; "
          f"alice_partial={partial} alice_released={alice_hit}; "
          f"builds={before.domain_builds}->{after.domain_builds} "
          f"releases={before.domain_releases}->{after.domain_releases} "
          f"used={after.index_used} built={after.domains_built} "
          f"maxrss_kb={resource.getrusage(resource.RUSAGE_SELF).ru_maxrss} "
          f"pass={'yes' if ok else 'no'}", flush=True)
    return ok, ""


def main():
    if sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("G2_CONTENTION_FAIL run as root on Linux")
    if "moobfs_layer" not in open("/proc/modules").read():
        subprocess.check_call(["insmod", sys.argv[1]])
    _team, alice_gid, bob_gid = ensure_users()
    mnt = "/mnt/g2-0188-contention"
    lower = "/srv/moob/ext4/g2-0188-contention"
    owned(lower)
    owned(mnt)
    mount_layer(lower, mnt, "")
    os.chmod(mnt, 0o755)
    prepare(mnt, "alice", ALICE, alice_gid, 0o700)
    plant(mnt, ALICE, alice_gid, [], "alice", SMALL)
    for i in range(BOB_DOMAINS):
        prepare(mnt, f"bob{i}", BOB, bob_gid, 0o700)
        plant(mnt, BOB, bob_gid, [], f"bob{i}", BIG)
    umount(mnt)
    mount_layer(lower, mnt, "")
    timed_search(mnt, ALICE, alice_gid, "alice", FLT, 100)
    used_a = get_info(mnt).index_used
    timed_search(mnt, BOB, bob_gid, "bob0", None, 10000)
    used_b = get_info(mnt).index_used - used_a
    umount(mnt)
    alice_ino = os.stat(os.path.join(lower, "alice")).st_ino
    budget = used_a + used_b * 3 // 2
    print(f"G2_CONTENTION shape alice={used_a} bob_domain={used_b} budget={budget} "
          f"searches={SEARCHES} runs={RUNS}", flush=True)
    results = [one_run(r, lower, mnt, f"index_budget={budget}", alice_gid, bob_gid, alice_ino)
               for r in range(1, RUNS + 1)]
    for ok, why in results:
        if why:
            print(f"G2_CONTENTION_FAIL {why}", flush=True)
    if all(ok for ok, _ in results):
        print("G2_CONTENTION_PASS", flush=True)
        return
    print("G2_CONTENTION_FAIL a bound of protocol section 14 was missed", flush=True)
    raise SystemExit(2)


if __name__ == "__main__":
    main()
