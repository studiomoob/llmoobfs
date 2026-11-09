# 0276 — Regression tests for review findings R2-1, R2-2, R2-12, V4/M5 and M8.7

Date: 2026-11-09, 02:15–03:55 Brasília time. Status: **done
locally**, not published. Calendar time, not effort; estimated effort S (hours).

## Goal

Close five entries of the section "Gaps" of the draft register `docs/review-findings.md`
(ADR-0030, criterion 2: every finding of an AI-assisted review turned into a regression test):
R2-1 and R2-2, whose reproducers ran only through the standalone `tests/review-3627172-vm.sh`;
R2-12, V4/M5 and M8.7, which had no test. Each test must run in the full `make vm-test` and fail
without the fix it guards. The kernel is frozen (ADR-0022): tests and harness only; `layer/`,
`include/` and the on-disk format are unchanged.

## Changes

- `vm/build-tests.sh`: builds `tests/review-deep-tree.c` and `tests/review-samedir-rename.c`.
- `vm/suite.sh`, three new steps after `hookrace` (also in the `MOOB_ONLY` list and the header):
  - `deeptree` (R2-1), ext4, btrfs and XFS, a fresh mount with `domain_depth=1`,
    `scan_delay_us=0`. Three runs of `review-deep-tree`, each line matched exactly:
    control `10 5` → `before=1 exists=0 eq_x=0 eq_y=0 open=-1 errno=13` (tag `dt-control-$fs`);
    deep `66 65` → `before=0 exists=0 eq_x=0 eq_y=0 open=-1 errno=13` (`dt-deep-indexed-$fs`);
    value `70 0` → `before=0 exists=0 eq_x=0 eq_y=0 open=0 errno=0` (`dt-value-indexed-$fs`).
    Marker `DEEP_TREE_PASS: $fs`.
  - `samedir` (R2-2), the same lowers and mount. Control `-1` →
    `path_after_drop=b exists_b=1 exists_a=0` (`sd-control-$fs`). Race: `moobfs_scan_reread`
    traced, `scan_delay_us=1000000`, rename 300 ms in, delay reset to 0; requires `RENAMED` and
    `path_after_drop=b exists_b=1 exists_a=0` (`sd-race-keeps-listing-name-$fs`) and a traced
    repeated reading (`sd-not-in-the-window-$fs`). Marker
    `SAMEDIR_RENAME_PASS: $fs readings_repeated=N`.
  - `attrlist` (R2-12), btrfs and XFS only: ext4 without `ea_inode` keeps a file's attributes in one
    block and cannot hold a list over 64 KiB. On the lower, `d/big` gets a valid `user.moob.kind` and 300
    `user.pad.NNNN…` attributes with 255-byte names (list 76,815 bytes), `d/ctl` the same kind.
    `moobfs-check` must exit 2 (`r212-check-status-$fs`) and print "attribute list larger than
    64 KiB: left out of the layer's index" (`r212-check-message-$fs`); the first `QUERY2` of
    domain `d` must succeed (`r212-build-$fs`) and answer `ctl` only (`r212-answer-$fs`).
    Marker `ATTRLIST_LIMIT_PASS: $fs`.
- `tests/layer.c`, two modes:
  - `behind <mount> <lower>` (V4/M5), called at the start of `run_lower` on each lower (tag
    `layer-behind-$fs`, marker `LAYER_BEHIND_PASS`). (1) `behind-f` created through the layer,
    linked on the lower to `behind-h`, cached by a `stat` through the layer, unlinked on the
    lower: `stat` through the layer must give `ENOENT`. (1b) another lower file renamed over a
    cached name that also keeps a second link: `stat` through the layer must give the new
    file's inode number and size. (2) a name cached negative, then created on the lower: `stat`
    through the layer must succeed with the lower's inode number. The second link keeps the old
    inode alive, so only the lower dentry's removal, not the link count, tells the layer the
    name is gone.
  - `padattrs <file> <count>`: plants the attributes of `attrlist`.
- `vm/suite.sh`, step `limits` part 1 (M8.7): counts the kernel log lines `building the index
  of a domain failed`, repeats the over-budget build (`query-build-cost … expect-enomem`, uid
  1003) twelve times, and fails `lm-build-failure-logged-repeatedly` if the count rose by more
  than one. Uid 1003 then has 13 of its 16 build attempts a minute (`build_admit()`), so the
  rate limit does not answer in place of the budget. `LIMITS_BUDGET_PASS` now reports the count.
- `tests/review-3627172-vm.sh`: a comment names the two suite steps; the script stays as the
  record of the original reproduction.
- Documents: `docs/testing.md` (four marker rows, `LIMITS_BUDGET_PASS` row extended),
  `docs/review-findings.md` (rows R2-1, R2-2, R2-12, V4, M8; Gaps items 5 to 8 and 12),
  `CHANGELOG.md`.

## What the code showed, and deviations from the plan

- **The once-only log.** `layer/index.c` logs a failed build with `pr_warn_once()` (journal
  0221), once per module load, not once per domain or mount. In a full suite an earlier step
  (for example the `E2BIG` build of `CLI_E2BIG_PASS`) may already have printed it, so the check
  is "rose by at most one", not "rose by one".
- **Link counts behind the layer.** The first version of `behind` also checked `st_nlink` (2
  after the lower link, 1 after the lower unlink). It failed on ext4 (`FAIL line 407: …
  st_nlink == 2`): `moobl_copy_attr()` deliberately leaves `i_nlink` alone on unlocked paths
  (getattr, revalidate), and the layer refreshes link counts only in its own name operations.
  That is documented behaviour, not V4/M5; the link-count checks were removed and the reason is
  written in the test.
- **`moobfs-check` on the big file** also reports "cannot list attributes": the second
  `listxattr()` asks for the whole length, the kernel caps a list at 64 KiB and answers `E2BIG`.
  The test checks the exit status and the 64 KiB message, not the problem count.
- **Depths.** With `domain_depth=1` the domain is at depth 1, so `10 5` puts the private
  directory at depth 6 and the file at depth 12 (the old script's comment says 11); `66 65` puts
  them at 66 and 68, `70 0` the file at 72.

## Evidence

Test host (Mac mini, Debian 13, KVM), Debian 6.12.111 guest of `make vm-test`, under `nice -n 10`
while a kernel build ran on the host; every run in a new clone under `~/b11night/gaps-*`.
Offline: `shellcheck vm/suite.sh vm/build-tests.sh` clean (after SC2174 was fixed),
`sh -n` of the changed scripts, `sh vm/build-tests.sh` with `-Wall -Wextra -Werror` (gcc 14).

Commit hashes before the DCO sign-off rebase are given with the hash after it.

**Positive** (`MOOB_ONLY="deeptree samedir attrlist limits ext4 btrfs xfs"`, source `101daaa`,
whose code equals `3ef8e4a`; 02:26–02:37): exit 0, `MOOBFS_ONLY_PASS`.
- `LAYER_BEHIND_PASS` and `LAYER_LOWER_PASS` on ext4, btrfs and XFS.
- `DEEP_TREE_PASS` on all three, each with the three expected `REVIEW2_RESULT` lines.
- `SAMEDIR_RENAME_PASS: ext4|btrfs|xfs readings_repeated=1`, control and race both
  `path_after_drop=b exists_b=1 exists_a=0`.
- `PADATTRS list=76815`, `ATTRLIST_LIMIT_PASS` on btrfs and XFS.
- `LIMITS_BUDGET_PASS: … refused with ENOMEM on a 1048576-byte budget, 13 times, logged 1
  time(s) in all …`; `LIMITS_MEMCG_PASS`.

**Negative rounds**, each patch applied in its own clone only, never committed, run alone on the
host (02:37–02:52):

| Round | Change (test copy only) | Steps | Result |
| --- | --- | --- | --- |
| A1 | depth guard of `scan_child()` disabled (`if (0 && …)`) | `deeptree` | `MOOBFS_VM_FAIL: dt-deep-indexed-ext4`; `depth=66 … before=1 exists=1 eq_x=1`: the stale answer of R2-1 |
| A2 | `name_is()` check of the loader removed | `samedir` | `MOOBFS_VM_FAIL: sd-race-keeps-listing-name-ext4`; race `path_after_drop=a` |
| B | ref-walk `moobl_d_revalidate()` back to "copy attributes, valid" (its three checks removed) | `ext4` | `FAIL line 412: stat(up("behind-f")…) == -1 && errno == ENOENT`, `MOOBFS_VM_FAIL: layer-behind-ext4` |
| B2 | only the "negative upper over positive lower" branch removed | `ext4` | `FAIL line 429: !stat(up("behind-x")…)` (errno 2), `MOOBFS_VM_FAIL: layer-behind-ext4` |
| C | the 64 KiB check of `tools/moobfs-check.c` removed | `attrlist` | `MOOBFS_VM_FAIL: r212-check-message-btrfs` (only "cannot list attributes" reported) |
| D | `pr_warn_once` → `pr_warn_ratelimited` for the build failure | `limits` | `MOOBFS_VM_FAIL: lm-build-failure-logged-repeatedly (1 -> 10)` |

Case (1b) of `behind`, the rename over a cached name, is caught by the same `d_unhashed()` check
as case (1): the lower rename unhashes the replaced dentry, as in journal 0268. No single check
isolates it from case (1).

**Full suite** (`MOOB_VM_KVM=1 make vm-test`, no `MOOB_ONLY`, source `5ab7f88`, the same tree
as `119429d`; 02:54–03:49, guest uptime 3,036 s of the harness's 3,600 s limit, the host loaded
by three virtme-ng guests and two syzkaller guests): exit 0, no `_FAIL` line, `MOOBFS_VM_PASS`,
`MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`; `LAYER_BEHIND_PASS` three times,
`HOOK_RACE_PASS`, `DEEP_TREE_PASS` and `SAMEDIR_RENAME_PASS` (`readings_repeated=1`) on ext4,
btrfs and XFS, `ATTRLIST_LIMIT_PASS` on btrfs and XFS, `LIMITS_BUDGET_PASS … 13 times, logged 0
time(s) in all`, `LIMITS_MEMCG_PASS`. `make lint` passed on the same copy and
`scripts/check-retired-claims.sh` locally (`RETIRED_CLAIMS_OK`).

In the full suite the build-failure line counted 0 before and after the twelve refusals: the
one line `pr_warn_once()` printed earlier in the run was no longer in the kernel's log buffer
when `limits` ran, or no earlier build failed with a logged errno. Either way the check still
fails on a line printed per failure, as round D shows (1 → 10 with `MOOB_ONLY=limits`).

## Limitations

- `deeptree` uses `domain_depth=1` only; `samedir` one timing of the rename in the reading.
- `attrlist` cannot run on ext4; a list growing past 64 KiB after the build is not tested.
- `behind` covers lowers without their own `d_revalidate` (ext4, btrfs, XFS), one process.
- The M8.7 check reads the log of the whole module load; with `pr_warn_once` the message can
  be printed by an earlier step, in which case the check sees no new line. It cannot tell
  "logged once" from "never logged" (the full suite counted 0).
- The full suite needed 3,036 s of the 3,600 s guest limit on a loaded host; a busier host can
  hit the limit for reasons unrelated to these steps.
- No KASAN run of these steps: they change no locking or lifetime in the module. The batch's
  KASAN suite will run them.

## Next step

The maintainer reviews the register's remaining gaps; the batch's KASAN suite runs the new
steps before publication.
