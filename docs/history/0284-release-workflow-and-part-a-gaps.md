# 0284 — The release workflow under immutable releases, Part A's remaining gaps, and the release decisions of 2026-11-11

Date: 2026-11-11. Work started 11:42 Brasília time; ended 12:32; second round 12:34–12:38. The
times cover the documents, the workflow and the local checks (the VM runs are not included;
the VM rounds of the evening and night are dated in their sections). Status: **done
locally, not published; every VM run of the batch done** ("Batch runs before publication",
below). Estimated effort S–M; the time recorded is calendar time of one session, close to the
effort.

## Goal

Two things a read-only audit of the stage-2 conditions found to be ours (kept outside the
repository): `.github/workflows/release.yml` would break on the next tag, and Part A of
`docs/review-findings.md`, the part inside criterion (2) of ADR-0030, still listed findings with
no regression test, with the register still marked "draft". Also: record the maintainer's release
decisions of 2026-11-11 and correct two stale lines of `docs/security-roadmap.md`. The kernel is
frozen (ADR-0022): nothing under `layer/`, the UAPI or the format changes.

## Decisions (maintainer, 2026-11-11, about 12:30 Brasília time)

1. The tag `v0.6.0-experimental` is cut only when stage 2 is declared.
2. "A third party reproduces everything" (§8, stage 2, "Done when") means Parts A and B of the
   route of issue #195: the core suite with acceptance, and fstests `quick` over the layer. Part
   C, a syzkaller campaign, is welcome and not required.
3. A reproduction at `d96b0db` counts for the commit that is tagged if nothing under `layer/`
   changes between the two.
4. The `.deb` packages (`moobfs-dkms`, `moobfs-tools`) stay unpublished until stage 2 is declared.
5. The version is `0.6.0`, not `0.5.1`. Justification: the rule of journal 0142 ("a patch
   version: the UAPI and the on-disk conventions do not change") says when a patch is enough for
   compatibility; it does not make every compatible change a patch. Since `v0.5.0-experimental`
   the UAPI header changed only in comments and the on-disk conventions did not, but the code
   adds functionality: the layer module builds and is supported on Linux 7.3 (ADR-0034, journals
   0275 and 0277) and on Ubuntu's 7.0 series (ADR-0036, journal 0281), and the `moobfs` CLI gains
   the workspace commands (ADR-0035, journal 0273). Semantic Versioning, whose
   `MAJOR.MINOR.PATCH` scheme `docs/compatibility.md` §5 uses, marks new backwards-compatible functionality with a minor
   version and keeps a patch for fixes. Alternative: `0.5.1`, by the letter of the 0142 rule; not
   taken, since a user reading `0.5.1` would expect fixes only.

Carried into `docs/distribution-strategy.md` §8 (amendment dated 2026-11-11, decisions 1 to 4),
the "Public release" row of `ROADMAP.md` (1 to 4) and `docs/compatibility.md` §5's list of tag
decisions (1 and 5).

## The release workflow under immutable releases

**Defect.** Immutable releases are on for the repository (`gh api
repos/studiomoob/llmoobfs/immutable-releases` → `enabled: true`, since about 2026-10-30;
`v0.5.0-experimental` predates it). The `archive` job created the pre-release already published,
with three assets; the `provenance` job (the SLSA generic generator v2.1.0, `upload-assets:
true`) then attached `llmoobfs-<tag>.intoto.jsonl`. GitHub's documentation, read through Context7:
with immutable releases on, "you cannot add, replace, or delete assets after a release is
published, and you cannot move or delete its tag while the release exists"; a deleted immutable
release's tag name cannot be used again. So the next tag would have ended with a published
release missing its provenance, and a deletion to retry would have burned `v0.6.0-experimental`.

**Design** (GitHub's recommended order: draft, attach every asset, publish):

- `archive` creates the release with `gh release create --draft --verify-tag --prerelease` and
  the archive, its SHA-256 and the Sigstore bundle. On a rerun it uploads to a draft
  (`--clobber`) and refuses a release that is already published.
- `provenance` keeps the generator, referenced by its tag as before, with `upload-assets: false`.
  The generator stores the signed provenance as a workflow artifact named by its
  `provenance-name` output. Its own uploader (`softprops/action-gh-release` v2.2.1) finds the
  release by tag; the REST lookup by tag returns published releases only, and whether that action
  would find the draft or create a second release was not established, so it is not used.
- A new job `publish` (needs both) downloads that artifact with `actions/download-artifact`
  v8.0.2, pinned by commit, checks its name and that it is not empty, checks the release is still
  a draft, uploads the provenance, checks the release has exactly the four expected assets, and
  publishes it with `gh release edit --draft=false --prerelease`. If anything fails, the release
  stays a draft, which can be fixed or deleted without burning the tag name.
- Template values reach the shell through `env:` only; permissions stay per job (`contents:
  write` for the jobs that touch the release; the generator keeps the permissions its reusable
  workflow declares).

**Validation.** actionlint 1.7.7 (the image of journal 0211, pinned by digest; it runs shellcheck
over each `run:` block) on `release.yml`: no finding. Over every workflow it reports only two
`info` findings already in `fuzz.yml` (SC2015, SC2012), not touched here. `scripts/check-pins.sh`:
`PINS_OK`. The workflow was **not exercised**: it runs only on a pushed `v*-experimental` tag,
and none was pushed.

**Proposed fork test, not done.** In a fork with immutable releases turned on: push a tag such as
`v0.0.1-experimental` on a throwaway commit; expect the draft with three assets after `archive`,
the provenance artifact after `provenance`, and a published pre-release with four assets after
`publish`; then download the assets and run the commands of `docs/security-operations.md`
(`sha256sum -c`, `python -m sigstore verify github`, `slsa-verifier verify-artifact` with the
fork's repository). A second tag whose `publish` job is made to fail (for instance by a fifth
asset) must leave a draft, deletable without burning the name. Not done here: it needs the
maintainer's agreement and a fork.
*Note, 2026-11-12 (journal [0288](0288-release-rehearsal-in-a-fork.md)):* done, in the
maintainer's fork, with the maintainer's agreement; both expectations held.

## Part A of the review register

The audit listed Gaps items 1, 2, 3, 9, 10, 11 and 12 of `docs/review-findings.md` as Part A
findings without a regression test. One by one:

| Finding | Test | Marker | Local result | VM | Decision needed |
| --- | --- | --- | --- | --- | --- |
| 0114 item 3, ref-walk revalidation reads a lower dentry `moobl_instantiate()` may free | none | — | — | — | yes: a test that fails without the fix needs a hold point inside ref-walk revalidation (a kernel change); a stress race under KASAN would pass on the fixed code and fail only by chance on the old one |
| 0114 item 6, long walks without `cond_resched()` | none | — | — | — | yes: the only symptom is scheduling latency or a soft lockup, reached only with a domain far beyond the index budget or a scheduler-latency probe; journal 0120 chose no test |
| 0114 item 8, directory reads with the caller's credentials | none | — | — | — | yes: the two differ only in what an LSM decides; testable without a kernel change under the SELinux harness of S12 (`vm/selinux-local.sh`, not in the suite), effort M |
| V8, rename copied the lower's whole `i_flags` | none | — | — | — | yes: no flag outside the copied mask (`S_SYNC`, `S_NOATIME`, `S_APPEND`, `S_IMMUTABLE`) has an effect the VFS shows on the layer inode after a rename that succeeds on ext4, btrfs or XFS; `S_SWAPFILE`, the one with a clear effect, makes the rename fail first (`may_delete()`) |
| V2, `sync_file_range(2)` succeeds and writes nothing | `tests/passthrough.c` limits | `PT_LIMITS_PASS` | compiled (`-static -O2 -Wall -Wextra -Werror`) | pending | no; a pin of the documented limit: no negative round, since nothing was fixed; it fails if the limit is ever lifted |
| V1 (`cachestat(2)` part) | same case | `PT_LIMITS_PASS` | compiled | pending | no; as V2 |
| M7, `capable()` and `ns_capable_noaudit()` | none | — | — | — | yes: no allowed or denied outcome differs, only whether an LSM logs; testable under SELinux audit as item 8 |
| M8.2 `getattr` | none | — | — | — | yes: on 6.12 `vfs_getattr()` adds `AT_GETATTR_NOSEC` itself, so the two paths cannot be told apart (journal 0221) |
| M8.3 `flush` | none | — | — | — | yes: none of ext4, btrfs and XFS has `->flush` |
| M8.4 `fileattr_set` | not applicable | — | — | — | no: not a defect (journal 0221) |
| M8.5 `SB_I_NOEXEC` documentation | `tests/passthrough.c` noexec | `PT_NOEXEC_PASS` | compiled | pending, with a negative round | no |
| M8.6 `scan_delay_us` kept | not applicable | — | — | — | no: a parameter kept with its reason; no behaviour |

`PT_LIMITS_PASS` writes 1 MiB through the layer without `fsync`, calls `sync_file_range` with
`WAIT_BEFORE | WRITE | WAIT_AFTER` on the layer descriptor (must return 0) and reads `cachestat(2)`
on both descriptors: the layer's shows no cached page, the lower's shows cached and dirty pages;
the same call on the lower descriptor then cleans them (the control). `PT_NOEXEC_PASS` checks the
errnos `docs/when-not-to-use.md` §3 states: `access(X_OK)` and `execve` of a 0755 script `EACCES`,
`mmap` with `PROT_EXEC` `EPERM`, and a character device (1, 3) made with `mknod` on the lower
cannot be opened through the layer (`EACCES`). `docs/testing.md` describes both in the
`PASSTHROUGH_PASS` row.

The register is **no longer marked "draft"**: every Part A finding is tested, answered as a
design objection, or listed for the maintainer's decision with its reason (Gaps, the note on
items 1 to 3 and 9 to 12). Its status line says the VM runs of the new tests are pending.

## Decision on the Part A items (maintainer, 2026-11-11, about 13:00 Brasília time)

Second round of this journal, 12:34–12:38 BRT. Checks rerun at the end: shellcheck 0.9.0, SPDX,
`STEPS_FILTER_PASS`, `RETIRED_CLAIMS_OK`, `PINS_OK`, `MATRIX_PAGE_OK`, the site and FAQ checks,
`make test` and `git diff --check origin/main`: pass.

1. **0114 items 3 and 6, V8, M7, M8.2 and M8.3 are recorded exceptions to criterion (2)** of
   ADR-0030: each fix was checked against the code, but cannot be shown by a regression test
   without changing the frozen kernel, or has no symptom observable on the supported kernels and
   lowers. The reasons are those of the table above, now in a dated amendment of ADR-0030 (a
   table, one reason per finding), in a note on `docs/distribution-strategy.md` §8, and in the
   rows and the Gaps note of `docs/review-findings.md`. Alternative: keep them as open gaps of
   criterion (2), which would keep stage 2 waiting on tests that need the freeze lifted; not
   taken. A change to the freeze reopens them.
2. **0114 item 8 gets a regression test under the SELinux setup of S12**, without a kernel
   change. It stays listed as pending in the register until its run and its negative round pass.

No current-state sentence became false: the earlier "needs the maintainer's decision" wording is
only in dated notes of the register and of this journal, so `docs/retired-claims.txt` is
unchanged by this round.

### The test of 0114 item 8

What it must tell apart: `moobl_iterate()` calls `iterate_dir()` on the lower directory file,
which `moobl_dir_open()` opened with the mounter's credentials. `iterate_dir()` calls
`security_file_permission()`. SELinux (`selinux_file_permission()`, `security/selinux/hooks.c`,
Linux 6.12) skips its check when the current task's SID is the SID that opened the file; otherwise
`file_has_perm()` first checks `fd { use }` from the current SID to the file's opener SID, then
the directory's own permission. Since journal 0120 the read runs under `override_creds()` with
the mounter's credentials, so the opener's SID and the current SID are the same; before, the
caller's SID was checked, `fd use` included.

How the test makes that visible: in the guest of `vm/selinux-local.sh` the layer is mounted by
the guest's root, in `kernel_t` (the guest has no init policy), so `kernel_t` opens every lower
directory file. A new domain of `vm/selinux/moobtest.te`, `moobtest_reader_t`, may list
`moobtest_dir_t` (the lower directory's type, which the layer's directories carry too under the
`fs_use_xattr` rule) and has no `fd use` on `kernel_t`. `tests/listdir.c`, run in that domain,
opens a directory and reads it with `getdents64`:

- control 1, `listdir fdprobe` with descriptor 3 opened by the `kernel_t` shell: it reads nothing,
  because SELinux replaced the descriptor at exec (`flush_unauthorized_files()`); otherwise the
  domain has `fd use` on `kernel_t` and the step fails as proving nothing;
- control 2, `listdir <lower>/t`: the direct listing succeeds (exit 0);
- the check, `listdir <layer>/t`: must succeed. With the caller's credentials in
  `moobl_iterate()`, the `fd use` check from `moobtest_reader_t` to `kernel_t` is denied and
  `getdents64` fails with `EACCES` (exit 11).

It runs in the `fsuse` run only, prints `  fsuse dirread: direct=<n> layer=<n>` and
`SELINUX_DIRREAD_PASS`, and fails with `MOOBFS_SELINUX_FAIL: fsuse: item 8: ...`. Files:
`tests/listdir.c`, `vm/selinux/moobtest.te`, `vm/selinux-guest.sh`, `vm/selinux-local.sh` (builds
`listdir`), `docs/testing.md` (SELinux section). Local evidence: `listdir.c` compiled with
`-static -O2 -Wall -Wextra -Werror`; shellcheck 0.9.0 on both scripts clean. The policy module was
not compiled here (it needs `selinux-policy-dev`, which this Mac does not have and which was not
installed).

**Planned negative round:** on a scratch branch, remove the `override_creds()` and
`revert_creds()` pair around `iterate_dir()` in `moobl_iterate()` (`layer/main.c`), rebuild the
module, run `sh vm/selinux-local.sh`; expected `  fsuse dirread: direct=0 layer=11`, an
`avc: denied { use }` line for `moobtest_reader_t` on `kernel_t` in `dmesg`, and
`MOOBFS_SELINUX_FAIL: fsuse: item 8: a directory read through the layer was checked with the
caller's credentials (exit 11)`.

## Stale lines of the security roadmap

- S6: "Open: a build-time refusal of an unlisted kernel (`make install` and the DKMS package
  refuse one, `make layer` does not)". Closed by journal 0277: `make layer` refuses by API series
  (`layer/moobl_compat.h`), as `docs/compatibility.md` "Open points" already says.
- S14 to S21: "S16 done locally". S16 (ADR-0033, journal 0271) is on `main` since pull request
  187, merged on 2026-11-08.
Both old wordings are added to `docs/retired-claims.txt`.

## Evidence

On this Mac, 2026-11-11, 11:42–12:32 BRT: actionlint as above; `tests/passthrough.c` compiled
with the flags of `vm/build-tests.sh` in the `golang:1.24-bookworm` image already present.
shellcheck 0.9.0 over the files `make lint` lists, `scripts/check-spdx.sh`,
`scripts/test-steps-filter.sh` (`STEPS_FILTER_PASS`), `scripts/check-retired-claims.sh`
(`RETIRED_CLAIMS_OK`), `scripts/check-pins.sh` (`PINS_OK`), `scripts/check-matrix-page.sh`
(`MATRIX_PAGE_OK`), `scripts/check-site.py`, `scripts/site-faq.py --check`, `make test` and
`git diff --check origin/main`: pass.

### Finding E of journal 0083 under KASAN (Mac mini, 2026-11-11 evening)

The test host came back at about 20:00 BRT. Clone of this branch at `a460a7b` in a disposable
directory; KASAN kernel 6.12.116 from the local cache, `MOOB_ONLY=fallbackfault sh vm/kasan-local.sh`.

- First positive round, 20:16–20:42: `MOOBFS_VM_FAIL: ff-answered-without-checks-1`, with the
  test's own message "no allocation failed under moobl_index_perm_changed: nothing tested". The
  test was wrong, not the module: its setup set the modes of the domain directory and the
  subdirectory with `chmod` through the layer, and each of those calls already stored the
  domain's note in `ix->fallback`. The injected `chmod` then only replaced an existing entry,
  which allocates nothing, so failslab had no allocation to fail. The setup now creates both
  directories under `umask(022)` with `mkdir(..., 0755)` and no `chmod`, so the injected `chmod`
  is the first to store the note.
- Positive round with that change, 20:44–20:58: `MOOBFS_KASAN_ONLY_PASS`;
  `FALLBACKFAULT_RESULT injected=0 faults=0 top=1 sub_file=0` and
  `injected=1 faults=2 top=1 sub_file=0` (two allocations failed under
  `moobl_index_perm_changed()`, and the file below the 0700 directory stayed hidden from uid 1001);
  no kernel diagnostic.
- Negative round on a scratch copy, `WRITE_ONCE(ix->fallback_all, true);` replaced by `(void)0;`
  (deleting the line would make the next statement the body of the `if`; an empty `;` is refused by
  the kernel build's `-Werror=empty-body`): `injected=1 faults=2 top=1 sub_file=1`, "0083 E: a file
  below a directory uid 1001 cannot search was answered", `MOOBFS_VM_FAIL:
  ff-answered-without-checks-1`. The test fails without the line and passes with it.

### Image setup and passthrough cases (Mac mini, 2026-11-11 evening)

Same clone, at `ea62dc1`; each negative round on a fresh clone of it with one edit.

- `sudo make image-setup-unit` (0211 item 3 of journal 0283): `IMAGE_SETUP_UNIT_PASS: clean
  image set up; a link to the host inside the image stopped the write`. Negative round,
  `loop,nosymfollow` changed to `loop` in `vm/syzkaller/image-setup.sh`: `IMAGE_SETUP_UNIT_FAIL:
  tampered image accepted`, exit 2.
- `MOOB_ONLY="ext4 btrfs xfs" make vm-test` on the Debian 6.12 guest: `PT_LIMITS_PASS` and
  `PT_NOEXEC_PASS` once on each of the three lowers, `MOOBFS_ONLY_PASS: steps ext4 btrfs xfs`.
  Negative round, `sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV | SB_I_SKIP_SYNC;` reduced to
  `SB_I_SKIP_SYNC` in `layer/main.c`, `MOOB_ONLY=ext4`: `PT_LIMITS_PASS`, `PT_NOEXEC_FAIL`,
  `MOOBFS_VM_FAIL: passthrough-ext4`. `PT_LIMITS_PASS` held on all three lowers, so the
  write-back concern noted below did not show.

### The SELinux test of 0114 item 8 does not work, and what it found (2026-11-11 night)

`sh vm/selinux-local.sh` on the test host (Debian 13, Linux 6.12 guest, the host's
`selinux-policy-default` 2:2.20250213-13~deb13u1): `MOOBFS_SELINUX_FAIL: fsuse: item 8 control:
the reader domain used a descriptor of kernel_t (139); the step proves nothing`, the same with
and without the negative edit. Debugged on scratch copies, with the guest's audit printk limit
off (`/proc/sys/kernel/printk_ratelimit` 0):

1. **Exit 139 is SIGSEGV at exec.** Every program run in `moobtest_reader_t`, `busybox true`
   included, died. The denial is `{ use }` from `moobtest_reader_t` to `kernel_t` on the
   program's own executable: exec opens it with the caller's credentials, and mapping it in the
   new domain needs `fd use` on the caller's domain. A domain entered from `kernel_t` without
   that rule cannot run anything, so the design (no `fd use` on `kernel_t`) cannot work.
2. **The premise was wrong.** The design assumed the lower directory file belongs to the
   mounter's SID because `moobl_dir_open()` passes the mounter's credentials to `dentry_open()`.
   SELinux sets a file's SID from `current_sid()` when it is allocated
   (`selinux_file_alloc_security()`), not from the credentials passed in, and
   `moobl_dir_open()` does not switch credentials. The lower directory file therefore carries the
   caller's SID. With a second mount made in a permissive domain `moobtest_mounter_t` and the
   reader allowed `fd use` on `kernel_t`, the listing succeeded with and without the negative
   edit: the old `moobl_iterate()` ran as the caller on a file with the caller's SID, so
   `selinux_file_permission()` skipped its check, and the current one runs as the mounter.
3. **Finding (not fixed).** In that same run the current code logged, for the mounter,
   `avc: denied { use } ... scontext=moobtest_mounter_t tcontext=moobtest_reader_t tclass=fd
   permissive=1` on `/run/se/lower/t`, and the code with the negative edit did not. Since journal
   0120 a directory read through the layer runs as the mounter on a file that has the caller's
   SID, so under SELinux the mounter's domain needs `fd use` on every caller's domain. A confined
   mounter without that rule gets `EACCES` on every directory read through the layer; an
   unconfined one (as in all our runs, `kernel_t`) does not notice. Regular files look the same:
   `moobl_open()` calls `backing_file_open()` with the mounter's credentials but without
   `override_creds()`, and reads and writes switch to the mounter's credentials. overlayfs opens
   both under `ovl_override_creds()` (`ovl_dir_open()`, `ovl_open_realfile()` in Linux 6.12),
   so its real files carry the mounter's SID. The file case was read in the source, not run.

Consequences: the committed SELinux test of item 8 (3e5d739) fails on every run and proves
nothing; the attempts above were not committed.

**Maintainer's decision, 2026-11-11, about 21:30 Brasília time:** record point 3 as a documented
stage-2 limit and fix it in a later batch; revert the item 8 test (da67b9c) and make 0114 item 8
a seventh exception to criterion (2) (ADR-0030 amendment of that time). Surfaces updated:
`docs/matrix-and-limits.md` (LSMs), `docs/when-not-to-use.md` (SELinux), the S12 row of
`docs/security-roadmap.md`, `docs/review-findings.md`, `docs/distribution-strategy.md` §8,
`CHANGELOG.md`, `docs/references.md`. Telling the old and the current `moobl_iterate()`
apart under SELinux needs a confined mounter, and with the code as it is that test would fail on
the current code because of point 3. Correcting point 3 is a change in `layer/`, which ADR-0022
freezes: it needs the maintainer's decision.

## Pending at the time (test host unreachable; all done since, see "Batch runs before publication")

In addition to the items of journal 0283, "Pending" (its `fallbackfault` and `image-setup-unit` items are done, above; item 1 below is done too):

1. `MOOB_ONLY=ext4 make vm-test` (the step that runs `tests/passthrough.c`; then btrfs and xfs) →
   `PT_LIMITS_PASS` and `PT_NOEXEC_PASS` beside `PASSTHROUGH_PASS` on each lower. Negative round
   for `PT_NOEXEC_PASS` on a scratch branch: drop `SB_I_NOEXEC | SB_I_NODEV` from
   `sb->s_iflags` in `layer/main.c` → `PT_NOEXEC_FAIL` and `MOOBFS_VM_FAIL: passthrough-ext4`.
   `PT_LIMITS_PASS` has no negative round (nothing was fixed); if a lower writes its dirty pages
   back before `cachestat` reads them, the case fails and must be revisited.
2. *(Withdrawn 2026-11-11 night: the test was reverted, see above.)* `sh vm/selinux-local.sh` on a Debian host with KVM and the packages its header lists →
   `SELINUX_DIRREAD_PASS` and `MOOBFS_SELINUX_RESULT: fsuse ...`; then the negative round planned
   above (`override_creds()` removed from `moobl_iterate()`) → `MOOBFS_SELINUX_FAIL: fsuse: item 8`.
3. The full `make vm-test` and the full KASAN suite, once for the batch.

## Limitations

- `release.yml` is linted, not run; the fork test above is proposed, not done. *(Note,
  2026-11-12: run in a fork, journal 0288.)*
- Seven Part A items are recorded exceptions to criterion (2), with no test (six by the decision
  of 13:00, 0114 item 8 by the decision of about 21:30).
- A confined mounter under SELinux needs `fd use` on every caller's domain (point 3 of the
  SELinux section); documented as a limit, the correction is proposed separately.

## Batch runs before publication (Mac mini, 2026-11-11 night)

Branch head `8c1cc51` for the pre-push checks; the VM suites ran on `a460a7b`, whose `layer/` and
tests are the same as the head's except for the reverted SELinux test (`da67b9c`), which the
suites do not run.

- Full `make vm-test`, ended 22:14: `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, rc=0.
- Full local KASAN/UBSAN/lockdep suite (`sh vm/kasan-local.sh`, kernel 6.12.116), ended 23:26:
  `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with no kernel diagnostics`,
  rc=0.
- Pre-push checks on `8c1cc51` (diffcheck base `d96b0db`), ended 23:32: lint, checkpatch, test,
  asan, hostile (clang/ASan), pylib, retired, pins, mutants, site, diffcheck and ubuntu-tests all
  PASS.
- `make container-test` on `8c1cc51` (01:13–01:25 on 2026-11-12, 695 s under KVM; its last run
  was on 2026-10-29): `MOOBFS_CONTAINER_PASS: docs/container-integration.md followed on Debian
  13 with Docker and Podman`, rc=0.
- Route A5 of issue #195 run as the issue states it, `KCONFIG=vm/kcsan.config
  KCACHE=$HOME/.cache/moobfs-kcsan sh vm/kasan-local.sh`, on a clean clone at `d96b0db`
  (23:48–01:13, kernel 6.12.116): `MOOBFS_PYLIB_PASS`, `MOOBFS_ACCEPTANCE_PASS`,
  `MOOBFS_LAYER_PASS`, `MOOBFS_KASAN_PASS: layer suite under KCSAN and lockdep with no kernel
  diagnostics`, rc=0. The command of the route works as written; nothing in it needs a change.

## The Debian 7.3.3 kernel left sid (2026-11-12 morning)

The first CI run of the pull request (#196) failed in `Native Linux VM (Debian 7.3.3)` after 13
s: `E: Unable to locate package linux-image-7.3.3+deb14-amd64`. Debian sid moved to `7.3.4-1`
and no longer carries 7.3.3; the job fails the same way on `main`, nothing in the batch caused
it. Choice: keep the tested kernel and fetch it from snapshot.debian.org at a fixed time
(`20261110T000000Z`) in `vm/Dockerfile`, only for the 7.3.3 image, over http (the slim image has
no CA certificates before its first install; apt checks the archive's signature, and
`check-valid-until=no` only accepts the snapshot's expired `InRelease`). Alternative not taken:
move the supported kernel to 7.3.4, a change to the support matrix and to every surface that
names 7.3.3, for the maintainer to decide. On the test host the image build fetched
`linux-image-7.3.3+deb14-amd64 7.3.3-1` and its dependencies from the snapshot.

## Next step

The maintainer's decision on the fork test; then the batch's pull request with the maintainer's
agreement.
