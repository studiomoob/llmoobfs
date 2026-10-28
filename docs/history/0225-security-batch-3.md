# 0225 — Security batch 3: issues 115, 117 and 119, finding F17, the plan map and Scorecard

Date: 2026-10-28. Status: branch `claudiofp/BATCH3`, published with its pull request. Work
started: 2026-10-28 09:20 America/Sao_Paulo; ended 12:40. Wall-clock time, with four parallel
fronts and the VM runs, not an engineering-effort measurement. Estimated effort: M.

## Goal

Work through the next items of the security order in one batch, with one complete run and one
pull request, after the maintainer asked to group the work so that builds, tests and CI run once:
issue #119 (a maintainer's reading of round 2, M4 to M8), issue #117 (the mount-wide index lock
on inode instantiation and eviction), issue #115 (passthrough gaps), the difference between the
kernel's and the tools' definition of an extended ACL, the site's plan map, and the open points
of OpenSSF Scorecard.

## Fronts and decisions

Four fronts ran in parallel, each in its own worktree and test-host directory, with its own
journal, negative round and targeted KASAN run; they were then taken onto this branch.

- Journal 0221, #119 M4 and M8. Decision of the maintainer: a casefolded lower directory is now
  refused by lookup with `EREMOTE` (`ESTALE` when casefolded behind the layer), overlayfs's
  errnos, instead of `EOPNOTSUPP`; a visible change of the experimental UAPI, recorded in the
  changelog. `scan_delay_us` stays in the shipped module so that the suite tests the binary users
  install, and the warnings a user can cause are printed once.
- Journal 0222 and ADR-0026, #119 M6 and M7 and the extended ACL. Decisions of the maintainer:
  the magic stays `'M'` and the tail padding byte of `struct moob_query_term` becomes a named
  field that must be zero (`EINVAL` otherwise); one privilege test, `CAP_SYS_ADMIN` without audit
  on denial, accepting that a denied v1 `QUERY` leaves no LSM audit record. Aligning the tools
  with the kernel found that the kernel's own rule was wrong: finding F17.
- Finding F17 (ADR-0011): a minimal access ACL written outside the VFS could refuse a caller the
  index's rule admitted by the mode bits. Decision of the maintainer: a finding and an advisory,
  GHSA-82w5-ph62-47wf, created as a draft and published with this batch's merge, without a CVE.
- Journal 0223, #117: inodes without an index record no longer take the mount-wide lock on
  instantiation and eviction; those with a record still do, so the issue stays open for them.
  Removing `rec->upper` is left as a proposal in that journal.
- Journal 0224, #115: fadvise and readahead, statx fields of the lower, `drop_inode` for inodes
  unlinked on the lower, and `O_NOATIME` in `F_SETFL`. Decisions of the maintainer: `O_NOATIME`
  follows the lower although overlayfs does not sync it, and `drop_inode` frees only inodes whose
  lower has no link left, not every inode as overlayfs does. `cachestat` remains a limitation.
- The site's plan map (`site/plan.html`), last updated on 2026-10-25, still said the index is
  rebuilt at mount and stopped at M3 phase 3b: it now states `QUERY2` by domain, the per-domain
  build, the authorization by partition and per result, and the command-line tool, library and
  MCP adapter. Journals 0156 and 0193 note that GHSA-qgjj-2f2p-q82m (F14) was published on
  2026-10-28; it had stayed a draft after its fix merged on 2026-10-20.
- OpenSSF Scorecard, 7.9 on 2026-10-28 at `e68da30`:
  - Vulnerabilities 9: `certifi` 2026.10.18 in `vm/kasan-requirements.txt` carried
    PYSEC-2026-4320; it moves to 2026.10.19, and Dependabot now also watches the hash-pinned
    Python requirements of `vm/` and `python/`.
  - SAST 9: CodeQL ran only on changes to user-space C, so 28 of the last 30 commits had a run;
    it now runs on every pull request and push to `main`.
  - Branch-Protection could not be read with the default token. A repository ruleset `main`
    now repeats the classic protection, with no bypass and only merge commits allowed; Scorecard
    reads rulesets without an admin token. The classic protection stays.
  - Signed-Releases and Packaging had no release assets. `.github/workflows/release.yml` signs
    each future tag's source archive with Sigstore and attaches SLSA provenance; it has not run
    yet.
  - Code-Review 0 needs approvals by someone other than the author, and Maintained 0 lasts until
    the repository is 90 days old; neither can be changed by this batch. The maintainer asked the
    external reviewer, in the batch's pull request, for a review.

## Merge of the fronts

The fronts were taken by cherry-pick in the order 0221, 0222, 0224, 0223. Documentation
conflicts (index, changelog, references, testing) kept both sides. Taking 0223 conflicted in
`layer/index.c` and `vm/suite.sh`, and the first resolution kept both sides there too: it
duplicated a comment, left two step lists in `vm/suite.sh` and dropped the `fi` closing the
`minacl` block. Commit `2f05994` corrects it; the added lines of every front are present once,
checked by comparing each front's diff with the batch's, and `sh -n vm/suite.sh` passes.

## Evidence

All on the dedicated host, at `80ccb0e` (the code of every front; later commits change only
documentation, the site and workflows):

- `make checkpatch`: nothing reported.
- Full KASAN/UBSAN/lockdep suite (upstream 6.12.112, 10:27–11:32): `MOOBFS_KASAN_PASS`, no kernel
  diagnostic, with `MINACL_PASS`, `IXLOCK_PASS`, `PASSTHROUGH_PASS`, `LAYER_SUBVOLUME_PASS`,
  `CASEFOLD_REFUSED_PASS`, `PERMMATRIX_STEP_PASS`, `DOMAINREUSE_PASS` and `MOOBFS_PYLIB_PASS`.
- `MOOB_VM_KVM=1 make vm-test`, Debian 6.12.111 (11:32–11:50): `MOOBFS_VM_PASS`,
  `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, no `MOOBFS_VM_FAIL`, no kernel diagnostic.
- `MOOB_VM_KVM=1 MOOB_DEBIAN_KERNEL=6.12.113 make vm-test` (11:50–12:12): stopped at
  `MOOBFS_VM_FAIL: rootpath-race-inconclusive-btrfs`, no kernel diagnostic. The step declares
  itself inconclusive when, in three attempts, the mount-wide build finishes before the query it
  races reaches the build mutex; under KVM the btrfs build took 0.10 s. The same step passed in
  the 6.12.111 run, and `MOOB_ONLY=rootpath` on 6.12.113 under KVM passed twice afterwards
  (`ROOTPATH_PASS` on ext4, btrfs and XFS each time). The complete 6.12.113 suite runs in CI
  under TCG.

## Limitations

- `release.yml` and the ruleset are not yet seen by Scorecard; `release.yml` runs at the next
  tag.
- The complete 6.12.113 run did not pass locally (see Evidence); its only failure was a timing
  step that cannot conclude under KVM, which passed when repeated. `rootpath` could size its
  fixture or count its attempts for a faster guest; recorded for issue #168.
- The limitations of each front are in its journal.

## Next step

Merge after the required checks pass, publish GHSA-82w5-ph62-47wf, close #115 and #119, comment
on #117, and decide the next tag, which `release.yml` would sign.
