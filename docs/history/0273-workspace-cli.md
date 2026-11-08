# 0273 — The workspace commands of the moobfs CLI (ADR-0035)

Date: 2026-11-08, morning, Brasília time; the local implementation round ended at 10:15 by the
host clock. Status: **in progress**: implemented and unit-tested locally, the Linux build checks
and the VM acceptance run are pending. Estimated effort M (one to three days), from ADR-0035;
labor time is recorded when the work closes.

## Goal

Stage 2 of `docs/distribution-strategy.md` (§8) lists a minimal `moobfs` CLI (§3) among its
deliverables, and its first acceptance step runs `workspace create`. The maintainer chose on
2026-11-08 to implement `mount`, `status` and `workspace` before stage 2, in user space only
(ADR-0022), following ADR-0035 (proposed), so that its review can read working code.

## Design

The specification is [ADR-0035](../decisions/0035-workspace-layout-and-cli.md); it is still
proposed, so `docs/distribution-strategy.md` §2.2 and §3 are left as they are (they still say
the CLI is not implemented and describe the older layout) until the maintainer accepts it.

- Files: `tools/workspace.h` holds what makes no system call (names, the POSIX ACL xattr codec
  and its mask, the plan of `workspace create`, the option parser, the mount data string, the
  supported-kernels match, the test of the lower's ancestors), all `static inline`, so that
  `tests/workspace_test.c` checks it on macOS and Linux in `make test`. `tools/moobfs-workspace.c`
  holds the workspace commands; `tools/moobfs.c` gains `mount` and `status`, and shares
  `moobfs_layer_root()`, `moobfs_count_exists()` (the existing `run_query2()` path) and
  `moobfs_usage()` through `tools/moobfs-cli.h`. `info`'s printing became `show_info()` so that
  `status` reuses it.
- ACLs are written as the xattrs `system.posix_acl_default` and `system.posix_acl_access` in
  the VFS's form, with the entry order of `posix_acl_valid()` and the mask recomputed as
  `setfacl` does; `tests/permmatrix.c`'s hand encoding was the model. An access ACL left
  without named entries is removed and the mode set from its base entries, rather than written
  minimal, so the result does not depend on the lower's `set_acl` dropping an equivalent ACL.
- Process and file hygiene: shadow-utils commands run with `posix_spawn` and an argument
  vector, from a fixed list of system directories rather than the caller's `PATH` (root runs
  them), with a fixed environment and their standard output on standard error; directories are
  made at `0700` with `mkdirat` and changed through descriptors opened `O_NOFOLLOW | O_CLOEXEC`.
  Every check of `create` runs before its first change; a failure stops and lists the steps
  done, with no rollback.
- `mount`: the list is read from `/usr/share/moobfs/supported-kernels` or
  `/usr/local/share/moobfs/supported-kernels`. Neither the package nor `make install` installed
  it before; `packaging/debian/rules` and `scripts/install.sh` (with its uninstall) now do. The
  lower check requires some directory above the lower to be root's alone (owner root, no search
  bit for others or for a non-root group, no access ACL); the lower itself becomes the mount
  root and keeps `0755`.

## Choices where ADR-0035 left room

- A namespace is at most 27 bytes, so that `moob-<ns>` fits the 32 bytes `groupadd` accepts:
  Debian 13's `passwd` `1:4.17.4-2` refuses a 33-byte group name (checked in a `debian:13`
  container).
- Exit statuses: 65 for an invalid name, a missing user or group, a namespace that is not a
  workspace, a refused lower or options the layer refused; 69 for a depth other than 2 and for a
  kernel outside the list or no list; 77 for a non-root caller; 1 for an existing namespace or a
  failed step.
- An existing group `moob-<ns>` is reused; a missing user whose name is already a group is
  refused, since `useradd --user-group` would fail.
- `grant ro` changes `<ns>`, `shared` and the default ACL only, as the ADR says: files already in
  `shared` stay unreadable to the user, and files created during the grant keep the entry after
  `revoke` (unreachable, the user can no longer enter `<ns>`). Both are stated in
  `docs/operations.md`; a recursive `setfacl` is not done.
- `status` without a mount point lists the layer mounts whose root is the filesystem's root and
  exits 69 when the module is not loaded.

## Changes

`tools/workspace.h`, `tools/moobfs-workspace.c`, `tools/moobfs-cli.h`, `tools/moobfs.c`,
`tests/workspace_test.c`, `Makefile` (`workspace_test` in `make test`, the second source of
`moobfs`, `make workspace-test`), `packaging/debian/rules`, `scripts/install.sh`,
`scripts/test-install.sh`, `vm/workspace-test/guest.sh`, `vm/install-test/in-container.sh` and
`run.sh` (`MOOB_TEST=workspace`), `docs/tools.md`, `docs/operations.md` (section 3 and a
workspace subsection of section 5), `man/moobfs.1`, `docs/references.md` §49, `CHANGELOG.md`.

## Evidence so far (not the test host)

- `tests/workspace_test.c` on macOS (Apple clang, the `make test` flags, and again under
  ASan/UBSan): all checks pass.
- In a `gcc:14` container (Linux, arm64): `make attr-tools` and `make test` pass with the
  project's `-Werror` flags, the static build too (glibc's usual link warnings that
  `getpwnam` and the like need its NSS libraries at run time); the refusals of `mount` (no
  list, a lower reachable by others, a non-root caller) and of `workspace` (invalid names,
  `shared`, not a layer) give the documented messages and statuses; `mount(2)` itself was
  refused there with `EPERM`, as a container should.
- `sh scripts/test-install.sh` in a `debian:13` container: `INSTALL_UNIT_PASS`. ShellCheck
  0.10.0 on the changed scripts and `mandoc -Tlint -Wwarning man/*`: clean.

Not tested yet: nothing has run against a mounted layer. The VM acceptance (`make
workspace-test`), `make lint`, `make checkpatch` and the build on the test host are pending.

## Next step

Run on the test host: `make test`, `make attr-tools`, `make lint`, `make checkpatch`,
`make install-unit`, then `make workspace-test` (and `make package-test`, since the package now
installs the list); record the results here, then the maintainer's review of ADR-0035.
