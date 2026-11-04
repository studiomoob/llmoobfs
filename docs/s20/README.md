# S20 procedure records

Security roadmap S20: for a change to the permission rule or the index protocol, a second
model writes the regression test from `docs/spec-query-v2.md` without seeing the
implementation. Each directory keeps the receipt of one run of the procedure:

- `TASK.md` and `HARNESS.md`: the only instructions given to the test writer;
- `package.sha256`: SHA-256 of every file in the isolated package it was allowed to read
  (the specification, the UAPI headers and two example tests);
- `notes.md`: the writer's own list of files read, assertions with their specification
  sections, assumptions and revisions.

Records: [`coldmove/`](coldmove/) — correction 4f17fb4, journal 0265.

To run the procedure again: copy only the specification, `include/`, the harness notes and
unrelated example tests into an empty directory outside the repository; give the writer the
behavior the change must guarantee, stated in specification terms, never the diff; integrate
its test byte for byte; run it against the corrected code (must pass) and against the code
without the correction (must fail); send back only failure lines, never code, and record each
revision the writer makes.
