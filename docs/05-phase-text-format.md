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

#### Destructive-write problem found, and fixed — `patches/07`

`wOp::Serialize_` substituted `UnknownOp` for an unregistered class on read
(`doc.cpp:1854-1863`) and then, on write, emitted `classname = Class->Name` — **the
substituted name**. Any build that did not know every module silently rewrote every
unrecognised operator as `UnknownOp` and damaged the document for good. Since phase 3 exists
to *write* documents, and all six bundled files contain unregistered classes, every
conversion would have been destructive.

Fixed by having `wOp` retain `ForeignClass`/`ForeignType` and write those back.

**Two corrections to how this was first written up**, both worth recording because the first
version was wrong in the direction of alarm:

- *"Two `wDocName` fields and three lines"* understated it. The reader discards more than the
  name: parameter words (`:1881`), strings (`:1898`), link names (`:1913`) and array data
  (`:1963`) are all dropped, because `UnknownOp` declares no storage. Retaining those too
  would mean replaying a raw record per foreign operator, and that is deliberately not done —
  it is fidelity for render-graph, material and effect operators this port does not support.
- *"The §4.4 guarantee is not achievable"* misread this document. The 3.4 gate already says
  **"limited to the subgraphs whose classes we have registered."** The guarantee was never
  scoped to unregistered operators, so there was no gate to renegotiate.

The precise guarantee now established: **identity and geometry survive a load/save; parameter
content of unregistered operators does not.** A resaved document is not byte-identical, and the
difference is visible — reloading a resaved `example.wz4` reports 4,481 unknown-class reads
against the original's 4,687, the gap being default-operator instances that also are not
written.

`wz4gen identity` tests exactly this and is a `ctest` case for all six documents: 4,899
operators and 221 distinct class identities preserved on `example.wz4`, 207 of them classes
this build cannot load.

#### The reachability risk, now measurable

Retaining the name made the second risk below answerable, and it resolves favourably. Across
the six documents, **817 `GenBitmap` operators** become reachable once phase 4 registers
`wz3_bitmap`, and 1,424 `Wz4Mesh` for phase 6. `example.wz4` alone:

| Output type | Operators | Status |
|---|---:|---|
| `Wz4Mesh` | 1,424 | phase 6 |
| `Wz4Render` | 1,299 | out of scope |
| `GenBitmap` | 624 | **phase 4** |
| `ModShader` / `ModMtrl` / `ModShaderSampler` / `SimpleMtrl` | 779 | out of scope |
| `Wz4Particles`, `Sph*`, `Wz4BSP`, … | ~250 | out of scope |

### 3.1 — Reader

**Split in two.** The `.wz4t` reader cannot resolve a parameter without knowing its kind,
offset and space, and — measured — **`wClass` does not carry that**. It has `ParaWords` and
`ParaStrings`, a *budget*, and nothing else; parameter names, kinds and offsets only ever
existed inside the generated `MakeGui`, which `-headless` omits (`patches/05`).

So the metadata from stage 2.3 is not a convenience for this phase. It is the only description
of a parameter that survives headless, and it has to be readable at runtime before the text
reader can exist.

#### 3.1a — metadata at runtime — **done**

`wz4port/wz4t/` — a new target, not `libwz4core/wz4t_*.cpp` as the plan said: `wz4core` is
upstream sources compiled in place, and mixing our code into it would blur the isolation seam.

- `json.{hpp,cpp}` — a small general JSON reader. General rather than shaped to this one
  schema because the ImGui editor will read the same files and would otherwise duplicate it.
- `meta.{hpp,cpp}` — `wMetaLibrary`: classes, parameters, the three offset spaces, choice
  widgets with shift and mask, and the table widget.
- `wz4gen describe <class>` — the parameter description `wClass` cannot give. Brought forward
  from 3.4 because it is the natural demonstration.
- `wz4gen checkmeta` — validates the whole metadata **from the consumer side**.

**Gate — passed.** `checkmeta` over all 33 modules: **370 classes, 2,728 parameters, 3,282
choice values, 13 table widgets, 0 problems.**

The parameter count is the interesting number: **2,728 matches opsmeta's own count exactly**,
producer and consumer arrived at independently. It did not at first — the reader was silently
ignoring the `array` block and reported 2,667. A 61-parameter discrepancy between two counts
that should agree is precisely the kind of thing that goes unnoticed for a phase, so the
reader now loads array rows too. (The choice-value counts differ legitimately: opsmeta
cross-checks 2,729 of the 3,282 because it skips labels that are ambiguous within their
option string.)

Two things `checkmeta` checks that opsmeta cannot, because they are questions about what a
*reader* needs: that a `continues` parameter lands on a word its owner actually declares, and
that every choice value fits inside its widget's mask once shifted — a choice escaping its
mask would have the editor writing bits belonging to a neighbouring control.

#### Findings for 3.1b

- **One storage-bearing parameter in the whole corpus has no symbol.**
  `TextObject.TextExport`'s `fileout "Filename";` gives a label and no name — legal, since
  `parse.cpp:623-637` makes both optional. It cannot be addressed by name, so the reader needs
  a label fallback. Exactly one case, so this is bounded.
- **9 classes leave words unaccounted for.** `padding` reserves words and produces no
  parameter at all (`parse.cpp:462-475`), so gaps are expected and a shortfall is not an
  error. An *overrun* would be, and there are none.
- My first version of the check demanded a symbol from every parameter and reported four
  failures. All four were legitimate DSL usage — `label "Edit";`, `action "Invert" (1);` and
  the `fileout` above. **The check was wrong, not the metadata.**

#### 3.1b — the `.wz4t` reader itself

Parse into a `wDocument`, resolving class names through `wMetaLibrary` and validating
parameter names and value kinds against it. Errors must name the line and the parameter.

This is a format humans write, so diagnostics matter more than usual: an error must say which
line and which parameter, and a misspelled parameter name must not be silently ignored.

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

- `wz4port/wz4t/` — `json`, `meta` (done), plus `wz4t_read`, `wz4t_write`.
  The plan said `wz4port/libwz4core/wz4t_*.cpp`; it is a separate `wz4t` target instead,
  because `wz4core` is upstream sources compiled in place
- `wz4port/tools/wz4gen/`
- The `.wz4t` grammar, documented and frozen
- Round-trip test over the bundled documents

## Risks

| Risk | Assessment |
|---|---|
| Round-trip is lossy for parameters we do not model | **Confirmed, and accepted.** Identity and geometry now survive (`patches/07`); parameter content of unregistered operators does not, deliberately. The gate is scoped to registered subgraphs, so this is within its terms |
| ~~Bundled documents depend on out-of-scope classes so heavily that little texture content is reachable~~ | **Retired — measured, and it is fine.** 817 `GenBitmap` operators across the six documents become reachable when phase 4 registers `wz3_bitmap`; 624 in `example.wz4` alone. 1,424 `Wz4Mesh` for phase 6. The per-operator suite still carries the correctness load, but the documents are a real corpus, not a token one |
| Sugar expansion produces layouts that collide | Low. `CheckDest` validates on construction and the error names the offending operator |
