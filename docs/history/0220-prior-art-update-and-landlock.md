# 0220 — Second prior-art search, "why not a service?" and QUERY2 under Landlock

Date: 2026-10-27 to 2026-10-28. Status: completed (documentation; one measurement in a VM).
Work started: 2026-10-27 (America/Sao_Paulo); ended 2026-10-28. Calendar time, mostly searches
run in parallel and waiting on them, not an engineering-effort measurement. Estimated effort: S.

## Goal

Repeat the search for projects that do what MoobFS does, seventeen days after
`docs/research-originality.md`; write down the answer to "why a kernel module and not a
service?"; and check, before asking anyone outside, whether the known gap with path-based LSMs
still holds for `QUERY2`.

## Search

Three parts run in parallel: agent filesystems (kernel, FUSE, services), kernel-side indexing or
query of xattrs and metadata, and academic work of 2023 to 2026. Repositories were cloned with
`--depth 1` and read, never built or run. lore.kernel.org refused automated reads, Semantic
Scholar answered HTTP 429 and DBLP gave no usable answer, so mailing lists and some venues were
covered through search engines only.

Result, recorded in `docs/research-originality.md` §10: none of the works of the first audit
gained an index, typed attributes, queries or credential filtering; the YoloFS paper, listed
there as unread, was read and has none of them; RAGFS recorded a decision against a kernel
driver. New entries: KiwiFS (schema-checked metadata, rebuildable index and queries, in user
space, authorized by tokens), OpenViking (retrieval filtered by application identity), LSFS,
four FUSE or NFS agent filesystems without attribute queries, macOS `searchfs()` (a kernel query
that checks the caller per result, without an index or xattrs), btrfs `TREE_SEARCH_V2`, a 2011
course tagfs and an abandoned 2004 patent application. Nothing found combines the parts of §5;
each part exists elsewhere on its own. The conclusion is "probable", given the coverage limits.

## "Why not a service?"

`docs/faq.md` question 21. Decision: answer with the single-authority argument (the query
follows the rule that guards `cat` and `grep`; no second permission list, no privileged process
in between, index and attribute change in the same call) together with its limits (uids, writes
on the lower while mounted, path-based LSMs, no shown performance advantage, root and kernel
risk) and the existing recommendation of SQLite or a service for most uses. Appended as question
21 rather than inserted, so that references to questions by number stay valid.

## QUERY2 under Landlock

Journal 0030 measured the gap with v1 `QUERY` on the Azure host. Repeated on 2026-10-28 for
`QUERY2`, in a KVM guest on the project's test host: `main` at `5fa567b`, Debian 6.12.111,
Landlock ABI 6, uid 1001, `tests/landlock_run.c` allowing directory listing beneath the domain
`dom` and file reads only beneath `dom/a`; files `dom/a/m1` and `dom/b/m1`, mode 0644, both with
`kind = note`. The suite step that drove it was temporary and is not committed.

| Operation (under Landlock) | Result |
| --- | --- |
| `QUERY2` on `dom` (`tests/query.c`, direct ioctl) | `a/m1` and `b/m1` |
| `getxattr` of `dom/b/m1` (`moobfs-attr get`) | `kind string note` |
| open `dom/b/m1` | `EACCES` |
| open `dom/a/m1` | succeeds |
| `moobfs query dom ...` (CLI) | fails: `/proc/self/mountinfo: Permission denied` |

Two conclusions. First, the gap holds for `QUERY2`, and it is the one Landlock documents for
itself: its documentation lists `stat` and xattr calls among the actions it cannot restrict yet,
and `security/landlock/fs.c` of Linux 7.3 registers no `inode_permission` or xattr hook (read,
not run). A metadata access right is tracked upstream (landlock-lsm/linux issue 11, with patch
series in April and September 2026). A query under Landlock therefore reveals what `getxattr`
already returns, and file content stays protected. Decision: document this (`docs/faq.md`
question 6, `docs/security-roadmap.md`) and honor Landlock's metadata right when it reaches
mainline; no change to the layer now. Second, the CLI fails under such a sandbox before reaching
the ioctl, because it reads `mountinfo`: issue 167.

## Changes

- `docs/research-originality.md`: §10 and its sources; dated notes where the YoloFS paper was
  listed as unread.
- `docs/faq.md`: question 6 on Landlock; new question 21; `site/faq.html` regenerated.
- `docs/security-roadmap.md`: dated note under the Landlock entry.
- `docs/references.md`: §19.

## Tests

- `python3 scripts/site-faq.py --check`: `site/faq.html` matches `docs/faq.md`.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- The Landlock measurement above, one run (and an earlier run with the CLI only), one kernel,
  ext4. AppArmor and kernels 7.x were not run.

## Limitations and next step

The search is not exhaustive (coverage limits above); several 2026 papers were read only as
abstracts, and Mesa and Archil only from vendor pages. Next: issue 167; the Landlock metadata
right, when merged, decides whether the layer adds a check.
