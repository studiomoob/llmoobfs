# 0180 — Memory-cgroup accounting of query-triggered allocations (issue 116)

Date: 2026-10-22. Status: **completed by PR 129 on 2026-10-23; see final addendum**.
Estimated effort: M (one to three days). Started 2026-10-22; labor time has
not yet been measured.

## Goal and starting evidence

Issue 116 records two costs: index and query allocations triggered by an
unprivileged caller use `GFP_KERNEL` without `__GFP_ACCOUNT`, and a result
buffer of up to 4096 × 272 bytes remains attached to each open file after a
query. Its code observation is supported by `layer/index.c` and
`moobl_qbuf_get()` in `layer/main.c`; the actual memory-cgroup effect has not
been measured. The buffer reuse fixed a measured query regression in journal
0139, so simply freeing it after every call would need a new performance
comparison. This work is separate from S9 fair sharing of the mount budget.

The branch `codex/issue116-memcg-0180` starts from `origin/main` at `d7fb946`.
PR 127, which corrects T9 and the concurrent budget release, was open and
unmerged when this investigation started. No change from that PR is included
here. The dedicated Mac mini was reachable and had no QEMU, virtme-ng or
syzkaller process at the initial check.

## Allocation API and test environment

The Linux 6.12.112 source cached for the project's KASAN build says in
`Documentation/core-api/memory-allocation.rst:53-56` that untrusted
userspace-triggered allocations should set `__GFP_ACCOUNT`, with
`GFP_KERNEL_ACCOUNT` as the shortcut. `mm/util.c:650-684` passes a kvmalloc
caller's flags to its vmalloc fallback; `mm/vmalloc.c:3734` counts vmalloc
pages for memcg when `__GFP_ACCOUNT` is set. The current upstream kernel
documentation retrieved via Context7 agrees with that guidance, but the
pinned 6.12.112 source is the implementation reference for this work.

The cached KASAN kernel has `# CONFIG_MEMCG is not set`, so its KASAN run
cannot prove accounting. The dedicated host's Debian 6.12.111 kernel has
`CONFIG_MEMCG=y`; a dedicated VM using that kernel is the proposed setting
for a bounded cgroup-v2 measurement. KASAN/lockdep remains a separate gate
for any eventual module change.

## Next experiment and limits

Construct a small guest test that joins a disposable memory cgroup, opens a
domain, issues an otherwise empty `QUERY2` with the maximum legal result
limit, and compares charged kernel memory while the file stays open and after
it closes. The empty answer avoids populating the userspace result array, so
the test isolates the retained kernel result buffer. Run it first against
unchanged code as a negative control, with no artificial low memory limit or
unbounded workload. Then audit the longer-lived index allocations and decide
which charges need a code change and whether a new ADR is required.

No memcg measurement, behavior correction, performance regression test or
compatibility conclusion was claimed at the start of this investigation.

## First experiment — 2026-10-22

`tests/memcg-query.c` and a `MOOB_ONLY=memcg` guest step were added as an
in-progress regression test. The guest mounts cgroup v2, enables the memory
controller, creates a private child cgroup and an empty readable domain, then
joins the helper to the cgroup before dropping to uid/gid 1001. The helper
opens the domain, maps but does not touch a userspace result array, and issues
an empty `QUERY2` at the legal maximum limit. It reads `memory.current` before
the ioctl, while the descriptor retains its buffer, and after close. It
requires at least half of the 1,114,112-byte buffer to be charged. The
reference Debian initramfs guest requires the memory controller; the separate
KASAN guest reports this step skipped because its kernel lacks `CONFIG_MEMCG`.

The test compiled with static GCC and `-Wall -Wextra -Werror` on the dedicated
host. The first direct native KVM wrapper attempt exited 127 before boot:
`mke2fs` was outside the host user's `PATH`. This was a wrapper setup error,
not a test result. The ephemeral wrapper was given `/usr/sbin:/sbin` and the
same targeted command, `MOOB_ONLY=memcg sh build/native-kvm.sh`, was repeated
against unchanged module code. The guest reported `before=262144`,
`held=262144`, `closed=262144`, delta zero bytes for a 1,114,112-byte buffer,
then `MOOBFS_VM_FAIL: memcg-query-uncharged`. The launcher exited 1. This is
a decisive negative control for the retained buffer's missing charge in this
guest; it does not yet measure index allocations.

Next: apply accounting to query and index/scan allocations, rerun the targeted
guest step, then measure the index separately and check for performance or
reclaim side effects before any publication decision.

## Corrected allocation paths and isolated negative rounds — 2026-10-22

All allocation calls in `layer/index.c` that passed plain `GFP_KERNEL` now pass
`GFP_KERNEL_ACCOUNT`. The `fs_reclaim_acquire()` and
`fs_reclaim_release()` arguments are reclaim-context annotations, not
allocations, and remain unchanged. The retained and concurrent-fallback
result buffers in `layer/main.c` also use `GFP_KERNEL_ACCOUNT`. This makes the
task's current memcg the charge target at allocation time; it does not
implement S9 per-user budget shares or move a charge when an open descriptor
is passed to a task in another cgroup. The per-open reuse remains because
journal 0139 measured the cost of allocating that buffer on every query.

The corrected targeted KVM run initially measured the empty-domain buffer:
`memory.current` rose from 262,144 to 2,277,376 bytes while the descriptor
held the 1,114,112-byte buffer, and was 180,224 bytes after close. The test
was then expanded to plant 650 attributed files outside the helper's cgroup
and query that separate domain with a one-result buffer. On the corrected
source, the same bounded native KVM command completed with exit 0:
`MEMCG_BUFFER_RESULT` delta 2,015,232 bytes;
`MEMCG_INDEX_RESULT` `slab_unreclaimable` delta 1,407,568 bytes, unchanged
after closing the descriptor; `MEMCG_ACCOUNT_PASS` and `MOOBFS_ONLY_PASS`.
The larger-than-requested buffer charge reflects allocator granularity; it
is not reported as the buffer's exact physical footprint.

For an isolated index negative, the current index diff was backed up in
`build/issue116-index-accounting.diff`. A separate disposable source copy
kept the corrected buffer code and guest test but used the original
`layer/index.c` from `origin/main`. The same targeted KVM command exited 1:
the buffer still charged 2,097,152 bytes, while the index case showed zero
change in `slab_unreclaimable` and failed at `memcg-index-uncharged`. The
earlier unmodified-module run had already failed the buffer case with zero
change in `memory.current`. These two negatives distinguish the effects; no
production worktree file was reverted. The saved logs on the dedicated host
are `build/memcg-negative-2.log`, `build/memcg-positive-2.log`, and
`build/memcg-negative-index.log` in their respective disposable source copies.

At this checkpoint, `make checkpatch`, `make test` (60 attribute checks and
nine image tests), shell syntax, SPDX, site links, FAQ synchronization,
retired-claim checks and `git diff --check` passed. Full KASAN/lockdep and a
full native-KVM suite are still pending. No controlled performance comparison
was run; a targeted `MOOB_ONLY` pass is not a full-suite result.

The accounting policy and its separation from S9 are proposed in ADR-0018;
the decision remains under validation until the full suites and resource-cost
review complete.

The testing guide now names the memcg markers and the KASAN configuration
limit; the English and Portuguese READMEs disclose the retained per-open cost,
and the security roadmap separates this accounting correction from S9 fair
sharing. The earlier R-MEM-1 finding is preserved with a dated update rather
than rewritten as though the 2026-10-12 audit had observed the new source.

## Transfer to the dedicated host — 2026-10-22

The local Docker `make vm-test` passed its M1 step and the ext4, btrfs and XFS
layer steps. During the later SQLite acceptance work, the emulated guest
reported an RCU stall and `watchdog: BUG: soft lockup` at guest time 1060 s.
There was no full-suite pass marker. At the maintainer's request to move this
work to the dedicated Mac mini, only the MoobFS test container was stopped;
the command exited 2 and this run is **inconclusive**, not a regression pass.
Other local containers were left alone. The source, logs and branch are being
transferred to a distinct Git checkout on the Mac mini while its KASAN VM
continues in a separate disposable source copy. A full native-KVM run on the
dedicated host is still required.

The verified bundle at `e2ffd84` was cloned into the dedicated host's
`~/moobfs-issue116-git-20261022` checkout; the source and test files matched
the running pre-merge KASAN source byte for byte. PR 127 then passed all
GitHub checks and was merged into `main` at `0d37836` by maintainer request
(journal 0179). This branch merged that main at `aef412b`, resolving only
the changelog and history-index conflicts by keeping both records. The KASAN
VM already in progress still tests the pre-merge source; its result must not
be attributed to the integrated branch. Integrated native-KVM and sanitizer
validation remain pending.

## Sanitizer result and integrated targeted run — 2026-10-22

The pre-PR-127 source copy's full `sh vm/kasan-local.sh` run ended with exit
0, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS` and `MOOBFS_KASAN_PASS`.
The guest printed `MEMCG_SKIPPED` because its pinned KASAN kernel has no
memory controller. The harness's kernel-diagnostic filter passed; a broad
manual text search also matched harmless boot lines about KASAN initialization,
lockdep configuration and a processor-platform-limit event, which are not
module diagnostics. This sanitizer result applies to the pre-merge source,
not to the branch after PR 127 was integrated.

In the dedicated host's Git checkout at `4eb3aaa`, a targeted native-KVM
`MOOB_ONLY=memcg sh build/native-kvm.sh` exited 0. The buffer case measured
`memory.current` 262,144 before, 2,277,376 while held and 180,224 after
close (delta 2,015,232 bytes). The index case measured
`slab_unreclaimable` zero before and 1,405,064 bytes after build, still
1,405,064 after closing the query descriptor. It printed
`MEMCG_ACCOUNT_PASS` and `MOOBFS_ONLY_PASS`. The full integrated native-KVM
suite is running separately; neither these targeted markers nor the old
KASAN pass are its verdict.

## Integrated native-KVM full suite — 2026-10-22

The full `sh build/native-kvm.sh` run in the dedicated host's Git checkout
at `4eb3aaa` exited 0 after PR 127 had been integrated. The guest printed
`MOOBFS_VM_PASS`, `MEMCG_ACCOUNT_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
`MOOBFS_LAYER_PASS`. The buffer's `memory.current` values were 262,144 before,
2,277,376 while held and 180,224 after close (delta 2,015,232 bytes). The
index's `slab_unreclaimable` rose from zero to 1,405,512 bytes and remained
there after the query descriptor closed. The complete suite included ext4,
btrfs and XFS lower filesystems, PR 127's over-budget race, git, SQLite WAL,
memory client, fault/replay cases and 9,000 fsx operations per supported
filesystem. A scan for kernel `BUG`, warning, Oops, KASAN, lockdep, soft lockup,
RCU stall and suite fail markers in `build/vm/console.log` found none. The
host-side log is `build/native-kvm-full-0180.log` in that checkout.

This is the integrated native-KVM result. The earlier local Docker TCG run
remains inconclusive. The integrated KASAN/UBSAN/lockdep run is now in progress
on the dedicated host; no sanitizer pass is claimed for the integrated branch
yet.

## Integrated sanitizer result and cost review — 2026-10-22

The full `PATH="$HOME/.cache/moobfs-kasan/venv/bin:$PATH" sh
vm/kasan-local.sh` run on the dedicated host's Git checkout at `4eb3aaa`
finished with exit 0. The guest printed `MOOBFS_ACCEPTANCE_PASS`,
`MOOBFS_LAYER_PASS` and `MOOBFS_KASAN_PASS`; the harness found no KASAN,
UBSAN, lockdep or other kernel diagnostic, including during allocation-fault
injection. It printed `MEMCG_SKIPPED` because this pinned sanitizer kernel
lacks `CONFIG_MEMCG`; the separate complete Debian native-KVM suite with
`CONFIG_MEMCG=y` supplies the accounting evidence. The host-side log is
`build/kasan-integrated-0180.log` and the final guest console is
`build/vng/console.log` in the dedicated checkout. The production code and
guest tests in that checkout match the local branch; only later documentation
commits differ.

The accounting change changes allocation flags, not allocation sizes,
lifetimes, query limits or the per-mount index budget. Its intended cost is
memcg charging and the possibility of a cgroup-limited allocation failing
where an uncharged one could previously succeed. The complete native-KVM
suite exercised index reclaim, over-budget concurrency and the existing
benchmark-equivalence tests without a functional regression. No controlled
before/after latency or throughput comparison was run, so this result does
not quantify accounting overhead. Performance remains a documented limit,
not a claim of unchanged speed.

The local implementation and its positive and negative accounting controls,
complete native-KVM suite, KASAN/UBSAN/lockdep suite and static checks have
passed. ADR-0018 is accepted as the accounting policy on this evidence;
publication and pull-request CI remain separate gates. Issue 116 remains
open until the change is published, reviewed and integrated.

After the final documentation update, `make test` passed 60 attribute checks
and nine image tests; `make checkpatch`, SPDX, FAQ synchronization, site-link
validation, retired-claim validation and `git diff --check` passed. These
checks did not rerun the VMs because no production code or guest test changed
after their completed runs.

## Publication checkpoint — 2026-10-22

After the maintainer authorized publication, the clean local branch
`codex/issue116-memcg-0180` at `8d0c22b` was pushed without force. The GitHub
base `main` remained `0d37836`, already integrated into this branch.
[PR 128](https://github.com/studiomoob/llmoobfs/pull/128) was opened against
`main` with head `8d0c22b278836a64c2b79c7a908ffa5a8305080e` and a
technical description of the tests and limits. Its eleven reported checks
were in progress at the first consultation. This publication is not a CI
pass or merge; issue 116 remains open. The two late documentation commits
were unsigned because the configured GPG secret key was unavailable in this
session. There was no history rewrite or force-push.

## CI sign-off failure and replacement — 2026-10-22

The `Lint` job on PR 128 passed shellcheck, SPDX, manual-page lint, pin and
checkpatch checks, then failed its commit-message gate: four documentation
commits reachable from the PR head lacked a `Signed-off-by:` trailer. This is
the DCO-style `git commit -s` trailer, distinct from cryptographic GPG commit
signing. The preceding checkpoint's count of two late unsigned documentation
commits was incorrect: three commits lacked the trailer when the PR opened,
and a fourth documentation commit was pushed afterward. The GPG key problem
did not cause this particular CI failure; omitting `-s` did.

The published PR 128 branch was not rewritten or force-pushed. The final
diff was backed up outside Git, a fresh branch
`codex/issue116-memcg-dco-0180` was created from `origin/main`, and the same
production and guest-test changes were committed with `git commit -s` as
`4a91b28`. [PR 129](https://github.com/studiomoob/llmoobfs/pull/129) was
opened as a draft for that branch while the documentation is replayed with
the same trailer. PR 128 remains a visible record of the failed attempt;
PR 129 is the intended replacement. The branch needs its own exact-head CI
before a merge decision. No new kernel behavior was introduced by this
reconstruction.

## Integration addendum — 2026-10-23

The opening status and the dated checkpoints above describe the work as it
stood at each observation. The exact head `90b3e68` of
[PR 129](https://github.com/studiomoob/llmoobfs/pull/129) completed all eleven
reported GitHub checks successfully, including the KASAN/lockdep job. With
the maintainer's explicit approval, the PR merged into `main` as
`40645e23ffca1bce0c665f77a7d36702371d0813` on 2026-10-23 at
09:03:36 UTC. Issue 116 closed automatically one second later through the
PR's `Closes #116` marker. The older PR 128 remains closed as the visible
failed-DCO attempt. The merge did not add a performance measurement; the
controlled before/after overhead comparison remains outstanding, and S9
fair sharing is still separate. These are integration observations, not new
local module or VM tests.
