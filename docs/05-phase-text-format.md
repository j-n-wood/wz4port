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

#### 3.1b — the `.wz4t` reader itself — **done**

`wz4port/wz4t/wz4t_read.cpp`. Resolves classes through `wMetaLibrary` and `Doc->FindClass`,
runs each operator's `SetDefaults` before applying settings — so a file only states what it
changes — and writes values into the right one of the three offset spaces.

**Gate — passed.** `wz4t_read` (a `ctest` case): 19 checks, 0 failures, over
`tests/cases/three_ops.wz4t`. It checks the derived graph, not just that parsing succeeded —
a 6-wide consumer under two 3-wide producers takes both in left-to-right order, a row's gap
connects nothing, `stack{}` chains, `row{}` places side by side — and it checks that the
reader **refuses** a missing header, an unknown version, an unknown class, a misspelled
parameter, too many values, and an op with no position. A parser that accepts anything is
worse than none here: a hand-written case would quietly test the defaults.

#### Two grammar decisions the spec left open

**Comments are `//`, not `#`.** §4.2's example uses `#` for a trailing comment on one line
and for a colour literal (`#ff8040c0`) two lines later; both cannot hold. sScanner offers `#`
comments as `sSF_NUMBERCOMMENT`, so the conflict is real rather than theoretical. The colour
syntax is in §4.2's normative bullet list and comments are not mentioned at all, so `#`
belongs to colours and comments are C-style.

**`#aarrggbb` survives, but only because a hex run can be reassembled.** A colour is not one
token. Measured, rather than assumed:

| Written | Tokenises as |
|---|---|
| `#ff8040c0` | `#`, NAME `ff8040c0` |
| `#08ff0000` | `#`, INT `08`, NAME `ff0000` |
| `#00112233` | `#`, INT `00112233` |
| `#1e500000` | `#`, **FLOAT** `1e500000` |

All three token kinds expose their exact source text (`Name`, `ValueString`), so concatenating
until eight hex digits are collected reconstructs the literal in every case. Had that not held,
the fallback was a quoted `"#ff8040c0"`; it was not needed. `3x1` splits into INT and NAME
`x1` and is handled the same way, with `3 x 1` also accepted.

#### A round-trip hazard found and closed early

**A freshly constructed `wDocument` already owns one empty page**: the constructor calls
`DefaultDoc()` (`doc.cpp:2514`, `:2609`), which appends a default-named page and connects.
Reading a two-page file therefore produced *three* pages.

Left alone, that would have shown up in 3.2 as a round trip gaining a stray empty page on
every pass. The reader now takes the default page over for the first `page` in the file rather
than appending, provided it is still empty and untouched.

### 3.2 — Writer — **done**

`wz4port/wz4t/wz4t_write.cpp`, plus `wz4gen convert` (brought forward from 3.4, since a writer
with no way to invoke it is not demonstrable). Canonical output: explicit `at` and `size` on
every operator, no sugar, pages in document order, operators sorted by `PosY` then `PosX`.

**Only non-default parameters are written.** Safe because the reader runs the operator's
`SetDefaults` before applying settings, and because `SetDefaults` (emitted by `wz4ops`) and the
metadata defaults (emitted by `opsmeta`) come from the same parse tree — they agree by
construction. It is also what keeps a per-operator case short enough to read, which §4.1 asks
for.

**Gate — passed.** `wz4t_round` over two cases: read → write → read, then compare **every
parameter word, string and link**, not just operator counts. Plus: writing twice must give
byte-identical text.

#### Flags decoding, and where it gives up

A packed integer is rendered as choice labels (`Format = DXT5`), which needs care:

- **`continue flags` declares more widgets on a word an earlier parameter owns**, so decoding
  gathers every widget touching that offset. Looking only at the owner's widgets would silently
  drop the continued bits on write.
- A label that is **ambiguous** across widgets cannot be written — the reader resolves the
  first match and would pick the wrong widget. `-` used as a blank entry in several widgets is
  the common case.
- A label that is **not a bare identifier** (`16 Samples`, `San Andreas Gap`) is not written
  either.
- Any of those, or a value with no matching choice, falls back to the raw integer for the whole
  parameter. Correctness first; the round trip is identical either way.

#### The false pass, and what it cost to find

The round trip passed on ASCII and then passed on a case with `café °C — ΔΣ 中文` in it — while
**mangling the text**. `café` was being stored as `cafÃ©`.

`sLoadText` decodes UTF-8 only when it finds a BOM (`system.cpp:1080`) and otherwise takes each
byte as one character. A hand-written `.wz4t` — the entire point of the format — has no BOM, so
it read as Latin-1. The writer then re-encoded those characters as UTF-8, which means **the
corruption is idempotent after the first pass**: read/write/read is perfectly stable, and every
comparison in the gate passes.

Two fixes, and the second matters more than the first:

1. The reader decodes UTF-8 itself, BOM optional, via `sLoadFile` +
   `sCopyStringFromUTF8`. The writer emits `sSaveTextUTF8` rather than `sSaveTextAnsi`, which
   truncated every character to a byte — and which also produced a file `grep` reported as
   **binary**, defeating the format's stated purpose.
2. The gate now asserts an **actual character value** against a compiler-encoded literal, not
   just stability. A round trip being stable is not the same as it being correct, and only the
   second kind of check can tell the difference.

### 3.4 — `wz4gen` CLI

`list`, `describe`, `checkmeta`, `identity` and `convert` already exist — they were built where
they were needed to demonstrate an earlier stage rather than held back to here. What remains
for 3.4 is `render` (stubbed) and the phase gate.

| Command | Behaviour |
|---|---|
| `wz4gen list [doc]` | Registered operators, or the operators in a document, with types and store names |
| `wz4gen describe <class>` | Full parameter description from the metadata |
| `wz4gen convert <in> <out>` | `.wz4` ↔ `.wz4t`, direction from extensions |
| `wz4gen render <doc> --op=<name> --out=<file>` | Evaluate an operator and write its result (PNG in phase 4, OBJ in phase 6) |

`render` is stubbed here and completed in phase 4; the other three are complete. The stub is
honest rather than absent: it loads the graph and resolves the named store, then says what is
missing (a texture library to evaluate and an image writer) and exits non-zero.

### 3.4 — **done**, and the phase gate passes

**Gate — passed** over all six bundled documents (`docround_*` in `ctest`). Per document it
checks operator count, class identity, page and geometry, store name / hide / bypass,
**parameters for every registered operator**, and that the re-derived input lists match the
originals exactly.

Scope is as the plan stated — "limited to the subgraphs whose classes we have registered".
Parameters are compared only for operators this build can load, because Altona discarded the
rest when it first read the `.wz4` (`UnknownOp` declares no storage; `patches/07`). Claiming
otherwise would be false. Their *identity*, *geometry* and *participation in connections* are
compared, which is what keeps the graph check meaningful: on `example.wz4` that is 4,899
operators, only 418 of them registered, and every connection re-derived correctly.

#### The reader needed a lenient mode

`.wz4t` → `.wz4` could not work at all until the reader could accept an unregistered class,
because the writer emits `GenBitmap.Text` for operators this build cannot load and the reader
rejected them by design.

So `wWZ4T_ALLOWUNKNOWN` substitutes the `UnknownOp` placeholder and remembers the original
name — mirroring exactly what Altona's own `.wz4` reader does. **It is off by default**, and
that default is the point: in a hand-written case an unknown class is a typo, and accepting it
would let the case pass while testing nothing. Only `convert` and the document round-trip
switch it on.

#### A 1-ULP float drift, found by the gate

`example.wz4` failed the first run, on `ScreenshotProxy.Screenshot`:

```
word 4: 0x3f9e9828 -> 0x3f9e9827
word 5: 0x3f07449f -> 0x3f07449e
word 6: 0xbf80754a -> 0xbf807549
```

One unit in the last place, on `Position`, `Target` and `Zoom`. The writer was exact — it
already prints at the shortest precision that round-trips — so the loss was in
`sScanner::ScanFloat()`, which is not correctly rounded. **A18 again, on the parsing side.**

Fixed by parsing the token's exact source text (`ValueString`) through libc, next to the
formatting helper that already existed for the same reason.

Worth noting what this nearly cost: the text diff of two conversions looks identical, and
5 of 6 documents passed. A per-operator golden image in phase 4 would have drifted for reasons
nobody would have thought to look for in the *scanner*.

---

## Deliverables — all done

- `wz4port/wz4t/` — `json`, `meta`, `wz4t_read`, `wz4t_write`.
  The plan said `wz4port/libwz4core/wz4t_*.cpp`; it is a separate `wz4t` target instead,
  because `wz4core` is upstream sources compiled in place
- `wz4port/tools/wz4gen/` — `list`, `describe`, `checkmeta`, `convert`, `identity`,
  and `render` stubbed for phase 4
- The `.wz4t` grammar, documented and frozen at version 1, with the two ambiguities in
  `02-target-model.md` §4.2 resolved
- Round-trip tests: two hand-written cases and all six bundled documents

## Risks

| Risk | Assessment |
|---|---|
| Round-trip is lossy for parameters we do not model | **Confirmed, and accepted.** Identity and geometry now survive (`patches/07`); parameter content of unregistered operators does not, deliberately. The gate is scoped to registered subgraphs, so this is within its terms |
| ~~Bundled documents depend on out-of-scope classes so heavily that little texture content is reachable~~ | **Retired — measured, and it is fine.** 817 `GenBitmap` operators across the six documents become reachable when phase 4 registers `wz3_bitmap`; 624 in `example.wz4` alone. 1,424 `Wz4Mesh` for phase 6. The per-operator suite still carries the correctness load, but the documents are a real corpus, not a token one |
| Sugar expansion produces layouts that collide | Low. `CheckDest` validates on construction and the error names the offending operator |
