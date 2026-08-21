# Patch 05 — `wz4ops -headless`, and the `.ops` guards it needs

**Files:** 4 in `tools/wz4ops/`, 3 headers, 2 `.ops` files
**Phase:** 2 (headless op runtime), stage 2.2
**Status:** applied
**Extended by:** patch 11 — a per-operator `headless = 0;` opt-out, and
`WZ4_HEADLESS` now defined in the generated `.hpp` as well as the `.cpp`
**Wider context:** `docs/architecture.md` entries A11–A15

## Why

Patch 04 made the *document model* GUI-free. That is not enough: every
operator module is a `.cpp` **generated** by `wz4ops`, and roughly 70% of what
it generates is `MakeGui` bodies, script bindings and wiki text. Those name
`wGridFrameHelper`, `ScriptContext` and `wPaintInfo`, so nothing generated
could compile headless no matter what `doc.hpp` looked like.

`-headless` emits only the half that can.

| | `basic_ops.cpp` | `wz3_bitmap_ops.cpp` |
|---|---:|---:|
| default | 6,230 lines | 8,884 lines |
| `-headless` | 1,850 lines | 2,166 lines |

## The decision behind this patch

The phase plan preferred a *separate* tool that reused `wz4ops`' parser and
emitted its own output, specifically to avoid patching `wz4ops`. Measuring
killed that preference:

- The parse tree is completely separable — `parse.cpp` and `doc.cpp` contain
  zero references into `output.cpp` or `wikitext.cpp` — **but**
  `Document::Types` and `Document::Ops` are private, so a separate tool needs
  an upstream patch anyway.
- Reimplementing the non-GUI half of `output.cpp` would duplicate ~600 lines
  of offset-sensitive emission: parameter packing, arrays, ties, string
  offsets, helper structs. Its failure mode is silently misaligned parameter
  data, which this project has no oracle to catch.
- The GUI-emitting parts of `output.cpp` are few and contiguous.

So the headless `.cpp` comes from `wz4ops -headless` — the same code path as
the GUI output for everything that determines layout, so the two cannot
diverge — and the metadata JSON (stage 2.3) will come from a separate
`wz4port/tools/opsmeta` reading the now-public parse tree.

## The change

### 1. `tools/wz4ops/` — the flag

```
 M doc.hpp     Types/Ops moved to public; added Headless and ExternIsGuiOnly()
 M doc.cpp     Headless = 0 in the constructor
 M main.cpp    Doc->Headless = sGetShellSwitch(L"headless"); usage text
 M output.cpp  the guards
```

Suppressed under `-headless`:

| Emitted thing | Why |
|---|---|
| `#include "gui/gui.hpp"`, `gui/textwindow.hpp`, `wz4lib/script.hpp` | GUI and script |
| `#include "wz4lib/doc.hpp"` → `doc_core.hpp` in the generated `.hpp` | the point of the exercise |
| `%sGui%s` — the whole `MakeGui` body, `OutputPara`, `OutputTies` | `wGridFrameHelper` |
| `%sHnd%s` — handles | `wPaintInfo` |
| `%sDrag%s` — special drag | `sWindowDrag`, `wPaintInfo` |
| `%sCed%s` — custom editor | `wCustomEditor` |
| `%sAct%s` — parameter-panel actions | editor-only; reaches `sSetClipboard` |
| `%sBind%s`, `%sBind2%s`, `%sBind3%s`, `OutputAnim()` | `ScriptContext` |
| `%sWiki%s` | editor-only, and bulky |
| the matching `cl->` assignments in `OutputMain` | follows the above |

Everything that decides *layout* — `OutputParaStruct`, the `Cmd` bodies,
`SetDefaults`, `SetDefaultsArray`, `GetDescription`, `OutputStruct`, and
`OutputMain`'s class registration — is emitted unchanged.

Two blocks are guarded with a brace that does not reindent its body
(`// (guarded without reindenting the body, to keep the patch small)`). That
is deliberate: reindenting 130 lines to add one condition would bury the
change.

`-headless` also emits `#define WZ4_HEADLESS 1` at the top of the generated
`.cpp`, which is what the `.ops` guards below key off. (Patch 11 emits it in the
generated `.hpp` too — the guards this patch needed were all in `code` blocks,
but a `header` block goes to the `.hpp`, and the `.hpp` is read by every consumer
of the module rather than only by the generated `.cpp`.)

### 2. Type `externals` — a signature rule, not a blanket skip

A `type` block's `externals` are hand-written C++ pasted into the generated
file, so some cannot compile headless. Dropping them all would be wrong:

- `wz3_bitmap_ops.ops:39` — `extern void Init()` is a `wType` virtual the
  headless build needs; it calls `xInitPerlin()`.
- `basic_ops.ops:495`/`:617` — `extern void Hit(wObject *,const sRay &,
  wHitInfo &)` names nothing outside `doc_core.hpp`.

`Document::ExternIsGuiOnly()` skips an extern only when its return type or
parameter list mentions `wPaintInfo`, `wGridFrameHelper` or `wCustomEditor`.
Across the two `.ops` files that drops 12 of the 15 `type` externs, keeping
`GenBitmap::Init`, `MeshBase::Hit` and `Scene::Hit`. (`basic_ops.ops` has 14
`extern` lines but only 13 are `type` externs — `:995`'s `ProgressPaint` sits
in a global `code` block.)

`wHandle` is deliberately *not* in that list: it only ever appears inside
`wPaintInfo`, and as a substring it would also match the core type
`wHandleSelectTag`. `wPaintInfo3D` is a typedef of `wPaintInfo` and matches as
a substring, which is what we want.

**Every skip is printed.** The suppression is never silent.

### 3. Three headers now include `doc_core.hpp` instead of `doc.hpp`

```
 M wz4lib/basic.hpp             nothing in it touches the gui half
 M wz4lib/poc.hpp               only declares Wireframe(...,wPaintInfo &,...)
 M wz4frlib/wz3_bitmap_code.hpp nothing in it touches the gui half
```

These are reached from `.ops` `header`/`code` blocks, so without this the
generated headless file pulls the whole toolkit back in through the side door.
`poc.hpp` is the interesting one: it *mentions* `wPaintInfo`, but only in a
declaration, which the forward declaration in `doc_core.hpp` satisfies.

### 4. Three `#ifndef WZ4_HEADLESS` guards in the `.ops` files

`code` and `header` blocks are verbatim C++, so the generator cannot reason
about them. Three places needed a guard:

| Where | What | Why |
|---|---|---|
| `basic_ops.ops:15` | `#include "wz4lib/gui.hpp"` | the editor's own windows; never built here |
| `basic_ops.ops:623` | `wPaintInfo pi; sClear(pi);` inside `Scene::Hit` | a dead local — `pi` is never read in that body. Guarding it keeps `Hit` available headless |
| `basic_ops.ops:1168` | the entire `Screenshot` operator body | renders the viewport to a texture and compares against a reference image: an editor test facility needing the whole 3D path. Headless sets `cmd->SetError(...)` instead |
| `wz3_bitmap_ops.ops:11` | `#include "wz4lib/poc_ops.hpp"` | **unused** — nothing in that file references anything from `poc`. It matters only because `poc` needs the generated `util/shaders.hpp`, which this port does not build |

## Why this is safe

- **The flag is inert when off.** With `-headless` absent, the regenerated
  tree was diffed against the pre-patch output: the *only* differences are the
  four `#ifndef`/`#ifdef` lines added to the `.ops` files (which are verbatim
  text, so they appear in the output) and the `#line` renumbering they cause.
  No emitted code changed. The `wz4ops_gate` target is retained precisely to
  keep proving this.
- **The headless output compiles, with the GUI poisoned.** The
  `headless_ops_gate` target builds both modules with
  `wz4port/tests/gui_poison.h` force-included, so a GUI header sneaking back
  in fails the build rather than passing quietly. Verified from a clean tree:
  0 errors.
- **Nothing GUI survives in the output.** The only occurrences of `wPaintInfo`
  in the headless `basic_ops.cpp` are the two inside `#ifndef WZ4_HEADLESS`
  regions; there are no `MakeGui`, `Bind` or `Wiki` symbols at all. 65 `Cmd`
  and `SetDefaults` functions and the full `AddTypes`/`AddOps` registration
  are retained.

## Behaviour

None without the flag. With `-headless`, three operator capabilities are
deliberately absent and should be revisited when the new editor exists:
parameter-panel **actions**, the **`Screenshot`** operator, and everything
under `type` externals that paints. Script bindings are absent pending the
stage 2.4 decision on whether `wExecutive` needs `script.cpp` at all.
