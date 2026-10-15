# Project continuity

Read `README.md`, `docs/history/README.md`, the most recent session journal and the relevant ADRs before changing the project.

## Mandatory history

- Record requests and results in `docs/history/`, as factual technical summaries: goal, decisions, alternatives, brief justifications, changes, evidence, limitations and next steps.
- Record decisions, including those that arise from user questions, but not the questions themselves nor the clarifying answers. Errors found together with their fix, discarded hypotheses and failed experiments continue to be recorded. An investigation in progress has its own journal, marked as in progress and updated at each round.
- Document architectural decisions in `docs/decisions/` with sequential IDs. Preserve old decisions; a change must point to the decision it replaces.
- Preserve previous specifications. Clearly identify proposal, implementation and actually tested behavior.
- Update `CHANGELOG.md` for relevant changes. Keep reproducible commands and summaries of results; never present an unexecuted test as passed.
- Make small, readable local commits when a unit is complete. Do not rewrite shared history, do not force-push and do not delete records to hide attempts or limitations. Single, recorded exception: on 2026-10-10 the history was rewritten, with the user's explicit authorization, to remove questions recorded by mistake and personal data (journal 0018).
- Do not store credentials, tokens, environment dumps or unnecessary personal data. Histories record brief technical justifications, not the assistant's private internal reasoning.
- Outreach stays outside the repository: drafts of messages, calls for reviewers or contributors, conversations, where something was posted, and research about people. When an outside answer changes the project, the journal records its technical content and the public link to it, with the person described by role, not by name.
- Before finishing a task, make clear the current state, the tests, the limitations and the next step. Correct factually wrong records with a dated note.
- Give every new ADR and roadmap item an estimated effort: S (hours), M (one to three days) or L (a week or more). Each journal records when its work started and ended, so that estimate and actual time can be compared when the work closes. Calendar time is not effort: say which one is recorded.

## Technical documentation

Use `npx ctx7@latest library <name> "<query>"` and then `npx ctx7@latest docs <id> "<query>"` for libraries, APIs, frameworks and CLIs. Maximum of three commands per question. Consult the code/headers of the exact kernel used in the build. Do not expose secrets in queries. On a quota error, report it and suggest `npx ctx7@latest login` or `CONTEXT7_API_KEY`.

## Scope and validation

- The project is a native filesystem mountable on Linux. Do not replace it with FUSE or a user-space API.
- Test modules in a dedicated VM. Creation tools must avoid overwriting existing files/devices by default.
- Format changes require a specification, tests of invalid inputs and an explicit compatibility decision.
- GitHub is a remote replica of the Git history; its creation and publication must follow the user's instructions in the session.
