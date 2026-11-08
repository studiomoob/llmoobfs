# 0274 — KCSAN on the code corrected in November

Date: 2026-11-08, about 10:20–11:19 Brasília time (the run). Status: **done**. Effort: S; the
calendar time was machine time.

## Goal

A review of the stage 2 criteria on 2026-11-07 found the last KCSAN record on the
module older than the November corrections (partition accounting, journal 0259; F1 and F2,
journal 0266; S16's self-check, journal 0271). This run repeats it on `main` at `1cac601`.

## Run

`vm/kasan-local.sh` with `KCONFIG=vm/kcsan.config` (the fragment of `kcsan.yml`, journal 0127),
upstream 6.12.112 (the pin of `1cac601`), on the test host, CPUs 0–3, with
`MOOB_ONLY="buildrace release hookrace selfcheck permchurn"`: the steps that race builds with
writes, releases, rename and unlink hooks, the self-check and the permission churn. The harness
judges reports with `vm/kdiag.sh`, which fails a run only on a report with a racing access in
the layer.

Result: `MOOBFS_ONLY_PASS` and `MOOBFS_KASAN_ONLY_PASS` ("under KCSAN and lockdep with no kernel
diagnostics"), exit 0. The console has 30 KCSAN reports, none with an access in `moobfs_layer`:

| Reports | Data race in | Note |
| --- | --- | --- |
| 21 | `tick_nohz_handler` / `tick_nohz_idle_got_tick` | timer core |
| 4 | `virtqueue_kick_prepare`, `virtqueue_get_buf_ctx` | virtio |
| 2 | `pids_release` | pids cgroup |
| 1 | `mb_cache_entry_create` | ext4's mbcache |
| 2 | `d_splice_alias` / `d_splice_alias` | see below |

The two `d_splice_alias` reports are two lookups of the same lower directory in parallel, both
in `ext4_lookup()` → `d_splice_alias()` (`fs/dcache.c`): one from a build's scan
(`scan_run()` → `lookup_one_len_unlocked()`), one from the layer's own `moobl_lookup()`. Both
racing accesses are inside the VFS; the layer only makes two lookups the VFS allows in parallel
(`lookup_one_len_unlocked()` takes the directory's lock shared, journal 0266's references).

## Limitations

- Five steps, not the whole suite (KCSAN's CI job runs `buildrace release`).
- Upstream 6.12.112; the move to 6.12.116 (journal 0272) is on a branch not yet merged.

## Next step

None for the module. The `d_splice_alias` race is the VFS's; it is not reported upstream here.
