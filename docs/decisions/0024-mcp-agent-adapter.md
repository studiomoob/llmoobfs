# ADR-0024 — An MCP adapter over stdio that runs as the agent's own uid

Date: 2026-10-27. Status: **accepted by the maintainer on 2026-10-27** as roadmap item C7, on
the recommendation recorded in journal 0208; the design below is proposed with the
implementation. Estimated effort: M (one to three days).

## Context

ROADMAP C7 asks for one adapter that exposes the library of C5 (ADR-0023) to agents through a
standard protocol, outside `layer/` and client-neutral, whose answers equal the library's, which
"runs under the agent's own uid and cannot act with another identity" (the confused-deputy risk
of distribution strategy §2.5), and through which the two-user isolation test passes.

The Model Context Protocol (MCP) is the protocol most agent hosts speak for tools. Its official
Python SDK (`mcp`, version 2) serves tools over stdio or over HTTP.

## Decision

1. **`moobfs.mcp` in the library's package**, optional extra `mcp` (`mcp>=2.3`), started as
   `python -m moobfs.mcp --root <memory root>`. LGPL-2.1-or-later like the rest of `python/`.
2. **stdio only.** The agent's host starts the server as its own subprocess, so the server has
   the agent's credentials and no others. No HTTP or socket transport is offered: a listening
   server would answer whoever connects with the server's identity, which is the confused deputy
   that §2.5 warns about.
3. **No identity of its own.** The server refuses to start as root (euid 0, which bypasses the
   permission checks the isolation rests on) and when its real and effective ids differ
   (setuid or setgid). It has no option to switch users. Every check of who may read or write is
   the kernel's, made with the agent's credentials: `QUERY2` on the domain, `open`, `rename`.
4. **Confined to the root.** Every path is a domain name (one component) and a path relative to
   it: no absolute path, no `..`, no NUL. A symbolic link on the way is refused, and the resolved
   path must stay under `--root`. So a tool cannot reach a file the agent could read elsewhere
   (its home, for instance) through this server.
5. **Tools.**
   - `info`: the root, whether the layer is mounted there, the server's uid, and the domains the
     agent can enter.
   - `query`: terms as `{name, op, value, type?, missing_ok?}`, an optional order and a limit.
     Matches are domain-relative paths with inode numbers, plus the backend that answered.
   - `read`: the content (UTF-8, up to a byte limit) and the typed attributes of one file.
   - `write`: one memory through `moobfs.publish`, refusing an existing name unless `replace`.
   - `delete`: one file.
   `--read-only` leaves out `write` and `delete`. Typing over JSON: an explicit `type` wins;
   otherwise a JSON boolean is a bool, an integer an int64, a fraction a double, a string a
   string; `time` takes an ISO 8601 string or integer nanoseconds.
   *Note, 2026-10-28 (journal 0227, [GHSA-4gr9-rvgr-44qj](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-4gr9-rvgr-44qj)):* decisions 3 and 4 did not hold
   against a member of a shared domain, who could swap a directory of a path for a symbolic link
   after the `lstat()` check, nor against a saved uid of 0 or an effective capability. The server
   now compares the full real, effective and saved ids, refuses any effective capability, and
   opens each component relative to the one before with `O_NOFOLLOW`, working on those
   descriptors; `read` opens with `O_NONBLOCK`. A directory moved elsewhere after it was opened
   is still used.
6. **Errors are explicit**: a refused query (`QueryRefused`), a permission denial or an invalid
   path is a tool error with its reason and no partial result.

## Prior art this builds on

- The MCP specification and its official Python SDK (`MCPServer`, stdio transport,
  `Client`/`ClientSession` for tests).
- The reference MCP filesystem server of the MCP project, which also confines its tools to
  allowed directories and refuses paths outside them; here the confinement is one root and the
  permission decisions stay with the kernel.
- Distribution strategy §2.5: a helper that issues `QUERY` for an agent must assume the agent's
  uid, otherwise it queries with other credentials.
- The library of ADR-0023, whose `Domain` gives the same answers with or without the module.

## Alternatives considered

- **An HTTP MCP server, one per host, serving several agents.** Rejected: it would need to
  authenticate each agent and switch to its uid for every call, which is what decision 3 rules
  out; any mistake there is a cross-user leak.
- **A server that starts as root and drops to a uid given on the command line.** Rejected: whoever
  can start it chooses the identity; that is the confused deputy, with extra steps.
- **Tools for raw `QUERY2` structures.** Rejected: agents would have to build binary terms; the
  JSON term with an optional type is enough and maps one to one onto `moobfs.Term`.
- **A separate package.** Not now: it would duplicate the library's release and tests; the extra
  keeps `mcp` off installations that do not use it.

## Consequences

- An agent reaches its memories with its own permissions and nothing more; an administrator gives
  it a uid and directories exactly as for any other client (operations guide, container recipe).
- The server is one more piece of user-space code to keep in step with the SDK; the SDK is pinned
  for the tests and the extra names a minimum version.
- Full-text and vector search are not offered (client contract section 8).
