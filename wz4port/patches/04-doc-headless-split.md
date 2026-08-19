# Patch 04 — split `doc.hpp` so the document model builds without a GUI

**Files:** 5 (3 new, 2 edited) plus `wz4lib/doc.hpp` reduced to a shim
**Phase:** 2 (headless op runtime), stage 2.1
**Status:** applied
**Wider context:** `docs/architecture.md` entries A8, A9, A10

## Why

Every operator source file in Werkkzeug4 includes `wz4lib/doc.hpp`, and
`doc.hpp` opened with

```cpp
#include "gui/gui.hpp"
#include "gui/listwindow.hpp"
#include "util/shaders.hpp"
```

so every operator transitively pulled in Altona's entire widget toolkit. Until
that is severed nothing headless is possible.

`util/shaders.hpp` makes it concrete rather than merely untidy: that header is
*generated* from `util/shaders.asc` by the `asc` shader compiler, which is out
of scope for this port. Before this patch, a translation unit that merely
included `doc.hpp` failed outright:

```
wz4lib/doc.hpp:21:10: fatal error: 'util/shaders.hpp' file not found
```

## What actually depends on the GUI

Measured, not assumed. Only four declarations in `doc.hpp` needed anything
from `gui/` or `util/shaders.hpp`:

| Declaration | Needs |
|---|---|
| `wPaintInfo` | `sSimpleMaterial` — `util/shaders.hpp` |
| `wGridFrameHelper` | `sGridFrameHelper` — `gui/frames.hpp` |
| `wCustomEditor` | `sWindowDrag` — `gui/window.hpp` |
| `wEditOptions` | `sGuiTheme` — `gui/manager.hpp` |

plus `wTreeOp` and `wPage`, which each hold a `sListWindowTreeInfo<>` from
`gui/listwindow.hpp`.

Everything else that looked like a graphics dependency — `sViewport`,
`sTargetSpec`, `sGeometry`, `sMaterial`, `sTexture2D`, `sVertexSingle`,
`sVertexBasic`, and `sMaterialEnv` despite the name — lives in
`base/graphics.hpp`, which is already part of the headless build via the blank
renderer. `sMessage` is in `base/types2.hpp`.

## The change

### 1. Two pure-data extractions out of `gui/`

`sGuiTheme` and `sListWindowTreeInfo<>` are plain serializable records that
happen to live in GUI headers. They are needed by `wEditOptions`, `wTreeOp`
and `wPage` **by value**, so a forward declaration cannot work. Both were
moved verbatim into new headers with no GUI dependency, and their original
homes now include those headers:

```
+ gui/treeinfo.hpp   sLW_MAXTREENEST, sListWindowTreeInfo<>, sListWindowTreeInfoFlags
+ gui/theme.hpp      sGuiTheme, sGuiThemeDefault, sGuiThemeDarker

  gui/listwindow.hpp   -33 lines, +1 include
  gui/manager.hpp      -34 lines, +1 include
```

`gui/treeinfo.hpp` includes only `base/types.hpp`; `gui/theme.hpp` only
`base/types2.hpp`, which is what `gui/manager.hpp` already included above the
`sGuiTheme` definition. Consumers of either GUI header see exactly what they
saw before.

### 2. `doc.hpp` split three ways

```
+ wz4lib/doc_core.hpp   the document model. No gui, no shaders.
+ wz4lib/doc_gui.hpp    wHandle, wPaintInfo, wGridFrameHelper, wCustomEditor
  wz4lib/doc.hpp        now just includes both, in that order
```

`doc_core.hpp` forward-declares the four GUI names it still mentions:

```cpp
class wPaintInfo;
struct wGridFrameHelper;
class wCustomEditor;
struct sWindowDrag;
```

`wType`'s virtuals take `wPaintInfo&` and `wClass` holds function pointers
whose signatures name `wGridFrameHelper&`, `wPaintInfo&`, `sWindowDrag&` and
`wCustomEditor*`. References and pointers to incomplete types are legal in
declarations, and nothing in the headless path dereferences them — this was
the main risk flagged in `docs/04-phase-headless-core.md` and it turned out to
cost nothing.

`doc_core.hpp` also gained `#include "base/graphics.hpp"`, which it previously
received indirectly through `gui/gui.hpp`.

## Divergences from the plan

`docs/04-phase-headless-core.md` put `wEditOptions`, `wHitInfo` and
`wHandleSelectTag` in `doc_gui.hpp`. They are in `doc_core.hpp` instead:

- `wEditOptions` and `wHandleSelectTag` are `wDocument` members **by value**
  (`EditOptions`, `SelectedHandleTags`), so they cannot be on the GUI side.
  `EditOptions.MemLimit` is read by the cache manager at `doc.cpp:3993`, and
  `TreeInfo.Level`/`.Flags` are part of the `.wz4` document serialisation
  (`doc.cpp:3203`, `:3240`, `:3859`) — both are load-bearing headless.
- `wHitInfo` is plain data with no GUI dependency at all.

The rule applied was: **`doc_gui.hpp` holds exactly what cannot compile
without the GUI or shader headers; everything else stays in core.**

The plan also called this patch `03-doc-headless-split.md`; 03 was already
taken by the Latin-1 conversion.

## Why this is safe

- **Nothing was rewritten.** Every declaration was moved verbatim. The two
  halves were diffed against `git show HEAD:.../doc.hpp` line by line,
  whitespace-normalised: the only differences are the new include guards, the
  three added `#include`s, the four forward declarations and comments. No
  declaration was added, dropped or altered.
- **The GUI consumer still works.** With `util/shaders.hpp` stubbed to two
  forward declarations, a TU that includes `wz4lib/doc.hpp` — pulling
  `gui/gui.hpp` and `gui/listwindow.hpp` with it — parses clean on macOS, with
  only the pre-existing benign Altona warnings. Before the split the same TU
  died on the missing `util/shaders.hpp`.
- **The core half is genuinely GUI-free.** `clang++ -H` on a TU including only
  `doc_core.hpp` lists exactly:
  `base/{types,types2,serialize,system,math,graphics}.hpp`,
  `gui/{treeinfo,theme}.hpp`, `compat/altona_config.hpp`. No window system, no
  widget toolkit, no shaders.
- **And it stays that way.** The `headless_core_gate` target in
  `wz4port/CMakeLists.txt` compiles a TU that includes only `doc_core.hpp`,
  with `wz4port/tests/gui_poison.h` force-included. That header poisons
  `sWindow`, `sGui_` and `sSimpleMaterial`, so re-adding a `gui/` or
  `util/shaders.hpp` dependency to `doc_core.hpp`, `gui/theme.hpp` or
  `gui/treeinfo.hpp` breaks the build immediately and names the file. The
  tripwire earned its keep on the first run: it caught that `sMaterialEnv`
  is declared in `base/graphics.hpp`, not in `util/shaders.hpp`.

## Behaviour

None. This is a header reorganisation; no definition, layout or signature
changed.
