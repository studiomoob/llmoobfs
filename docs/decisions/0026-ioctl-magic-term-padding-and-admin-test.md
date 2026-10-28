# ADR-0026 — Keep the ioctl magic, name the term's tail byte, one privilege test for the index

Date: 2026-10-28. Status: **accepted by the maintainer on 2026-10-28** (decisions given with the
assignment of issue #119 M6 and M7; journal 0222). Estimated effort: S (hours). Amends the UAPI
of `include/moobfs_ioctl.h` under ADR-0022 (fixes and hardening only) and
`docs/spec-query-v2.md` §4.1, which carries a dated note pointing here; replaces no decision.

## Context

The fourth review round (journal 0155, issue #119) found two low-severity points of the
interface, both certain:

- **M6.** The ioctl magic `'M'` is registered in
  `Documentation/userspace-api/ioctl/ioctl-number.rst` (Linux 6.12.112, lines 173–177) by four
  in-tree users, one of them for every number (`soundcard.h`; also `mtd-abi.h` 01–16,
  `megaraid_sas.h` 01–03, `fsl-diu-fb.h` 00–0F). And `struct moob_query_term` had one byte of
  implicit tail padding: its fields end at byte 1097 (`name[65]`, three `__u8`, `__u16 len` at
  offset 68, `value[1024]`, `pad[3]`) and its alignment is 2, so `sizeof` is 1098. The kernel
  checked `pad[0..2]` and ignored the 1098th byte, so a caller could put anything there and a
  later version could never give it a meaning.
  `docs/research-stacked-fs-kernel-rules.md` R-UAPI-6 already noted the magic.
- **M7.** Three call sites decided "administrator" for the index with two functions:
  `capable(CAP_SYS_ADMIN)` in `build_admit()` (exemption from the per-uid and mount-wide build
  admission) and in `do_query()` (mount-wide `QUERY` with domains on), and
  `ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN)` in `do_get_info()` (the index counters,
  ADR-0011 F1). In 6.12.112 both reach `ns_capable_common()` (`kernel/capability.c:351`) on the
  initial user namespace and set `PF_SUPERPRIV` on a grant; they differ only in
  `CAP_OPT_NOAUDIT`, that is, whether an LSM (SELinux, AppArmor) logs a denial. The same
  privilege was thus audited on two paths and not on the third.

## Decision

1. **The magic stays `'M'`.** The command numbers are part of the frozen UAPI (ADR-0022), and a
   collision is harmless here: the commands are issued only on a directory of a mounted layer,
   `moobl_root_ioctl()` returns `ENOTTY` for anything else, and each request carries a version
   and is validated before use. If MoobFS is ever proposed upstream, a free magic is chosen and
   registered in `ioctl-number.rst` then (`Documentation/process/submit-checklist.rst`), as a
   new UAPI version.
2. **The tail byte becomes a named reserved field.** `pad[3]` becomes `pad[4]`; `pad[3]` is the
   byte that was implicit padding, and `term_check()` returns `EINVAL` when any byte of `pad` is
   not zero, for `QUERY`, `QUERY2` and every query of `QUERY2_BATCH`. A `BUILD_BUG_ON` in
   `moobl_index_init()` fails the build if the term ever has implicit padding again. A wider
   name (`reserved`) was not added beside `pad`: one zero-checked array is what the other
   structures of the header use (`moob_query2.pad[5]`).
3. **One privilege test.** `moobl_index_admin()` in `layer/index.c` is the only test, used by the
   three sites: `ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN)`. The capability and the
   namespace do not change, so who may run the mount-wide `QUERY`, read the counters or skip the
   build admission does not change. No audit record on denial, because denial is the expected
   outcome for nearly every caller: every client that calls `GET_INFO` and every first build of a
   domain by an ordinary user goes through the test, and an audited test would log an LSM denial
   for each, noise an administrator learns to ignore. The kernel does the same where a privilege
   only widens what an ordinary caller already gets: `kallsyms_show_value()`
   (`kernel/ksyms_common.c:28`, `CAP_SYSLOG` without audit to show addresses that are zeros
   otherwise) and `cap_vm_enough_memory()` (`security/commoncap.c:1401`, `CAP_SYS_ADMIN` without
   audit for the reserve). A grant still marks the task `PF_SUPERPRIV`, so privilege use stays
   accounted.

## Alternatives considered

- **A new magic now.** Rejected: it changes every command number, a break of the frozen UAPI for
  a collision with no effect, and a free registration matters only for an upstream submission.
- **Leave the padding implicit and document it.** Rejected: the byte would stay unchecked, so it
  could never carry a meaning, and `-Wpadded` or `pahole` would keep reporting it.
- **`capable()` everywhere** (audit every denial). Rejected for the reason above; it would also
  change `GET_INFO`, which is already unaudited, from silent to noisy for every client.
- **Audited test for `do_query()` only**, the one site where denial refuses the operation
  (`EPERM`). Rejected by the maintainer's decision for one test with one behaviour; what is lost
  is an LSM record of an ordinary user trying the mount-wide `QUERY` with domains on, which the
  caller sees as `EPERM` anyway.
- **`CAP_DAC_READ_SEARCH` instead of `CAP_SYS_ADMIN`.** It is the narrower fit for "see what
  discretionary access control would show", which is what the mount-wide `QUERY` and the
  counters reveal. Recorded and not taken: it changes who may query, which the maintainer
  excluded, and a process with `CAP_DAC_READ_SEARCH` alone (a backup tool) would gain the
  exemption from the build admission, which is about memory and CPU, not reading.

## Compatibility

No on-disk format change. The structure sizes, offsets, command numbers and request versions do
not change (`sizeof(struct moob_query_term)` stays 1098, `pad` stays at offset 1094), so a binary
built against the old header issues the same ioctls. A caller that zeroes the request
before filling it, as `tools/`, `tests/`, the CLI, the SQLite extension, `bench/`, the `ctypes`
library (`python/src/moobfs/_uapi.py`, `ctypes.Structure` zero-initialises) and
`bench/client/langgraph_store.py` all do, is unaffected. A caller that leaves a nonzero byte at
offset 1097 of a term now gets `EINVAL` where it was accepted; the README marks the interface
experimental, and `docs/compatibility.md` §3 allows a reserved field checked for zero. The
privilege change has no visible effect on the answers or errnos; under an LSM, denials of
`CAP_SYS_ADMIN` from `QUERY` and from first builds are no longer logged.

## Evidence

`tests/query.c` refuses `pad[3]` in `QUERY` and in the first and second terms of `QUERY2`; it
fails against a module whose `term_check()` reads only `pad[0..2]`. The `demo` step checks the
`EPERM` of `QUERY` and the hidden counters for uid 1001 and both granted to root; `buildlimit`
checks that root builds a domain the mount-wide admission refuses. Commands and markers:
journal 0222.
