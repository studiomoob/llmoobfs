# 0228 — Security batch 5: fair shares of the index budget (S9, F2) and the G2 latency ceiling

Date: 2026-10-28. Status: branch `claudiofp/BATCH5`, published with its pull
request. Work started: 2026-10-28 20:30 America/Sao_Paulo; ended 23:20. Wall-clock time, with
the VM runs, not an engineering-effort measurement. Estimated effort: M for S9 (ADR-0027), M for
the G2 measurement.

## Goal

The next item of the security order after batch 4 and the `v0.5.0-experimental` tag (journals
0226, 0227): the part of finding F2 that ADR-0021 left open, one uid able to fill
`index_budget` and so refuse every other uid's first domain, and G2's latency ceiling, which no
protocol section had set. Also in the batch: the three Dependabot updates (setuptools 84.2.1,
virtme-ng 1.42, certifi 2026.10.23) and a dated note on journal 0226.

## Decisions

The maintainer chose both rules before any code or measurement:

- **S9: lend and give back** (ADR-0027). A uid may use the whole budget while nobody else needs
  it; the share is the budget divided by the uids with a built domain, the builder counted once;
  a build that does not fit releases the builder's own domains first, then, while the builder is
  below its share, the least recently queried domain of a uid above its share; otherwise
  `ENOMEM`. Rejected again: a reserved fraction (refuses a domain that fits alone) and keeping
  ADR-0021 as it was. The protocol's §9 property is amended accordingly (dated note there).
- **G2: relative bounds** (protocol §14): under a noisy neighbour, a warm query's p99 is at
  most twice the uncontended p99; the p99 of the neighbour's building queries is at most the
  10 s deadline of ADR-0019; no partial answer; the protected domain is not rebuilt. Three runs.
  Relative bounds do not depend on one host's speed. A 3x bound and an absolute one in
  milliseconds were the alternatives.

## Changes

- `layer/index.c`, `layer/moobl.h`: every `charge()` and `uncharge()` names its partition, and
  each domain record keeps `used`, the bytes of its names, entries, inode records and paths; an
  inode record moved to another partition moves its bytes (`rec_set_partition()`); a new record
  counts in the outside part until it is placed. `domain_drop()` warns if a dropped partition
  still counts a byte, and `part_sub()` if a count would go below zero: the invariant is checked
  in every suite run. `domain_release()` takes a mode (`MOOBL_REL_ANY` for the shrinker,
  `MOOBL_REL_OWN`, `MOOBL_REL_LEND`); the shares (`struct moobl_shares`, at most 32 uids, past
  which the share stays larger than the exact one and fails toward `ENOMEM`) are computed for
  the unlocked choice and again under the index lock before the takedown.
- `vm/suite.sh`: new step `fairshare` in the full suite; `buildcostevict` follows the amended
  property (uid 1001's domain, over its share, is lent to uid 1002).
- `bench/client/g2_share.py`: the cross-user mount now checks both halves of the amended
  property (protected and lent) with a measured budget; `bench/client/g2_contention.py`: the
  §14 measurement.
- `docs/decisions/0027-fair-shares-of-the-index-budget.md`; dated notes on ADR-0011 (F2 fixed)
  and ADR-0021 (decision 4 amended); protocol §9 note and §14; `docs/references.md` §26.
- Surfaces: `README.md`, `README.pt-BR.md`, `SECURITY.md`, `docs/when-not-to-use.md`,
  `docs/faq.md`, `site/faq.html`, `docs/security-roadmap.md` (S1, S9), `ROADMAP.md`,
  `docs/status.md`, `docs/status.pt-BR.md`, `CHANGELOG.md`; old wordings in
  `docs/retired-claims.txt`.
- Dependabot: `python/build-requirements.txt` (setuptools 84.2.1), `vm/kasan-requirements.txt`
  (virtme-ng 1.42, certifi 2026.10.23), cherry-picked with their authorship.

## Evidence

Dedicated host (Debian 13, kernel `6.12.111+deb13-amd64`), VM runs with `MOOB_VM_KVM=1`.

- `checkpatch`: no warning in the tracked sources; the module builds with no warning.
- `MOOB_ONLY="fairshare buildcostevict release overbudget ext4" make vm-test`:
  `MOOBFS_ONLY_PASS`, no `WARNING` in the console. `FAIRSHARE_PASS: a=683520 b=2278120
  budget=2619880; b alone, a lent b's room, b refused while a within its share`;
  `BUILD_COST_CROSS_USER_LENT ... budget=1774216`; `RELEASE_PASS` and `OVERBUDGET_PASS` on the
  three filesystems.
- Negative round, the same tests on the module of `main` (`dfd779c`): `buildcostevict` fails at
  `bce-second-user` (ADR-0021's refusal), and `fairshare` alone fails at `fs-a-not-lent`.
- `g2_share.py` on the host: `G2_SHARE shape alice=428600 bob=852600 budget=1066900`; protected:
  bob `ENOMEM`, no release, alice not rebuilt; lent: alice's build released bob's domain, bob's
  next search `ENOMEM`, alice kept; same-uid release still works; `G2_SHARE_PASS`. On the module
  of `main` it fails in the lent phase (alice `ENOMEM`).
- KASAN/UBSAN/lockdep on the steps the change touches (`MOOB_ONLY="fairshare buildcostevict
  release overbudget ext4"`, upstream 6.12.112, virtme-ng 1.42): `MOOBFS_KASAN_ONLY_PASS`, no
  kernel diagnostic.
- G2, protocol §14, `g2_contention.py` on the host: shape alice 257 400 bytes, one of bob's
  domains 854 600, budget 1 539 300. Run 1: alice p99 33.873 ms alone and 36.816 ms beside bob
  (ratio 1.09), bob p99 0.154 s; run 2: 33.787 and 37.416 ms (1.11), 0.145 s; run 3: 35.565 and
  38.020 ms (1.07), 0.145 s. No partial answer, alice's domain never released;
  `G2_CONTENTION_PASS`. Bob's queries per run: 219 to 223, of which 66 or 67 built a domain and
  153 to 156 were refused with `EDQUOT` by ADR-0019's per-uid build admission.
- Full KASAN/UBSAN/lockdep suite (21:43–23:01): `MOOBFS_KASAN_PASS`, no kernel diagnostic, with
  `FAIRSHARE_PASS`, `ROOTPATH_PASS` on the three filesystems and `MOOBFS_PYLIB_PASS`.
- `make vm-test`, Debian 6.12.111: twice with `MOOB_VM_KVM=1` it stopped at
  `MOOBFS_VM_FAIL: rootpath-race-inconclusive-btrfs`, no kernel diagnostic (the timing step of
  journal 0225, which cannot conclude when the btrfs build is too fast; it ran beside the KASAN
  run). Under TCG, the CI's mode (22:27–23:11): `MOOBFS_VM_PASS`, `MOOBFS_LAYER_PASS`,
  `MOOBFS_ACCEPTANCE_PASS`, `ROOTPATH_PASS` on ext4, btrfs and XFS, `FAIRSHARE_PASS`, no
  `MOOBFS_VM_FAIL`, no kernel diagnostic.
- `shellcheck` on the shell scripts and the SPDX check: clean.

## Errors found on the way

- The first `g2_share.py` shape (alice 600 memories) gave a budget below the 1 MiB minimum the
  script requires; alice now has 1000.
- A wait loop on the host matched its own command line (`pgrep -f` over ssh) and held the
  negative round for about 20 minutes; waits now use the exact process name or a pid.
- The first local KASAN run stopped at the module build: files left by the Docker build were
  owned by root (as in journal 0219); the tree was given back to the user and synced again.

## Limitations

- A domain is released whole, so a uid can end a release below its share; two uids that both
  want more than a share still release each other's domains above it, at the rate ADR-0019
  allows.
- The share table holds 32 uids; with more, the share is larger than the exact one and fewer
  domains qualify (a release then fails toward `ENOMEM`, never toward taking a protected domain).
- The shrinker still drops any idle domain of any uid.
- The G2 neighbour measured is the one ADR-0019's build admission leaves: most of bob's queries
  were refused with `EDQUOT`, so the bound is met with that limit in place, not without it. One
  host, one shape; no comparison with another module.
- G2 itself stays open for its user-visible diagnostics and a deployment recipe (ROADMAP).
- `rootpath` still cannot conclude under KVM on a fast btrfs build (issue #168).

## Next step

Merge after the pull request's checks. Then the consumable track (C6), then G6, as agreed; the
rest of S9 (a cap on `QUERY` results per user, the mount-time scan) and G2's diagnostics and
recipe stay in the roadmap.
