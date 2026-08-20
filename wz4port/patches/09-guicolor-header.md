# Patch 09 — extract `enum sGuiColor`, and reach the 2D drawing layer headlessly

**Files:** 2 upstream (`gui/window.hpp`, `wz4frlib/wz3_bitmap_code.cpp`),
1 new upstream (`gui/guicolor.hpp`), plus `wz4port/compat/font_freetype.cpp`
**Phase:** 4 (texture library), stage 4.5
**Status:** applied
**Wider context:** `docs/06-phase-texture.md` stage 4.5

## Why

`GenBitmap::Text` (`wz4frlib/wz3_bitmap_code.cpp:2505`) is the only texture
operator that needs an OS font. Stage 4.1 stubbed it because Altona's `sFont2D`
has GDI and X11 backends and none for macOS. Stage 4.5 implements it on
FreeType.

The implementation itself needs **no upstream change at all**: `sFont2D` keeps
its state behind an opaque `struct sFont2DPrivate *prv`, which is exactly the
seam needed to implement the class from another translation unit. Altona
declares the whole 2D software drawing layer in `base/windows.hpp` and defines it
only in `base/windows.cpp` (GDI) and `base/windows_xlib.cpp` (X11), neither of
which this build compiles — so the symbols are simply absent and
`wz4port/compat/font_freetype.cpp` supplies them.

What did need changing was **reaching the declarations**.

## Change 1 — `enum sGuiColor` moved to `gui/guicolor.hpp`

`GenBitmap::Text` names `sGC_BLACK` and `sGC_MAX` to drive the drawing layer.
Those live in `enum sGuiColor` in `gui/window.hpp` — a GUI header, declared
alongside `sWindow`, which the headless build cannot include and which the
`gui_poison.h` tripwire exists to keep out.

The enum itself has no GUI dependency. It is 22 integer constants.

So it moved to a new `gui/guicolor.hpp`, verbatim, and `gui/window.hpp` now
includes that. **One definition, two consumers.** Same shape as `gui/theme.hpp`,
`gui/treeinfo.hpp` (patch 04) and `gui/palette.hpp` (patch 06): a GUI-free
declaration that headless code legitimately needs, relocated rather than
duplicated.

### Why not duplicate it in `wz4port/`

That was the alternative, and it would have kept `altona_wz4/` untouched. It was
rejected because **phase 5 puts a GUI on the texture library**, so both
definitions would land in one translation unit and collide — a guaranteed future
error rather than a hypothetical one. A relocation cannot collide with itself.

## Change 2 — two guarded includes in `wz3_bitmap_code.cpp`

```cpp
#if WZ4PORT_HAVE_SFONT2D
#include "base/windows.hpp"
#include "gui/guicolor.hpp"
#endif
```

The original build reached `base/windows.hpp` through `gui/gui.hpp`, whose
include the headless code generator suppresses (patch 05). `base/windows.hpp`
itself pulls only `base/types.hpp` and `base/serialize.hpp`, both already in the
headless build, so including it directly is cheap and safe — it declares
`sWindowModeCodes` and `sHasWindowFocus` but never `sWindow`, so the poison
tripwire is untroubled. (Whole-identifier matching is what makes that work.)

Both includes are inert unless `WZ4PORT_HAVE_SFONT2D` is defined, which
`CMakeLists.txt` only does when FreeType is present **and links**.

The first attempt put these inside the `#else` branch of the existing guard —
which is inside a function body, where `#include` is not legal. Corrected
immediately; noted here because the guard's shape invites the mistake.

## Invariant

`git status` on `altona_wz4/` shows exactly three files for this patch:
`gui/guicolor.hpp` (new), `gui/window.hpp` (enum replaced by an include),
`wz4frlib/wz3_bitmap_code.cpp` (two guarded includes).

Nothing in Altona's font or drawing layer is patched. `sFont2D`'s methods are
defined entirely in `wz4port/compat/font_freetype.cpp`.

## What is deliberately not implemented

Only what `GenBitmap::Text` uses, plus the cheap metric queries. `PrintMarked`,
`PrintBasic`, the `sRect` overload of `Print`, `sGetLetterDimensions`,
`GetCharCountFromWidth` and `AddResource` belong to the GUI text layer and are
left undefined on purpose: referencing one gives a clear undefined-symbol error
pointing at this file, which is better than a stub that silently draws nothing.

`util/image.cpp`'s font-atlas builder uses several of those, and is not pulled
into this build's link today. If phase 5 needs it, that is where to start.
