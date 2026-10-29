# Public surfaces: where the project's state is stated

The same facts about MoobFS (what works, what was tested, what was found, who maintains it, what
help is wanted) are stated in many places. `AGENTS.md` asks that a change of state updates every
place that states it, in the same unit of work, so that no surface contradicts another. This file
is the inventory; walk it when closing a batch, a merge, a finding or a publication. Two checks
in CI enforce part of it: `scripts/check-retired-claims.sh` (no claim listed in
`docs/retired-claims.txt` may appear in a current-state document; of `docs/security-roadmap.md`
and `docs/adoption-risks.md`, whose dated history keeps old claims, only the section between
`<!-- current-state -->` and `<!-- /current-state -->` is read, journal 0234) and `scripts/site-faq.py --check`
(the FAQ page is generated from `docs/faq.md`). Walking it also means closing what is done or
obsolete: issues whose acceptance is met or whose premise no longer holds, finished calls.

Surfaces outside the repository are listed by kind only; their content (posts, drafts, accounts)
stays outside, as `AGENTS.md` asks for outreach. A surface left stale on purpose (for instance
until a coordinated disclosure) is named in the journal of the change, with the reason.

## In the repository

| Surface | States | Update when |
| --- | --- | --- |
| `README.md` and `README.pt-BR.md` (mirror, same facts), with `docs/status.md` and `docs/status.pt-BR.md` (the status detail moved out of the README opening, journal 0197) | status box, numbers, review status, maintainers, how to help, quick start, who it is for and ways to try it | any change of status, tests run, review, numbers, maintainers |
| `site/` (`index.html`, `guide.html`, `plan.html`, `faq.html`) | landing page, test guide, plan, FAQ | same as the README; new markers or steps for the guide |
| `scripts/site-journey.py` → `site/journey.html`, `status.html`, `research.html`, `how.html` and `site/pt/` (ADR-0015) | milestones, status against `adding-new-filesystems.rst`, research topics, how the project is made | a milestone, a status answer or a research topic changes; both languages |
| `CHANGELOG.md` | changes since the last tag | every relevant change |
| `ROADMAP.md`, `docs/security-roadmap.md` | plan, S-items and their progress | an item starts, progresses or closes |
| `docs/decisions/` (ADR status lines, ADR-0011 findings) | decisions and findings | a decision is taken or replaced; a finding is found or fixed |
| `docs/history/README.md` | journal index | every journal |
| `docs/help-wanted.md`, `CONTRIBUTING.md` | what help is wanted | a call opens or closes |
| `docs/testing.md`, `docs/faq.md`, `docs/when-not-to-use.md`, `docs/compatibility.md`, `docs/operations.md`, `docs/container-integration.md` | what tests prove, answers, limits, compatibility | a test step, a limit or a compatibility decision changes |
| `SECURITY.md`, `docs/security-operations.md`, `site/.well-known/security.txt` | reporting channel, supported versions, safe harbor, access, incident steps, third-party components | the channel, the supported versions, an owner, a secret, a key, a protection rule or a dependency changes; `security.txt` before its `Expires` date |
| `docs/references.md` | works used | something new is used (same commit) |

## On GitHub, outside the tree

| Surface | States | Update when |
| --- | --- | --- |
| Repository description and topics | one-line pitch | the pitch changes |
| Issues with review requests and calls (`help wanted`, `good first issue`) | reading lists, limits, findings, what is asked | a finding, a test or a limit they cite changes |
| Discussions ("Call for reviewers", "Welcome") | calls and status for newcomers | a call is answered, closed or changed |
| Releases and tags | source snapshots and known defects | a tag is made |
| Security advisories | private reports and their fixes | a fix is merged (then published) |

## Outside GitHub (content not kept in the repository)

| Surface | Update when |
| --- | --- |
| Project page and posts on professional networks (LinkedIn), fediverse account | a milestone worth announcing; the page's description when the pitch or status changes |
| Mailing-list threads and e-mails awaiting answers | a promise made there is kept or changes (for example a test promised on a list) |
| Working pages for the maintainer (plan maps, studies) and drafts of posts and talks | before publishing a draft, check it against the current state |
