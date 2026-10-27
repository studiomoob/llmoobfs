# Project continuity

Read `README.md`, `docs/history/README.md`, the most recent session journal and the relevant ADRs before changing the project.

## Mandatory history

- Record requests and results in `docs/history/`, as factual technical summaries: goal, decisions, alternatives, brief justifications, changes, evidence, limitations and next steps.
- Record decisions, including those that arise from user questions, but not the questions themselves nor the clarifying answers. Errors found together with their fix, discarded hypotheses and failed experiments continue to be recorded. An investigation in progress has its own journal, marked as in progress and updated at each round.
- Document architectural decisions in `docs/decisions/` with sequential IDs. Preserve old decisions; a change must point to the decision it replaces.
- Preserve previous specifications. Clearly identify proposal, implementation and actually tested behavior.
- Update `CHANGELOG.md` for relevant changes. Keep reproducible commands and summaries of results; never present an unexecuted test as passed.
- Make small, readable local commits when a unit is complete. Do not rewrite shared history and do not delete records to hide attempts or limitations. Never force-push to `main`, to history already merged or to a contributor's branch. Single, recorded exception: on 2026-10-10 the history was rewritten, with the user's explicit authorization, to remove questions recorded by mistake and personal data (journal 0018).
- Pull requests enter `main` with a merge commit. Rebase a branch on `main` before pushing it, and do not merge `main` into a topic branch. When `main` moves while one of our own pull requests is open and unmerged, rebase it again and push with `--force-with-lease`; that is the only forced push allowed. For a contributor's pull request, ask the contributor to rebase; if they cannot, merging `main` into their branch is the exception, recorded in the journal of the merge (journal 0217).
- Do not store credentials, tokens, environment dumps or unnecessary personal data. Histories record brief technical justifications, not the assistant's private internal reasoning.
- Outreach stays outside the repository: drafts of messages, calls for reviewers or contributors, conversations, where something was posted, and research about people. When an outside answer changes the project, the journal records its technical content and the public link to it, with the person described by role, not by name.
- Before finishing a task, make clear the current state, the tests, the limitations and the next step. Correct factually wrong records with a dated note.
- Keep every public surface in step: a change of state (status, tests run, findings, reviews, maintainers, calls for help or research topics, publications) updates every place that states it, in the same unit of work and in both languages where a surface has two. The inventory is `docs/public-surfaces.md`; a surface left stale on purpose (for example until a coordinated disclosure) is named, with the reason, in the journal of the change. Keeping in step includes closing what is done or obsolete (issues, calls, drafts) and, when a stated fact stops being true, adding its old wording to `docs/retired-claims.txt`, which CI checks against every current-state document.
- Work through local batches in the agreed order of `ROADMAP.md`, `docs/security-roadmap.md` and `docs/client-test-plan.md`; confirm the priorities with the maintainer. Do not open a pull request for every small step.
- For each kernel correction, run `make checkpatch`, the suite steps the correction touches in a dedicated VM (`MOOB_ONLY=...`) and a negative round on those steps showing that its regression test fails without the correction. If a change affects locking or object lifetime, also run those steps under KASAN/lockdep at once. Once per batch, before publication, run the full `make vm-test` and the full local KASAN/lockdep suite. Submit one pull request per batch and merge only with the maintainer's approval after the required checks pass.
- Own and close solidity, test, documentation and polish issues when their acceptance criteria are met; do not reserve them for a hypothetical contributor. Feature issues wait for a feature decision.
- Keep the module client-neutral: ai-memory is evidence from one client, not the design target. Do not change `layer/` or the UAPI for a need specific to that client; put client-specific behavior in an adapter.
- Before designing a mechanism, inspect relevant prior art, preferably in the exact Linux kernel sources used by the build. Cite algorithms, papers, libraries, tools and kernel sources used by a decision in `docs/references.md` in the same commit.
- Give every new ADR and roadmap item an estimated effort: S (hours), M (one to three days) or L (a week or more). Each journal records when its work started and ended, so that estimate and actual time can be compared when the work closes. Calendar time is not effort: say which one is recorded.

## Technical documentation

Use `npx ctx7@latest library <name> "<query>"` and then `npx ctx7@latest docs <id> "<query>"` for libraries, APIs, frameworks and CLIs. Maximum of three commands per question. Consult the code/headers of the exact kernel used in the build. Do not expose secrets in queries. On a quota error, report it and suggest `npx ctx7@latest login` or `CONTEXT7_API_KEY`.

## Scope and validation

- The project is a native filesystem mountable on Linux. Do not replace it with FUSE or a user-space API.
- Test modules in a dedicated VM. Creation tools must avoid overwriting existing files/devices by default.
- Format changes require a specification, tests of invalid inputs and an explicit compatibility decision.
- GitHub is a remote replica of the Git history; its creation and publication must follow the user's instructions in the session.
