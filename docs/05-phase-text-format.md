# Phase 3 — Text graph format and CLI

**Goal:** a human-readable graph format and a headless command-line tool, so that from here
on every change can be validated automatically against reviewable test cases.

**Why before the texture library:** the test harness has to exist before the thing it tests,
or the first correctness signal arrives far too late.

---

## Why a new format

`.wz4` is a tagged binary format with UTF-16 strings and raw parameter words. It is a fine
document format and we keep reading and writing it — but nobody can review a test case in it,
and nobody can hand-write one.

There is no text format anywhere in the tree; this was checked. So we define one.

Its job is narrow: express a small graph clearly enough that a reviewer can look at the case,
look at the output image, and say whether they match.

---

## Format

Specified in `02-target-model.md` §4. Summary of what the parser must handle:

**Canonical form** — explicit geometry, what the writer emits:

```
wz4t 1

page "textures"

op GenBitmap.Perlin at 4,2 size 3x1 {
  Size    = 256, 256
  Freq    = 4, 4
  Octaves = 5
  Seed    = 1
}
```

**Sugar** — for hand-written cases:

- `stack at X,Y { op ... op ... }` places operators top to bottom at width 3, forming a chain.
- `row at X,Y { op ... op ... }` places operators side by side at the same Y.

Expanded on read; the writer always emits canonical form.

**Key properties:**

- Operators identified as `OutputType.ClassName`, matching `.wz4`'s own identification, so the
  mapping is exact.
- **Connections are never written.** They are derived from geometry by `ConnectStack`. This is
  deliberate: the format exercises the rule rather than working around it.
- Parameter names are the `.ops` names verbatim; values are validated against the metadata.
- `name = ` sets the store name; `hide` and `bypass` are bare flags.
- Values: integers, floats, comma-separated vectors, `#aarrggbb` colours, quoted strings, and
  identifiers for `flags`/`radio` choices with integer fallback.

---

## Stages

### 3.1 — Reader

Parse `.wz4t` into an in-memory `wDocument`, resolving class names and validating parameter
names and value kinds against the phase 2 metadata. Errors must name the line and the
parameter — this is a format humans write, so diagnostics matter more than usual.

**Gate:** a hand-written three-operator case parses and its derived connections are correct.

### 3.2 — Writer

Emit canonical `.wz4t` from a `wDocument`. Deterministic ordering (by page, then `PosY`, then
`PosX`) so output is diff-stable.

**Gate:** writer output re-parses to an identical document.

### 3.3 — `.wz4` interoperation

Wire up Altona's existing `.wz4` reader and writer — no new format work, just exposing what
`doc.cpp` already does headlessly.

**Gate:** all five bundled documents load and report operator counts:
`demos/example/example.wz4` (1 MB, the broadest operator sample),
`demos/the_cube/fr-062_party.wz4`, `fr-062_texts_kb.wz4`,
`demos/easterparty/teaser1.wz4`, `teaser2.wz4`.

Documents referencing out-of-scope operators (render graph, materials, effects) will contain
unknown classes. Altona loads these as `UnknownOp` rather than failing, which is exactly what
we want: the texture subgraphs remain intact and extractable.

### 3.4 — `wz4gen` CLI

| Command | Behaviour |
|---|---|
| `wz4gen list [doc]` | Registered operators, or the operators in a document, with types and store names |
| `wz4gen describe <class>` | Full parameter description from the metadata |
| `wz4gen convert <in> <out>` | `.wz4` ↔ `.wz4t`, direction from extensions |
| `wz4gen render <doc> --op=<name> --out=<file>` | Evaluate an operator and write its result (PNG in phase 4, OBJ in phase 6) |

`render` is stubbed here and completed in phase 4; the other three are complete.

**Gate — phase gate.** Round-trip: `.wz4` → `.wz4t` → `.wz4` preserves every operator, its
geometry, its parameters and its store name; and re-deriving connections from the result
reproduces the original input lists exactly. Run against all five bundled documents, limited
to the subgraphs whose classes we have registered.

---

## Deliverables

- `wz4port/libwz4core/wz4t_read.cpp`, `wz4t_write.cpp`
- `wz4port/tools/wz4gen/`
- The `.wz4t` grammar, documented and frozen
- Round-trip test over the bundled documents

## Risks

| Risk | Assessment |
|---|---|
| Round-trip is lossy for parameters we do not model | Moderate. Mitigated by carrying raw parameter words through unmodelled operators unchanged, so `UnknownOp` survives a round-trip even though we cannot render it |
| Bundled documents depend on out-of-scope classes so heavily that little texture content is reachable | Low-moderate. `example.wz4` is explicitly a broad operator sample. Worth checking early — if it disappoints, the per-operator suite in phase 4 carries the load instead |
| Sugar expansion produces layouts that collide | Low. `CheckDest` validates on construction and the error names the offending operator |
