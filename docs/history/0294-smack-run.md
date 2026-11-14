# 0294 — Smack: a first run of the layer

Date: 2026-11-14. Work started about 07:45 Brasília time; guest rounds 08:10–10:20 on the test
host, after batch 28's queue; ended about 10:40. Status: **done; in the pull request of batch 29,
after batch 28's, with the maintainer's agreement.** Estimated effort S; the time recorded is
calendar time of one morning session, most of it waiting for the test host.

## Goal

The documents said that Smack never ran (`docs/matrix-and-limits.md`, `docs/when-not-to-use.md`,
`docs/help-wanted.md`, both READMEs, security roadmap S12). SELinux ran once (journal 0104) and
AppArmor once (journal 0290). Smack is the third exclusive major LSM. It decides by the label of
the file (`security.SMACK64`), not by its path. Answer, each with evidence:

1. Does the layer show the lower's labels, also when a confined process reaches a file first
   (the defect journal 0104 fixed for SELinux: `security.*` read with the caller's credentials)?
2. Do read, append, stat, attribute writes, listing and creation by a labelled, unprivileged
   process get the same decision through the layer as on the lower file?
3. Do files created through the layer get the labels Smack gives directly, in a plain and in a
   transmuting directory?
4. Does `QUERY2` leave out, for that process, a result Smack hides from it (ADR-0011, F6)?
5. Does a relabel through the layer reach the lower and take effect, and is it refused to an
   unprivileged process?

The kernel stays frozen (ADR-0022): harness, tests and documents only.

## Prior art and sources read

`security/smack/smack_lsm.c` of Linux 6.12.116, the KASAN pin of `.github/workflows/kasan.yml`:

- **Labels of an inode.** `smack_d_instantiate()` starts from the superblock's default label
  (`final = sbsp->smk_default`, the floor `_` when no mount option names one) and, for a
  filesystem it does not special-case, replaces it with the value `smk_fetch()` reads with
  `__vfs_getxattr()`, through the layer's xattr handler. If the read fails, the inode keeps the
  default. `smack_inode_getsecurity()` answers a later read of `security.SMACK64` from the label
  cached on the inode, not from the disk.
- **Checks.** `smack_inode_permission()` on every permission check; `smack_file_ioctl()` asks for
  write access to the file's label when the ioctl number has `_IOC_WRITE`, and read access when
  it has `_IOC_READ`.
- **Index.** `smack_inode_listsecurity()` always names `security.SMACK64`, so the layer's
  `lsm_labels()` (`layer/index.c`) sees an LSM that labels inodes, and every `QUERY2` result is
  checked for the caller (journal 0104).
- **Privilege.** Root passes Smack's checks with `CAP_MAC_OVERRIDE`. So the subject must run
  without capabilities, as an ordinary uid.

Debian's kernels do not build Smack, so the guest boots the KASAN pin built with Smack as the
only major LSM.

## Harness

- `vm/smack.config` is added to `vm/kasan.config`. It builds `CONFIG_SECURITY_SMACK`, makes it
  the default (`CONFIG_LSM="landlock,lockdown,yama,smack,bpf"`), and leaves out SELinux and
  AppArmor. So the run also has KASAN, UBSAN and lockdep.
- `vm/smack-local.sh`, on the host:
  - builds that kernel into `~/.cache/moobfs-smack`, the module, and static `query`,
    `moobfs-attr` and `listdir`;
  - makes a 256 MiB ext4 lower;
  - boots the guest with virtme-ng and runs `vm/smack-guest.sh`;
  - fails on any kernel diagnostic or on a failed check.
- `vm/smack-guest.sh`, in the guest:
  - loads the rules `Sub Rd r`, `Sub Rw rwa`, `Sub Hid -`, `Sub Dir rwxat` and `Sub Tr rwxat`
    through `/sys/fs/smackfs/load2`;
  - labels the lower files, `tr` with `SMACK64TRANSMUTE`;
  - mounts the layer as root.
- **The subject**: one shell writes the label `Sub` to `/proc/self/attr/smack/current`, then
  execs util-linux's `setpriv` as uid 1000, with no groups, no inheritable capabilities and an
  empty bounding set.
- **Self-test first**: the subject runs, may read `Rd` and may not read `Hid` on the lower. A
  failure stops the run.
- **Steps**: for each check, the exit status directly on the lower and through the layer. Then
  a control (the subject reads `Hid` on the lower and is refused), and a requirement that the
  decisions include both allowed and denied operations.
- **Markers**: `SMACK_LABELS_PASS`, `SMACK_PARITY_PASS`, `SMACK_CREATE_PASS`, `SMACK_QUERY_PASS`,
  `SMACK_QUERY_IOCTL_PASS`, `SMACK_RELABEL_PASS`, `SMACK_RELABEL_DENIED_PASS`,
  `MOOBFS_SMACK_RESULT: fails=<n>`, and `MOOBFS_SMACK_FAIL` on any failed check.
- Not part of `make vm-test` nor of CI, like the SELinux and AppArmor harnesses.

## Rounds

Test host, Debian 13, KVM; guest kernel 6.12.116 with KASAN, UBSAN, lockdep and Smack (`lsm=capability,landlock,smack`).

**Round 1** (08:10–08:40, commit `b7a0359`): invalid, the harness's error.
- Inside `busybox sh`, the bare name `setpriv` ran busybox's own applet, not util-linux's. Every
  operation of the subject failed, on the lower and through the layer alike. So the parity step
  passed with nothing allowed, and the creation, `QUERY2` and relabel steps failed.
- The negative round's patch did not build (`old` used uninitialised under `-Werror`).
- Fix (`43309a7`): `setpriv` by its full path, and the self-test above. The parity step now also
  requires allowed and denied operations.

**Round 2** (10:00–10:02, `43309a7`): positive run `fails=1`, the `QUERY2` step.
- The subject's `QUERY2` on the domain `t` returned `EACCES` before the layer ran. `QUERY2` is
  `_IOWR`, and `smack_file_ioctl()` asks for write access to the directory's label. `t` has
  root's label `_`, which the subject may only read.
- This is Smack's rule, not the layer's: a confined process can issue `QUERY2` only on a domain
  whose label it may write.
- The negative patch did not build again (an unused `ret`).

**Round 3** (10:16–10:18, `62a108c`): the query moved to `t/q`, a directory labelled `Dir`.
- `QUERY2` returned `ENOTTY`: with `domain_depth=1`, `query2_target()` accepts only a
  top-level directory.
- The negative round built and failed as expected (below).

**Round 4** (10:18–10:20, `24b58cc`): the query on the domain `q`, labelled `Dir`, holding `qrd`
(`Rd`) and `qhid` (`Hid`).

| Run | Result |
| --- | --- |
| positive (`24b58cc`) | `fails=0`: every marker above |
| negative: `moobl_xattr_get()` without `override_creds()` | `fails=2`: labels and parity |

Positive run:

- **Labels**: after `drop_caches`, the subject's `stat` of `hid` through the layer is the first
  contact. Every label read through the layer then equals the lower's; the layer root has `_`,
  as the lower root.
- **Decisions**: 18 operations (11 allowed and 7 denied directly); each agrees through the layer.
- **New files**: created through the layer, a file gets `Sub` in `dir` and `Tr` in the
  transmuting `tr`, as directly.
- **`QUERY2` on `q`**: as root, `qrd` and `qhid`; as the subject, `qrd` only. On `t`, the
  subject is refused (`EACCES`, Smack's ioctl rule).
- **Relabel**: root's relabel of a `Hid` file to `Rd` through the layer reaches the lower, and
  the subject's next read succeeds. The subject's own relabel through the layer is refused, and
  the lower keeps `Rw`.
- **Console**: no kernel diagnostics.

Negative run:

- The subject's first contact leaves `hid` with the default label `_` in the layer: its
  `__vfs_getxattr()` ran with the subject's credentials and was refused.
- Its `stat` of `hid` is then allowed through the layer (exit 0) and refused directly.
- The other steps pass, the query included: `qhid` was not reached first by the subject.

So the label read of journal 0104, made for SELinux, is what keeps Smack's decisions the same
through the layer; `SMACK_LABELS_PASS` and `SMACK_PARITY_PASS` are its regression test under
Smack.

## Answers

1. **Labels**: yes, with the label read made with the mounter's credentials. Without it, a
   confined first caller leaves the layer's inode with the default label, and Smack then
   decides on the wrong label.
2. **Decisions**: the same for every operation tried.
3. **New files**: the same labels as directly, transmuting directory included.
4. **`QUERY2`**: it leaves out what Smack hides from the caller. The caller also needs write
   access to the domain's label: Smack checks `_IOWR` ioctls as writes.
5. **Relabel**: through the layer it reaches the lower and takes effect at once. An
   unprivileged process cannot relabel.

## Limitations

- **Coverage**: one run, one kernel (upstream 6.12.116, a test build), ext4 lower, rules written
  for the test.
- **Not exercised**:
  - mmap and exec through the layer, `SMACK64EXEC` and `SMACK64MMAP`;
  - Smack's network labels, `smackfsdef`/`smackfsroot` mount options;
  - a distribution booted with Smack;
  - mounting by a labelled, unprivileged mounter.
- **The `QUERY2` ioctl rule** is Smack's. A deployment must give its clients write access to
  the label of the domains they query. That is recorded in `docs/when-not-to-use.md`, not
  changed.

## Next step

The batch's full suites (`make vm-test`, the full KASAN suite, the pre-push checks) and the pull
request after batch 28's.

## Surfaces brought up to date

Both READMEs, `docs/matrix-and-limits.md`, `docs/when-not-to-use.md` (a new item: the `QUERY2`
ioctl rule), `docs/help-wanted.md` (the call now asks for a distribution booted with SELinux),
`docs/testing.md` (a Smack section), security roadmap S12, a dated note on ADR-0037,
`CHANGELOG.md`, `docs/references.md` §65; the old wording is in `docs/retired-claims.txt`.
