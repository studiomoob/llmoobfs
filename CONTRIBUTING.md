# Contributing to MoobFS

Thank you for considering a contribution. Please read this page once; it is
short on ceremony but strict about evidence, because the code runs in ring 0.

## Experimental stage: what to expect

MoobFS is an experimental research project with one maintainer and only source pre-release tags (the last one is `v0.4.1-experimental`; work from `main`).
The layer module has been tested on one kernel series (Debian 6.12.111 in QEMU,
upstream 6.12.112 under KASAN/lockdep), over ext4, btrfs and XFS, in a VM. The
attribute index lives only in RAM, built per domain by the first query on it and released under
memory pressure (ADR-0012). The UAPI (v1 and v2) may
change. Only pilot-grade measurements exist. Our own tests and reviews keep
finding kernel-side defects (the README banner and `CHANGELOG.md` list them);
there has been no outside human review of the kernel code (two source reviews by AI assistants, journals 0111 and 0114;
a first outside contributor reviewed the scripts, tools and Python library, pull requests 161 to 163). Do not expect production readiness, compatibility promises or quick
turnaround on every issue. See the status banner in [README.md](README.md).

## Where to start

- [docs/development.md](docs/development.md): host requirements, build, host
  tests, the VM harness and KASAN.
- [docs/testing.md](docs/testing.md): what each test and each `*_PASS` marker
  proves, and how to read the logs.
- Issues labelled `difficulty/good-first-issue` (see [docs/labels.md](docs/labels.md))
  are tasks that need no kernel work and can be verified on the host or in CI.
- Before a substantial change, open an issue (or a proposal using the template)
  so alternatives, scope and compatibility can be discussed first. Small fixes
  can go straight to a pull request.
- For vulnerabilities, follow [SECURITY.md](SECURITY.md); never a public issue.

Never upload real agent memories, credentials or personal data as fixtures or
in logs. Never load the module on a machine you use for anything else: use the disposable VM,
or a dedicated test host as described in [docs/operations.md](docs/operations.md).

## Language policy

Everything in the repository is in English: code, comments, commit messages, issues, pull
requests, documentation, specifications, ADRs, journals and agent instructions
([ADR-0010](docs/decisions/0010-english-everywhere.md)). `README.pt-BR.md` is a courtesy mirror
of the English README. Translations of earlier Portuguese material are faithful: they do not
change claims, statuses, numbers or decisions.

## Developer Certificate of Origin (DCO)

Every commit must carry a `Signed-off-by:` line with your real name and an
email address you control:

```sh
git commit -s
```

By adding that line you certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/):
that you wrote the change or have the right to submit it under the project
license (GPL-2.0-only), that you understand the contribution is public and will
be kept with the sign-off, and that any third-party material you included is
under a compatible license with attribution preserved. There is no CLA. CI
rejects pull requests whose commits lack a sign-off. The requirement applies to
commits made from this change onward; existing history is not affected.

## Commit messages

Kernel style: a short imperative subject with an area prefix, a blank line, then
a body that explains **why** (the problem, the alternatives considered, what was
tested), then trailers.

```text
layer: refuse index_budget outside 1M..4G at mount time

Values below 1 MiB make the index unusable and values above 4 GiB
overflow the accounting on 32-bit builds. Reject both with EINVAL
instead of clamping, so the mounter sees the misconfiguration.

Tested: make vm-test (build/vm/console.log), KASAN run <link>.
Journal: docs/history/00NN-....md
Signed-off-by: Your Name <you@example.com>
```

Area prefixes in use: `layer:`, `kernel:`, `common:`, `tools:`, `tests:`,
`vm:`, `ci:`, `docs:`, `site:`, `kasan:`. Reference the issue, ADR or journal
entry in the body. Keep commits small and legible; one logical change each.

## Project history rules

These come from [AGENTS.md](AGENTS.md) and apply to every contributor, human or
AI-assisted:

- Every change records a journal entry in `docs/history/` with the next
  sequential id and a line in `docs/history/README.md`: objective, decisions,
  changes, **tests actually executed** (with log paths), limitations, next step.
  Small documentation fixes may be added as a dated addendum to the relevant
  entry instead.
- Architectural decisions become an ADR in `docs/decisions/` with the next
  sequential id. Old ADRs are never edited or deleted; a new ADR points to the
  one it supersedes.
- Update `CHANGELOG.md` under "Unreleased" for anything a user or contributor
  would notice.
- Never rewrite shared history: no force-push to `main`, no deleting records to
  hide attempts or limitations. On a PR branch, rebase is fine before review;
  after review has started, add commits instead. To catch up with `main`, rebase
  your branch rather than merging `main` into it; the maintainer may ask for
  that. The maintainer merges, with a merge commit.
- Record facts, not private reasoning. Correct a factually wrong record with a
  dated note rather than silently editing it.

## Scope rules

- MoobFS is a native Linux filesystem layer ([ADR-0001](docs/decisions/0001-native-filesystem.md),
  [ADR-0007](docs/decisions/0007-stacked-memory-layer.md)). Proposals to replace
  it with FUSE, a daemon or a user-space API are out of scope.
- Changes to the on-disk M1 format ([docs/format-m1.md](docs/format-m1.md)), to
  the attribute encoding (`common/attr.c`, `include/moobfs_attr.h`) or to the
  UAPI (`include/moobfs_ioctl.h`) need, before code: a specification update, an
  explicit compatibility decision (ADR when it is architectural, including a
  version bump when the UAPI changes), and tests with **invalid inputs**.
- No new feature claims "semantic search", "vector index" or similar in the
  kernel; retrieval beyond typed-attribute predicates belongs to clients.
- Adding a lower filesystem or a kernel version to the matrix is a scope change
  that needs a journal entry and, if it changes guarantees, an ADR.

## Coding style

- Kernel code: Linux kernel style (tabs, 80 columns as a guide, `checkpatch.pl`
  clean when practical), explicit bounds and endianness, no warnings disabled to
  get a build.
- Tools and tests: C11 with `-Wall -Wextra -Werror -Wconversion -Wshadow` as in
  the `Makefile`.
- Every new `.c`, `.h`, `.py` and `.sh` file starts with an
  `SPDX-License-Identifier: GPL-2.0-only` line (CI checks the C sources).
- Shell scripts in `vm/` and `scripts/` must pass `shellcheck` at every severity (`make lint`, as CI runs it). A finding left on purpose gets a `# shellcheck disable=` line with its reason.

## Testing requirements

| Change touches | Required before review |
| --- | --- |
| Anything | `make test` on the host; CI green (`Tools and sanitizers`, `Lint`). |
| `tools/`, `common/`, `include/`, `tests/` | `make test` with ASan/UBSan (see `docs/testing.md`), `make fuzz` when the attribute encoding is involved, and `make vm-test`. |
| `layer/`, `kernel/`, `vm/` | `make vm-test` green locally with the log path in the PR; a new or extended test in `vm/suite.sh` or `tests/` for every behaviour change, including invalid inputs for parsers and ioctls. |
| `layer/`, `vm/`, `include/`, `common/`, `tests/`, `sqlite/`, `tools/`, `Makefile` | The KASAN/lockdep workflow (`.github/workflows/kasan.yml`) is a required check of `main`. It runs the suite when the pull request touches these paths, passes at once otherwise, and must finish with `MOOBFS_KASAN_PASS` and no `BUG`, `WARNING`, lockdep or UBSAN report. |
| `docs/`, `site/` | `python3 scripts/check-site.py` for the site; link check for docs. No fabricated kernel runs. |

"Tested" means executed, with a log you can point to. Compiling on macOS tests
nothing on Linux. Do not write that a change was tested in the VM unless
`make vm-test` (or the KASAN workflow) actually ran on it.

## Pull request checklist

The PR template at [.github/pull_request_template.md](.github/pull_request_template.md)
contains the checklist. In short: scope backed by an issue or ADR; tests run
with log paths; VM suite for kernel-side changes; KASAN link for `layer/` or
`vm/`; journal entry and `CHANGELOG.md`; DCO sign-off on every commit; no claim
without evidence; UAPI and format boxes answered.

## Review and acceptance

A pull request is merged when all of the following hold:

1. CI is green: `Tools and sanitizers`, `Native Linux VM` and `Lint`; for
   `layer/` or `vm/` changes also `KASAN and lockdep`.
2. The scope matches an accepted ADR or a discussed issue. Format or UAPI
   changes without a prior issue are not accepted.
3. Behaviour changes come with tests in the VM (`vm/suite.sh` or `tests/`),
   including invalid inputs where there is a parser or an ioctl.
4. The journal entry and `CHANGELOG.md` line exist and describe only tests that
   actually ran.
5. No unproven claims: nothing about performance, security, "tested on Linux"
   beyond the logs, or production readiness. Check the list of statements to
   avoid in [docs/research-originality.md](docs/research-originality.md) §7.
6. For `layer/`, `include/` and `common/`: one maintainer approval **plus one
   recorded external review** (a review comment on the PR by someone other than
   the author and the maintainer, with their name). This rule stands until the
   project has a second maintainer, when it becomes two approvals. A review
   comment is a record, not a proof of safety.
7. The maintainer merges, usually by squash with a final message written by the
   maintainer; a bisectable series may be merged with history preserved.

Reverting is preferred over rewriting. Passing CI is necessary but does not
prove safety or production readiness.

## Performance claims

No performance, latency, memory or "faster than X" statement is accepted in
code, docs, site or commit messages unless it cites an evidence file in the
repository (for example `build/bench/<date>/...` attached to a journal entry)
with the command, kernel, hardware, repetitions and the raw numbers. The
benchmark methodology is in [docs/benchmark-plan.md](docs/benchmark-plan.md)
and [docs/benchmark-harness.md](docs/benchmark-harness.md) (M4, not started).

## Labels

Issues and PRs use the scheme in [docs/labels.md](docs/labels.md): area,
difficulty, priority and status labels. Performance issues are born with
`status/needs-evidence` and `area/bench-m4`.

## AI-assisted contributions

AI tools are welcome. Contributors remain responsible for understanding the
patch, license compatibility, correctness and test evidence. Follow
[AGENTS.md](AGENTS.md); Claude and Cursor load the same policy through their
project entry points. Summarize material AI assistance and what a human reviewed
in the PR. Do not invent test results, references or approvals. Record concise
technical rationale, not hidden reasoning, private conversations or secrets.
