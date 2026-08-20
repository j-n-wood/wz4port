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

**Order changed:** 3.3 (`.wz4` interoperation) was done **first**, before the reader.

Phase 2 left `.wz4` loading untested, and stages 2.1 and 2.4 both had to work to keep
`wEditOptions` and `wDocOptions` byte-compatible — the theme record and the colour palette
were extracted out of the GUI library specifically so the document format would survive. That
made loading a real document the cheapest, highest-information check available, and doing it
before building a text format on top de-risks everything else in the phase.

It was the right call: it surfaced a blocker for the 3.4 round-trip gate (below) that would
otherwise have appeared at the very end of the phase.

### 3.3 — `.wz4` interoperation — **done, first**

Exposed through `wz4gen list`, which loads a document and reports pages, live operator count,
a class tally, store names and error counts.

**Gate — passed.** All bundled documents load. The plan named five; there are six.

| Document | Pages | Operators | Unregistered |
|---|---:|---:|---:|
| `demos/example/example.wz4` | 79 | 4,899 | 4,481 |
| `demos/the_cube/fr-062_party.wz4` | 16 | 883 | 761 |
| `demos/the_cube/fr-062_texts_kb.wz4` | 1 | 37 | 9 |
| `demos/easterparty/teaser1.wz4` | 3 | 239 | 180 |
| `demos/easterparty/teaser2.wz4` | 3 | 281 | 202 |
| `wz4/screens4/test.wz4` | 9 | 751 | 571 |

7,090 operators across 111 pages. `screens4/test.wz4` was added to the set because it is the
only document that exercises `Call`/`Input` subroutines heavily.

Each is a `ctest` case now (`load_*`), and `wz4gen` sets the error code both on a failed load
**and** on a load that yields zero operators — `sLoadObject` can report success on a file it
did not understand, and an empty read must not pass silently.

**Errors are classified by cause, not by message.** Two different messages have the same root
cause, and reporting them raw would look alarming on a document that is fine:

- `UnknownOp` is declared with **zero inputs** (`basic_ops.ops:239`), so an operator it
  replaced still sits in geometry that feeds it: *"too many inputs"*, 637 times on
  `example.wz4`.
- `UnknownOp`'s output type is `AnyType`, which does not satisfy a typed input, so a *real*
  consumer above it fails: *"input has wrong type"* — every `MakeTexture(BitmapBase)` fed by an
  unregistered generator.

An operator counts as placeholder fallout if it, or any of its inputs, is an `UnknownOp`. What
remains across all six documents: **15 connection errors and 1 unexplained error**, and all of
them are properties of the documents themselves — dangling `Load` link names, and six duplicate
store names in `screens4/test.wz4`. Nothing attributable to the port.

#### Blocker found for the 3.4 round-trip gate

`wOp::Serialize_` substitutes `UnknownOp` for an unregistered class on read
(`doc.cpp:1854-1863`) and then, on write, emits `classname = Class->Name`
(`doc.cpp:1867`) — **the substituted name**. So a `.wz4` → `.wz4t` → `.wz4` round trip would
write back `UnknownOp` and destroy the original operator's identity permanently.

The round-trip guarantee in `02-target-model.md` §4.4 is therefore **not achievable as the
code stands**, for any document containing an unregistered class — which is all six.

The risk table below already anticipated needing to "carry raw parameter words through
unmodelled operators unchanged"; this is the concrete mechanism. Two options for 3.4:

1. **Scope the gate** to documents whose classes are all registered — but there are none, and
   there will not be until phase 4 at the earliest.
2. **Have `wOp` retain the original class and type name** when it substitutes, and write those
   back instead of `UnknownOp`'s. Two `wDocName` fields and three lines in `Serialize_`.

Option 2 is preferred and is the right size. Decide and record at the start of 3.4; it must
land before the writer can claim a round trip.

### 3.1 — Reader

Parse `.wz4t` into an in-memory `wDocument`, resolving class names and validating parameter
names and value kinds against the phase 2 metadata. Errors must name the line and the
parameter — this is a format humans write, so diagnostics matter more than usual.

**Gate:** a hand-written three-operator case parses and its derived connections are correct.

### 3.2 — Writer

Emit canonical `.wz4t` from a `wDocument`. Deterministic ordering (by page, then `PosY`, then
`PosX`) so output is diff-stable.

**Gate:** writer output re-parses to an identical document.

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
| ~~Round-trip is lossy for parameters we do not model~~ | **Confirmed and worse than stated**, in 3.3. It is not just parameters: the *class identity* is lost, because `Serialize_` writes back the substituted `UnknownOp` name. Must be fixed before 3.4's gate — see the two options above |
| Bundled documents depend on out-of-scope classes so heavily that little texture content is reachable | **Confirmed as a real concern.** With only `basic` registered, 91% of `example.wz4` is `UnknownOp`. How much becomes reachable once `wz3_bitmap` is registered is not yet known — it cannot be measured until phase 4 links the generator implementation, and `wOp` does not retain the original class name, so the unknown classes cannot even be counted by name today. Assume the per-operator suite in phase 4 carries the correctness load, and treat document round-tripping as a bonus |
| Sugar expansion produces layouts that collide | Low. `CheckDest` validates on construction and the error names the offending operator |
