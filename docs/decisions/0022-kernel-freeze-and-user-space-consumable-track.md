# ADR-0022 — Kernel freeze stays; user-space work toward a consumable module may proceed

Date: 2026-10-25. Status: **accepted by the maintainer on 2026-10-25**, in the session
recorded in journal 0196. Estimated effort of the decision itself: S (hours). The work it
opens is estimated per item in the "Consumable path" section of `ROADMAP.md`.
This is ADR-0022 because ADR-0021 is the latest on `main` at `4f5148f`.

*Note, 2026-11-07:* [ADR-0033](0033-s16-freeze-exception.md) makes one narrow exception, for
S16's self-check in a test build only; the freeze otherwise stays.

## Context

Since 2026-10-14 the maintainer has held a stabilization rule: no new features until what
exists is solid; only fixes, tests, test infrastructure and outreach. The repository states it
in one line of `ROADMAP.md` ("New features wait for project review and the maintainer's
decision") and on the landing page; the full rule was never written as a decision. When it was
set, one line of work ran, and two batches of kernel fixes were still untested.

On 2026-10-25 three fronts run in parallel:

1. one agent matures the core (`layer/`, UAPI, S9, G2), in fixes and tests only;
2. one agent runs the evolution track G1–G6 in user space (protocol, adapter package in
   `bench/client/`, restore drill), accepted by the maintainer on 2026-10-24;
3. this decision, about what stands between the module and someone who wants to use it.

The second front already shows that user-space work proceeds while the kernel is frozen. What
is missing is the path from "it passes in our VM" to "a technical user installs it, mounts it,
and reaches it from an application without reading the source". `docs/adoption-risks.md`
(risks 5, 6, 10 and 11) and stage 2 of `docs/distribution-strategy.md` §8 already describe most
of that path; they are proposals, marked FUTURE, and nothing schedules them.

## Decision

1. **The kernel freeze stays.** `layer/`, `include/` (UAPI), `common/attr.c` and the disk
   conventions receive only fixes, tests and hardening until the maintainer lifts the freeze.
   That line belongs to the core front. ADR-0014 (projection) keeps waiting.
2. **User-space work toward a consumable module may proceed now**, as the "Consumable path"
   items C1–C7 in `ROADMAP.md`: first-contact documentation, a documented installation and
   mount, a validated container recipe, a `moobfs` command-line tool, a supported client
   library, preparation of DKMS and packages, and an optional agent adapter. Each item has
   acceptance criteria and its own journal, and needs the maintainer's go-ahead before it
   starts, as every roadmap item does.
3. **No item of this path changes the kernel or the UAPI.** If one finds that it needs a kernel
   change (for example `QUERY2` through a bind mount of a subdirectory, distribution strategy
   §2.5), it records the need and hands it to the core front; it does not work around the
   freeze.
4. **Installable packages are prepared, not published.** DKMS configuration, `.deb` packages
   and signing instructions may be written and tested in dedicated VMs. Publishing them in a
   package repository, or recommending installation on any host other than a dedicated test
   host or VM, stays behind the stage 2 gates of `docs/distribution-strategy.md` §8 and the
   "Public release" row of `ROADMAP.md`. A from-source guide for a dedicated test host (item
   C2) is not such a recommendation. The README warning ("Experimental. Not for production.")
   stays until those gates pass.
5. **Client neutrality applies.** The library and the adapters serve any client; ai-memory is
   test evidence, not the target. Client-specific behavior lives in an adapter.

## Alternatives

- **Keep the total freeze.** Rejected. It blocks work that cannot affect kernel stability, and
  it is no longer the rule in practice: G1–G6 already proceeds in user space.
- **Lift the freeze entirely.** Rejected. The module still has no outside human review, S9 fair
  sharing is open and syzkaller has an open coverage gap; new kernel behavior would add surface
  while that work runs.
- **Publish DKMS packages now, as a preview.** Rejected. An easy install of an experimental
  ring-0 module invites production use before the stage 2 gates pass, and a kernel defect would
  then reach hosts that the project cannot reach to fix.
- **Fold the path into G1–G6.** Rejected as the only home. G1 owns the client package and C5
  builds on it; installation, containers, packaging and first contact are not evaluation
  packages and would stretch G6's protocol. The two tracks cross-reference each other instead.

## Prior art

OpenZFS ships an out-of-tree filesystem module to Linux through DKMS (`zfs-dkms` in Debian),
with a stated range of supported kernels that its build refuses outside of; that is the shape
proposed for stage 2. DKMS itself (`dkms.conf`, `BUILD_EXCLUSIVE_KERNEL`) and the kernel's
module-signing documentation (`Documentation/admin-guide/module-signing.rst`, v6.12.112) define
the build and Secure Boot constraints. The citations are in `docs/references.md`.

## Consequences

The rule that agents and contributors apply becomes: a change to `layer/`, the UAPI or the
format is a fix or waits; a user-space change that serves an item C1–C7 may proceed once that
item is started. The public warning and the release gates do not change. Three fronts touch
`ROADMAP.md`, `CHANGELOG.md` and the history index, so their pull requests expect merge
conflicts there and resolve them by keeping every entry. Journal numbers are taken at merge
time when two fronts collide.
