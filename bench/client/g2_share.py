#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regression for ADR-0021 and ADR-0027. Run as root on the dedicated Linux host.

Cross-user mount, domain_depth=1: alice has 1000 memories and bob 2000. The budget is bob's
measured bytes plus half of alice's, so either fits alone, both do not, and the share (half the
budget) is above alice's bytes and below bob's. Protected: alice builds, bob's search returns
ENOMEM, alice's domain stays built and her next search does not rebuild. Lent: on a fresh mount
bob builds alone, alice's build releases bob's domain (bob is over his share), bob's next search
returns ENOMEM and alice stays built. Same-uid mount (index_budget=1M, 2000 each): the second
domain still releases the first. Exit 0 is the property; exit 2 a module that misses it.
"""
import errno
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from g1_mounted import ALICE, BOB, ensure_users, get_info, mount_layer, umount
from g2_baseline import ask, owned, plant, prepare


def fail(message):
    print(f"G2_SHARE_FAIL {message}", flush=True)
    raise SystemExit(2)


def main():
    if sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("G2_SHARE_FAIL run as root on Linux")
    ko = sys.argv[1]
    if "moobfs_layer" not in open("/proc/modules").read():
        subprocess.check_call(["insmod", ko])
    _team, alice_gid, bob_gid = ensure_users()
    n = 2000
    small = 1000
    cross = "/mnt/g2-0188-cross"
    cross_lower = "/srv/moob/ext4/g2-0188-cross"
    owned(cross_lower)
    owned(cross)
    mount_layer(cross_lower, cross, "")
    os.chmod(cross, 0o755)
    prepare(cross, "alice", ALICE, alice_gid, 0o700)
    prepare(cross, "bob", BOB, bob_gid, 0o700)
    plant(cross, ALICE, alice_gid, [], "alice", small)
    plant(cross, BOB, bob_gid, [], "bob", n)
    umount(cross)
    mount_layer(cross_lower, cross, "")
    if ask(cross, ALICE, alice_gid, [], "alice") != (small, None):
        fail("alice's measuring search")
    used_a = get_info(cross).index_used
    if ask(cross, BOB, bob_gid, [], "bob") != (n, None):
        fail("bob's measuring search")
    used_b = get_info(cross).index_used - used_a
    umount(cross)
    budget = used_b + used_a // 2
    print(f"G2_SHARE shape alice={used_a} bob={used_b} budget={budget}", flush=True)
    if not (budget >= 1 << 20 and used_a < budget // 2 < used_b):
        fail("shape: the share must lie between alice's and bob's bytes")
    opts = f"index_budget={budget}"

    mount_layer(cross_lower, cross, opts)
    first = ask(cross, ALICE, alice_gid, [], "alice")
    after = get_info(cross)
    bob = ask(cross, BOB, bob_gid, [], "bob")
    mid = get_info(cross)
    again = ask(cross, ALICE, alice_gid, [], "alice")
    end = get_info(cross)
    umount(cross)
    print(f"G2_SHARE protected alice={first} bob={bob} alice_again={again} "
          f"builds={after.domain_builds}->{end.domain_builds} "
          f"releases={after.domain_releases}->{end.domain_releases}", flush=True)
    if first != (small, None) or again != (small, None):
        fail("alice's searches")
    if bob != (None, errno.ENOMEM):
        fail("bob was not refused while alice is within her share")
    if mid.domain_releases != after.domain_releases or end.domain_builds != after.domain_builds:
        fail("alice's domain was released or rebuilt")

    mount_layer(cross_lower, cross, opts)
    alone = ask(cross, BOB, bob_gid, [], "bob")
    b0 = get_info(cross)
    lent = ask(cross, ALICE, alice_gid, [], "alice")
    b1 = get_info(cross)
    refused = ask(cross, BOB, bob_gid, [], "bob")
    kept = ask(cross, ALICE, alice_gid, [], "alice")
    b2 = get_info(cross)
    umount(cross)
    print(f"G2_SHARE lent bob_alone={alone} alice={lent} bob_again={refused} alice_again={kept} "
          f"builds={b0.domain_builds}->{b1.domain_builds}->{b2.domain_builds} "
          f"releases={b0.domain_releases}->{b1.domain_releases}->{b2.domain_releases}", flush=True)
    if alone != (n, None) or lent != (small, None) or kept != (small, None):
        fail("a search that should have answered")
    if b1.domain_releases != b0.domain_releases + 1:
        fail("alice's build did not release bob's domain, which was over its share")
    if refused != (None, errno.ENOMEM) or b2.domain_builds != b1.domain_builds:
        fail("bob was not refused, or alice was rebuilt")

    same = "/mnt/g2-0188-same"
    same_lower = "/srv/moob/ext4/g2-0188-same"
    owned(same_lower)
    owned(same)
    mount_layer(same_lower, same, "index_budget=1M")
    os.chmod(same, 0o755)
    prepare(same, "east", ALICE, alice_gid, 0o700)
    prepare(same, "west", ALICE, alice_gid, 0o700)
    plant(same, ALICE, alice_gid, [], "east", n)
    ecount, eerr = ask(same, ALICE, alice_gid, [], "east")
    east = get_info(same)
    plant(same, ALICE, alice_gid, [], "west", n)
    wcount, werr = ask(same, ALICE, alice_gid, [], "west")
    west = get_info(same)
    print(f"G2_SHARE same east={ecount}/{eerr} west={wcount}/{werr} "
          f"releases={east.domain_releases}->{west.domain_releases} "
          f"builds={east.domain_builds}->{west.domain_builds}", flush=True)
    umount(same)
    if ecount != n or wcount != n or eerr or werr:
        fail("same-uid search failed")
    if west.domain_releases <= east.domain_releases:
        fail("same-uid build did not release")
    print("G2_SHARE_PASS")


if __name__ == "__main__":
    main()
