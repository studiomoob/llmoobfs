# ADR-0033 — A narrow exception to the kernel freeze for S16's self-check

Date: 2026-11-07. Status: **accepted** by the maintainer on 2026-11-07 (journal 0270). Amends
[ADR-0022](0022-kernel-freeze-and-user-space-consumable-track.md) by one exception; the freeze otherwise
stays. Estimated effort: S (hours) for this decision; S16 itself stays M (one to three days).
Estimate, not labor time.

## Context

ADR-0022 froze the kernel module, the UAPI and the on-disk format on 2026-10-25: only
corrections, tests and user-space work go in. S16 of the [security roadmap](../security-roadmap.md)
is "a test-only module parameter that, after each update hook and build, re-reads the touched
files from the lower and warns on a difference", done when "the suite runs with it on and stays
silent; a mutant of S14 makes it warn". It is a test, but it lives in the module, so it waited
for this decision. With S14, S15 and S17 to S21 done (S18 on 2026-11-07, journal 0248), S16 is
the last item of the S14–S21 block that ADR-0030 makes a condition of stage 2.

The alternative, moving S16 to stage 3, would close stage 2 sooner without the one check that
compares the index with the lower after every change, inside the kernel, while the suite runs.
The S14 mutants and the oracles of the suite compare answers; S16 compares the record itself.

The module already ships test-only parameters (`scan_delay_us`, `scan_spin_us`,
`rename_hook_delay_us`, `scan_hold_us`, `scan_getxattr_errno_once`). Journal 0221 (M8.6) kept
them in the shipped module rather than behind a test-only build, so that the suite exercises
the binary users install: only root can write them, they are 0 by default, and while 0 each
costs one predicted branch. The filesystems below the layer put their consistency checks behind
build options instead (`CONFIG_XFS_DEBUG`, "extra sanity-checking functions in various code
paths", and `CONFIG_XFS_DEBUG_EXPENSIVE`; `CONFIG_BTRFS_DEBUG` and `CONFIG_BTRFS_ASSERT`;
`CONFIG_EXT4_DEBUG`; Linux 6.12.112 `fs/xfs/Kconfig`, `fs/btrfs/Kconfig`, `fs/ext4/Kconfig`);
that is the alternative considered below.

## Decision

S16 may change the module, within these bounds:

1. The self-check is a module parameter in the shipped module, under the rules of journal
   0221 (M8.6): writable only by root, off by default, and while off it costs one predicted
   branch on each path it watches. With it off, the module behaves as before.
2. When on, it only reads and warns: it never changes an answer, a record or the lower.
3. No change to the UAPI or the on-disk format.
4. The rules for a kernel correction apply (`AGENTS.md`): `make checkpatch`, the suite steps in
   a VM, a negative round (an S14 mutant makes it warn) and the touched steps under
   KASAN/lockdep, since it reads the lower from the update paths.

Anything else in the module stays frozen under ADR-0022.

## Consequences

- S16 can start; once it is done, the S14–S21 block of ADR-0030 is complete, and stage 2
  waits only for the remaining deliverables of `docs/distribution-strategy.md` §8.
- Alternative not taken: a test-only build, as the lower filesystems' debug options do. It
  would leave the shipped object code untouched, but the suite would then test, for this check,
  a binary users do not install, against M8.6; and the suite would build the module twice.
- Risk: code that runs only in tests can itself be wrong and hide nothing or warn falsely. The
  negative round with an S14 mutant is the guard against a check that never fires.
