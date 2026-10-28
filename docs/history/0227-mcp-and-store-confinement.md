# 0227 — MCP adapter and LangGraph store confinement (two private reports)

Date: 2026-10-28. Status: branch `claudiofp/BATCH4`, published with its pull request. Work
started: 2026-10-28 19:05 America/Sao_Paulo; ended 19:20. Wall-clock time, not an
engineering-effort measurement. Estimated effort: S.

## Goal

Fix two private reports that an external reviewer filed at 19:03 with tested patches, before the
`v0.5.0-experimental` tag (journal 0226): both defects are on `main` since 2026-10-27 and no tag
contains them, so the tag waits for the fix rather than ship two known defects (the rule of
journals 0087 and 0142). User space only; ADR-0022 does not apply.

## The reports

- **GHSA-4gr9-rvgr-44qj, MCP adapter (medium).** `Memory.file_path()` checked each component
  with `lstat()` and returned a path that `read`, `write` and `delete` then used again. A shared
  domain is `root:<group>` `2770` without a sticky bit (`docs/operations.md`), so another member
  can swap a directory of the path for a symbolic link between the check and the use; the
  server then read, wrote or removed files outside `--root` with the agent's rights, and
  `chmod()` gave the link's target the domain's mode. `read` opened before checking `S_ISREG`
  and without `O_NONBLOCK`, so a FIFO held the synchronous server forever. `check_identity()`
  compared only real and effective ids, missing a saved uid of 0 and effective capabilities such
  as `CAP_DAC_READ_SEARCH`, which defeat the kernel checks ADR-0024 decision 3 relies on.
- **GHSA-3wx4-fh9w-2v66, LangGraph store (low).** LangGraph's `BaseStore` applies its namespace
  label rule (strings, not empty, no period) in `get`, `put`, `search` and `delete`, not to the
  operations given to `batch()`; `MoobStore` implements only `batch`, and a label is a directory
  name, so `PutOp(("..", "outside"), ...)` wrote, read and removed `.json` files outside the root.
  Only an application that builds operations from untrusted labels and calls `batch()` directly
  is affected.

## Decision

Take both patches as the reviewer wrote them (commits keep the reviewer's authorship and
sign-off), after reading them against the code:

- MCP: each component is opened relative to the one before with `O_DIRECTORY | O_NOFOLLOW`;
  missing directories are made with `mkdir(dir_fd=)` and given the domain's mode with `fchmod`
  on the opened descriptor; `publish()` takes a `dir_fd` and runs every step (existence check,
  temporary file, rename, directory fsync) relative to it; `read` opens with `O_NONBLOCK`;
  `delete` stats and unlinks relative to the parent descriptor; `check_identity()` compares the
  full `getresuid()` and `getresgid()` triples and refuses any bit of `CapEff`.
- Store: every operation of a batch is checked with LangGraph's rule before any runs.

Read-through notes: the root itself is still opened by path, as it is the administrator's;
`publish()` without `dir_fd` behaves as before for the other callers (the CLI and the store).
A directory moved elsewhere after it was opened is still used (the reviewer's own limitation):
the server then acts inside that directory, which the agent could already reach.

## Changes

- `python/src/moobfs/mcp.py`, `python/src/moobfs/publish.py`, `python/src/moobfs/langgraph.py`
  and their tests (the reviewer's two commits).
- `docs/decisions/0024-mcp-agent-adapter.md`: a dated note on decisions 3 and 4.
- `CHANGELOG.md`, `python/README.md`, `docs/security-roadmap.md` (S1), `docs/status.md`,
  `docs/status.pt-BR.md`.

## Evidence

On the dedicated host (Debian 13, Python 3.13.5, `langgraph==1.2.15`, `mcp==2.3.0`, the library
installed with pip from the branch):

- The whole Python suite three times: 39 tests, OK, one skipped because it needs root; that test
  (`test_linux`) then passed as root.
- Negative round: the new tests on the source of `e5b6591` fail in three runs of three:
  `test_batched_labels_stay_inside_the_root`, `test_component_swapped_for_a_link`,
  `test_fifo_does_not_hang_read`, `test_identity_refused`.
- `make pylib-test` (what CI runs, without the optional SDKs): OK, 13 skipped.

The CI job does not install LangGraph or the MCP SDK, so the new tests run only where those are
installed; the runs above are the evidence for them.

## Limitations

A directory moved away after it was opened is still used. No CVE is requested, as for the
earlier advisories. The tag `v0.5.0-experimental` is cut after this merge (journal 0226).

## Next step

Merge, publish both advisories with the reviewer credited, rebase the release branch and tag.
