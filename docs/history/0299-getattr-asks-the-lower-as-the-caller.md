# 0299 — `stat` asks the lower's security hook as the caller (ADR-0040)

Date: 2026-11-16. Work started at about 11:40 Brasília time; END_TIME. Status: **in progress; on
the local branch `claudiofp/b34-getattr-caller` (on batch 33), batch 34; not pushed. Held for
coordinated disclosure: the draft advisory GHSA-w7w8-m49c-vrvr is private until the fix is on
`main`.** Estimated effort S (ADR-0040); the time recorded is calendar time, most of it machine
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

On the s390x host (exploratory; Ubuntu 24.04, KVM), queue started at about 14:16, from a bundle
of the local branch (nothing published):

RUNS

## Limitations

- The SELinux analogue (a confined mounter domain without `getattr` on a type) is not exercised:
  the confined mounter of journal 0286 is permissive.
- Runs so far on s390x only, which is exploratory; the dedicated host's runs are pending.

## Surfaces held back

Until the advisory is published, the batch changes no public surface beyond these files: the
`CHANGELOG.md` entry, `SECURITY.md` (the row of `v0.6.0-experimental` and the advisory list),
`docs/security-roadmap.md` S12, `docs/when-not-to-use.md` and the list of exceptions in ADR-0022
are updated in the same commit that is pushed with the advisory's publication.

## Next step

The runs on the s390x host; then, when the dedicated host is back, the batch's full `vm-test`,
the full KASAN suite and the Smack and SELinux runs; then the maintainer's agreement to push,
merge and publish the advisory.
