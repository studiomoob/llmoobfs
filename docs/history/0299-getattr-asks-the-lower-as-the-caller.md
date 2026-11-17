# 0299 — `stat` asks the lower's security hook as the caller (ADR-0040)

Date: 2026-11-16. Work started at about 11:40 Brasília time; the last run ended on the 17th at 00:03 (calendar time). Status: **done; batch 34, branch `claudiofp/b34-getattr-caller`. The advisory
GHSA-w7w8-m49c-vrvr is published once the fix is on `main`.** Estimated effort S (ADR-0040); the time recorded is calendar time, most of it machine
time. Work done by Claude Code (an AI assistant) for the maintainer.

## Goal

Fix the check held back in journal 0298: through a layer mounted by a Smack-labelled mounter
without `CAP_MAC_ADMIN`, a reader allowed the mounter's label could `stat` a lower file that the
lower refuses it (ADR-0040 has the mechanism). Found by Claude Code running the S12 Smack harness
for the maintainer; no outside reporter.

## Decisions (maintainer, 2026-11-16)

- A low-severity security defect, not an accepted limit; handled through the private path of
  `SECURITY.md` (draft advisory created on GitHub the same day).
- The fix is a kernel change under the freeze, so an exception like ADR-0038 (ADR-0040).
- Tests go to the s390x host while the dedicated x86-64 host is unreachable; the batch's runs on
  the dedicated host follow when it is back.

## Changes

- `layer/main.c`, `moobl_lower_getattr()`: `vfs_getattr()` on the lower path on every kernel, the
  6.12 `AT_GETATTR_NOSEC` flag cleared first. Before, `vfs_getattr_nosec()` on every call from
  the VFS.
- `vm/smack-guest.sh`, step 8: the held-back checks return. `stat hid` through the labelled
  mounter's layer must be refused; `stat only` allowed; the labels of `hid` and `only` through
  that layer are the mounter's (Smack's default, unchanged by the fix).
- `vm/smack-local.sh`: builds and boots on an s390x host as `vm/kasan-local.sh` does
  (`vm/kasan-s390x.config`, `--no-virtme-ng-init`); x86-64 unchanged.
- `docs/decisions/0040-getattr-asks-the-lower-as-the-caller.md`.

## Audit of the other metadata paths (code reading, `layer/main.c`, Linux 6.12.116)

| Path | Credentials toward the lower | Lower's hook for the caller |
| --- | --- | --- |
| `getattr` | caller | now yes (`vfs_getattr()`); before, no |
| `listxattr` | caller | yes (`vfs_listxattr()`) |
| `getxattr`, names outside `security.` | caller | yes (`vfs_getxattr()`) |
| `getxattr`, `security.` names | mounter | the VFS checked the caller against the layer inode; when the mounter may not read the label the read fails, so nothing is shown |
| `readlink` | caller | yes (`vfs_get_link()` runs `security_inode_readlink()`) |
| `readdir` | mounter | the caller passed `moobl_permission()` on the directory against the lower inode |
| `fileattr_get` | caller | needs an open file, which passed `moobl_permission()` |
| `statfs` | caller | no per-file data; the caller's `statfs` hook ran on the layer |

Only `getattr` had the gap.

## Evidence

On the s390x host (exploratory; Ubuntu 24.04, KVM), queue started at about 14:18, from a bundle
of the local branch (nothing published):

| Run (BRT) | Tree | Result |
| --- | --- | --- |
| 14:18 | `bc02ac2` | `make checkpatch`: exit 0 |
| 14:18–14:32 | `bc02ac2` | full `MOOB_VM_KVM=1` suite (`vm/run.sh`, `MOOB_ARCH=s390x`): `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, exit 0 |
| 14:32–14:38 | `bc02ac2` | `vm/smack-local.sh` (6.12.116, `vm/kasan.config` + `vm/kasan-s390x.config` + `vm/smack.config`): every `SMACK_*_PASS`, `MOOBFS_SMACK_RESULT: fails=0`; through the labelled mounter's layer `stat hid: lower=1 layer=1 expected=refused`, `stat only: lower=0 layer=0`; labels of `hid` and `only` there `Mnt`; no kernel diagnostic |
| 14:38–14:43 | `bc02ac2` with `layer/main.c` of `bf2e576^` (negative) | `stat hid: lower=1 layer=0 expected=refused`, `MOOBFS_SMACK_FAIL: labelled mounter: 1 checks not as expected`; every other Smack step passed |
| 14:43–16:35 | `bc02ac2` | `MOOB_ONLY="ext4 btrfs xfs" vm/kasan-local.sh`: `MOOBFS_KASAN_ONLY_PASS` (KASAN, UBSAN, lockdep, no kernel diagnostics; not a complete run), exit 0 |

The negative round shows that the regression check fails without the fix and that nothing else
in the Smack run depends on it.

On the dedicated host (Debian 13, KVM), at `1df0763` (before the branch was rebased on the last
journal commits of batch 33: `bf2e576` became `e7c7265` and `bc02ac2` became `f0b00d7`, with
the same trees under `layer/` and `vm/`), each run on a fresh clone of a bundle, 2026-11-16:

| Run (BRT) | Result |
| --- | --- |
| 22:11–22:12 `vm/smack-local.sh` | every `SMACK_*_PASS`, `MOOBFS_SMACK_RESULT: fails=0`; `stat hid` through the labelled mounter's layer `lower=1 layer=1 expected=refused`; no kernel diagnostic |
| 22:12–22:13 the same with `layer/main.c` of `bf2e576^` (negative) | `stat hid ... lower=1 layer=0`, `MOOBFS_SMACK_FAIL: labelled mounter: 1 checks not as expected`, `fails=1`; every other step passed |
| 22:13–22:14 `vm/selinux-local.sh` | `SELINUX_MAPCHANGE_PASS`, `SELINUX_EFFECT_PASS`, `SELINUX_MOUNTER_PASS`, `SELINUX_DIRREAD_PASS`, `fsuse differ=0`, exit 0 |
| 22:14–22:35 full `MOOB_VM_KVM=1 make vm-test` | `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, exit 0 |
| 22:35–23:53 full `vm/kasan-local.sh` | `MOOBFS_PYLIB_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_KASAN_PASS`, exit 0 |
| 23:53–00:03 pre-push checks | lint, checkpatch, unit tests, ASan, hostile images, Python library, retired claims, pins, matrix page, mutants, site, `git diff --check` against `b40e3b6`, `make static`, Ubuntu tests: all `PASS` |

The SELinux run has no step for this defect (its confined mounter is permissive); it shows that
the fix changes no SELinux decision the harness compares.

## Limitations

- The SELinux analogue (a confined mounter domain without `getattr` on a type) is not exercised:
  the confined mounter of journal 0286 is permissive.
- Linux 7.0 and 7.3 builds of the change are checked by CI only, when the pull request is opened.

## Surfaces

Held back while the branch was local, then updated in one commit before the push, on
2026-11-17 (the maintainer's agreement to merge and publish): `CHANGELOG.md`, `SECURITY.md` (the
row of `v0.6.0-experimental` and a dated update), `docs/status.md` and `docs/status.pt-BR.md`,
`docs/review-findings.md`, security roadmap S12, the note in ADR-0022, ADR-0040's status, journal
0298 (a dated note) and `docs/references.md` §69.

## Next step

With the maintainer's agreement, in order: the pull request of batch 33; then, rebased on `main`,
this batch's pull request with the held-back surfaces; the merge; then the advisory's
publication, with the fix's links.
