# 0234 — Documentation and manual workflows of issue #168

Date: 2026-10-29. Status: done on `claudiofp/b7-docs-gaps`, for the batch pull request. Work
started: 2026-10-29 12:27 UTC; ended about 13:40 UTC. Wall-clock time, most of it waiting for the
two workflow runs, not an engineering-effort measurement. Estimated effort: S; actual: S.

## Goal

Three items of issue #168 (limitations left by security batch 2) that need documentation or a
manual run, not kernel code (ADR-0022 keeps kernel, UAPI and format frozen):

1. Journal 0216: a lower that leaves `i_generation` at 0 is still exposed to inode-number reuse,
   and so is `EXT4_IOC_SETVERSION`. State it in `docs/when-not-to-use.md` and
   `docs/compatibility.md`.
2. `scripts/check-retired-claims.sh` does not read `docs/security-roadmap.md` or
   `docs/adoption-risks.md`, whose dated history gives false hits, and two current claims there
   were stale: S12's "never been loaded with SELinux enforcing" and risk 7's "Index in RAM,
   rebuilt at every mount".
3. Journal 0211: the manual KCSAN and arm64 KASAN workflows changed only through lint since
   their input filter was added. Run each once on `main`.

## Decisions

- **The generation limit, checked in the build's kernel.** Before writing, the statements of
  journal 0216 were read again in the Linux 6.12.112 sources (`gregkh/linux` tag `v6.12.112`):
  `EXT4_IOC_SETVERSION` and its old number `FS_IOC_SETVERSION` need `inode_owner_or_capable()`
  and return `ENOTTY` with `metadata_csum`; `EXT4_IOC_GETVERSION` has no check beyond an open
  descriptor; the VFS starts every inode at generation 0 and `fs/nfs/inode.c` never sets it;
  FUSE takes the server's value. tmpfs, not named in journal 0216, gives a random generation.
  The text names the cases verified there and does not list other filesystems that were not
  read. It adds that a lower directory out of ordinary users' reach closes the ioctl's way but
  not the lower without generations, where an administrator's removal behind the layer (ADR-0011
  A2) is enough. In `compatibility.md` it is a dated note under rule 4 (lower filesystems), since
  the rule already excludes network and FUSE lowers; the note gives one reason why.
- **A marked current-state section in each of the two dated documents**, read by the check. Both
  documents are dated analyses whose later notes quote the claims they correct, so reading them
  whole fails on history: four retired phrases appear in the security roadmap ("never been
  loaded with SELinux", "findings F1 to F5", "no outside human review yet." and "F1 to F16")
  and one in the adoption risks ("rebuilt at every mount"), all in dated text. Each now has a section
  between `<!-- current-state -->` and `<!-- /current-state -->` that states, item by item, where
  things stand at `7ac52cd`; the check reads only those lines and fails when a marker is missing,
  so that deleting the markers cannot switch it off. Alternative not taken: moving the history
  of both files into journals, which rewrites two documents that other records link by section.
- **Stale sentences corrected by dated notes, not rewritten**, as `AGENTS.md` asks: the status
  line of 2026-10-10 of the security roadmap, its table "verified on 2026-10-10", S12's first
  bullet (the module ran with SELinux enforcing in journal 0104), S9's "mount-time scan" (nothing
  is scanned at mount since journal 0098), and row 7 and section 7 of the adoption risks (built
  per domain since journal 0098, released under pressure since journal 0101). In the two
  current-state documents of the check, `when-not-to-use.md`'s tested-kernel bullet (it lacked
  Debian 6.12.113, journal 0206) and "not a strict CPU/I/O cap or fair memory shares" (fair
  shares are on `main`, ADR-0027) were rewritten, and their old wording went to
  `docs/retired-claims.txt`.
- **The workflows were run as they are on `main`**, with their default steps (KCSAN:
  `buildrace release`; arm64: `ext4`), the inputs a weekly run uses.

## Changes

- `docs/when-not-to-use.md` §3: a bullet on lowers without inode generations and
  `EXT4_IOC_SETVERSION`; the tested-kernel bullet names Debian 6.12.113 and the arm64 job; the
  build-limit paragraph no longer says `main` lacks fair shares.
- `docs/compatibility.md` §1 rule 4: a dated note.
- `docs/security-roadmap.md`: section "Where each item stands (2026-10-29)" (S1 to S22), and dated
  notes on the status line, the 2026-10-10 table, S9 and S12.
- `docs/adoption-risks.md`: section "Where each risk stands (2026-10-29)" (risks 1 to 15), and
  dated notes at the top, after the summary and under section 7.
- `scripts/check-retired-claims.sh`: reads the marked sections of the two documents.
- `docs/retired-claims.txt`: two phrases and the header; `docs/public-surfaces.md`: what the
  check reads; `docs/references.md` §32; this journal, the index and the changelog.

Neither of the two documents nor `when-not-to-use.md` or `compatibility.md` has a Portuguese
version, so no second language was updated.

## Evidence

Kernel sources: `https://raw.githubusercontent.com/gregkh/linux/v6.12.112/` for
`fs/ext4/ioctl.c`, `include/uapi/linux/ext4.h`, `include/uapi/linux/fs.h`, `fs/inode.c`,
`fs/nfs/inode.c`, `fs/fuse/inode.c`, `fs/ext4/ialloc.c` and `mm/shmem.c`; lines as cited in
`docs/references.md` §32.

Retired-claims check (macOS, `/bin/sh`):

- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- Negative round 1: "Index rebuilt at every mount" put into row 13 of the adoption risks'
  section: `retired claim still stated: "rebuilt at every mount"`, exit 1.
- Negative round 2: the closing marker removed from the security roadmap: `no current-state
  section in docs/security-roadmap.md`, and the lines after the opening marker (the dated
  history) then reported "never been loaded with SELinux", "findings F1 to F5", "no outside
  human review yet." and "F1 to F16"; exit 1. Both files restored.
- `shellcheck --shell=sh scripts/check-retired-claims.sh`, shellcheck 0.9.0 (`shellcheck-py`
  0.9.0.6, the version of the `Lint` job): no finding.
- Branch protection read with `gh api repos/studiomoob/llmoobfs/branches/main/protection`: the
  required checks are `Tools and sanitizers`, `Native Linux VM` and `Layer suite under
  KASAN/lockdep`, the basis of the S6 row.

Workflow runs on `main` at `7ac52cd`, started with `gh workflow run kcsan.yml --ref main` and
`gh workflow run kasan-arm64.yml --ref main` at 12:28 UTC and followed with `gh run watch`:

- KCSAN, [40873505079](https://github.com/studiomoob/llmoobfs/actions/runs/40873505079):
  **success**, job 12:28:15 to 12:51:47 UTC (23 min 32 s), KVM usable. Steps `buildrace` and
  `release` on ext4, btrfs and XFS: `BUILD_RACE_PASS` and `RELEASE_PASS` for each (every answer
  equal to the oracle), `UNPRIV_RELEASE_PASS`, `MOOBFS_KASAN_ONLY_PASS: steps buildrace release
  under KCSAN and lockdep with no kernel diagnostics (not a complete run)`, and `KCSAN_OUTSIDE: 43
  report(s) with no racing access in moobfs_layer (7 reached through it)`. The kernel's own
  races are those of the earlier scheduled runs (`tick_nohz_handler`, `virtqueue_kick_prepare`,
  `pids_release`, `mb_cache_entry_create`, jbd2, `d_splice_alias`, `d_add`); runs 40503633164 and
  39417785735 logged 34 and 26 such `BUG: KCSAN` lines.
- arm64 KASAN, [40873509652](https://github.com/studiomoob/llmoobfs/actions/runs/40873509652):
  **failure**, job 12:28:20 to 12:54:29 UTC (26 min 9 s), under emulation (no `/dev/kvm`).
  `ARM64_BIN_COPY: checksums equal to the host`, then the ext4 step passed up to `CLI_PASS:
  ext4`, and the guest's own `wc`, a dynamic Ubuntu program run from the virtio-fs root, died:
  `wc[2084]: unhandled exception: DABT (lower EL), ESR 0x0000000092000004, level 0 translation
  fault in ld-linux-aarch64.so.1`, then `MOOBFS_VM_FAIL: q2-count-moved-out-ext4` (the step
  counts lines with `wc`). The log has no `BUG:`, `KASAN:`, `UBSAN:`, `WARNING: CPU`, lockdep or
  `Oops` line. This is the failure class of journal 0137: under TCG, programs executed from
  virtio-fs pages crash at random, which is why the workflow runs the test programs from a
  checked tmpfs copy; the system programs still run from virtio-fs.
- arm64 KASAN again, [40877439822](https://github.com/studiomoob/llmoobfs/actions/runs/40877439822),
  dispatched the same way at 12:56 UTC to tell an intermittent fault from a regression:
  **success**, job 12:56:20 to 13:36:17 UTC (39 min 57 s); the ext4 markers through
  `HOSTILE_LOWER_PASS: ext4, 3 trees`, `LAYER_LOWER_PASS: ext4 reflink=no` and
  `MOOBFS_KASAN_ONLY_PASS: steps ext4 under KASAN, UBSAN and lockdep with no kernel diagnostics
  (not a complete run)`.

The last runs of both before this one were scheduled and predate journal 0211's merge
(KCSAN [40503633164](https://github.com/studiomoob/llmoobfs/actions/runs/40503633164),
2026-10-27 12:25 UTC; arm64 [40315886194](https://github.com/studiomoob/llmoobfs/actions/runs/40315886194),
2026-10-26 11:32 UTC), so these are the first runs of the input filter.

## Limitations

- The generation limit is documented, not removed: a lower without generations and ext4's
  `EXT4_IOC_SETVERSION` remain exposed (journal 0216). No test exercises either; NFS and FUSE are
  not supported lowers.
- The two current-state sections are summaries written by hand from the dated notes; the check
  catches only phrases listed in `docs/retired-claims.txt`, not a new claim that goes stale.
- The site's status page says the build limits "are not fair memory shares"
  (`scripts/site-journey.py`); as a description of those limits it is true, and it was left as
  it is.
- The arm64 workflow was not changed. Its system programs (`wc`, `grep`, the shell) still run
  from the virtio-fs root, where journal 0137's crashes under emulation happen. Of the seven
  runs since the tmpfs copy (39265856488 on), only 40873509652 failed, this way. Running them
  from tmpfs too would mean
  copying a dynamic userland into the guest, and a retry on a user-space fatal signal could hide
  a real defect that corrupts what a program reads; neither was done. The workflow is not a
  required check and the failure stays recorded here.
- The workflow runs used the default steps only: KCSAN over `buildrace` and `release`, arm64
  KASAN over `ext4` under emulation, not the whole suite.

## Next step

Close the three items of issue #168 above when this branch is on `main`; the issue's other items
are not part of this journal.
