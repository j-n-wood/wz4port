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

Three declarations account for essentially all of it (line numbers are from the pre-split
`doc.hpp`; all three now live in `doc_gui.hpp`):

- `wPaintInfo` (`doc.hpp:113`) — the viewport painting context, passed to `type` blocks' `Show`
  and to `handles` blocks.
- `wGridFrameHelper : sGridFrameHelper` (`doc.hpp:304`) — the parameter panel builder, the
  argument to every generated `MakeGui`.
- `wCustomEditor` (`doc.hpp:326`) — full-window custom editors.

---

## Stages

### 2.1 — Split `doc.hpp` — **done**

Upstream patch, and the most significant one in the project. Recorded as
`wz4port/patches/04-doc-headless-split.md` (03 was already taken by the Latin-1
conversion).

Split into:

- **`doc_core.hpp`** — the whole document model: `wObject`, `wType`, `wClass`,
  `wClassInputInfo`, `wOp`, `wOpInputInfo`, `wStackOp`, `wTreeOp`, `wPage`, `wDocument`,
  `wCommand`, `wExecutive`, `wDocOptions`, `wEditOptions`, `wHandleSelectTag`, `wHitInfo`,
  the flag enumerations, and `sREGOPS`. No GUI include.
- **`doc_gui.hpp`** — `wHandle`, `wPaintInfo`, `wGridFrameHelper`, `wCustomEditor`.
  Includes `gui/gui.hpp`, `gui/listwindow.hpp` and `util/shaders.hpp`.
- **`doc.hpp`** — includes both, so upstream consumers are unaffected and the original
  editor would still build.

The rule applied when deciding where a declaration goes: **`doc_gui.hpp` holds exactly what
cannot compile without the GUI or shader headers; everything else stays in core.** Measured,
that is only four declarations plus two `sListWindowTreeInfo<>` members.

The awkward part — `wType` and `wClass` holding *pointers to* GUI-facing functions
(`MakeGui`, `Handles`, `Show`, `Paint`) — cost nothing. `doc_core.hpp` forward-declares
`wPaintInfo`, `wGridFrameHelper`, `wCustomEditor` and `sWindowDrag`; references and pointers
to incomplete types are legal in declarations and nothing headless dereferences them.

`type` blocks' `Show` implementations *do* take `wPaintInfo&` by reference and use it. Those
live in the generated `.cpp` from the `.ops` file, so they are excluded by the headless
generation mode (stage 2.2) rather than by the header split.

**Two divergences from the original plan**, both forced by measurement:

1. `wEditOptions` and `wHandleSelectTag` are `wDocument` members *by value*, so they cannot
   live on the GUI side. `wHitInfo` is plain data with no GUI dependency. All three are in
   core.
2. `wEditOptions` holds an `sGuiTheme`, and `wTreeOp`/`wPage` hold `sListWindowTreeInfo<>`.
   Both are plain serializable data that merely happen to live in GUI headers, and both are
   held by value. They were extracted verbatim into two new dependency-free upstream headers,
   `gui/theme.hpp` and `gui/treeinfo.hpp`, which `gui/manager.hpp` and `gui/listwindow.hpp`
   now include. Same patch.

The alternative — shadowing `gui/listwindow.hpp` and `gui/manager.hpp` from
`wz4port/compat/include/` the way `altona_config.hpp` is shadowed — was rejected: silently
replacing two real upstream headers for *every* translation unit is a worse landmine than a
verbatim extraction that leaves both consumers unaffected.

**Gate — passed.**

- A TU including only `doc_core.hpp` compiles clean, and `clang++ -H` shows it pulls
  `base/{types,types2,serialize,system,math,graphics}.hpp`, `gui/{treeinfo,theme}.hpp` and
  `compat/altona_config.hpp` — nothing else.
- A TU including `doc.hpp` (both halves, so `gui/gui.hpp` and `gui/listwindow.hpp` too)
  parses clean on macOS once `util/shaders.hpp` is stubbed. Before the split it died on that
  missing generated header, so the GUI consumer is no worse off and demonstrably intact.
- The two halves were diffed line-by-line against `git show HEAD:.../doc.hpp`: no declaration
  added, dropped or altered.

And the invariant is now enforced by the build rather than by discipline. The
`headless_core_gate` target (`wz4port/tests/headless_core.cpp`) compiles a translation unit
that includes only `doc_core.hpp`, with `wz4port/tests/gui_poison.h` force-included ahead of
it. That header `#pragma GCC poison`s `sWindow`, `sGui_` and `sSimpleMaterial` — three tokens
that between them cover every header in `gui/` (all of them reach `gui/window.hpp`) and the
generated `util/shaders.hpp`. Re-adding a GUI dependency to `doc_core.hpp`, `gui/theme.hpp`
or `gui/treeinfo.hpp` now fails the build and names the offending file.

The tripwire paid for itself on its first run by catching a wrong assumption of mine:
`sMaterialEnv` is declared in `base/graphics.hpp`, not in `util/shaders.hpp`.

It is an `OBJECT` library, not an executable, because `wz4lib/doc.cpp` is not built until
stage 2.4. Promote it to a linked test then.

### 2.2 — Headless operator generation — **done**

Recorded as `wz4port/patches/05-wz4ops-headless.md`.

The generated code for each operator includes `MakeGui`, the script `Bind*` functions and
wiki text, none of which are wanted headless and all of which reference GUI types. It turned
out to be most of the file:

| | default | `-headless` |
|---|---:|---:|
| `basic_ops.cpp` | 6,230 lines | 1,850 |
| `wz3_bitmap_ops.cpp` | 8,884 lines | 2,166 |

#### Decision on open question 1 — **both, split by responsibility**

The question was whether `wz4ops`' parser can be reused by a separate tool (preferred, to
avoid an upstream patch) or whether patching `wz4ops` is the pragmatic choice. Measured:

- The parse tree is **completely separable**. `parse.cpp` and `doc.cpp` contain zero
  references into `output.cpp` or `wikitext.cpp`, and every node class (`Op`, `Type`,
  `Parameter`, `Input`, `ExprNode`, `Tie`, `Struct`, `External`, `CodeBlock`) is public in
  `tools/wz4ops/doc.hpp`. The only obstacle is that `Document::Types` and `Document::Ops` are
  **private** with no accessor — a two-line visibility patch.
- So "no upstream patch" is not on the table either way, which removes the reason the
  separate tool was preferred.
- Reimplementing the *non*-GUI half of `output.cpp` in a separate emitter would duplicate
  roughly 600 lines of offset-sensitive emission — parameter packing, arrays, ties, string
  offsets, helper structs. Its failure mode is silently misaligned parameter data, which is
  exactly the class of bug this project has no oracle to catch.
- Meanwhile the GUI-emitting parts of `output.cpp` are few and contiguous.

So:

- **The headless `.cpp` comes from `wz4ops -headless`.** Same code path as the GUI output for
  everything that determines layout, so the two cannot diverge.
- **The metadata JSON comes from a new `wz4port/tools/opsmeta`**, linking only `parse.cpp`
  and `doc.cpp`. The JSON is *our* format and the editor's contract; keeping it in our tree
  means stage 2.3 can evolve the schema without touching `altona_wz4/` again.

#### What `-headless` suppresses

| Emitted thing | `output.cpp` | Why it goes |
|---|---|---|
| `#include "gui/gui.hpp"`, `gui/textwindow.hpp`, `wz4lib/script.hpp` in the `.cpp` | `:44-46` | GUI and script |
| `#include "wz4lib/doc.hpp"` in the `.hpp` → `doc_core.hpp` | `:34` | the whole point |
| `%sGui%s` — the `MakeGui` body | `:748-879`, `OutputPara`, `OutputTies` | `wGridFrameHelper` |
| `%sHnd%s` — handles | `:599-611` | `wPaintInfo` |
| `%sDrag%s` — special drag | `:728-736` | `sWindowDrag`, `wPaintInfo` |
| `%sCed%s` — custom editor | `:703-712` | `wCustomEditor` |
| `%sBind%s`, `%sBind2%s`, `%sBind3%s` | `:962-1094` | `ScriptContext` |
| `%sWiki%s` | `:1097-1113` | editor-only, and it is bulky |
| the matching `cl->` assignments | `:1291`, `:1293-1296`, `:1298`, `:1304`, `:1331` | follows the above |

Everything that decides *layout* — `OutputParaStruct`, the `Cmd` bodies, `SetDefaults`,
`SetDefaultsArray`, `Actions`, `GetDescription`, `OutputStruct`, `OutputMain`'s class
registration — is emitted unchanged.

#### Decision on open question 3 — a signature rule, not a blanket skip

`type` blocks' `externals` are hand-written C++ pasted into the generated file, so some of
them use `wPaintInfo` and cannot compile headless. The obvious rule — drop them all — is
**wrong**, and measuring the two `.ops` files shows why:

- `wz3_bitmap_ops.ops:37` declares `extern void Init()`, a `wType` virtual the headless build
  needs (it calls `xInitPerlin()`).
- `basic_ops.ops:493`/`:615` declare `extern void Hit(wObject *,const sRay &,wHitInfo &)` —
  every type in that signature is in `doc_core.hpp`.

So `-headless` skips an `extern` only when its return type or parameter list names a type
declared in `doc_gui.hpp`: `wPaintInfo`, `wGridFrameHelper`, `wCustomEditor`, `wHandle`. On
these two files that drops 12 of the 15 `type` externs, keeping `GenBitmap::Init`,
`MeshBase::Hit` and `Scene::Hit`.

Every skip is printed, so the suppression is never silent.

#### The residual: GUI includes inside user `code` blocks

A `.ops` file's global `code` and `header` blocks are verbatim C++, and one of them is a
problem: `basic_ops.ops:15` has `#include "wz4lib/gui.hpp"` — the *editor's* window classes,
which are out of scope for this port and which we never build. `wz3_bitmap_ops.ops` is clean.

Handled by making the block conditional on a macro the generator controls, so the non-headless
output is byte-identical:

```
code
{
#include "base/graphics.hpp"
#ifndef WZ4_HEADLESS
#include "wz4lib/gui.hpp"
#endif
}
```

with `-headless` emitting `#define WZ4_HEADLESS 1` at the top of the generated `.cpp`.

Building it turned up three more of these, not one:

| Where | What | Why |
|---|---|---|
| `basic_ops.ops:15` | `#include "wz4lib/gui.hpp"` | as predicted |
| `basic_ops.ops:623` | `wPaintInfo pi; sClear(pi);` in `Scene::Hit` | a **dead local** — never read. Guarding it is what keeps `Hit` available headless |
| `basic_ops.ops:1168` | the whole `Screenshot` operator body | renders the viewport and compares against a reference image. Headless sets `cmd->SetError(...)` |
| `wz3_bitmap_ops.ops:11` | `#include "wz4lib/poc_ops.hpp"` | **unused** — nothing in the file references `poc`. It only mattered because `poc` needs the generated `util/shaders.hpp` |

And three headers reached `doc.hpp` from `.ops` `header`/`code` blocks and had to be pointed
at `doc_core.hpp`: `wz4lib/basic.hpp`, `wz4lib/poc.hpp`, `wz4frlib/wz3_bitmap_code.hpp`. The
middle one is instructive — it *mentions* `wPaintInfo`, but only in a declaration, which the
forward declaration satisfies.

`actions` blocks also had to go: they are parameter-panel buttons, and one of them calls
`sSetClipboard`, which lives in `base/windows.hpp` and is implemented in `windows_xlib.cpp`
— a file phase 1 deliberately excluded.

**Gate — passed.** `headless_ops_gate` generates and compiles both modules with
`gui_poison.h` force-included; clean build, 0 errors. Compile-only, an `OBJECT` library:
the operator bodies reference wz4frlib implementation classes that arrive in phase 4.

`wz4ops_gate` is retained deliberately. It is what proves `-headless` is inert when off: with
the flag absent, the regenerated tree differs from the pre-patch output *only* by the four
guard lines added to the `.ops` files and the `#line` renumbering they cause.

#### Deliberately absent from the headless build

Three operator capabilities, all revisitable when the new editor exists:

- **parameter-panel `actions`** (invert transform, copy/paste transform),
- **the `Screenshot` operator**,
- **`type` externals that paint** — 12 of the 15 across the two files.

Script bindings are absent pending the 2.4 decision on `script.cpp`.

### 2.3 — Metadata emission

Build `wz4port/tools/opsmeta`: our `main.cpp` plus a JSON emitter, linking **only**
`tools/wz4ops/parse.cpp` and `tools/wz4ops/doc.cpp`. Verified in 2.2 that those two contain
no references into `output.cpp` or `wikitext.cpp`, and `Document::Types`/`Ops` were made
public by patch 05 so the parse tree can be read without the emitter.

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
- `Doc` is a global (`doc_core.hpp:778`) consulted from inside generation code. Keep it; making it
  non-global is a refactor with no benefit to us.
- `script.cpp` is GUI-free but large; include it only if `wExecutive` requires it. Determine
  and record.

**Gate — phase gate.** A test program links `libwz4core`, registers the `basic` operator
module, constructs a document programmatically, connects two operators by geometry alone, and
prints the derived input lists. Plus: op inventory printable from the metadata.

---

## Deliverables

- `wz4port/patches/04-doc-headless-split.md`
- `wz4port/patches/05-wz4ops-headless.md`
- `wz4port/tools/opsmeta/`
- `wz4port/libwz4core/`
- Metadata JSON for `basic` and `wz3_bitmap`, hand-reviewed
- The metadata schema, documented and frozen

## Open questions

1. ~~Can `wz4ops`' parser be reused cleanly, or is patching `wz4ops` the pragmatic choice?~~
   **Answered in 2.2: both.** The parse tree is fully separable and `opsmeta` will read it;
   the headless `.cpp` comes from `wz4ops -headless` so that layout-determining emission
   stays on one code path. See patch 05.
2. **Open.** Does `wExecutive` require `script.cpp`, or can the scripting path be excluded?
   Settle in 2.4. `-headless` already stops the generated code referencing `ScriptContext`.
3. ~~How are `type` blocks' `Show`/`Paint` externs best excluded?~~ **Answered in 2.2:** a
   generation mode with a signature rule, not a compile-time guard and not a blanket skip.
   A blanket skip would have dropped `GenBitmap::Init` and both `Hit`s.

## Risks

| Risk | Assessment |
|---|---|
| ~~The `doc.hpp` split is messier than 18 lines suggests~~ | **Retired.** Forward declaration was sufficient for every function-pointer member, exactly as hoped. The one surprise — two by-value members of GUI-resident POD types — cost two verbatim header extractions |
| Metadata schema proves insufficient once the editor is built | Moderate. Mitigated by fixing the schema against the *complete* widget inventory in `01-existing-model.md` §5.2 rather than against what phase 4 happens to need |
| Conditional expressions harder to externalise than expected | Low-moderate. The grammar is small and fully documented |
