# Benchmark campaign results

One directory per campaign holds its text artifacts (report, `results.csv`, `results.json`,
environment, equivalence checks, hash manifest) and a `README.md` that says how it ran. Every
campaign so far is **pilot-grade, on one cloud VM, and not an M4 result**. Read the campaign
README and its journal before quoting any number.

| Campaign | What it measured | Journal |
| --- | --- | --- |
| [`2026-10-11-azure-pilot-s1`](2026-10-11-azure-pilot-s1/README.md) | Procedure check of the harness, 1000 memories | 0025 |
| [`2026-10-11-azure-pilot2-s1`](2026-10-11-azure-pilot2-s1/README.md) | Pilot after the method fixes (per-shape W3, `QUERY` as a regular user) | 0025 |
| [`2026-10-11-azure-pilot3-s1`](2026-10-11-azure-pilot3-s1/README.md) | Same pilot with the `QUERY` fast path | 0027 |
| [`2026-10-11-azure-pilot4-s1`](2026-10-11-azure-pilot4-s1/README.md) | Same pilot with RCU-walk dentry revalidation | 0028 |
| [`2026-10-11-azure-profile-q7`](2026-10-11-azure-profile-q7/README.md) | `perf` profile of one `QUERY` shape | 0028 |
| [`2026-10-11-azure-query-scale`](2026-10-11-azure-query-scale/README.md) | `QUERY` and SQLite from 1000 to 60,000 memories, ext4 | 0029 |
| [`2026-10-11-azure-remount`](2026-10-11-azure-remount/README.md) | Remount (index rebuild) time, 25,000 to 60,000 memories | 0031 |

Later campaigns ran on a dedicated host (journal 0042) and have a directory each under this one,
named `2026-10-12-macmini-*`, `2026-10-13-macmini-*` and `2026-10-14-macmini-*`, with their own `README.md`; the table
above was not extended for them.

The weekly benchmark workflow (`.github/workflows/bench.yml`, journal 0238) does not add
campaigns here: its results stay Actions artifacts (`bench-ondemand-<fs>`, 90 days), since they
are a trend monitor on GitHub's shared runners, not reference measurements.

## Raw archives

The raw records (`*.rec`, SQLite databases, manifests, `perf.data`) are too large for Git. They
are attached to a single GitHub release,
[`bench-evidence`](https://github.com/studiomoob/llmoobfs/releases/tag/bench-evidence), whose
text is [`../evidence-release.md`](../evidence-release.md). That file also lists the size and
SHA-256 of each archive. Download URL pattern:

```text
https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/<archive>
```

The first seven archives were separate pre-releases named `bench-2026-10-11-*`. Their tags
remain; journal 0041 records the move.

## Adding a campaign

1. Commit the text artifacts under `bench/results/<date>-<host>-<campaign>/` with a `README.md`.
2. Pack the raw files as `<date>-<host>-<campaign>-raw.tgz`, and add a row to the table in
   `bench/evidence-release.md` with its size and SHA-256. Use `—` in the "Original tag" column.
3. Run `scripts/bench-evidence-release.sh add <archive>`, read the dry-run output, then rerun it
   with `--apply`. The script uploads the archive, checks its size and SHA-256 through the API
   and the public URL, and refreshes the release text.

Do not create a new pre-release per campaign. The procedure is also in
[`docs/benchmark-harness.md`](../../docs/benchmark-harness.md) §7.1.
