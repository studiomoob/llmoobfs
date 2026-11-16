# 0296 — Recoll decodes MoobFS typed attributes, behind a build option

Date: 2026-11-15. Work started about 07:40 Brasília time and ended about 10:40. Status:
**done.** Estimated effort S; the time recorded is calendar time, mostly waiting.

## Goal

Record an outside project's answer that changes what the documentation says about one client of
the typed attributes.

## What happened

- On 2026-10-19 the project wrote to Recoll's author in its first batch of contacts with related
  projects. The author answered that Recoll treats extended attributes as text and that decoding
  `user.moob.*` would be a little code in `internfile/extrameta.cpp`. A merge request with that
  code, tested before and after in a container, was opened the same day:
  <https://framagit.org/medoc92/recoll/-/merge_requests/57>.
- On 2026-11-15 the author (Recoll's maintainer) merged the code rewritten as a separate file,
  `src/internfile/moobfsxattrs.cpp`, compiled only with the meson option `-Dmoobfs=true`, which is
  off by default "until moobfs becomes more commonly used":
  <https://framagit.org/medoc92/recoll/-/commit/76ccda944936fb64d400b6a24ab04cbd9544e498>.
  The merge request was closed in favour of that commit, with a note of thanks.

## Check of the decoder

The decoder was read against `docs/client-contract.md` §4 as of `main` `1f9dd80`. It matches:

- type bytes 1 to 6 and little-endian payloads;
- strings up to 1 024 bytes;
- an `int64` printed in decimal;
- a `double` printed with the fewest digits (15 to 17) that read back as the same value, and a
  NaN not decoded;
- a `bool` decoded only when the byte is 0 or 1;
- `time` printed as whole seconds since the epoch, rounded down for negative values, as Recoll
  prints `dmtime`;
- `raw` left out of the index.

A value that does not match the encoding is kept as stored and logged.

## Not done

- No Recoll build with `-Dmoobfs=true` was run against the layer. The interoperability test
  (journal 0140, `vm/interop-guest.sh`, `.github/workflows/interop.yml`) uses the Ubuntu 24.04
  package, which is built without it; its observations (the stored bytes) still hold for that
  package.
- No Recoll release with the option is known on this date.

## Changes

- `docs/client-contract.md` §4: a dated note that Recoll built with `-Dmoobfs=true` decodes the
  values.
- `docs/references.md` §67.

## Next step

None for the project. If the encoding of §4 changes, tell Recoll's maintainer before it lands
(said in the closing note).
