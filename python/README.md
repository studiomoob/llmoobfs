# moobfs — Python client library

Experimental, version 0.x (roadmap item C5, ADR-0023). LGPL-2.1-or-later; the rest of the
repository is GPL-2.0-only.

- **Typed attributes**: `set_attr`, `get_attrs`, `Value`, with the validation of the layer, so
  what this library writes is what `QUERY2` indexes.
- **Publishing**: `publish(directory, name, data, attrs)` follows the write sequence of the
  [client contract](../docs/client-contract.md) section 6 and the homogeneity rule, and refuses
  an existing name unless `replace=True`.
- **Queries**: `Domain(path).query(Term("kind", "==", "note"), order=Order("t", "time", True))`
  asks the kernel (`QUERY2`, every page followed) when the directory is on a mounted layer, and
  otherwise walks the directory with the kernel's matching and homogeneity rules. Both give the
  same answers (`python/tests/kernel_equal.py`). `Domain.batch` sends up to eight requests.
  `QueryRefused` means a resource limit; `on_refusal="scan"` answers it from the walk instead.
- **Detection**: `layer_mounted(path)`, `get_info(mount_root)`, `usable_info(info)`.
- **LangGraph**: `moobfs.langgraph.MoobStore`, a `BaseStore` on the same files as the G1
  adapter, with filters pushed into `QUERY2` where they can be and checked again after the read.
  Optional: `pip install "./python[langgraph]"`. G1's mounted gate passed on it (journal 0207).
- **MCP**: `python -m moobfs.mcp --root /srv/memory` serves the tools `info`, `query`, `read`,
  `write` and `delete` to an agent over stdio (ADR-0024). It runs as the agent's own user and
  refuses root, setuid and capabilities, offers no network transport, and keeps every path inside the root,
  opening it one component at a time without following links;
  `--read-only` leaves out `write` and `delete`. Optional: `pip install "./python[mcp]"`. An
  agent host starts it like any stdio MCP server, under the agent's uid.

```sh
python3 -m venv venv && venv/bin/pip install ./python       # from the repository root
make pylib-test                                             # the same, in build/, plus the tests
```

Python 3.9 or later, no dependency outside the standard library (LangGraph only for
`moobfs.langgraph`, the MCP SDK only for `moobfs.mcp`). Linux for anything that
touches files; the encoding and matching code runs anywhere. Not on PyPI.

Without the module the walk costs one `open`, one `fstat`, one `listxattr` and a `getxattr` per
typed attribute of every file in the domain, for every query; `QUERY2` is the fast path. Below
the domain it never resolves a path and never follows a symbolic link: every entry is opened
relative to its directory's descriptor with `O_NOFOLLOW` and read through it, so a member of a
shared domain who swaps a name for a link during the walk cannot make it report a file outside
the domain (journal 0231). Full-text
and vector search stay in the client (contract section 8).
