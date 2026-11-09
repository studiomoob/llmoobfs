# 0278 — A test of its own for finding V-c: a reader stalled inside a directory read

Date: 2026-11-09. Started 11:33, ended 12:32 Brasília time (calendar time, which includes
waiting for the VMs and for a rebuild of the KASAN kernel; the hands-on effort was about two hours).
Estimated effort: S (hours). Status: done on the local branch `claudiofp/b19-gap-e`, not
published.

## Goal

Review journal 0114 ends its verification with a suspected finding, called V-c in the findings
register: "a per-result check waiting on the lock of one of the layer's own directories, held by
a reader stalled on a fault in its buffer; closed by the same change as item 2" (item 2 is
finding F10, fixed in journal 0116 by releasing `query_sem` while the candidates of a chunk are
checked). Journal 0116 tested item 2 with a FUSE mount (step `stall`) but not this path, and the
register lists V-c with no test of its own. This journal adds one, with a negative round. No
change to `layer/`, the UAPI or the format (ADR-0022).

## The scenario, from the kernel sources

Read in Linux 6.12.116 (`docs/references.md` §52):

- `getdents64(2)` calls `iterate_dir()` (`fs/readdir.c`), which takes the directory's `i_rwsem`
  for reading and keeps it while `filldir64()` writes each entry to the caller's buffer. The
  layer's `moobl_iterate()` calls `iterate_dir()` on the lower directory inside that, so both the
  layer's and the lower's directory locks are held. A fault on the buffer waits with them held.
- A buffer registered with userfaultfd for missing pages makes that wait last until the
  registering process fills the page (`handle_userfault()`, `fs/userfaultfd.c`). Trapping a
  kernel-mode fault needs `CAP_SYS_PTRACE` or `vm.unprivileged_userfaultfd=1`; with the flag
  `UFFD_USER_MODE_ONLY`, which any user may pass, a kernel fault is not trapped.
- A creation in the directory takes its lock for writing and queues behind the reader. A lookup
  that misses the dentry cache takes the lock for reading (`lookup_slow()`, `fs/namei.c`) and,
  the semaphore being owned by a reader with a writer waiting, queues behind the creation
  (`rwsem_down_read_slowpath()`, `kernel/locking/rwsem.c`).
- The per-result check (`caller_may_read()`) resolves each result's recorded path with
  `vfs_path_lookup()`. With the result's dentry evicted, it is such a lookup.

So the check of a result in that directory waits for as long as the reader likes. Before journal
0116 it waited holding `query_sem` for reading; a rebuild queued for writing behind it, and every
query of every user behind the rebuild. Since journal 0116 it waits holding nothing of the
mount's, and only the query that made it waits.

On btrfs the reader may even hold the lower directory's lock exclusively:
`btrfs_readdir_get_delayed_items()` upgrades it while delayed items are pending. A build that
reads that directory then waits in its scan; the scan runs without `query_sem` (journal 0116
moved only the takedown under it), so the outcome is the same.

## Changes

- `tests/dirstall.c` (new): `dirstall probe` exits 0 when a userfaultfd that traps kernel faults
  can be made, 77 when it cannot, and prints `vm.unprivileged_userfaultfd`.
  `dirstall <dir> <ready> <release> <max hold>` opens the directory, starts `getdents64` in a
  thread into one anonymous page registered for missing faults, creates `<ready>` once the fault
  arrives (the directory is locked from then on), waits for `<release>` (at most `<max hold>`
  seconds, a failure), fills the page with `UFFDIO_COPY` and checks that `getdents64` returned
  entries. Built by `vm/build-tests.sh` with `-lpthread`.
- `vm/suite.sh`, new step `dirstall`, in the full suite, on ext4, btrfs and XFS: a domain `D`
  with `D/top` and `D/a/f`, a domain `E` with `E/g`, `chown -R` to uids 1001 and 1002 and a
  `chmod` of `D/a` so `D` is checked per result. Root's reader stalls on `D/a`; `drop_caches`
  evicts `a/f`'s dentry; a creation in `D/a` must still be waiting after 1 s. Then, as in step
  `stall`: (1) a query of `D` by uid 1001, (2) after a directory rename in `D`, a query that
  rebuilds `D` and takes `query_sem` for writing, (3) a query of `E` by uid 1002. (3) must answer
  within 3 s, and (1) must still be waiting, which shows the scenario was reached. After the
  release the reader, the creation and both queries of `D` must finish within 30 s, each with
  `top` and `a/f`. On any failure the reader is released first, so nothing is left waiting.
  Markers `DIRSTALL_PASS` and `DIRSTALL_SKIPPED` (no userfaultfd for kernel faults).
- `vm/kasan.config`: `CONFIG_USERFAULTFD=y`. virtme-ng's minimal base leaves it off, so the
  KASAN guest would skip the step; Debian's kernels have it. The CI kernel cache is keyed on this
  file, so the next KASAN job rebuilds its kernel once. `vm/kcsan.config` is unchanged: the step
  skips there.
- `docs/testing.md`: rows for `DIRSTALL_PASS` and `DIRSTALL_SKIPPED`. `docs/references.md` §52:
  the kernel sources and documentation above.

A first version used `chmod 0711` on `D/a`, as step `stall` does, and failed at
`ds-domain-answer-2-after-release-ext4`: query (2) answered `top` without `a/f`. Not a defect:
the rebuild after the rename applies the homogeneity rule again, and a file below a search-only
directory is heterogeneous and never answered (ADR-0025, `docs/spec-query-v2.md` §3). A probe in
the same VM showed it without any stall: after a fresh build, `a/f` below `0711` or `0700` is
left out for its owner and for root, below `0755` it is answered. Step `stall` never met this,
since there `a/f` is behind the FUSE mount and expected absent. The step now uses `chmod 0755`.

## Evidence

On the test host (Mac mini, KVM), from fresh clones of the branch under `~/b11night/gapE-*`:

| Run | Harness, kernel | Command | Result |
| --- | --- | --- | --- |
| Positive | Debian 6.12.111+deb13, native | `MOOB_VM_KVM=1 MOOB_ONLY=dirstall sh vm/run.sh` | `DIRSTALL_PASS` and `MOOBFS_ONLY_PASS`, 11:55 to 11:58 (build and boot included); on each of ext4, btrfs, XFS the reader stalled and was released after 8.3 s, and the waiting queries of `D` answered in the same second as the release |
| Negative | same, with `query_sem` kept across the checks | same | fails at `ds-other-user-held-up-by-a-check-waiting-on-a-directory-lock-ext4`, 11:58 to 11:59; the release afterwards let the guest finish normally |
| Positive | upstream 6.12.116, KASAN, UBSAN, lockdep | `KCACHE=~/.cache/moobfs-kasan-uffd MOOB_ONLY=dirstall sh vm/kasan-local.sh` | `DIRSTALL_PASS` and `MOOBFS_KASAN_ONLY_PASS` (no kernel diagnostic, lockdep included), 12:29 to 12:31 with the kernel already built; the reader stalled 9.1 to 9.3 s on each filesystem. The kernel rebuild with `CONFIG_USERFAULTFD=y` took about 40 minutes on CPUs 2 and 3, and a first run there, from the commit that still used `chmod 0711`, failed as the native one did (below) |

The negative round was made on a separate clone only, never committed: in `query_run()` the
`up_read(&ix->query_sem)` before the per-result checks was removed and `query_repin()` replaced
by success, which is the code before journal 0116. The query of `E` by the other user then did
not answer, as finding F10 predicts for this path. That clone is not reused.

The KASAN kernel was built in a separate cache (`~/.cache/moobfs-kasan-uffd`, a copy of the
shared one) so that runs of other branches, whose `vm/kasan.config` lacks the option, do not
rebuild the shared kernel back and forth.

`shellcheck --shell=sh` on `vm/suite.sh` and `vm/build-tests.sh` passed on the test host.

## Limitations

- The reader runs as root, whose userfaultfd traps kernel faults. An ordinary user under the
  default `vm.unprivileged_userfaultfd=0` cannot make this stall with userfaultfd; a buffer
  mapped from a FUSE file whose daemon does not answer would do it, through `fusermount3` or a
  user namespace, and is not exercised. The kernel side, a reader holding the directory's lock
  while its copy waits, is the same.
- The query that walks the locked directory still waits as long as the reader: it is bounded
  only by the reader, as journal 0116 recorded in its Limitations. What the step proves is that
  the wait stays with that query and does not reach the mount's other queries.
- The step shows the wait on the layer's directory lock with a creation queued behind the
  reader. Without a queued writer a shared lookup is not held up on ext4 and XFS at all.
- Not run on the 6.12.113, 6.12.115 or 7.3.3 kernels, nor under KCSAN (it skips there).
- The findings register (`docs/review-findings.md`, on the branch of pull request 191) still
  says V-c has no test of its own (row V-c of A2, item 4 of the gaps); it is to be updated when
  this branch and that one meet.

## Next step

The full suite of the batch, then the register's row and gap item.
