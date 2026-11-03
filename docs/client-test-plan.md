# Real memory client test — plan

Status: original proposal 2026-10-12; stages 1/2 subsequently produced the bounded results
recorded in journals 0057, 0064–0068 and 0071. Stage 3 end-to-end integration remains separate.
The proposal and its original scope below are preserved; it is not evidence of a completed
final usefulness evaluation (G6). Current-module reruns with intervals remain open (ROADMAP).

## Question

The QUERY2 series answered "is the query fast and safe" (journals 0043 to 0049). It did not answer
"does an agent memory system gain anything by running on MoobFS". This plan measures that with an
existing client, unchanged first, then with its structured recall served by `QUERY2`.

## Client: ai-memory

[ai-memory](https://github.com/akitaonrails/ai-memory) (MIT, Rust) keeps memories as Markdown files
with YAML front matter under `<data_dir>/wiki/<workspace>/<project>/<kind>/*.md`, and a derived
SQLite index (`docs/research-ai-memory.md`). Its structured queries are mapped in
`docs/research-client-query-patterns.md` §1: scope by workspace and project, `is_latest`,
`expires_at IS NULL OR > now` ("missing or ..."), `kind IN (...)`, `ORDER BY updated_at DESC
LIMIT n`. Those are exactly the shapes of QUERY2 (`MOOB_TERM_MISSING_OK`, batch, `order_by`).
It has no storage abstraction (`rusqlite` and `std::fs` called directly), so stage 3 needs a fork.

Mapping: mount the layer on `<data_dir>/wiki` with `domain_depth=1`, so each workspace directory is
a domain; front matter fields become typed attributes (`kind`, `tier`, `pinned`, `expires_at`,
`agent`, `tags`, `updated_at`).
(Correction of 2026-10-12, journal 0063: `updated_at` and `is_latest` are columns of ai-memory's
SQLite, not front matter keys, and its page directories are UUIDs; see "Stage 2 design" below.)

## Stages

1. **Transparency (no code change).** Run ai-memory with `<data_dir>/wiki` on the layer and on plain
   ext4, same machine, same scripted session replay (capture, consolidate, recall, commit). Measure
   page write latency, `git commit`, watcher reconciliation and recall latency; check that its own
   test suite passes with the data directory on the layer. Done when: same results on both, and
   the overhead of the layer on this workload is measured.
2. **Structured recall on QUERY2 (side adapter).** A small tool mirrors front matter into
   `user.moob.*` attributes at write time (from the watcher events, as a sidecar). Replay ai-memory's
   structured queries (recent pages, kind filters, expiry, handoff state) both through its SQLite and
   through QUERY2, compare answers and latency, and measure what the sidecar costs per write against
   what ai-memory spends keeping SQLite in sync. Full-text and vector search stay in SQLite.
   Done when: equal answers on a replayed workload, latencies and write costs reported.
3. **End to end (fork).** Replace the structured part of `recent_pages_for_project` and the scope
   and expiry predicates of search with QUERY2 calls in a fork, and run the client's own LongMemEval
   harness to check that recall quality is unchanged while latency and sync cost change.

## Risks and open points

- The data set: ai-memory's harness and LongMemEval give a workload; its size must reach at least
  the 60 000 memories used in journal 0044 to be comparable.
- Building ai-memory runs third-party build scripts (about 396 thousand lines of Rust); it is done
  on the dedicated benchmark host only, at a pinned commit.
- Its watcher does not tell its own writes from others'; the sidecar must avoid write loops.
- Multi-user read filters (`readable_repository_sql`) have no direct QUERY2 equivalent beyond the
  domain; stage 2 compares single-workspace queries first.

## Stage 2 design (proposal of 2026-10-12, journal 0063)

What the code says (ai-memory 385b81e, read only):

- Pages are `<wiki>/<workspace-uuid>/<project-uuid>/<family>/<name>.md`, families `sessions`,
  `_rules`, `_slots`, `decisions`, `gotchas`, `concepts`, `procedures`, `notes`, `runbooks`,
  `profile` and others. Front matter is untyped YAML; the keys that matter here are `tier`
  (working, episodic, semantic, procedural), `pinned` (bool, forced for `_slots/`), `expires_at`
  (RFC 3339 or a date), `kind` and `agent`.
- `updated_at` (store clock, microseconds) and `is_latest` exist only in SQLite. On disk a page file
  is always the latest version, so `is_latest = 1` holds for every file.
- Writes are atomic (temporary file, fsync, rename, fsync of the directory) and the SQLite row is
  updated in the same call; the watcher reconciles outside edits after 300 ms, and every 30 s.
- **The LongMemEval replay exercises none of the structured queries**: it only creates
  `sessions/<id>.md` pages and searches with BM25, scope, `is_latest` and not-expired. A stage 2
  workload must therefore create the other page families itself.

Decisions:

1. **Domains are projects** (`domain_depth=2` over `<wiki>`): each `<workspace>/<project>`
   directory is a domain, so the project scope of every per-project query is the directory the
   query is issued on, and no `workspace`/`project` attribute is needed. Alternative: depth 1 with a
   `project` attribute in every term list; rejected, it repeats what the path already says.
2. **Workload** (`W-AIM`): a real `ai-memory serve` (zero-LLM) receives, through its MCP
   `memory_write_page` tool, a deterministic mix per project: rules, slots (pinned), decisions,
   gotchas, notes and procedures, a fraction with `expires_at` in the past or the future, the four
   tiers, plus LongMemEval session pages. Scales grow to the 60 000 pages of journal 0044.
3. **Mirror**: after the workload, a tool reads each page's front matter and its `updated_at` from
   ai-memory's SQLite and writes `user.moob.family`, `tier`, `pinned`, `kind`, `expires_at`
   (time) and `updated_at` (time). Using the store's own `updated_at` keeps the ordering key
   identical on both sides, so answers can be compared exactly. The file mtime is the alternative
   for a sidecar that does not read the database; its cost and its ties are measured separately.
4. **Shapes compared** (ai-memory's SQL, from `store/src/reader.rs`, against `QUERY2` on the
   project directory):
   - recent pages: not expired, `ORDER BY updated_at DESC LIMIT n` (`memory_recent`);
   - pinned pages, most recent first (`memory_query` with `pin_first`);
   - rules, most recent first (briefing);
   - settled knowledge: kind in (rule, decision) (`QUERY2_BATCH`, one conjunction per value);
   - stale episodic pages: `tier = episodic AND updated_at < cutoff ORDER BY updated_at ASC`;
   - incremental sync: `updated_at > t ORDER BY updated_at ASC` paged (keyset in SQL, cursor in
     `QUERY2`).
   Handoffs live only in SQLite and stay out; so do full-text, vectors and the
   readable-repository filter of the global queries.
5. **Measures**: for every shape, equal answers (same pages in the same order; pages with equal
   `updated_at` compared as a set, since `QUERY2` breaks ties by inode and SQLite by row order); latency of the SQL on ai-memory's database against `QUERY2`, warm and cold;
   the mirror's cost per page; index memory from `GET_INFO`.

Done when: equal answers on every shape and scale, latencies, and the mirror's cost reported,
including the shapes where `QUERY2` loses.

### Results so far (2026-10-12, journals 0064 and 0065)

Tools written (`bench/client/`); measured at 150 and 1 905 pages, one run each. Answers are equal
on every shape. Warm latency: `QUERY2` wins on `pinned`, `rules` and `settled`, is equal on
`recent` and `stale` and **loses on `incremental`**. Cold, both sides are dominated by the open.

Two facts about the client change how the scales must run, and replace the estimate of one hour
for 60 000 pages:

- its scheduled maintenance (lint and forget sweep, 60 s after start) deletes expired pages and
  adds its own pages while the workload is written;
- its write cost grows with the store (75 to 221 ms per page over 2 000 pages).

Still open: the scales up to 60 000 pages, the live sidecar and the mirror's cost against what
ai-memory spends on SQLite.

Update of the same day (journal 0068): the scales are done, 2 000 to 60 000 pages, with equal
answers at each. Pages above 6 000 are loaded as files and indexed by the client's `reindex`
(decision 2 said MCP; its cost per write grows with the store). Still open: the live sidecar and
its cost against the client's own synchronization.

Update of 2026-10-13 (journal 0071): the live sidecar is built and measured at 2 000 pages. Every
structured field equals the client's database, the file's mtime orders pages as the store's clock
does, and the client's writes and watcher are unaffected. Stage 2 is done; its measurements are
pilot-grade and from one client.
