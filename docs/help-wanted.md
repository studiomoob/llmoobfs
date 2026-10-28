# How to help

MoobFS is an experimental kernel module with one maintainer and no independent human review of the whole kernel code yet (two
source reviews by AI assistants, journals 0111 and 0114; an external reviewer privately reported three kernel defects and wrote the fix of one,
F16, journals 0215, 0216 and 0218; an outside contributor reviewed the scripts, tools and Python library, pull requests
161 to 163). This
page says what would help most, by how much kernel knowledge it takes. Read the status block at
the top of the [README](../README.md) first: nothing here is ready for production, and the
project says where it loses as readily as where it wins.

Every item below is small and bounded on purpose. A finding of any size is recorded in the
project's journal with credit, if the person wants it.

## If you know Linux filesystems or the VFS

| What | Where |
| --- | --- |
| Review the credential model: name operations with the caller's credentials, data operations on an open file with the mounter's, checked against a table. Not the overlayfs model, which checks both and runs every real operation as the mounter ([journal 0099](history/0099-credential-model-corrected.md)). Partly answered: an external reviewer found that the write credentials kept the mounter's `CAP_FSETID` (F16, [journal 0218](history/0218-write-credentials-fsetid.md)) | [issue 16](https://github.com/studiomoob/llmoobfs/issues/16) |
| Review the locking of the attribute index against rename, unlink, eviction and memory reclaim | [issue 17](https://github.com/studiomoob/llmoobfs/issues/17) |
| Tell us what a stacked filesystem usually gets wrong that our tests have not caught. What was found so far is in [ADR-0011](decisions/0011-threat-and-credential-model.md) (findings F1 to F17) and journals [0082](history/0082-index-lock-and-reclaim.md), [0083](history/0083-index-inspection-fixes.md), [0112](history/0112-mount-leak-and-sql-omit.md), [0120](history/0120-review-medium-and-low-items.md), [0144](history/0144-independent-review.md), [0151](history/0151-review-d110d4e.md), [0156](history/0156-round-2-review-batch.md), [0215](history/0215-search-only-directories.md), [0216](history/0216-domain-record-generation.md) and [0218](history/0218-write-credentials-fsetid.md) | open an issue |
| Run the suite under AppArmor or Smack (SELinux enforcing was exercised once, journal [0104](history/0104-selinux-investigation.md)) | [testing.md](testing.md) |

## If you build memory for agents

| What | Where |
| --- | --- |
| Check whether your system fits: one file per memory, fields as typed attributes, scopes as directories | [client-contract.md](client-contract.md), section 1 |
| Tell us which of your structured queries do not fit four ANDed comparisons with one ordering | [research-client-query-patterns.md](research-client-query-patterns.md) |
| Try the existing LangGraph store adapter, whose answers were compared with its reference store; the first client was ai-memory, unmodified | [journal 0089](history/0089-second-client-langgraph-adapter.md) |

## If you want to try it or test it

| What | Where |
| --- | --- |
| Run the tests in a VM (Docker only): `make vm-test`; a subset: `MOOB_ONLY="btrfs reserve" make vm-test` | [testing.md](testing.md) |
| Try another kernel than 6.12 and report what breaks | [compatibility.md](compatibility.md) |
| Repeat a measurement on your hardware; the scripts and raw results are in `bench/` | [bench/results/](../bench/results/README.md) |

## If you do not touch kernels

Documentation review and reproducing the documented tool commands need no kernel knowledge.
Manual pages, exit codes, a compiler warning matrix, the accessibility audit and the SPDX check
were completed in [journal 0125](history/0125-seven-issues-closed.md). Report a concrete gap if
you find one; those completed tasks are not reserved for future contributors.

## How to report

- A defect that an ordinary user could use against others, or that crashes the kernel: privately,
  through [SECURITY.md](../SECURITY.md).
- Anything else: an issue. For a review, the `Review request` template keeps the answer short.
- Rules for changes: [CONTRIBUTING.md](../CONTRIBUTING.md). Kernel changes need the VM suite and the
  KASAN job green; commits are signed off (DCO).
