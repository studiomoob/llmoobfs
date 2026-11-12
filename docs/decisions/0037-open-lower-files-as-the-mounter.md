# ADR-0037 — A third narrow exception to the kernel freeze: open lower files as the mounter

Date: 2026-11-11. Status: **proposed**; it needs the maintainer's acceptance before it is merged.
Amends [ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md) by one exception, beside
those of [ADR-0033](0033-s16-freeze-exception.md) and
[ADR-0034](0034-kernel-compat-freeze-exception.md); the freeze otherwise stays. Estimated effort:
S (hours). Estimate, not labor time.

## Context

[ADR-0011](0011-threat-and-credential-model.md) says data operations on an open file run with the
mounter's credentials. `moobl_open()` and `moobl_dir_open()` passed the mounter's credentials to
`backing_file_open()` and `dentry_open()` without switching to them. An LSM gives a file the
identity of the task that allocates it, not of the credentials passed in (SELinux:
`fsec->sid = current_sid()` in `selinux_file_alloc_security()`, Linux 6.12.116), so the lower file
carried the caller's SID while reads, writes and, since journal 0120, directory reads ran as the
mounter. Under SELinux the mounter's domain then needed `fd use` on the domain of every process
that uses the layer; a confined mounter without that rule gets `EACCES`. Seen on 2026-11-11 with a
permissive mounter domain (`avc: denied { use } ... tclass=fd` from the mounter to the reader,
journal 0284) and documented as a stage-2 limit by the maintainer's decision of that evening,
with the fix left to a later batch.

overlayfs opens its real files under `ovl_override_creds()` (`ovl_open_realfile()` and
`ovl_dir_open_realfile()`, Linux 6.12.116), so they carry the mounter's identity.

## Decision (proposed)

`moobl_open()` and `moobl_dir_open()` open the lower file under `override_creds()` with the
mounter's credentials, as overlayfs does. Nothing else changes: the permission checks of the open
itself already used the mounter's credentials (`security_file_open()` reads `file->f_cred`), the
caller is still checked against the lower inode by `moobl_permission()` on the path walk, and the
UAPI and the format do not change.

Regression test: SELinux step `SELINUX_MOUNTER_PASS` (`vm/selinux-guest.sh`): a second mount of
the lower made in a permissive domain `moobtest_mounter_t`, a reader that lists and reads through
it, and no `fd use` from the mounter to the reader in the audit log; control: the mounter's own
`open` of the lower directory is logged. Negative round: the old `layer/main.c`.

## Alternatives

- Keep the limit and document it (the state of journal 0284): a confined mounter, the normal case
  on a distribution with SELinux enforcing and a service that mounts the layer, cannot use it
  without a broad rule.
- Grant `fd use` in the shipped policy rule: the rule would have to name every caller's domain.

## Consequences

The exception is limited to these two call sites. AppArmor and Smack also label files from the
allocating task; neither was run. The change re-runs the steps that open lower files (the whole
suite) and KASAN once for its batch.
