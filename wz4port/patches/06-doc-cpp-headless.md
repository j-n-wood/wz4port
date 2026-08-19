# Patch 06 — compile the operator runtime without a GUI

**Files:** 6 edited, 4 added
**Phase:** 2 (headless op runtime), stage 2.4
**Status:** applied
**Wider context:** `docs/architecture.md` entries A22–A25

## Why

Patch 04 made the document model's *header* GUI-free and patch 05 did the same
for *generated* operator code. That leaves the runtime's own translation units:
`wz4lib/doc.cpp`, `build.cpp`, `basic.cpp` and `script.cpp`. Without them there
is no `libwz4core` and nothing to link.

The phase-2 survey put `doc.cpp` at "16 GUI-touching lines out of 4,469". That
count was of `sGui->` references, and it missed the real problem: **`doc.cpp`
contains the entire implementation of `wPaintInfo`** — about 840 lines, roughly
40 member functions, all of it viewport painting. It is the `doc.hpp` situation
again, one layer down.

## Guarded, not moved

`doc.cpp` already contained three `#if !sCOMMANDLINE` blocks around its logging
overlay, and `sCOMMANDLINE` is `sCONFIG_OPTION_SHELL` (`base/types.hpp:605`) —
which this port already defines for every target. **Upstream had already built
the switch we needed and wired it to the define we were already setting.**

So the painting half of `doc.cpp` is bracketed with the same guard rather than
moved to a new file. A `.cpp` has no consumers to protect, so the argument that
forced a real split in patch 04 does not apply here: a brace costs one line
where a move costs 900 and a transcription risk.

```
 M wz4lib/doc.cpp     includes reordered; 6 regions guarded; 2 notify calls
                      rerouted; palette reference retargeted
 M wz4lib/build.cpp   1 notify call rerouted; gui/gui.hpp include removed
 M wz4lib/basic.cpp   3 function bodies guarded; 1 include guarded; 1 include
                      spelling normalised
 M wz4lib/build.hpp   doc.hpp -> doc_core.hpp
 M wz4lib/script.hpp  1 line: a genuine clang error (below)
 M wz4lib/doc_core.hpp  declares wNotifyHook
```

What is guarded out under `sCOMMANDLINE`:

| Region | What it is |
|---|---|
| `doc.cpp` "painting" banner, ~840 lines | the whole `wPaintInfo` implementation |
| `wType::Show` | clears the viewport and paints handles |
| `wEditOptions::ApplyTheme` | `sGui->SetTheme` |
| `wDocument::ChargeCaches` | renders every beat to warm the cache; demo player only |
| `wDocument::Show` | the display entry point |
| `ProgressPaint`, one `sUpdateWindow` | the 2D progress bar |
| `basic.cpp` `ScreenshotProxyType_::Show`, `UnitTestType_::Show` | render-to-texture and golden-image display |
| `basic.cpp` `UnitTest::Test` | the editor's golden-image comparison; uses `App->UnitTestPath` |
| `#include`s of `gui/color.hpp`, `wz4lib/wz4shaders.hpp`, `wz4lib/gui.hpp` | needed only by the above |

`wz4lib/wz4shaders.hpp` is worth calling out: it is generated from
`wz4shaders.asc` by the `asc` compiler, which this port does not build, and its
only use in `doc.cpp` is `new AlphaMtrl` at line 109 — inside
`wPaintInfo::wPaintInfo()`. One line of one function made the whole file
uncompilable.

## Three seams introduced

### 1. `wNotifyHook` replaces `sGui->Notify`

Three call sites (`doc.cpp` in `Connect()` and `ChangeR()`, `build.cpp:629`) told
the GUI that a memory range had changed. That is the only reason `build.cpp`
included `gui/gui.hpp` — the include even said so: `// for notify`.

```cpp
// doc_core.hpp
extern void (*wNotifyHook)(const void *ptr,sDInt bytes);
```

Signature matches `sGui->Notify(const void *,sDInt)` exactly, so an editor
installs a one-line forwarder. Headless leaves it null. This is the
"notification hook interface" the phase plan asked for.

### 2. `gui/theme.cpp` — the theme *definitions*

Patch 04 extracted `sGuiTheme`'s **declaration** into `gui/theme.hpp` so
`wEditOptions` could hold one by value. That was enough to compile and not
enough to link: `sGuiThemeDefault`, `sGuiThemeDarker`, `sGuiTheme::Serialize`
and `sGuiTheme::Tint` were all still in `gui/manager.cpp`, which pulls the
widget toolkit and a window system.

Moved verbatim into a new `gui/theme.cpp` (68 lines, pure data plus colour
arithmetic). `wEditOptions::Init` assigns `sGuiThemeDefault` and
`wEditOptions::Serialize_` streams `CustomTheme`, so both are core requirements.

### 3. `gui/palette.hpp` / `gui/palette.cpp` — the colour swatches

`wDocOptions::Serialize_` reads and writes 32×4 floats that lived as
`static sF32 sColorPickerWindow::PaletteColors[32][4]` — document format data
parked in a GUI window class. Dropping it would shift every field after it and
corrupt loading, so the bytes had to stay.

Storage moved to `gui/palette.cpp`. The class member survives as a **reference
to array**, so all eight uses inside `gui/color.cpp` still compile untouched:

```cpp
// gui/color.hpp
static sF32 (&PaletteColors)[32][4];
// gui/color.cpp
sF32 (&sColorPickerWindow::PaletteColors)[32][4] = sGuiPaletteColors;
```

Two lines instead of ten, and behaviour-identical for the original editor.

## Two genuine bugs, not portability preferences

**`script.hpp:267`** declared a member with its own class qualification:

```cpp
- Expression *ScriptCompiler::_AssignTo(Expression *a,Expression *b);
+ Expression *_AssignTo(Expression *a,Expression *b);
```

`error: extra qualification on member '_AssignTo'`. MSVC accepts it; clang does
not. Same category as patch 02.

**`basic.cpp:11`** spelled its generated include as `"basic_ops.hpp"` while its
siblings `doc.cpp` and `build.cpp` use `"wz4lib/basic_ops.hpp"`. The bare form
only resolves when the generated file sits next to the source, which is exactly
what this port avoids. Normalised to match its siblings; works in both builds.

## New files outside `altona_wz4/`

- `wz4port/compat/altona_missing.cpp` — `sCheckBreakKey()`. Declared at
  `base/system.hpp:780` for every platform, defined **only** in
  `base/system_win.cpp:3050`. It is an upstream gap on POSIX, not something this
  port broke. Ours returns 0: a headless build has no key state to poll. A CLI
  wanting Ctrl+C to abort a build should install a SIGINT handler and report it
  here — worth doing when `wz4gen` exists, not before.
- `wz4port/tests/core_connect.cpp` — the phase gate.

`util/stb_image.c` was already in the tree and simply needed adding to the
build; `util/image.cpp` calls it for PNG and JPG decoding.

## Behaviour

None for a GUI build: every guard is `#if !sCOMMANDLINE`, and `sCOMMANDLINE` is
0 unless `sCONFIG_OPTION_SHELL` is set. The non-headless generated tree was
re-diffed after this patch and is unchanged.

For a console build, five capabilities are absent by construction, all of them
display: viewport painting, handle manipulation, theme application, the
progress bar, and the golden-image comparison. Nothing in the evaluation path
is affected.

## Verified

- `libwz4core` compiles with `wz4port/tests/gui_poison.h` force-included, so a
  GUI header reappearing anywhere in the runtime breaks the build.
- `core_connect` links it and runs: 11 types and 38 classes registered from
  `basic`, a document built programmatically, and the graph derived from block
  geometry alone — including that a 6-wide consumer under two 3-wide producers
  takes both in left-to-right order, and that a one-row gap severs a connection.
- Clean build 0 errors; `ctest` 2/2.
