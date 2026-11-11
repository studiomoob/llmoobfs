# Documentation index

Every document under `docs/`, with its language and one line on what it covers. Everything in the
repository is in English ([ADR-0010](decisions/0010-english-everywhere.md)); documents first written
in Portuguese were translated in place on 2026-10-12, and their originals remain in the git history.
The last two, `security-roadmap.md` and `benchmark-harness.md`, were translated by an outside
contributor (pull requests 46 and 47) and put in place on 2026-10-13.

Status words matter: a **proposal** describes intended behaviour, a **specification** is
accepted but may be only partly implemented, and only the journals and
[testing.md](testing.md) record what actually ran.

## Start here

| Document | Lang | What it covers |
| --- | --- | --- |
| [status.md](status.md) | EN | Current state in detail: the last tag, corrections merged since, open issues (journal 0197) |
| [status.pt-BR.md](status.pt-BR.md) | PT | Courtesy mirror of status.md |
| [matrix-and-limits.md](matrix-and-limits.md) | EN | One page: supported and exploratory kernels, lowers, which suites ran where, known limitations (stage 2, journal 0282) |
| [operations.md](operations.md) | EN | Install on a dedicated test host, mount by hand and at boot, users, upgrade, uninstall (roadmap C2) |
| [container-integration.md](container-integration.md) | EN | Docker and Podman: bind the agent's domain under its uid, queries inside the container, what is refused (roadmap C3) |
| [architecture-overview.md](architecture-overview.md) | EN | How the stacked layer is built, with a source citation for each claim |
| [when-not-to-use.md](when-not-to-use.md) | EN | Comparison with plain files, SQLite, FUSE and memory frameworks; when not to use MoobFS |
| [adoption-risks.md](adoption-risks.md) | EN | Proposal: fifteen adoption risks, the evidence for each and the fix, work or deployment rule that answers it |
| [faq.md](faq.md) | EN | Short sourced answers to common questions |
| [help-wanted.md](help-wanted.md) | EN | What would help most, by how much kernel knowledge it takes |
| [development.md](development.md) | EN | Environment setup, `make` targets, devcontainer, lint and checkpatch |
| [accessibility.md](accessibility.md) | EN | Accessibility audit of the project site and what CI enforces |
| [tools.md](tools.md) | EN | `moobfs-attr` and `moobfs-check`: synopsis, diagnostics and exit status |
| [testing.md](testing.md) | EN | What each test runs, its success markers, and what they do not prove |
| [research-test-infrastructure.md](research-test-infrastructure.md) | EN | How comparable projects get test hardware; resources open to MoobFS (journal 0123) |
| [public-surfaces.md](public-surfaces.md) | EN | Every place that states the project's state, and when each must be updated (journal 0122) |
| [labels.md](labels.md) | EN | Issue and pull request labels, including `good first issue` and `help wanted` |

## Specifications and formats

| Document | Lang | What it covers |
| --- | --- | --- |
| [spec-m2.md](spec-m2.md) | EN | M2: the memory layer stacked over a mature filesystem (pass-through, typed attributes, index, `QUERY`) |
| [spec-m3.md](spec-m3.md) | EN | M3: identity, isolation and provenance, change cursor, batch reads |
| [client-contract.md](client-contract.md) | EN | What any memory system must follow to use the layer: scope, permissions, attributes, identity, write sequence, queries |
| [spec-query-v2.md](spec-query-v2.md) | EN | Domain-scoped `QUERY2` (UAPI v2): domains, homogeneity rule, ioctl contract, phases Q2a–Q2e |
| [format-m1.md](format-m1.md) | EN | The read-only M1 image format, kept as a regression |
| [kernel-infrastructure.md](kernel-infrastructure.md) | EN | Mature kernel infrastructure the layer reuses, checked against Linux v6.12 |
| [native-mvp.md](native-mvp.md) | EN | The original full MVP proposal; its write interface was superseded by ADR-0007 |
| [spec-m2-full-volume-superseded.md](spec-m2-full-volume-superseded.md) | EN | Superseded M2 proposal for a full writable volume, kept for the record |

## Decisions and history

| Document | Lang | What it covers |
| --- | --- | --- |
| [decisions/](decisions/) | EN | Architecture decision records ADR-0001 to ADR-0021, with alternatives and reasons |
| [history/](history/README.md) | EN | Technical journal: one entry per piece of work, with evidence and limitations |

Key decisions for newcomers: [ADR-0007](decisions/0007-stacked-memory-layer.md) (a stacked layer
over a mature filesystem) and [ADR-0009](decisions/0009-query-role-and-user-space-index.md)
(the in-kernel index partitioned by permission domain); for security,
[ADR-0011](decisions/0011-threat-and-credential-model.md) (threat and credential model).

## Benchmarks (M4)

| Document | Lang | What it covers |
| --- | --- | --- |
| [client-test-plan.md](client-test-plan.md) | EN | Proposal: test with a real agent memory client (ai-memory), in three stages |
| [benchmark-plan.md](benchmark-plan.md) | EN | M4 plan: questions, baselines, workloads, what a result must show |
| [g6-evaluation-protocol.md](g6-evaluation-protocol.md) | EN | G6 protocol and G1 scope; G1 mounted gate and the G3 identity-map drill passed on 2026-10-24 |
| [benchmark-harness.md](benchmark-harness.md) | EN | Harness specification: dataset, SQLite baselines, latency records, report, where raw archives go |
| [bench-accelerated-guest.md](bench-accelerated-guest.md) | EN | How to run a campaign in an accelerated guest or a cloud VM |
| [../bench/results/](../bench/results/README.md) | EN | Pilot campaign results and the `bench-evidence` release for raw archives |

## Assessments and research

| Document | Lang | What it covers |
| --- | --- | --- |
| [value-assessment.md](value-assessment.md) | EN | Critical assessment of usefulness and value; the English summary is when-not-to-use.md |
| [research-originality.md](research-originality.md) | EN | Prior-art audit (SFS, BFS, YoloFS and others) and what may be claimed |
| [references.md](references.md) | EN | Verified list of the papers, projects and tools MoobFS builds on, includes, compares against or used to decide |
| [research-metadata-search.md](research-metadata-search.md) | EN | State of the art in file-metadata search, authorization and change feeds (for ADR-0009) |
| [research-algorithm-fit.md](research-algorithm-fit.md) | EN | Assessment: established query, cache and retrieval algorithms against current code and G1–G6; experiments proposed, none started |
| [research-metadata-search-addendum.md](research-metadata-search-addendum.md) | EN | Addendum checking the items left unverified in the previous document |
| [research-academic-survey.md](research-academic-survey.md) | EN | Systematic literature survey behind ADR-0009 |
| [research-client-query-patterns.md](research-client-query-patterns.md) | EN | Structured queries in six LLM-agent memory systems, read from their source |
| [research-ai-memory.md](research-ai-memory.md) | EN | Technical reading of ai-memory and how it relates to MoobFS |
| [ecosystem.md](ecosystem.md) | EN | Free software relevant to the project, checked before building anything |
| [project-review-2026-10-10.md](project-review-2026-10-10.md) | EN | Implementation and roadmap review, with static `QUERY` findings |

## Plans and process

| Document | Lang | What it covers |
| --- | --- | --- |
| [security-roadmap.md](security-roadmap.md) | EN | Security and robustness items S1–S11 (threat model, syzkaller, fuzzing, hardening) |
| [research-stacked-fs-kernel-rules.md](research-stacked-fs-kernel-rules.md) | EN | Stacked-FS rules from kernel v6.12 primary sources (credentials, locking, lifetime, ioctl/UAPI, xattrs, hostile lower, memory), each with source and MoobFS compliance status |
| [distribution-strategy.md](distribution-strategy.md) | EN | Proposed deployment model, DKMS packaging, kernel matrix and release stages |
| [community-plan.md](community-plan.md) | EN | Plan for external contributions (policies, templates, CI); largely executed |
| [seo-geo-plan.md](seo-geo-plan.md) | EN | Discoverability audit and plan for the README, site and docs |
| [dx-plan.md](dx-plan.md) | EN | Contributor-experience plan for GitHub, in three batches |
| [ai-workflow.md](ai-workflow.md) | EN | How AI-assisted development is governed (`AGENTS.md`, `CLAUDE.md`, Cursor rules) |

## Assets

| Path | What it is |
| --- | --- |
| [assets/social-preview.svg](assets/social-preview.svg), [assets/social-preview.png](assets/social-preview.png) | GitHub social preview (1280×640); render with `assets/render-social-preview.sh` |
