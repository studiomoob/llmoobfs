# Status

The current state of MoobFS in more detail than the README's warning. Until 2026-10-25 the text
below opened the README; it moved here with roadmap item C1 (journal 0197) so that a new reader
meets what the project is before its status history. The facts are unchanged. The README's
[Status](../README.md#status) section keeps the summary list, and
[Limitations](../README.md#limitations) the limits.

## Last tag and later corrections

**Experimental. Not for production.** One kernel series (6.12), tested in a VM only; no independent
human review of the whole kernel code yet (two independent source reviews by AI assistants, journals 0111 and 0114;
an external reviewer privately reported three kernel defects and wrote the fix of one, F16, journals 0215, 0216 and 0218;
an outside contributor reviewed the scripts, tools and Python library with an AI assistant, pull requests 161 to 163). The last
tag, `v0.4.1-experimental` (2026-10-20), is a source snapshot before this review batch.
The two findings of the [independent review](history/0144-independent-review.md)
have corrections on `main`; the review batch passed the full integrated VM suite
and local KASAN/UBSAN/lockdep. The required pull-request checks passed before merge.
`v0.4.1-experimental` still contains both defects. A later review (journal 0151) found a third,
a chmod during an index build that left the file in `QUERY2` answers; fixed in journal 0152.
See [Limitations](../README.md#limitations).

The later domain-rename and concurrent index-budget corrections were merged
through [PR 126](https://github.com/studiomoob/llmoobfs/pull/126) after its
11 reported checks passed; issues 110 and 111 were closed. A subsequent native
KVM run found another concurrent-budget refusal. Trying another eligible
domain when the oldest one's build mutex is busy, and the remaining
unprivileged allocation-fault test for T9, were integrated by
[PR 127](https://github.com/studiomoob/llmoobfs/pull/127) after all eleven
checks passed, including KASAN/lockdep ([journals 0178](history/0178-unprivileged-allocation-fault-queries.md)
and [0179](history/0179-kvm-overbudget-race-followup.md)). Issue 114 is
closed. The memcg accounting correction was merged through
[PR 129](https://github.com/studiomoob/llmoobfs/pull/129) after all eleven
reported checks passed, and issue 116 was closed. The experimental resource-cost
guard for issue 118 was merged by [PR 135](https://github.com/studiomoob/llmoobfs/pull/135)
after eleven green checks; the issue and S9 fair sharing remain open
([journal 0182](history/0182-query-build-resource-cost-investigation.md),
[roadmap](../ROADMAP.md)). No controlled before/after performance comparison
has measured the memcg accounting overhead ([journal 0180](history/0180-memcg-accounting-investigation.md)).
The bounded per-mount work choice was subsequently merged by
[PR 138](https://github.com/studiomoob/llmoobfs/pull/138) after twelve green
checks ([journal 0193](history/0193-merged-per-mount-build-work-policy.md));
issue 118 remains open for broader evidence.
*Update, 2026-10-27 ([journal 0210](history/0210-security-batch-1-domain-records-and-slow-lower.md)):*
issue 118 is closed. Over an ext4 lower slowed by `dm-delay` the first build of a 5,000-file
domain took 0.3 s with no delay and 9.8 s at 200 ms per I/O; at 400 ms it stopped at the deadline
with `ETIMEDOUT` and no partial answer, and the next query answered in full. G5's domains of about
25,000 real memories built at `index_build_work=65536`. Issue 113 (domain records kept until
unmount) is fixed in the same batch.
*Update, 2026-10-28 ([journal 0219](history/0219-security-batch-checks.md)):* a second security
batch fixes three private reports from an external reviewer, each with a test and a negative
round: `QUERY2` naming files under a search-only directory (F15 of ADR-0011,
[GHSA-95h9-j7c2-gcw6](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-95h9-j7c2-gcw6), [journal 0215](history/0215-search-only-directories.md)),
domain and file records answered for a reused inode number
([GHSA-756j-cgwq-gvwm](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-756j-cgwq-gvwm), [journal 0216](history/0216-domain-record-generation.md))
and a set-user-ID bit kept through a write (F16, [GHSA-6xf4-r246-92r6](https://github.com/studiomoob/llmoobfs/security/advisories/GHSA-6xf4-r246-92r6),
[journal 0218](history/0218-write-credentials-fsetid.md)). `v0.4.1-experimental` contains all
three.
