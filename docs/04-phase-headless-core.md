# Phase 2 — Headless op runtime and metadata

**Goal:** the Werkkzeug operator runtime — documents, pages, operators, type checking, the
builder and the executive — builds and runs with **no GUI, no window system and no graphics
API**. And the `.ops` metadata is available as data.

**Why this is the structural work:** today every operator source file transitively includes
Altona's entire widget toolkit, because `wz4lib/doc.hpp` includes `gui/gui.hpp` at line 18.
Until that is severed, nothing headless is possible.

---

## Starting position

The coupling is far lighter than the include graph suggests. Measured:

| File | LOC | GUI-touching lines |
|---|---:|---:|
| `wz4lib/doc.hpp` | 1,100 | **18** |
| `wz4lib/doc.cpp` | 4,469 | 16 |
| `wz4lib/build.cpp` | 998 | 1 |
| `wz4lib/basic.cpp` | 980 | 7 |
| `wz4lib/script.cpp` | 4,355 | 0 |

Three declarations account for essentially all of it:

- `wPaintInfo` (`doc.hpp:113`) — the viewport painting context, passed to `type` blocks' `Show`
  and to `handles` blocks.
- `wGridFrameHelper : sGridFrameHelper` (`doc.hpp:304`) — the parameter panel builder, the
  argument to every generated `MakeGui`.
- `wCustomEditor` (`doc.hpp:326`) — full-window custom editors.

---

## Stages

### 2.1 — Split `doc.hpp`

Upstream patch, and the most significant one in the project.

Split into:

- **`doc_core.hpp`** — `wObject`, `wType`, `wClass`, `wClassInputInfo`, `wOp`, `wOpInputInfo`,
  `wStackOp`, `wTreeOp`, `wPage`, `wDocument`, `wCommand`, `wExecutive`, `wDocOptions`, the
  flag enumerations, and `sREGOPS`. No GUI include.
- **`doc_gui.hpp`** — `wPaintInfo`, `wGridFrameHelper`, `wCustomEditor`, `wHandle`,
  `wHandleSelectTag`, `wHitInfo`, `wEditOptions`. Includes `gui/gui.hpp`.
- **`doc.hpp`** — includes both, so upstream consumers are unaffected and the original
  editor would still build.

The awkward part is that `wType` and `wClass` hold *pointers to* GUI-facing functions
(`MakeGui`, `Handles`, `Show`, `Paint`). Approach: forward-declare `wPaintInfo` and
`wGridFrameHelper` in `doc_core.hpp` and keep the members as pointers to incomplete types.
Nothing in the headless path dereferences them.

`type` blocks' `Show` implementations *do* take `wPaintInfo&` by reference and use it. Those
live in the generated `.cpp` from the `.ops` file, so they are excluded by the headless
generation mode (stage 2.2) rather than by the header split.

Recorded as `wz4port/patches/03-doc-headless-split.md`.

**Gate:** `doc.hpp` still compiles for a hypothetical GUI consumer; `doc_core.hpp` compiles
with no GUI headers on the include path.

### 2.2 — Headless operator generation

The generated code for each operator includes `MakeGui`, the script `Bind*` functions and
wiki text, none of which are wanted headless and all of which reference GUI types.

**Preferred approach:** a new tool, `wz4port/tools/opsmeta`, that links `wz4ops`' parser
(`parse.cpp`, `doc.cpp` from `tools/wz4ops/`) and emits its own output — a headless `.cpp`
plus the metadata JSON. This keeps the entire metadata path inside `wz4port/` and leaves
`wz4ops` untouched.

**Fallback if the parser proves hard to reuse cleanly:** add a `--headless` flag to `wz4ops`
suppressing the `MakeGui`/`Bind*`/`Wiki` emission at `tools/wz4ops/output.cpp:630-1230`, and a
`--meta=` flag for the JSON. This costs an upstream patch, so it is second choice.

Decide this early in the stage by reading `tools/wz4ops/doc.hpp` and assessing how separable
the parse tree is from the emitter. Record the decision.

**Gate:** headless `.cpp` generated for `basic_ops.ops` and `wz3_bitmap_ops.ops`, compiling
against `doc_core.hpp` with no GUI.

### 2.3 — Metadata emission

Emit the JSON schema sketched in `02-target-model.md` §6, and **fix it exactly** in this
stage — the editor's entire UI is downstream of it.

Must carry, per class: name, output type, tab, column, shortcut, flags, input list with types
and optional/vararg/weak markers, parameter word and string counts, and the full parameter
list. Per parameter: kind, name, storage offset, ranges, steps, defaults, choice strings with
their bit layouts, tie groups, array descriptors, and conditional-visibility expressions.

Conditionals are the one genuinely awkward item. The original compiles `if(expr)` to C++
inside `MakeGui`. We emit the expression as a small tree and evaluate it at runtime. The
grammar to support is documented in `01-existing-model.md` §5.3: comparison and boolean
operators, integer literals, parameter symbols, `input[n]`, and `Flags.choicename`.

Per type: symbol, label, parent, colour, flags, gui modes, column headers.

**Gate:** metadata JSON for `basic` and `wz3_bitmap` reviewed by hand against the `.ops`
sources. Every parameter of every texture operator is represented and correct.

### 2.4 — `libwz4core`

Build `wz4lib/{doc,build,basic}.cpp` plus generated `basic_ops` as a static library, GUI-free.

Expected friction:

- `basic.cpp` has 7 GUI-touching lines to guard or stub.
- `doc.cpp` has 16, largely `sGui->Notify` calls in change propagation and `App->` references.
  Replace with a small notification hook interface that the editor implements and the CLI
  ignores.
- `Doc` is a global (`doc.hpp:965`) consulted from inside generation code. Keep it; making it
  non-global is a refactor with no benefit to us.
- `script.cpp` is GUI-free but large; include it only if `wExecutive` requires it. Determine
  and record.

**Gate — phase gate.** A test program links `libwz4core`, registers the `basic` operator
module, constructs a document programmatically, connects two operators by geometry alone, and
prints the derived input lists. Plus: op inventory printable from the metadata.

---

## Deliverables

- `wz4port/patches/03-doc-headless-split.md`
- `wz4port/tools/opsmeta/`
- `wz4port/libwz4core/`
- Metadata JSON for `basic` and `wz3_bitmap`, hand-reviewed
- The metadata schema, documented and frozen

## Open questions to resolve in this phase

1. Can `wz4ops`' parser be reused cleanly, or is patching `wz4ops` the pragmatic choice?
2. Does `wExecutive` require `script.cpp`, or can the scripting path be excluded?
3. How are `type` blocks' `Show`/`Paint` externs best excluded — generation mode, or a
   compile-time guard in the emitted code?

## Risks

| Risk | Assessment |
|---|---|
| The `doc.hpp` split is messier than 18 lines suggests | **The main risk of this phase.** Function-pointer members referencing GUI types are the crux; forward declaration should suffice, but it needs proving early |
| Metadata schema proves insufficient once the editor is built | Moderate. Mitigated by fixing the schema against the *complete* widget inventory in `01-existing-model.md` §5.2 rather than against what phase 4 happens to need |
| Conditional expressions harder to externalise than expected | Low-moderate. The grammar is small and fully documented |
