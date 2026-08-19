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

### 2.3 — Metadata emission — **done**

Build `wz4port/tools/opsmeta`: our `main.cpp` plus a JSON emitter, linking **only**
`tools/wz4ops/parse.cpp` and `tools/wz4ops/doc.cpp`. Verified in 2.2 that those two contain
no references into `output.cpp` or `wikitext.cpp`, and `Document::Types`/`Ops` were made
public by patch 05 so the parse tree can be read without the emitter.

Emit the JSON schema sketched in `02-target-model.md` §6, and **fix it exactly** in this
stage — the editor's entire UI is downstream of it.

#### What the corpus actually contains

Counted from the emitted metadata — i.e. from the parser itself — over all 33 `.ops` files.
(A first pass counted keywords in the raw text and got this wrong; it included commented-out
operators and missed modifiers written in non-canonical order. Don't grep the DSL when a
parser is available.)

| Widget | Corpus |
|---|---:|
| `float` | 1220 |
| `flags` | 1181 |
| `int` | 358 |
| `group` | 264 |
| `color` | 142 |
| `string` | 121 |
| `link` | 36 |
| `action` | 30 |
| `label` | 16 |
| `fileout` | 12 |
| `filein` | 9 |
| `radio` | 2 |
| `strobe` | 1 |
| `char` | 1 |
| **`bitmask`** | **0** |
| **`custom`** | **0** |
| **`tie`** | **0** |

`float` covers `float`, `float2`, `float30`, `float31` and `float4`: they share `TYPE_FLOAT`
and are distinguished by `layout: "vector"` plus `ctype`, not by kind. `padding` never appears
because it produces no parameter at all — it only advances the offset counter
(`parse.cpp:462-475`), so it shows up as a gap.

Two consequences:

1. **`bitmask`, `custom` and `tie` are dead syntax.** They are documented in
   `01-existing-model.md` §5.2 because the DSL and the GUI implement them, but **no `.ops`
   file in the tree uses any of them.** The schema will represent them — it is cheap and the
   DSL supports them — but they are *untested by construction*, and that must be said out
   loud rather than counted as coverage. Note that `float2`/`float30`/`float31`/`float4`
   already give tied groups by another route (`XYZW`), which is presumably why `tie` died.
2. **Conditionals are not an edge case.** 82 `if(...)` in the two gate modules alone. Getting
   them wrong means panels of the wrong shape everywhere, not in one corner. They also nest
   deeply enough to break a fixed-depth JSON writer — see below.

#### Three traps the §6 sketch does not show

Found by reading the parser, and each one silently corrupts the editor if missed:

**a. There are three separate offset spaces, not one.** The sketch has a bare `"offset"`.
In fact `parse.cpp:651-694` allocates:

| Kinds | Space | Addressed by |
|---|---|---|
| everything with a `CType` | 32-bit words | the `Para` struct / `wOp::EditData` |
| `string`, `filein`, `fileout` | string slots | `wOp::EditString[n]` |
| `link` | link slots | `wOp::Links[n]` |

The three counters run independently, so word offset 0, string offset 0 and link offset 0
all exist in the same operator. The schema names the space explicitly.

**b. `char X[n]` consumes `(n+1)/2` words, not `n`.** `parse.cpp:684-685`. Every other kind
consumes `count`. The schema emits the *actual* word consumption per parameter so the editor
never re-derives it.

**c. `count` means two different things.** For `float2/30/31/4` it is a vector addressed
`.x .y .z .w` and occupies one struct field (`XYZW=1`); for `float X[n]`/`int X[n]` it is a
C array. Same field in the parse tree, different layout. The schema carries an explicit
`layout: scalar | vector | array`.

#### Conditionals are less awkward than expected

The plan called these the one genuinely awkward item. Reading `parse.cpp:1016-1029` shows
why they are not:

**`Flags.choicename` is already desugared at parse time** into
`(Symbol & mask) == value` — `sFindFlag` resolves the choice against that parameter's option
string while parsing, and the tree that survives contains only `EOP_BITAND`, `EOP_EQ` and
integer literals. `EOP_SYMBOL` therefore only ever names a bare parameter, and nothing
downstream needs to know about choice names.

Nested `if` blocks are also already flattened: `parse.cpp:920-921` ANDs an enclosing
condition into the inner one, so each parameter carries one complete condition and there is
no nesting to represent.

That leaves a five-node grammar to emit: binary op, unary op, integer literal, parameter
symbol, `input[n]`. `opsmeta` resolves each `EOP_SYMBOL` to the referenced parameter at emit
time and emits its offset alongside the name, so the editor evaluates without a name lookup.
An unresolvable symbol is an **error**, not a warning — it means a typo that would otherwise
silently disable a parameter's visibility rule.

#### One thing that comes for free

Palette column inference — 0 inputs → 0, 1 → 1, more → 2, and 3 if any input type differs
from the output type — happens in the **parser** (`parse.cpp:334-341`), before an explicit
`column = N;` can override it. So `Op::Column` is always the effective value and the editor
needs no inference of its own.

#### Schema shape

Per module: `schemaVersion`, module name, priority. Per type: symbol, label, parent, colour,
flags, gui modes, column headers. Per class: name, label, output type, tab type, column,
shortcut, flags, extract prefix, grid columns, para word/string/link/array counts, helper
words, file in/out masks and filter, inputs (type, optional/weak/varargs, link method,
default op), action ids, and the parameter list. Per parameter: kind, name, label, symbol,
offset **and space**, words consumed, layout, count, min/max/step/rstep, log-step flag, hex
format, defaults, choice widgets with shift/mask/choices, channel string, line count,
modifier flags, and the condition tree.

`schemaVersion` exists so the editor can reject a mismatch instead of misreading. Output is
deterministic — source order, fixed key order, no timestamps, no absolute paths — so a schema
or `.ops` change is reviewable as a diff.

#### What was built

`wz4port/tools/opsmeta/` — `main.cpp`, `emit.cpp`, `json.cpp` — plus `opsmeta_gate` in CMake.
`schemaVersion` is **1**; the contract is frozen in `02-target-model.md` §6.

Two things went beyond the plan, both because they paid for themselves immediately:

**1. The gate runs over the whole corpus, not the two named modules.** `opsmeta_gate` emits
metadata for all 33 `.ops` files, because `opsmeta` validates as it goes and the extra 31
modules cost milliseconds. That turns ~450 parameters of coverage into 2,728:

```
33 modules · 40 types · 370 classes · 2728 parameters
2729 choice values cross-checked against sFindFlag
```

It found a crash on its first run that the two gate modules never triggered: the JSON writer
had a fixed 16-level depth stack, and condition trees nest as deeply as the source nests
`if(...)`, two levels per expression node. The depth stack is now dynamic.

**2. `opsmeta` validates rather than just emitting**, and refuses to write a file it cannot
stand behind:

- **Offset overlap and range.** `wz4ops` checks this in `OutputParaStruct` — inside the C++
  emitter, which `opsmeta` deliberately does not link. Without its own check, the JSON could
  describe a layout the generated struct would have rejected, and the editor writes straight
  into `wOp::EditData` at whatever offset the metadata gives it. Verified by a deliberately
  broken `.ops` with two parameters pinned to word 0: it reports the overlap, exits non-zero
  and writes nothing.
- **Choice decomposition against `sFindFlag`.** The `options` parse is a re-implementation of
  Altona's, and 1,181 `flags` parameters depend on it. So every unique choice label is fed
  back through `sFindFlag` itself and the mask and shifted value must agree. Labels appearing
  in more than one widget of the same string are skipped — `sFindFlag` returns the first
  match, so there is nothing to compare (the common case is `-` as a blank entry, as in
  `"-|abs:*1-|sin"`).
- **Unresolvable symbols.** A conditional or `continue` naming a parameter that does not exist
  is an error, not a warning.

#### Corrections the work forced

- **The text-based widget census was wrong.** It counted commented-out operators (both `char`
  uses in `basic_ops.ops` are inside a `/* */` block) and its regex missed modifiers in
  non-canonical order, badly undercounting `string`. The census in this document is now taken
  from the emitted metadata, which is the parser's own answer. Corpus-wide: `float` 1220,
  `flags` 1181, `int` 358, `group` 264, `color` 142, `string` 121, `link` 36, `action` 30,
  `label` 16, `fileout` 12, `filein` 9, `radio` 2, `strobe` 1, `char` 1. `bitmask`, `custom`
  and `tie` remain at zero, and all 383 `ties` arrays emit empty.
- **Altona's float formatter is not correctly rounded.** Asked for nine decimals it renders
  `4.0f` as `4.00000023` and `0.125f` as `0.125000007`, both exactly representable. In a file
  whose purpose is hand review and diffing, that is worse than useless. `json.cpp` goes
  through libc `snprintf`/`strtof` instead, printing at the shortest precision that
  round-trips.
- **A leading space in an option string is load bearing.** The corpus writes `" 1| 2| 4"`
  rather than `"1|2|4"` because a leading digit is consumed as an *explicit value*, leaving an
  empty label. `" 1D| 2D| 3D"` works for the same reason. Faithfully reproduced, and now
  commented where it matters.

**Gate — passed.** Metadata emitted for `basic` and `wz3_bitmap`, hand-checked against source
(`Perlin`: 10 parameters, offsets 0–9, `paraWords: 10`, `0x0808` default decoding to 256×256,
both `Mode` widgets at shift 0 and 1 — all matching), plus the machine checks above over the
full corpus. Clean build, 0 errors, `ctest` green.

#### Deferred deliberately

**No golden-file test of the schema yet.** §6 asks for the metadata to be diffable so schema
changes are reviewable, and it is — but nothing yet fails a build when the shape changes
unintentionally, because `build/meta/` is gitignored. Phase 4 already plans golden outputs and
a runner; the schema golden belongs there rather than as a bespoke mechanism now.

### 2.4 — `libwz4core` — **done**

Recorded as `wz4port/patches/06-doc-cpp-headless.md`. Target is `wz4core`; the gate test is
`core_connect`.

#### The survey's line count was misleading

The plan expected "`doc.cpp` has 16 GUI-touching lines". That was a count of `sGui->`
references, and it missed the actual problem: **`doc.cpp` contains the whole implementation of
`wPaintInfo`** — about 840 lines under its own "painting" banner, roughly 40 member functions.
It is the `doc.hpp` situation one layer down.

A single line of it mattered out of proportion: `new AlphaMtrl` at `doc.cpp:109`, inside
`wPaintInfo`'s constructor, is the only use of `wz4lib/wz4shaders.hpp` — which is generated by
the `asc` compiler this port does not build. One line made the file uncompilable.

#### Upstream had already built the switch

`doc.cpp` already had three `#if !sCOMMANDLINE` blocks around its logging overlay, and
`sCOMMANDLINE` is `sCONFIG_OPTION_SHELL` (`base/types.hpp:605`) — **which this port has
defined for every target since phase 1.** So the painting half is bracketed with the same
guard rather than moved out.

Guarded rather than split because a `.cpp` has no consumers to protect: the argument that
forced a real file split in 2.1 does not apply, and a brace costs one line where a move costs
900 plus a transcription risk.

#### Three seams

1. **`wNotifyHook`** replaces the three `sGui->Notify` calls. Signature matches
   `sGui->Notify(const void *,sDInt)` so the editor installs a one-line forwarder; headless
   leaves it null. This is the notification hook the plan asked for, and it is the only reason
   `build.cpp` included `gui/gui.hpp` — the include said so.
2. **`gui/theme.cpp`** (new). Patch 04 extracted `sGuiTheme`'s *declaration*, which was enough
   to compile and not to link — the two theme constants, `Serialize` and `Tint` were still in
   `gui/manager.cpp`. Moved verbatim.
3. **`gui/palette.hpp` + `gui/palette.cpp`** (new). `wDocOptions::Serialize_` streams 32×4
   floats that lived as a static member of `sColorPickerWindow`: document format data parked in
   a GUI window class. Storage moved; the class member survives as a **reference to array**, so
   all eight uses in `gui/color.cpp` compile untouched. Two lines rather than ten.

#### Open question 2 answered: `script.cpp` is required

`wExecutive::Execute` drives `ScriptContext` directly — `PushGlobal`, `ClearImports`,
`AddImport`, `Run`, `FlushLocal` at `doc.cpp:4050-4200` — and `wOp::GetScript` compiles a
context per operator. The scripting path is part of the executive, not an optional layer.

It costs nothing platform-wise: `script.cpp` has no GUI or graphics dependency and compiled
headless on the first attempt.

#### Two genuine upstream bugs

- `script.hpp:267` declared a member with its own class qualification
  (`Expression *ScriptCompiler::_AssignTo(...)` inside `class ScriptCompiler`). Clang rejects
  it, MSVC accepts it — same category as patch 02.
- `basic.cpp:11` spelled its generated include `"basic_ops.hpp"` where its siblings use
  `"wz4lib/basic_ops.hpp"`. The bare form only resolves when the generated file sits beside
  the source, which is exactly what this port avoids.

#### And one upstream gap outside `altona_wz4/`

`sCheckBreakKey()` is declared at `base/system.hpp:780` for every platform and defined **only**
in `base/system_win.cpp`. It is a user-abort poll for long operator evaluations. Supplied by
`wz4port/compat/altona_missing.cpp`, returning 0. A CLI that wants Ctrl+C to interrupt a build
should install a SIGINT handler and report it there — phase 3, with `wz4gen`.

**Gate — passed.** `core_connect` links `wz4core`, registers `basic` (11 types, 38 classes),
builds a document programmatically and lets `Connect()` derive the graph from geometry alone.
It checks the rules from `01-existing-model.md` §2.2 rather than just printing: a 6-wide
consumer under two 3-wide producers takes both, in left-to-right order; a one-row gap severs
the connection; moving a block to touch an edge connects it with no other edit; and the
reverse edge is recorded. 14 checks, 0 failures. Clean build 0 errors, `ctest` 2/2.

#### Divergence: `headless_core_gate` stays an `OBJECT` library

The plan said to promote it to a linked test once `libwz4core` existed. Not done, on purpose:
its value is that it compiles a TU including **only** `doc_core.hpp`, and linking `wz4core`
would drag in more headers and weaken exactly that property. `core_connect` now covers "links
and runs"; the two targets test different things.

---

## Deliverables — all done

- `wz4port/patches/04-doc-headless-split.md`
- `wz4port/patches/05-wz4ops-headless.md`
- `wz4port/patches/06-doc-cpp-headless.md`
- `wz4port/tools/opsmeta/`
- `wz4core` (the plan called it `libwz4core`; it is a CMake target, not a directory —
  the sources are upstream's, compiled in place)
- Metadata JSON for `basic` and `wz3_bitmap`, hand-reviewed
- The metadata schema, documented and frozen at `schemaVersion: 1`

## Open questions

1. ~~Can `wz4ops`' parser be reused cleanly, or is patching `wz4ops` the pragmatic choice?~~
   **Answered in 2.2: both.** The parse tree is fully separable and `opsmeta` will read it;
   the headless `.cpp` comes from `wz4ops -headless` so that layout-determining emission
   stays on one code path. See patch 05.
2. ~~Does `wExecutive` require `script.cpp`, or can the scripting path be excluded?~~
   **Answered in 2.4: it is required.** `wExecutive::Execute` drives `ScriptContext` directly
   at `doc.cpp:4050-4200`. It costs nothing platform-wise — no GUI, no graphics.
3. ~~How are `type` blocks' `Show`/`Paint` externs best excluded?~~ **Answered in 2.2:** a
   generation mode with a signature rule, not a compile-time guard and not a blanket skip.
   A blanket skip would have dropped `GenBitmap::Init` and both `Hit`s.

## Risks

| Risk | Assessment |
|---|---|
| ~~The `doc.hpp` split is messier than 18 lines suggests~~ | **Retired.** Forward declaration was sufficient for every function-pointer member, exactly as hoped. The one surprise — two by-value members of GUI-resident POD types — cost two verbatim header extractions |
| Metadata schema proves insufficient once the editor is built | Moderate. Mitigated by fixing the schema against the *complete* widget inventory in `01-existing-model.md` §5.2 rather than against what phase 4 happens to need |
| Conditional expressions harder to externalise than expected | Low-moderate. The grammar is small and fully documented |
