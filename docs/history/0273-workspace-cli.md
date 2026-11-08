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
- `grant ro` changed `<ns>`, `shared` and the default ACL only, as the ADR then said
  (superseded by the correction below).
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

## Correction, 2026-11-08: shared files were never found by queries

The first VM run of `make workspace-test` on the test host (with `bab0f82`, which only adds
diagnostics to the guest script) failed at the shared-area query: `a2`, a member of
`moob-proj`, read `shared/s1.md` (`a1:moob-proj 0660`, no ACL), and `moobfs query` of `shared` as
`a2` exited 0 with no result.

The error was in ADR-0035, not in the code: it said files failing the homogeneity rule "are
answered with per-result checks". `docs/spec-query-v2.md` §3 says such a file "does not appear in
v2 queries", and `QUERY2` reads only the homogeneous partition of the domain (`layer/index.c`,
`query_run()`, `name_find(..., false)`); fallback mode adds per-result checks to that partition and
never adds heterogeneous files. With the default ACL `u::rwx,g::rwx,o::---`, every shared file was
`0660`, owned by an agent in a root-owned domain, so it failed both branches of `reach_ok()` and no
shared file could ever be queried. The unit tests could not see this; the VM run did.

Correction (ADR-0035 amended with a dated note that quotes the wrong sentences):

- The shared area's default ACL is `u::rwx,g::rwx,o::r-x`: files `0664`, subdirectories `2775`,
  no access ACL. They pass `reach_ok()`'s first branch (`layer/index.c:659–679`: every class
  reads, no extended ACL), which does not look at the domain directory. The read bit for others
  opens nothing: others cannot traverse `<ns>` (`0750`), and `shared` itself stays `2770`.
- `grant ro` adds `u:<user>:r-x` to the access ACL of `<ns>` and of `shared` only; the default
  ACL is untouched, so files made during a grant are not extended. The ACL on `shared` does not
  change its files' homogeneity (first branch again).
- Agent areas re-checked against spec §3 and `reach_ok()`: a `0600` file `<a>:<a>` in an area
  `<a>:<a>` `0700` passes the second branch (same owner and group, no extended ACL, the area lets
  only the owner class traverse and the file grants it read); a `0644` file passes the first.
- The documented cost is now a shared file made narrower than `0644` (an explicit `0600`, a
  later `chmod`): it is left out of queries and listed by `moobfs-check`.
- Found while checking: a change of mode, owner, group or access ACL of a domain directory puts
  the domain in fallback mode until the next mount (`moobl_index_perm_changed()`,
  `layer/index.c:3106–3122`). `workspace create` does it to every area and `grant`/`revoke ro` to
  `shared`; answers are unchanged, with a permission check per result. Stated in the ADR and
  `docs/operations.md`; the VM script prints `fallback_domains`.
- `vm/workspace-test/guest.sh`: `s1.md` is `a1:moob-proj 664` and found by `a2`; a file made
  during the `ro` grant is `0664` without ACL, found by `a2` and by the `ro` user; a `0600` shared
  file is left out of queries and listed in `left_out`; shared files of the default mode are not.
  Query checks print the exit status, output, error and permissions on failure, as in `bab0f82`.
- `tests/workspace_test.c` checks the new default ACL bytes and text; it passes on macOS (also
  under ASan/UBSan). ShellCheck 0.10.0 and `mandoc -Tlint` clean.

## Evidence on the test host (2026-11-08)

- On `7d4240a`'s content (before the correction): `make test`, `make attr-tools`, `make lint`,
  `make checkpatch`, `make install-unit` and `make json-test` pass; `make man-lint` could not run
  (no `mandoc` on the host; it passed where it was available). The first `make workspace-test`
  failed at "a2's query of shared": the query exited 0 with no result while `a2`, a member of
  `moob-proj`, could read `s1.md` (`a1:moob-proj` `0660`, no ACL); with the diagnostics of
  `bab0f82` this is the left-out heterogeneous file described above.
- On `c984496` (the correction): `make test` passes and `MOOB_HOST_QEMU=1 make workspace-test`
  ends in `MOOBFS_WORKSPACE_PASS` after 412 s under KVM (Debian 13, kernel 6.12.113, after
  `make install`): the refusals (open lower, unlisted kernel, a layer as the lower, depth 1); the
  layout and default ACL `u::rwx,g::rwx,o::r-x`; each agent sees its own area only; `a1`'s shared
  file `0664` without ACL is found by `a2`'s query and read and written by `a2` through the group; a
  `0600` shared file is left out; `grant ro` lets the auditor read and query the shared files,
  including one made during the grant, and nothing else; `revoke ro` restores minimal ACLs;
  `grant rw`/`revoke rw`; `list` counts; `status` (`fallback_domains=3`, as stated above);
  `moobfs-check` clean, with only `narrow.md` left out.

- `make package-test` (in its container; with `MOOB_HOST_QEMU=1` the host lacks `debhelper`)
  failed in phase 1, not on this change: the guest booted `6.12.115+deb13.1-amd64`, a Debian 13
  security kernel that the cloud image's `unattended-upgrades` installed after phase 0
  (`DKMS` refused it, as it must for an unlisted kernel). The kernel was already installed on
  the image's first boot, so holding the packages did not help (a second run failed the same
  way); removing the other kernel packages failed too (a third run: the
  filter took the image's own `-unsigned` package and 6.12.115 still booted). Phase 0 now turns
  automatic upgrades off and tells GRUB to boot the image's kernel (`GRUB_DEFAULT=saved`,
  `grub-set-default`), whatever else is installed. The matrix itself
  lags Debian 13 since that release; adding 6.12.115+deb13.1 is work for journal 0272.

- With the GRUB pin (`976d146`'s content), `make package-test` ends in `MOOBFS_PACKAGE_PASS`
  (Secure Boot, DKMS refusal of an unlisted kernel, key enrollment, unsigned build refused,
  install, upgrade and roll back with 20 decisions equal to the oracle, `make install` copy
  refused), and `make install-test` in `MOOBFS_INSTALL_PASS`, both on the image's 6.12.113.

## Next step

The pre-push checks, then the maintainer's review of ADR-0035 before any pull request.
