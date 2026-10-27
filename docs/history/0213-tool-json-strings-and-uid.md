# 0213 — Every moobfs-check string escaped; --uid without root's groups

Date: 2026-10-27. Status: done on `marcelometal/check-json-escape`. Numbered 0213 because
pull requests 160, 161 and 162 took 0210 to 0212 first. Work started: 2026-10-27
about 15:15 America/Sao_Paulo; ended about 15:45. Calendar time, not an effort measurement.
Estimated effort: S.

## Goal and decision

A source review of the user-space tools by a contributor, made with an AI assistant, found that
`moobfs-check` escaped only the `path` member of its JSON document. The attribute name of a
problem and the `reason` of a `left_out` entry were printed with `%s`, and both can hold bytes a
writer of the tree chooses: a name is reported precisely when it fails validation, and a reason
embeds the name of a directory. A file carrying `user.moob.a"}, {"path": "forged", ...` produced
a document that `json.load` accepted with one forged problem and `problem_count` 1; a directory
named `d"x` under `--domain-depth` produced one that did not parse. `docs/tools.md` promised the
escaping and `problem_count` equal to the length of `problems`. Exit status and counters stayed
correct. The same review found that `moobfs-index --uid` kept root's supplementary groups and
read a non-numeric uid as 0, staying root.

Decisions:

- One escaping function for every string of the `moobfs-check` document, the same rule as
  `moobfs query --json` and `moobfs-index --json`: `"`, `\` and control characters escaped,
  nothing else converted (non-UTF-8 names still give a document strict parsers reject, as
  `docs/tools.md` says).
- `moobfs-index --uid` drops supplementary groups when it runs as root and refuses a value that
  is not a decimal uid (exit 64). `tests/query.c --uid`, the kernel-side client the suite
  compares with the oracle, drops them too, so both sides keep the same identity. A non-root
  caller passing its own uid still works.
- `moobfs query` and the SQLite table no longer trust two fields beyond what the UAPI header
  states: a `count` above the request's `limit` is an error (`EPROTO`), and a record whose
  `path_len` does not fit `path`, or that has no NUL after its path, is a result with an unknown
  path. The module already guarantees both; the header promises only the length.

## Changes

`tools/moobfs-check.c`, `tools/moobfs-index.c`, `tools/moobfs.c`, `sqlite/moobfs_vtab.c`,
`tests/query.c`, `tests/hostile.c` (three invalid names with `"`, `\` and a fragment of JSON),
`tests/hostile-user.sh` (every `moobfs-check` document must parse and count its problems),
`tests/tools-json.sh` (an invalid name and a private directory name with `"` and `\`, compared
exactly), `docs/tools.md`, `man/moobfs-index.1`, the changelog and the history index. The
kernel, the UAPI and the attribute encoding are untouched.

## What ran

On Debian forky, kernel 7.2.12, gcc 16.2 and clang 21.1, `TMPDIR` on a filesystem with user
extended attributes:

- `make test`; `make tools attr-tools bench-tools` with `-Werror` under gcc and clang;
  `make sqlite-ext`.
- `make hostile-test CC=clang HOSTILE_SEEDS=50` with address and undefined behaviour sanitizers
  and `-fno-sanitize-recover=all`, as in CI: `HOSTILE_USER_PASS: 50 trees`,
  `TOOLS_JSON_PASS ... (xattr fixture=1)`, no sanitizer report.
- Negative round, `tools/moobfs-check.c` of `origin/main` with the new tests:
  `tests/hostile-user.sh` failed at seed 1 (`Invalid control character`, the `k\t0` name was
  never escaped either) and `tests/tools-json.sh` failed with `Expecting ',' delimiter`.
- `moobfs-index --uid abc` and `--uid 1000x` exit 64.
- `mandoc -Tlint -Wwarning man/*` in the pinned `debian:trixie-slim` image of `vm/Dockerfile`.
- The complete `make vm-test` (Debian 6.12.111 guest under QEMU in Docker), because
  `tests/query.c` and `tests/hostile.c` feed the suite: 13 minutes, exit 0, ending in
  `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`, with
  `HOSTILE_LOWER_PASS` and `ISOLATION_PASS` on ext4, btrfs and XFS.

## Limitations

The tools still read attributes by path after `nftw()`: a writer who swaps a directory for a
symbolic link between the walk and the read redirects the read when root runs the tools over a
tree others write. That needs fd-relative reads or a documented restriction, and is left for a
separate decision.

## Merge note (2026-10-27, maintainer)

The index line and the title now use the repository's `0213 —` form; nothing else changed at
merge.

## Next step

Review and merge.
