# 0222 — Term padding named, one privilege test, extended ACL as the kernel decides it (ADR-0026)

Date: 2026-10-28. Status: implemented on the local branch `b3/b` (security/solidity batch 3);
not pushed, not merged; the issue is not ticked. Work started 2026-10-28 about 09:15
America/Sao_Paulo; ended 2026-10-28 about 09:55. Wall-clock time, not an effort measurement. Estimated
effort: S.

## Goal and decision

Three points of issue #119 and of the review of journal 0215, with the maintainer's decisions
given with the assignment:

1. **M6** (journal 0155): the ioctl magic `'M'` is shared with four in-tree users, and
   `struct moob_query_term` had one byte of implicit tail padding (fields end at byte 1097,
   alignment 2, `sizeof` 1098) that the kernel never read. Decision: keep `'M'` (UAPI frozen,
   ADR-0022) and record why; the byte becomes `pad[3]` of `pad[4]`, which `term_check()` requires
   to be zero (`EINVAL`). Compatibility decision in ADR-0026: sizes, offsets, command numbers and
   versions unchanged; callers that zero the request (our tools, tests, the CLI, SQLite extension,
   `bench/`, the `ctypes` library) are unaffected; a caller with garbage there now gets `EINVAL`.
2. **M7** (journal 0155): `capable(CAP_SYS_ADMIN)` in `build_admit()` and `do_query()`,
   `ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN)` in `do_get_info()`: same namespace, two
   audit behaviours. Decision: one test, `moobl_index_admin()`, still `CAP_SYS_ADMIN` in the
   initial namespace (who may query does not change), without audit on denial, after
   `kallsyms_show_value()` and `cap_vm_enough_memory()`: denial is the expected outcome for every
   `GET_INFO` and every first build by an ordinary user. `CAP_DAC_READ_SEARCH` recorded as the
   considered alternative (ADR-0026).
3. **Extended ACL** (pre-existing finding of the review of journal 0215): the kernel counted
   entries (`a_count > 3`), the C tools (`moobfs-index`, `moobfs-check`) took any stored
   `system.posix_acl_access` for an extended ACL, the Python scan counted entries. Decided from
   the kernel (6.12.112): `acl_permission_check()` (`fs/namei.c:350`) judges everyone but the
   owner by the ACL's entries when there is an ACL and group bits, so the correct question is
   whether the ACL may decide otherwise than the mode bits: `posix_acl_equiv_mode()`
   (`fs/posix_acl.c:313`) not 0, or its bits differ from `i_mode`. Checking on the test host
   showed that the entry count was not only different from the tools but wrong in the kernel: a
   minimal ACL `u=rw,g=r,o=-` planted with debugfs on a `0644` ext4 file refuses `nobody`
   (`cat: Permission denied`) while the kernel's rule admitted the file by its bits, so `QUERY2`
   answered it to users who cannot read it. ext4 drops an ACL equivalent to the mode on every
   write through the VFS (`posix_acl_update_mode()`, called at `fs/ext4/acl.c:252`), so only an
   ACL written outside the VFS (offline, another system) reaches this. The kernel's `ext_acl()`
   therefore changes too (separate commit, for the maintainer to accept or drop), and the three
   user-space mirrors follow it exactly.

## Alternatives

- M6, M7: in ADR-0026 (a new magic now; the padding left implicit; `capable()` everywhere; an
  audited test for `do_query()` only; `CAP_DAC_READ_SEARCH`).
- Extended ACL, user-space only (make the tools count entries like the old kernel): rejected
  once the leak above was reproduced; the mirrors would then have matched a wrong rule.
- Extended ACL, "any stored ACL is extended" in the kernel (the tools' rule): fail-safe too, but
  it treats a minimal ACL equal to the bits, which decides exactly as the bits, as restrictive;
  `posix_acl_equiv_mode()` is the kernel's own notion of "expressible as mode bits".
- Fixture by a lower filesystem that keeps minimal ACLs instead of debugfs: none of ext4, XFS,
  btrfs or tmpfs does (all call `posix_acl_update_mode()`); a FUSE lower would test FUSE.

## Changes

- `include/moobfs_ioctl.h`: `pad[3]` → `pad[4]` in `struct moob_query_term`, "must be 0".
- `layer/index.c`: `term_check()` also refuses `pad[3]`; `BUILD_BUG_ON` that the term has no
  implicit padding; `moobl_index_admin()` used by `build_admit()`, `do_query()` and
  `do_get_info()`; `ext_acl()` with `posix_acl_equiv_mode()` and the bits.
- `python/src/moobfs/_uapi.py`, `bench/client/langgraph_store.py`: `ctypes` mirrors `pad * 4`.
- `tools/ext-acl.h` (new), shared by `tools/moobfs-index.c` and `tools/moobfs-check.c`; `Makefile`
  dependencies. `moobfs-check` reasons now "has an extended access ACL" and "the domain directory
  has an extended access ACL". An unreadable ACL (an error other than `ENODATA`/`EOPNOTSUPP`) is
  now restrictive in the C tools, as in the kernel and the Python scan.
- `python/src/moobfs/scan.py`: `_ext_acl(path, mode)` parses the ACL as `posix_acl_equiv_mode()`;
  `python/tests/test_unit.py` class `ExtendedAcl`.
- `tests/query.c`: `pad[3]` refused in `QUERY` and in the first and second terms of `QUERY2`.
- `vm/suite.sh`: new step `minacl`; `buildlimit` checks that root builds a domain the
  mount-wide admission refuses (build counts 65 and 66). `debugfs` staged by
  `vm/in-container.sh` and linked by `vm/kasan-guest.sh`.
- Documents: ADR-0026; dated notes in `docs/spec-query-v2.md` (§3 and §4.1),
  `docs/compatibility.md` §3 and `docs/research-stacked-fs-kernel-rules.md` R-UAPI-6;
  `docs/testing.md` (`MINACL_PASS`, buildlimit); `docs/references.md` §21; `CHANGELOG.md`.

## Evidence

All on the dedicated test host (Debian 13, KVM), tree of branch `b3/b` at the commits of this
journal, 2026-10-28.

- `make checkpatch`: `checkpatch.pl: OK`, `spelling.txt: OK`, `const_structs.checkpatch: OK`,
  exit 0 (after the `0777` fix below).
- `MOOB_VM_KVM=1 MOOB_ONLY="ext4 demo minacl permmatrix buildlimit" make vm-test`:
  `HOMOGENEITY_GAPS_PASS: ext4, …`, `LAYER_LOWER_PASS: ext4 reflink=no` (includes
  `query invalid` and `q2invalid`, which now refuse `pad[3]`), `DEMO_PASS: QUERY2 per domain, v1
  refused to a user, heterogeneous file left out` (EPERM for uid 1001, counters shown to root and
  hidden from uid 1001), `PERMMATRIX_STEP_PASS: 12 seeds, … 281 answers the rule before journal
  0116 gave wrongly, 12 the rule before 0215`, `BUILD_LIMIT_PASS: work denied on ext4, btrfs and
  XFS without published output; uid and mount admission recover; CAP_SYS_ADMIN outside the
  admission`. `minacl` failed in that run on the test's own command (see errors); after the fix,
  `MOOB_VM_KVM=1 MOOB_ONLY=minacl make vm-test`: kernel answer `14 plain`, `15 same`;
  `MINACL_PASS: kernel answered plain and same, left out other0 (minimal ACL other than the mode)
  and named; moobfs-index and moobfs-check equal; python scan: not-run`, `MOOBFS_ONLY_PASS`.
- KASAN/UBSAN/lockdep: `MOOB_ONLY="minacl demo" sh vm/kasan-local.sh`: `DEMO_PASS`,
  `MINACL_PASS: … python scan: equal`, `MOOBFS_KASAN_ONLY_PASS: steps minacl demo under KASAN,
  UBSAN and lockdep with no kernel diagnostics (not a complete run)`. Run although no locking or
  lifetime changed, to have the Python scan in the guest (`python3` exists only there).
- Negative rounds, each on the same tree with one part reverted:
  - M6, `term_check()` without `t->pad[3]`, `MOOB_ONLY=ext4`: `FAIL line 587: ioctl(fd,
    MOOB_IOC_QUERY, &q) == -1 && errno == EINVAL (errno=0 Success)`,
    `MOOBFS_VM_FAIL: query-invalid-ext4`.
  - Kernel ACL, `ext = acl->a_count > 3`, `MOOB_ONLY=minacl`: the kernel answered `16 other0`
    too, `MOOBFS_VM_FAIL: ma-leak: other0 answered to uid 1001, which cannot read it`.
  - Tools, `tools/moobfs-index.c` and `tools/moobfs-check.c` of `e68da30`, `MOOB_ONLY=minacl`:
    kernel `plain`, `same`; moobfs-index `plain` only; `MOOBFS_VM_FAIL: ma-oracle-mismatch`.
  - Python, on macOS: `python3 -m unittest discover -s python/tests` 36 tests OK (18 skipped,
    Linux-only); the class `ExtendedAcl` against the scan of `e68da30` (called with one
    argument): `FAILED (failures=2)` (`test_minimal`, `test_absent_unreadable_malformed`).
- M7 behaviour: `demo` and `buildlimit` above (the admin and non-admin outcomes); no negative
  round, since the outcomes are meant not to change.
- Planting checked by hand first on the host (loop-mounted ext4, Debian 6.12 kernel): debugfs
  1.47.2 `ea_set` stored `system.posix_acl_access (16)`, the mounted file showed `-rw-r--r--+`,
  and `sudo -u nobody cat` was refused for the ACL with `o=-`.

## Errors found on the way

- A first manual check of the planted ACL on the test host ran `chmod 644` after `debugfs`, which
  goes through `posix_acl_chmod()` and the filesystem's `set_acl`, so ext4 dropped the minimal
  ACL and `getfattr` found none; the fixture sets the mode before planting and never chmods
  afterwards.
- The first `minacl` run failed with `cat: No such file or directory`,
  `MOOBFS_VM_FAIL: ma-same-unreadable`: under busybox `command -v cat` prints `cat`, not a path,
  so `asuid` could not open it. The step now uses `query --uid 1001 <dir> open <name>`.
- The first `buildlimit` edit renamed with the same parity as the previous rename (`d/t` no
  longer existed); fixed before any run by advancing `bl_i`.
- checkpatch preferred `0777` to `S_IRWXUGO` in `ext_acl()`; folded into the kernel commit before
  any test.
- The specification's sentence "Files with an extended access ACL stay outside the partition …
  except through condition 1" does not describe the code, which never admits a file with an
  extended ACL; corrected by a dated note, text kept.

## Limitations

- `minacl` runs on ext4 only: debugfs is ext4's tool. XFS and btrfs keep the same
  `posix_acl_update_mode()` behaviour; their `ext_acl()` path is the same kernel code.
- M7 has no negative round: it changes no allowed or denied outcome, only whether an LSM logs a
  denial, and no SELinux/AppArmor audit check was run. The `demo` and `buildlimit` steps show the
  outcomes are unchanged.
- `bench/userspace/udx.h` (the user-space index benchmark) still carries an older rule (others'
  bit only, any stored ACL); it is a benchmark baseline, not a mirror the suite compares, and is
  left as it is.
- Not run: the full `make vm-test` and the full KASAN suite (once per batch, by the coordinator).

## Next step

Maintainer: accept or drop the kernel change of `ext_acl()` (commit "layer: an access ACL is
extended when it may decide otherwise than the mode"); the user-space commit depends on it.
Then the batch's full suite and KASAN, and the public surfaces of the batch.
