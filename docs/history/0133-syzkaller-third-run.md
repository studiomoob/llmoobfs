# 0133 — syzkaller: a third six-hour run, no report

Date: 2026-10-18 to 2026-10-19. Status: done; per-function coverage not collected.

## Goal

Run syzkaller again over the layer after the fixes merged since the second run (journal 0109),
on the benchmark host, while the F2 and F4 branches waited for review (journals 0130 and 0132).

## What ran

- Commit: `main` at bf1a34b (pull request 84). The layer, `common/`, `include/` and
  `vm/syzkaller/` are identical on `main` at 3df77b1 (`git diff --stat bf1a34b 3df77b1` on those
  paths is empty), so the result holds for the current `main`. The F2 and F4 branches were not
  fuzzed.
- Kernel: the pinned 6.12.112 of the KASAN job with the options of `vm/syzkaller/kernel.config`
  (KCOV with comparisons), as in journal 0109.
- Command, on the benchmark host:
  `SYZ=<checkout> GO=<go bin> sh vm/syzkaller-local.sh 21600 module image run`.

## Result

| | second run (0109) | third run |
|---|---|---|
| duration | 6 h | 6 h (21 600 s), manager exit status 0 |
| executions | 567 085 | 720 747 (about 33 per second on average) |
| corpus | — | 1 205 programs |
| coverage reported by the manager | — | 16 146 |
| crash directories | 0 | 0 |

The manager's coverage figure counts kernel-wide coverage points; it is not the per-function
measure the threshold of security roadmap S3 uses, and the two are not comparable.

## Limitations

- The per-function coverage of `layer/` was not collected before the manager exited, so the
  threshold status stays at the second run's measure (78 % of the handler PCs against 85 %).
- One host, one kernel, x86-64. The release paths and the shrinker are still out of a program's
  reach (security roadmap S3).

## Next step

- Save the manager's coverage report at the end of a run, so every long run measures the S3
  threshold.
- Fuzz again once F2 (pull request 89) and F4 (pull request 90) are on `main`: both change write
  and index paths that the descriptions reach.
