# Task: an executable regression test written from the specification only

A recent correction changed how the index keeps its bookkeeping when a file's permissions
change. Write the regression test for it from the behavior below and from the specification;
you do not get the implementation or the change.

Behavior the change must guarantee (spec sections 2, 3, 4.2 and 9):

1. A file can have hard links in two domains: A, which has been queried (built), and B,
   which no query has touched since mount (cold).
2. Changing that file's mode (chmod) through the B path, while B is cold, must leave B cold:
   no index for B is created as a side effect; GET_INFO must show no extra built domain, and
   no domain may be released unexpectedly.
3. Every later QUERY2 answer for A must match what the lower filesystem decides for that
   file under the homogeneity rule (spec section 3): while the mode makes the file
   inconsistent with A's homogeneous permissions it must not be returned as a member of A;
   after the alias is removed and the mode restored, it must be returned again. A query must
   never return an answer that is falsely complete.
4. GET_INFO counters (built domains, records, bytes, whatever the UAPI exposes) must return to
   their initial shape after cleanup.
5. No kernel warning may be emitted (the harness checks dmesg).

Vary what the specification permits: chmod via the A path as well as B; link created before
and after A is built; unlink of either alias; more than one file. Keep it deterministic.

Deliverable in out/: one C file `coldmove.c` that takes `LAYER LOWERDIR` as arguments, prints
`COLDMOVE_PASS` on success, plus `notes.md` listing every file you read and each assertion and
the spec section it comes from.
