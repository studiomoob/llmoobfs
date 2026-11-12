# 0283 — Stage-2 decisions of 2026-11-11: issue 117, acceptance step 5, the known-leak condition and Part B, with tests for Part B

Date: 2026-11-11. Work started 08:35 Brasília time; ended 08:53, for the documents and the
local tests (the pending runs are not included). Status: **documents and tests written locally,
not published; the VM runs and their negative rounds are pending** (the test host was
unreachable, see "Pending"). Estimated effort S (hours); the time recorded is calendar time of
one session, close to the effort.

## Goal

Record four decisions the maintainer took on 2026-11-11, about 08:30 Brasília time, on the stage-2
conditions of `docs/distribution-strategy.md` §8 and its ADR-0030 amendment, and write the
regression tests that Part B of `docs/review-findings.md` lists as missing where no kernel change
is needed. The kernel is frozen (ADR-0022): `layer/`, the UAPI and the format are unchanged.

## Decisions

1. **Issue 117 (finding M2 of journal 0155) is a documented limit of stage 2.** Journal 0223
   removed the mount-wide index lock from the instantiation and eviction of inodes without an
   index record; an inode with a record still takes `ix->lock` for writing on both. The effect is
   contention under load, with no wrong answer and no security effect. Removing it changes the
   kernel, which is frozen. The issue stays open as a candidate for the review of the freeze after
   `v0.6.0`. Alternative: keep it as a stage-2 gap of criterion (2); not taken, because the part
   that is a defect of the criterion's kind (a review finding without a test) has its test
   (`ixlock`), and what remains is a performance property that only a kernel change removes.
2. **Acceptance step 5 is restated with a dated amendment.** Its text, "`QUERY` on the
   subdirectory returns `ENOTTY` as documented", was written when the mount-wide v1 `QUERY` was the
   only query. The behaviour was checked against the code and the tests before amending:
   `moobl_root_ioctl()` (`layer/index.c`) dispatches `QUERY2` and `QUERY2_BATCH` on any directory
   and returns `ENOTTY` for `QUERY` and `GET_INFO` on any directory but the mount root;
   `query2_target()` returns `ENOTTY` for a directory that is not at `domain_depth`. Tests:
   `make container-test` (`vm/container-test/guest.sh`: `QUERY2_DOMAIN_BIND_docker` and
   `_podman` answer through a bind of the domain directory; `INFO_DOMAIN_BIND_*`: `moobfs info`
   exits 69 there) and `tests/query.c` (v1 `QUERY` on a subdirectory, and `QUERY2` on a
   non-domain directory, both `ENOTTY`). Step 5 now reads: read, write and xattr work with an
   aligned uid; `QUERY2` on the bound domain directory answers; `GET_INFO` and the v1 `QUERY`
   there return `ENOTTY`. The old text is kept above the amendment. It is not added to
   `docs/retired-claims.txt`: it is stated only in §8, which is not a current-state document of
   `scripts/check-retired-claims.sh` and keeps it as history, and the literal phrase is still true
   of the v1 `QUERY`, so listing it would also forbid correct statements.
3. **GHSA-756j-cgwq-gvwm does not count as an open known leak.** It is fixed (journal 0216:
   records keep `i_generation`; step `domainreuse`, `DOMAINREUSE_PASS`, two negative rounds) and
   its advisory was published with the merge of pull request 166 (journal 0219). The §8 condition
   "no leak is known" therefore holds as far as recorded findings go.
4. **Part B of `docs/review-findings.md` stays informative**, outside criterion (2) of ADR-0030
   (first agreed on 2026-11-09, confirmed on 2026-11-11). Its findings with no test get one
   where the test needs no kernel change.

## Changes for the decisions

- `docs/distribution-strategy.md` §8: an amendment dated 2026-11-11 after the ADR-0030 amendment
  of stage 2 (step 5, issue 117, the known-leak condition, Part B); the earlier text is kept.
- `docs/decisions/0030-stage-2-review-criterion.md`: status line and an "Amendment, 2026-11-11"
  section (Part B, issue 117, GHSA-756j-cgwq-gvwm, step 5).
- `docs/when-not-to-use.md` §3: a bullet for the remainder of issue 117.
- `docs/status.md` and `docs/status.pt-BR.md`: the issue-117 sentence of the 2026-10-28 update
  gains the decision, in both languages.
- `ROADMAP.md`: the "Public release" row states the three stage-2 decisions.
- `docs/review-findings.md`: a dated note on Part B, notes on rows M2, 0083 A, 0083 E, 0211-1,
  0211-3, 0213-2, 0213-3, on Gaps items 14 to 17 and on section 2 item 7.
- `docs/security-roadmap.md`: not changed; its current-state rows do not state issue 117 or
  Part B.
- The text of a comment for issue 117 was written outside the repository for the maintainer to
  post; nothing was posted on GitHub.

## Tests for Part B

Gaps items 14 to 17 of the register list the Part B findings with no test. Each one:

| Finding | Test | Marker | Where it runs |
| --- | --- | --- | --- |
| 0083 E (unchecked `xa_store` on a directory's permission change fails open) | `tests/fallbackfault.c`, step `fallbackfault` of `vm/kasan-guest.sh` | `FALLBACK_FAULT_PASS` | KASAN harness only (failslab); in the full KASAN run, so in the required check of pull requests that touch `vm/` |
| 0211-1 (workflow `steps` input filter) | `scripts/test-steps-filter.sh` | `STEPS_FILTER_PASS` | `make lint` (CI `Lint` job) |
| 0211-3 (`nosymfollow` of the syzkaller image setup) | `scripts/test-image-setup.sh` | `IMAGE_SETUP_UNIT_PASS` | `make image-setup-unit`, root with loop devices; not in CI |
| 0213-2 (`moobfs-index --uid`: refusal of a non-decimal uid, root's groups dropped) | `tests/tools-uid.sh` | `TOOLS_UID_PASS` | `make hostile-test` (CI "Tools and sanitizers"; the group part only as root with `setpriv`) |
| 0213-3 (the CLI trusts `count` and `path_len` only as far as the header) | `tests/tools-uapi.sh` with `tests/fake-query2.c` (`LD_PRELOAD`) | `TOOLS_UAPI_PASS` | `make hostile-test` |

**0083 E.** `moobl_index_perm_changed()` stores a mark for the domain in `ix->fallback`; when that
store fails it sets `fallback_all`, so every domain is answered with per-result checks until the
next mount. `tests/fallbackfault.c` makes a domain with a readable file and a file below a
subdirectory, has uid 1001 query it (both answered, the domain built), changes the subdirectory to
0700 as root and queries as uid 1001 again: the readable file must be answered and the other must
not. The case runs twice, each on a fresh mount since `fallback_all` lasts until unmount: without
injection (the per-domain mark), and with every slab allocation of the test's own task whose stack
holds `moobl_index_perm_changed()` failed. One injected failure, as `fail-nth` gives, would not
reach the fallback: `__xa_store()` (`lib/xarray.c`, Linux 6.12.111, read for this) allocates a
node with `GFP_NOWAIT` inside the lock and, on failure, retries once with the caller's flags
through `__xas_nomem()`. So the test uses failslab in probability mode (100 %, `times`
100 000, `task-filter`, `require-start`/`require-end` set to that function's range from
`/proc/kallsyms`, the next symbol as the end), and reads `times` back: an unchanged counter fails
the case as "nothing tested". The chmod reaches the lower before the hook, so it must succeed.
What it cannot tell: whether the failed allocation was the `xa_store` itself or another one of
that function (with no record for the subdirectory, `depth_and_domain()` and the placement do not
allocate before it; ACL reads after it may).

**0083 A was not given a test.** The fix drops the alias reference taken in `result_path()` after
the query's read lock is released (`cand[i].held`, `do_query2` path of `layer/index.c`). Holding it
under the lock matters only when that `dput()` is the last reference of an unhashed dentry whose
inode is then evicted, since eviction of an inode with a record takes the index lock for writing
(issue 117). On the current code the unlink and rename paths cannot produce that inside the
window: they set the link count and then wait for the write lock in `moobl_index_drop_inode()`
before the VFS unhashes the dentry, so the query's reference is never the last one. One path was
found by reading: a file unlinked directly on the lower, whose layer dentry a concurrent lookup
invalidates (`moobl_d_revalidate()` then `d_invalidate()`) while the query holds the alias;
`moobl_drop_inode()` evicts an inode whose lower link count is 0. That window is the time the
query spends gathering one chunk under the read lock, microseconds, and nothing in the module
holds it open: the test parameters (`scan_delay_us`, `scan_hold_us`, `rename_hook_delay_us`) act
on the build and on the rename hook, not on the query. A race test would pass on the current code
and would not reliably fail with the fix reverted, which is not a regression test; a hold point
in the query path is a kernel change. Recorded, not written. The path found by reading was not
run.

**Not written either:** the remainder of B2 R3 (`caller_may_read()` omits a result on lookup
errors other than `-ENOMEM`): a test needs the expected behaviour, which waits for a decision on
those errors. F7 and 0081 1-a stay guarded by the lockdep assertions in `layer/index.c`, which the
KASAN harness runs; no new test. The SQLite table's half of 0213-3 (`sqlite/moobfs_vtab.c`) has
no test: `tests/tools-uapi.sh` covers the `moobfs` CLI only.

## Evidence

Run on this Mac (macOS, Docker Desktop on Apple silicon, its Linux VM's kernel
`7.0.14-linuxkit` arm64), 2026-11-11 between 08:40 and 08:50 BRT:

- `sh scripts/test-steps-filter.sh` with macOS's `/bin/sh` and in the pinned
  `debian:trixie-slim` image (dash): `STEPS_FILTER_PASS: both workflows refuse 14 hostile values
  and accept 4 step lists under C, en_US.UTF-8 and pt_BR.UTF-8`. Negative rounds, each on a
  scratch copy of the workflows: `kcsan.yml`'s list widened with `E`, `.`, `/` and `-` →
  `STEPS_FILTER_FAIL: .github/workflows/kcsan.yml (C) accepted 'Ext4'` (both shells); the filter
  line removed from `kasan-arm64.yml` → `STEPS_FILTER_FAIL: ... expected one filter line, found
  0`; the contributor's first wording (`a-z0-9`, journal 0211) → on macOS
  `STEPS_FILTER_FAIL: .github/workflows/kcsan.yml (en_US.UTF-8) accepted 'Ext4'`, the locale
  case the merge note of journal 0211 describes.
- `tests/tools-uapi.sh` and `tests/tools-uid.sh`, with `moobfs`, `moobfs-index`, `moobfs-attr`
  (`-std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow`, as the Makefile's `WARN`) and
  `fake-query2.so` built with GCC 12 in the `golang:1.24-bookworm` image already on this Mac (no
  `make` in it, so the compiler lines were typed by hand), run as root:
  `TOOLS_UAPI_PASS: count above limit refused (EPROTO); path_len out of bounds and a missing NUL
  give an unknown path`; `TOOLS_UID_PASS: 8 invalid --uid values exit 64; supplementary groups
  checked` (run in `debian:trixie-slim`, which has `setpriv`, and in the bookworm image).
  Negative rounds on a scratch copy of `tools/`: the `count > limit` check of `run_query2()`
  removed → `TOOLS_UAPI_FAIL: count above limit: exit 0`; only the NUL half of
  `check_record()` removed → `TOOLS_UAPI_FAIL: nonul: exit 0` (the CLI printed past the stored
  length); `setgroups(0, NULL)` removed from `moobfs-index` → `TOOLS_UID_FAIL: --uid 1001 kept
  root's supplementary group 4242 and listed g/f`.
- `tests/fallbackfault.c` compiled with the flags of `vm/build-tests.sh` (`-static -O2 -Wall
  -Wextra -Werror`) in the same image: no warning. Not run (needs the KASAN kernel).
- `scripts/test-image-setup.sh`: not run (no `mount` in the images on this Mac; nothing was
  installed to replace the test host).
- shellcheck 0.9.0 (`koalaman/shellcheck:v0.9.0`, the version of the `Lint` job) over the files
  `make lint` lists and the two new `tests/*.sh`: no finding. `scripts/check-spdx.sh`,
  `scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`), `scripts/check-pins.sh` (`PINS_OK`),
  `scripts/check-site.py` (12 pages valid), `scripts/site-faq.py --check`, `make test` (9 Python
  tests OK) and `git diff --check`: pass. `make checkpatch` was not run: no file it reads
  (`layer/`, `kernel/`) changed.

## Second round, 2026-11-11 from 11:27 Brasília time: merges, the call #195, rebase

**Merges.** Pull request 184 (Dependabot, the `codeql-action` group) was merged on 2026-11-11 at
08:54 BRT as `d529545`. Pull request 194 (batch b21: Ubuntu 26.04's Linux 7.0 and the matrix page,
journals 0281 and 0282) was rebased on it, passed its checks (19 of 19 at `609a46a`, 09:50 BRT)
and was merged at 11:24 BRT as `d96b0db`. This branch was then rebased on `d96b0db`; the
conflicts in `docs/references.md` (§54 and §55), `CHANGELOG.md`, `docs/history/README.md` and the
§8 notes of `docs/distribution-strategy.md` were resolved by keeping both sides.

**Post-merge runs on `d96b0db`** (`gh run list --branch main`, read at 11:33 BRT): CodeQL, Static
analysis, OpenSSF Scorecard, Pages and TLA+ model succeeded; the CI run 43038158762 was still
running, with the compiler-warning jobs passed, the Debian VM jobs (6.12.111, 6.12.113, 6.12.115,
7.3.3) and "Tools and sanitizers" in progress, and two jobs failed on the runner's network, not
on the code: "Native Linux VM (Ubuntu 7.0.0-43)" stopped building its Docker image after 12 s
because `archive.ubuntu.com` could not be resolved (`apt-get update` of the `resolute` image),
and "Lint" failed in its "Manual pages" step because `mandoc` could not be downloaded
(`Temporary failure resolving 'azure.archive.ubuntu.com'`). Both need a rerun by the maintainer;
nothing was rerun from here. The KASAN workflow does not run on a push to `main`.
*Note, 2026-11-11, 13:30 BRT (journal 0285):* the two jobs failed four attempts in all, from
about 11:40 BRT. The cause is outside the project, not the code: Canonical's authoritative
nameservers for `ubuntu.com` stopped answering over IPv4, which GitHub's runners use, so every
`*.ubuntu.com` name failed to resolve. Journal 0285 records the incident and the retries and
fallback mirror added for it.

**Call for help.** Issue #195, "Help wanted: reproduce the stage-2 test evidence on your own KVM
host", was opened at 11:25 BRT: a call for an independent reproduction of the stage-2 evidence,
with a route pinned to `d96b0db`; it is pinned in place of the closed issue #5 and linked from
issue #68, and the outside contributor of pull requests 161 to 163 was asked on it. It answers
the "a third party reproduces everything" line of §8. Surfaces updated: `docs/help-wanted.md`,
`README.md` and `README.pt-BR.md` ("How to help"), `docs/faq.md` and `site/faq.html`
(regenerated), `site/index.html`, `docs/status.md` and `docs/status.pt-BR.md` (a dated update on
stage 2), and a dated note in `docs/distribution-strategy.md` §8. `scripts/site-journey.py .`
was run and changed nothing. No stated fact stopped being true ("no independent reproduction
yet" still holds), so `docs/retired-claims.txt` is unchanged.

**Matrix page.** `docs/matrix-and-limits.md`, on `main` since journal 0282, now states issue 117 as
a documented stage-2 limit and GHSA-756j-cgwq-gvwm as fixed and published, not an open leak (67
lines; `scripts/check-matrix-page.sh`: `MATRIX_PAGE_OK`).

The VM items of "Pending" stay pending: the test host was still unreachable.

## Pending (the test host was unreachable)

The test host stopped answering about 08:30 BRT. Once it is back, from a checkout of this branch:

1. `MOOB_ONLY=fallbackfault sh vm/kasan-local.sh` → `FALLBACK_FAULT_PASS` and
   `MOOBFS_KASAN_ONLY_PASS`, with `FALLBACKFAULT_RESULT injected=1 faults=N` (N ≥ 1).
   Negative round: on a scratch branch, remove `WRITE_ONCE(ix->fallback_all, true);` from
   `moobl_index_perm_changed()` and run again; expected `FALLBACKFAULT_RESULT injected=1 ...
   sub_file=1` and `MOOBFS_VM_FAIL: ff-answered-without-checks-1`, with case 0 still passing.
   If case 1 passes there too, the test does not guard E and must be revisited before the PR.
2. `sudo make image-setup-unit` (or in a privileged `debian:trixie-slim` container with
   e2fsprogs) → `IMAGE_SETUP_UNIT_PASS`; negative round with `,nosymfollow` removed from
   `vm/syzkaller/image-setup.sh` → `IMAGE_SETUP_UNIT_FAIL`.
3. `make hostile-test` natively with clang, ASan and UBSan as in CI (the preloaded library
   under clang's static runtime was not run here).
4. Once for the batch, before publication: the full `make vm-test` and the full
   `sh vm/kasan-local.sh` (the full run includes `fallbackfault`), and the pre-push
   `~/b11night/prepr/run6.sh` with its diff-check base set to `origin/main`.

## Limitations

- None of the kernel-side evidence exists yet: `fallbackfault` is written, compiled and wired,
  not run, and its negative round is pending. Since the KASAN workflow runs the full guest script
  on pull requests touching `vm/`, the branch must not be pushed before item 1 above passes.
- The user-space tests ran in containers on arm64, not natively on the test host or in CI.
- `fallbackfault` covers ext4 only; it cannot tell which allocation of the function failed.
- 0083 A, the remainder of B2 R3 and the SQLite half of 0213-3 have no test (reasons above).

## Next step

The pending runs of the section above, then the batch's pull request after b21, with the
maintainer's agreement.
