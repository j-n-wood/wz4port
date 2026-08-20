# Architecture and decision record

## What this document is

The structure of the port as it now stands, and the record of how it got that
way — including the roads not taken and the measurements that closed them.

It exists because this project's structural decisions are mostly *subtractive*
and *negative*: seams cut through a 2012 code dump, dependencies deliberately
not taken, upstream changes deliberately not made. That reasoning is invisible
in the resulting code, and it is exactly the reasoning a later change is most
likely to undo by accident.

How it differs from its neighbours:

| Doc | Answers |
|---|---|
| `progress.md` | *Where are we?* Current state, next step, session bootstrap |
| `01-existing-model.md` | *How does Werkkzeug4 work?* Upstream reference, cited |
| `02-target-model.md` | *What are we building?* Target design and divergences |
| `03`–`09` | *What is the plan for phase N?* Per-phase plans, kept current |
| `wz4port/patches/*` | *What exactly changed in this one upstream file, and why?* |
| **this doc** | *Why is the structure shaped like this, and what else was tried?* |

Entries are **not rewritten when superseded** — they are marked. A decision
that was reversed is more useful than one silently replaced.

---

## Part 1 — the structure as it stands

### Layers

```
                       ┌─────────────────────────────────────────┐
  not built yet        │  editor (Dear ImGui)   ·   wz4gen CLI   │
                       └─────────────────────────────────────────┘
                                          │
                       ┌─────────────────────────────────────────┐
  phase 4+             │  libwz4tex   ·   libwz4geo              │
                       └─────────────────────────────────────────┘
                                          │
                       ┌─────────────────────────────────────────┐
  phase 2.4 ✓          │  wz4core — doc.cpp build.cpp basic.cpp   │
                       │           script.cpp  util/image.cpp     │
                       │           gui/{theme,palette}.cpp        │
                       └─────────────────────────────────────────┘
                                          │
   ════════════════════ the GUI seam ═════════════════════════════
                                          │
                       ┌─────────────────────────────────────────┐
  phase 2.1/2.2 ✓      │  wz4lib/doc_core.hpp                    │
                       │  generated *_ops.cpp  (wz4ops -headless)│
                       └─────────────────────────────────────────┘
                                          │
                       ┌─────────────────────────────────────────┐
  phase 1 ✓            │  altona_base  ·  altona_util            │
                       │  (types, math, serialize, system,       │
                       │   blank renderer, scanner)              │
                       └─────────────────────────────────────────┘
                                          │
   ════════════════════ the isolation seam ═══════════════════════
                                          │
                            altona_wz4/  (upstream, in place)
```

Above the GUI seam, `wPaintInfo`, `wGridFrameHelper`, `wCustomEditor` and the
Altona widget toolkit exist. Below it they do not — not as includes, not as
identifiers, not transitively.

`wz4core` sits *below* the seam despite compiling `doc.cpp`, because the painting
half of that file is preprocessed away by `sCOMMANDLINE` (A22). Its object files
are built with the poison header, so this is checked, not asserted.

### The three seams

**1. The isolation seam** — between our code and the 2012 dump. Upstream is
compiled *in place*, never copied or forked. Everything new lives in
`wz4port/`. Every upstream change is enumerated in `wz4port/patches/`, and the
invariant is that `git status` on `altona_wz4/` shows nothing not listed there.
The preferred order for making upstream work is: **include-path override →
shim header → patch → fork**, and a fork has not yet been needed.

**2. The GUI seam** — between the document model and the widget toolkit. Cut in
phase 2 by splitting `wz4lib/doc.hpp` into `doc_core.hpp` and `doc_gui.hpp`,
and by teaching `wz4ops` to generate only the half of each operator module
that compiles without a window system.

**3. The generation seam** — between `.ops` source and the two code paths that
consume it. `wz4ops` emits C++ (two flavours, differing only by `-headless`);
`opsmeta` will emit metadata JSON from the same parse tree. Anything that
determines *memory layout* is emitted by exactly one code path, on purpose.

### What the build enforces, and what only discipline enforces

Enforced by the build — breaking these fails compilation and names the file:

| Invariant | Mechanism |
|---|---|
| `doc_core.hpp` pulls no GUI, no shaders | `headless_core_gate` + `tests/gui_poison.h` |
| Generated headless modules pull no GUI | `headless_ops_gate` + the same poison header |
| `-headless` is inert when absent | `wz4ops_gate` regenerates the un-flagged tree |
| SSE2 → NEON translation is bit-exact | `simd_parity` (70,184 checks, via `ctest`) |
| `doc_core.hpp` declarations stay complete | `static_assert`s in `tests/headless_core.cpp` |
| Metadata offsets do not overlap or exceed their space | `opsmeta` validation, all 33 modules |
| The choice decomposition agrees with Altona | `opsmeta` cross-check against `sFindFlag`, 2,729 values |
| Every conditional symbol resolves | `opsmeta` errors rather than emitting `offset: -1` |
| The operator runtime links and runs with no GUI | `core_connect` (`ctest`), against `wz4core` built with the poison |
| Connection-from-geometry behaves as documented | `core_connect`'s 14 checks against `01-existing-model.md` §2.2 |
| The `.wz4` document format still reads | `load_*` — all six bundled documents, non-empty, via `ctest` |
| A load/save by this build does not damage a document | `identity_*` — class identity tally preserved, all six documents |
| The metadata reads back consistently | `checkmeta` — offsets, `continues` owners, choice masks, all 33 modules |

Enforced only by discipline — nothing catches a regression:

- The isolation invariant (`git status` on `altona_wz4/`). Reviewed by hand.
- The `-headless` extern filter is a **substring test on a signature**, not a
  type check. A new GUI type in `doc_gui.hpp` must be added to the list in
  `output.cpp` by hand.
- Nothing checks that a `.ops` `code` block does not reach for the editor. It
  fails at compile time in `headless_ops_gate`, which is late but not silent.

---

## Part 2 — decision record

Status is one of **standing**, **superseded** or **provisional**.

### A1 · macOS builds as `sPLAT_LINUX`, not a new platform id — standing

*Phase 1.* Altona's `sPLAT_LINUX` means "POSIX desktop", and 21 guards across
the tree spell that as `(sPLAT_WINDOWS || sPLAT_LINUX)`.

**Plan said:** add `sPLAT_APPLE`; the enum slot is even reserved for it.
**Instead:** macOS reports as `sPLAT_LINUX`. The two `pthread_t`-shaped guards
in `system.hpp` (`:370`, `:422`) are already correct for macOS pthreads.

**Why:** a new platform id means revisiting all 21 guards for no behavioural
gain, and every one is a chance to get it wrong. Genuine differences are
per-file and few.

**Consequence:** an upstream change avoided entirely. The cost is a permanent
piece of misdirection — a reader sees "LINUX" on a Mac — which is why it is in
`CLAUDE.md`, `progress.md` and `README.md`.

### A2 · `altona_config.hpp` lives outside `altona_wz4/` — standing

*Phase 1.* `base/types.hpp:67` does `#include "../altona_config.hpp"`, and
upstream's documented setup step is to create that file inside
`altona_wz4/altona/`.

A quoted include that fails to resolve next to its including file is retried
against each `-I` directory **with the relative path intact**. So with
`wz4port/compat/include` on the path:

```
compat/include/../altona_config.hpp  ==  compat/altona_config.hpp
```

**Consequence:** the config header never enters the upstream tree.
`compat/include/.keep` is load-bearing — the directory must exist. Verified
before being relied on, which is the only reason to trust it.

This is the template for the whole isolation seam: *an override beats a patch.*

### A3 · POSIX gaps closed by a force-included shim, not a forked system file — standing

*Phase 1.* **Plan said:** copy `system_linux.cpp` (2,274 lines) to
`compat/system_osx.cpp` and adapt it. **Instead:**
`compat/include/wz4port_posix_compat.h` supplies the glibc-isms (`lseek64`,
`mmap64`, `ftruncate64`, `stat64`, `strdupa`, `pthread_yield`, `O_LARGEFILE`)
and is force-included into every TU, plus a stub `compat/include/linux/joystick.h`
because macOS has no `/dev/input/js*`.

**Consequence:** the upstream change went from a 2,274-line fork to **four
lines** (`patches/01`), and `system_linux.cpp` compiles in place.

### A4 · SIMD translated by vendored `sse2neon`, and verified rather than trusted — standing

*Phase 1.* The texture generator's only external dependency is
`<emmintrin.h>`. `compat/include/simd_compat.hpp` selects `sse2neon.h`
(pinned v1.9.1, MIT) on arm64 and the real intrinsics elsewhere.

`tests/simd_parity.cpp` checks all 43 intrinsics that `wz3_bitmap_code.cpp`
uses against independent scalar models. It is a standalone target — no Altona,
no shared flags — deliberately, so it cannot be broken by build-configuration
drift.

**Why it matters more than it looks:** there is no reference oracle for this
port (see Part 5). Bit-parity on the SIMD layer is one of the few places where
correctness can be established absolutely rather than by eyeball.

### A5 · Source encoding normalised to UTF-8; `°` deliberately preserved — standing

*Phase 1, `patches/03`.* 30 files were Latin-1. clang **errors** on Latin-1
inside string literals, which would have blocked `wz4lib/script.cpp`.
Converted, verified character-identical against the git originals.

**The trap that made this urgent:** a single invalid UTF-8 byte makes `grep`
treat a file as binary and report *nothing*, silently. That produced a wrong
survey finding (`sCONFIG_GUID` reported as makefile-only when it is used at
`types.hpp:2143`). Measurement tooling was lying, and nothing said so.

**Not cleaned up:** `°` is the dot-product operator in the ASC shader
language — real syntax in ten `.asc` files, registered at
`shadercomp/asc_doc.cpp:305`, tokenised as `case 0xb0:` in
`wz4lib/script.cpp:2993`.

### A6 · The built Altona subset, and why `graphics.cpp` is not optional — standing

*Phase 1.* `altona_base` is the set upstream's own `base/Makefile.linux.boot`
builds: `types`, `types2`, `serialize`, `math`, `system`, `input2`,
`system_linux`, `graphics`, `graphics_blank`. `altona_util` adds `scanner` and
`scanconfig`.

Excluded on purpose: `sound.cpp`, `devices_win.cpp`, `windows.cpp` (Windows
bodies), `windows_xlib.cpp` (needs X11), and all four real renderers.

**Discovered at link time:** `graphics.cpp` and `graphics_blank.cpp` cannot be
dropped even for console tools, because `types.cpp` references
`sRender3DFlush()`. An attempt to defer them failed.

**Later consequence:** excluding `windows_xlib.cpp` is what makes
`sSetClipboard` unavailable, which is what forced `actions` blocks out of the
headless build (A13) — a phase 1 decision surfacing two stages later.

### A7 · Generated code lives in the build tree, and `wz4ops` is invoked from it — standing

*Phase 1.* `wz4ops` derives *both* its output paths *and* its generated
**function names** from the input path with the extension stripped. So
`wz4ops a/b/basic_ops.ops` emits `void AddTypes_a/b/basic_ops(...)`, which does
not compile.

`wz4_add_ops()` copies each `.ops` into `build/generated[-headless]/<subdir>/`
and runs the tool there with a bare filename, yielding `AddTypes_basic_ops` —
what `sREGOPS` (`doc_core.hpp:70-74`) expands to. This also keeps generated
output out of `altona_wz4/`, serving the isolation seam.

*Amended in phase 2.2:* Altona's shell parser treats the token after a
`-switch` as that switch's first parameter, so the switch must come **after**
the filename. `wz4ops -headless x.ops` silently prints the usage text.

### A8 · `doc.hpp` split into `doc_core.hpp` + `doc_gui.hpp` — standing

*Phase 2.1, `patches/04`, commit `e349a17`.* The structural change of the
project. Every operator source file included `doc.hpp`, which opened with
`gui/gui.hpp`, `gui/listwindow.hpp` and `util/shaders.hpp`.

`util/shaders.hpp` is what made this a hard blocker rather than untidiness: it
is **generated** from `shaders.asc` by the `asc` shader compiler, which is out
of scope. Before the split, a TU that merely included `doc.hpp` died:

```
wz4lib/doc.hpp:21:10: fatal error: 'util/shaders.hpp' file not found
```

**The survey said 18 GUI-touching lines. The real count was four declarations
plus two by-value members:**

| Needs the GUI | Because of |
|---|---|
| `wPaintInfo` | `sSimpleMaterial` — `util/shaders.hpp` |
| `wGridFrameHelper` | `sGridFrameHelper` — `gui/frames.hpp` |
| `wCustomEditor` | `sWindowDrag` — `gui/window.hpp` |
| `wEditOptions` | `sGuiTheme` — `gui/manager.hpp` |
| `wTreeOp`, `wPage` | `sListWindowTreeInfo<>` — `gui/listwindow.hpp` |

Everything that *looked* like a graphics dependency — `sViewport`,
`sTargetSpec`, `sGeometry`, `sMaterial`, `sTexture2D`, `sVertexSingle`,
`sVertexBasic`, and `sMaterialEnv` despite the name — is in
`base/graphics.hpp`, already present via the blank renderer. `sMessage` is in
`base/types2.hpp`.

**The predicted main risk cost nothing.** `wType`'s virtuals take
`wPaintInfo&` and `wClass` holds function pointers naming
`wGridFrameHelper&`, `sWindowDrag&` and `wCustomEditor*`. References and
pointers to incomplete types are legal in declarations, and nothing headless
dereferences them, so four forward declarations sufficed.

**Placement rule adopted:** `doc_gui.hpp` holds *exactly* what cannot compile
without the GUI or shader headers; everything else stays in core. This
overrode the plan, which had put `wEditOptions`, `wHitInfo` and
`wHandleSelectTag` on the GUI side — the first two are `wDocument` members **by
value** (and `EditOptions.MemLimit` drives the cache manager, while
`TreeInfo.Level/Flags` are part of `.wz4` serialisation), and `wHitInfo` has no
GUI dependency at all.

### A9 · Two PODs extracted out of `gui/` rather than the headers shadowed — standing

*Phase 2.1, same patch.* `sGuiTheme` and `sListWindowTreeInfo<>` are plain
serialisable data that merely *live* in GUI headers, and both are needed **by
value**, so forward declaration cannot work. Moved verbatim into new
`gui/theme.hpp` and `gui/treeinfo.hpp`, which their original homes now include.

**Rejected:** shadowing `gui/listwindow.hpp` and `gui/manager.hpp` from
`wz4port/compat/include/` the way `altona_config.hpp` is shadowed (A2).

**Why rejected, even though the project's stated order prefers an override to a
patch:** that ordering is about *risk*, and here the override is the riskier
option. Silently replacing two real upstream headers for **every** translation
unit is a landmine; a verbatim extraction leaves both headers' existing
consumers seeing exactly what they saw before. The rule is not "override
always" — it is "least surprising thing that works".

### A10 · The GUI seam is enforced by identifier poisoning — standing

*Phase 2.1.* Nothing about the include path keeps `doc_core.hpp` GUI-free
afterwards: the real `gui/` headers are still on it and, on macOS, they parse
cleanly. **A green build proves nothing.**

`wz4port/tests/gui_poison.h` is force-included into `headless_core_gate` and
`headless_ops_gate` and nowhere else. It `#pragma GCC poison`s three tokens:

| Token | Covers |
|---|---|
| `sWindow` | every header in `gui/` — all of them reach `gui/window.hpp`; appears nowhere in `base/` or `util/` |
| `sGui_` | `gui/manager.hpp`, in case that layering changes |
| `sSimpleMaterial` | the generated `util/shaders.hpp` |

Poisoning matches whole identifiers, so `doc_core.hpp`'s forward declaration of
`struct sWindowDrag` is untouched. `gui/theme.hpp` and `gui/treeinfo.hpp` are
deliberately *not* represented — they are the A9 extractions, and are allowed.

**It earned its keep on the first run** by catching a wrong assumption:
`sMaterialEnv` is declared in `base/graphics.hpp`, not `util/shaders.hpp`.
Poisoning it failed the gate on a header we legitimately need.

**Rejected:** a directory of stub headers shadowing all 16 `gui/` headers. More
files, more to maintain, and it makes the real headers unreachable for anyone
who later wants them.

### A11 · `wz4ops` gains `-headless`; the metadata tool stays separate — standing

*Phase 2.2, `patches/05`.* The phase plan preferred a *separate* tool reusing
`wz4ops`' parser and emitting both the headless `.cpp` and the metadata JSON,
specifically to avoid patching `wz4ops`. Measurement killed that preference:

- The parse tree **is** completely separable — `parse.cpp` and `doc.cpp`
  contain zero references into `output.cpp` or `wikitext.cpp`, and every node
  class is public. **But** `Document::Types` and `Document::Ops` were private,
  so a separate tool needed an upstream patch anyway. "No patch" was never on
  the table.
- Reimplementing the non-GUI half of `output.cpp` would duplicate ~600 lines of
  offset-sensitive emission — parameter packing, arrays, ties, string offsets,
  helper structs. **Its failure mode is silently misaligned parameter data**,
  which is precisely the bug class this project has no oracle to catch.
- The GUI-emitting parts of `output.cpp` turned out few and contiguous.

**Decision — split by responsibility:**

- **The headless `.cpp` comes from `wz4ops -headless`.** Same code path as the
  GUI output for everything that determines layout, so the two cannot diverge.
- **The metadata JSON comes from `wz4port/tools/opsmeta`**, linking only
  `parse.cpp` and `doc.cpp`. The JSON is our format and the editor's contract;
  keeping it in our tree lets the schema evolve without touching upstream.

`Document::Types`/`Ops` were made public to serve the second half.

**How much was GUI:** ~70% of the generated code.

| | default | `-headless` |
|---|---:|---:|
| `basic_ops.cpp` | 6,230 lines | 1,850 |
| `wz3_bitmap_ops.cpp` | 8,884 lines | 2,166 |

### A12 · The generator/`.ops` contract is a macro — standing

*Phase 2.2.* A `.ops` file's global `code` and `header` blocks are verbatim
C++, so the generator cannot reason about them. `-headless` emits
`#define WZ4_HEADLESS 1` at the top of the generated `.cpp`, and the `.ops`
files guard the offending lines with `#ifndef WZ4_HEADLESS`.

**Why a macro rather than new `.ops` syntax:** the non-headless output stays
byte-identical apart from the guard text itself, so the flag is provably inert
when off (A15). New syntax would have meant a parser change plus a `.ops`
change, for the same effect.

Four sites needed it — three more than predicted:

| Where | What | Why |
|---|---|---|
| `basic_ops.ops:15` | `#include "wz4lib/gui.hpp"` | the editor's own windows |
| `basic_ops.ops:623` | `wPaintInfo pi; sClear(pi);` in `Scene::Hit` | a **dead local**, never read. Guarding it is what keeps `Hit` headless |
| `basic_ops.ops:1168` | the whole `Screenshot` operator body | renders the viewport and compares against a reference image |
| `wz3_bitmap_ops.ops:11` | `#include "wz4lib/poc_ops.hpp"` | **entirely unused** — it mattered only because `poc` needs the ungenerated `util/shaders.hpp` |

Three headers also reached `doc.hpp` from `.ops` `header`/`code` blocks and now
include `doc_core.hpp`: `wz4lib/basic.hpp`, `wz4lib/poc.hpp`,
`wz4frlib/wz3_bitmap_code.hpp`. `poc.hpp` is the instructive one — it *mentions*
`wPaintInfo`, but only in a declaration, which the forward declaration
satisfies.

### A13 · `type` externals filtered by signature, not dropped wholesale — standing

*Phase 2.2.* A `type` block's `externals` are hand-written C++ pasted into the
generated file, so some cannot compile headless. The obvious rule — drop them
all — is **wrong**:

- `wz3_bitmap_ops.ops:39` — `extern void Init()` is a `wType` virtual the
  headless build needs; it calls `xInitPerlin()`.
- `basic_ops.ops:495`/`:617` — `extern void Hit(wObject *,const sRay &,
  wHitInfo &)` names nothing outside `doc_core.hpp`.

`Document::ExternIsGuiOnly()` skips an extern only when its return type or
parameter list mentions `wPaintInfo`, `wGridFrameHelper` or `wCustomEditor`.
That drops **12 of the 15** `type` externs across the two files, keeping
`GenBitmap::Init`, `MeshBase::Hit` and `Scene::Hit`. **Every skip is printed** —
the suppression is never silent.

`wHandle` is deliberately absent from the list: as a substring it would also
match the core type `wHandleSelectTag`. `wPaintInfo3D` is a typedef of
`wPaintInfo` and matches as a substring, which is wanted.

**Also excluded from the headless build, deliberately:**

- **parameter-panel `actions`** — editor-only by definition, and one calls
  `sSetClipboard`, which lives in `base/windows.hpp` and is implemented in
  `windows_xlib.cpp`, excluded back in A6;
- **the `Screenshot` operator** — headless sets `cmd->SetError(...)`;
- **`Bind*`/`OutputAnim`** script bindings, pending A16.

All three are revisitable when the new editor exists.

### A14 · The "guarded without reindenting" convention — standing

*Phase 2.2.* Two guards in `output.cpp` wrap ~130 lines in `if(!Headless)` (or
skip with `continue`) **without reindenting the body**, and say so in a
comment. This is deliberate: reindenting 130 lines to add one condition buries
the change in an upstream diff that reviewers must read.

The `continue` form carries a hazard, flagged in the code: everything from that
line to the end of the loop body is headless-excluded, so anything new that
should survive `-headless` has to go above it.

### A15 · `wz4ops_gate` is retained as the inertness proof — standing

*Phase 2.2.* `wz4ops_gate` regenerates the operator modules **without**
`-headless`, into `build/generated/`, and nothing compiles them. It looks
redundant next to `headless_ops_gate`. It is not: it is what proves the flag is
inert when off.

With the flag absent, the regenerated tree was diffed against the pre-patch
output. Every difference is either `#line` renumbering or the literal guard
text added to the `.ops` files (which are verbatim, so the guards appear in the
output). **No emitted code changed.** Delete the target and that guarantee goes
with it.

### A16 · Metadata is validated at emit time, not trusted — standing

*Phase 2.3.* `opsmeta` does not just serialise the parse tree; it refuses to write a file it
cannot stand behind. Three checks, all always-on:

| Check | Why it has to live here |
|---|---|
| Parameter offsets do not overlap and fit the declared count | `wz4ops` checks this in `OutputParaStruct` — **inside the C++ emitter, which `opsmeta` deliberately does not link** (A11). Without its own check the JSON could describe a layout the generated struct would have rejected, and the editor writes straight into `wOp::EditData` at whatever offset the metadata gives it |
| Every unique choice label round-trips through `sFindFlag` | The `options` decomposition is a re-implementation of Altona's parse, and 1,181 `flags` parameters depend on it |
| Conditional and `continue` symbols resolve | A typo would otherwise silently disable a visibility rule, or leave a widget editing offset −1 |

The `sFindFlag` cross-check is the notable one: this project has **no reference oracle**
(Part 5), so wherever a piece of upstream behaviour can be re-executed and compared against,
it should be. 2,729 choice values are checked on every build.

Labels appearing in more than one widget of the same option string are skipped, because
`sFindFlag` returns the first match and there is nothing to compare against. That is genuine
ambiguity in the format, not a gap in the check — the common case is `-` as a blank entry, as
in `"-|abs:*1-|sin"`.

### A17 · The metadata gate covers the whole corpus, not the two named modules — standing

*Phase 2.3.* `opsmeta_gate` emits metadata for all 33 `.ops` files, including the ~25 that
belong to out-of-scope subsystems and that nothing will ever read. It costs milliseconds and
raises coverage from ~450 parameters to 2,728.

**It found a crash on its first run** that the two gate modules never triggered: the JSON
writer had a fixed 16-level depth stack, and condition trees nest as deeply as the source
nests `if(...)` — two levels per expression node. The depth stack is now dynamic, on the
grounds that there is no defensible constant.

Generalisable: in a project with no oracle, *breadth of input* is one of the few substitutes,
and it is usually cheaper than it looks.

### A18 · Floats in the metadata go through libc, not Altona — standing

*Phase 2.3.* `sTextBuffer::PrintF(L"%.9f",...)` renders `4.0f` as `4.00000023` and `0.125f`
as `0.125000007`. Both are exactly representable; Altona's `sFloatInfo` formatter simply is
not correctly rounded past a few digits.

The metadata's stated purpose is to be reviewed by hand and diffed, so `json.cpp` uses
`snprintf`/`strtof` and prints at the shortest precision (6 to 9 significant digits) that
round-trips to the same float32. Output is `4`, `0.125`, `0.001`.

This is the first place the port has had to route *around* an Altona facility rather than
through it. Worth remembering when the editor needs to display numbers.

### A19 · `wExecutive` requires `script.cpp` — resolved, standing

*Answered in phase 2.4.* `wExecutive::Execute` drives `ScriptContext` directly —
`PushGlobal`, `ClearImports`, `AddImport`, `Run`, `FlushLocal` at
`doc.cpp:4050-4200` — and `wOp::GetScript` compiles a context per operator. The
scripting path is part of the executive, not a layer above it.

It costs nothing platform-wise: `script.cpp` has no GUI or graphics dependency
and compiled headless on the first attempt. So `wz4core` links it, and the
"exclude scripting" option is closed.

Note the asymmetry this creates: `-headless` (A11) still omits the generated
`Bind*` functions, so operators do not *expose* their parameters to scripts, but
the executive can still run script source attached to an operator. Revisit when
the editor needs the scripting UI.

### A22 · `doc.cpp` guarded with `sCOMMANDLINE`, not split — standing

*Phase 2.4, `patches/06`.* The phase survey said `doc.cpp` had "16 GUI-touching
lines out of 4,469". That counted `sGui->` references and missed that **the file
contains the entire `wPaintInfo` implementation** — ~840 lines under its own
"painting" banner. Third time a line count has understated the coupling (see
also A8, and the census in A17).

One line mattered out of proportion: `new AlphaMtrl` at `doc.cpp:109`, inside
`wPaintInfo`'s constructor, is the *only* use of `wz4lib/wz4shaders.hpp`, which
is generated by the `asc` compiler this port does not build.

**Upstream had already built the switch.** `doc.cpp` carried three
`#if !sCOMMANDLINE` blocks around its logging overlay, and `sCOMMANDLINE` is
`sCONFIG_OPTION_SHELL` (`base/types.hpp:605`) — which this port has defined for
every target since phase 1. The painting half is bracketed with the same guard.

**Why guarded and not split, when A8 split the header:** a `.cpp` has no
consumers. The header split was forced by every operator file including
`doc.hpp`; nothing includes a `.cpp`, so the only requirement is that the
compiled object be GUI-free. A brace costs one line where a move costs 900 and a
transcription risk. Same reasoning as A14.

Look for `sCOMMANDLINE` before inventing a mechanism — Altona was written for a
console build and the guards are already scattered through `base/`.

### A23 · Data definitions, not just declarations, had to leave `gui/` — standing

*Phase 2.4.* A8/A9 extracted `sGuiTheme` and `sListWindowTreeInfo` *declarations*
so the document model could compile. That was enough to compile and **not enough
to link**: the definitions were still in `gui/manager.cpp`, which pulls the
widget toolkit and a window system.

Two more extractions, same pattern, now of definitions:

| New file | Holds | Was in |
|---|---|---|
| `gui/theme.cpp` | `sGuiThemeDefault`, `sGuiThemeDarker`, `sGuiTheme::Serialize`, `::Tint` | `gui/manager.cpp` |
| `gui/palette.hpp` + `.cpp` | `sGuiPaletteColors[32][4]` | `gui/color.cpp` |

The palette is the interesting one. `wDocOptions::Serialize_` streams those
32×4 floats — they are **document format data**, and dropping them would shift
every following field and corrupt loading. They lived as
`static sF32 sColorPickerWindow::PaletteColors[32][4]`.

Solved without touching `gui/color.cpp`'s eight use sites, by making the class
member a **reference to array** bound to the extracted storage:

```cpp
static sF32 (&PaletteColors)[32][4];                                  // color.hpp
sF32 (&sColorPickerWindow::PaletteColors)[32][4] = sGuiPaletteColors;  // color.cpp
```

Two lines instead of ten, behaviour-identical for the original editor. Worth
remembering as a technique: a reference-to-array member can relocate storage out
of a class without disturbing any user of the name.

**Generalisation for later phases:** whenever a *declaration* is extracted to
break a compile dependency, check whether the matching *definition* needs the
same treatment. It will not show up until something links.

### A24 · `wNotifyHook` — the one abstraction added to upstream — standing

*Phase 2.4.* Three call sites (`doc.cpp` in `Connect()` and `ChangeR()`,
`build.cpp:629`) called `sGui->Notify(x)` to tell the GUI a memory range had
changed. It was the sole reason `build.cpp` included `gui/gui.hpp`; the include
even said `// for notify`.

```cpp
extern void (*wNotifyHook)(const void *ptr,sDInt bytes);
```

The signature deliberately mirrors `sGui->Notify(const void *,sDInt)`, so an
editor installs a one-line forwarder and nothing about the semantics changes.
Headless leaves it null.

This is the only place the port has *added* an abstraction to upstream rather
than extracting or guarding existing code, and it stays that way on purpose: a
function pointer with an identical signature is the smallest seam that removes
the dependency.

### A25 · `sCheckBreakKey` is an upstream POSIX gap, filled in `wz4port/` — standing

*Phase 2.4.* Declared at `base/system.hpp:780` for every platform; defined only
at `base/system_win.cpp:3050`, where it polls the PAUSE key. `system_linux.cpp`
never defines it. Not something this port broke — it would fail to link for any
POSIX consumer of `wExecutive`.

Filled by `wz4port/compat/altona_missing.cpp`, returning 0, rather than by
patching `system_linux.cpp`: it is our decision about what the function should
mean on a console, so it belongs in our tree.

It is a user-abort poll — `wExecutive::Execute` drains it before a build and
tests it per command (`doc.cpp:4034`, `:4047`), and `basic.cpp` polls it inside
long loops. **A CLI that wants Ctrl+C to interrupt a long generation should
install a SIGINT handler and report it here.** That is the natural home for the
feature and it is worth doing in phase 3, with `wz4gen`.

### A20 · Parameters live in three separate offset spaces — standing

*Phase 2.3.* Not a decision so much as a discovered constraint the schema had to expose.
`parse.cpp:651-694` runs **three independent offset counters**:

| Kinds | Space | Addressed by |
|---|---|---|
| anything with a `CType` | 32-bit words | the `Para` struct / `wOp::EditData` |
| `string`, `filein`, `fileout` | string slots | `wOp::EditString[n]` |
| `link` | link slots | `wOp::Links[n]` |

So word 0, string 0 and link 0 all exist in the same operator. The metadata sketch in
`02-target-model.md` §6 had a single bare `offset`, which would have had the editor writing
text into a float. Every parameter now names its space.

Two adjacent traps, same origin:

- **`char X[n]` consumes `(n+1)/2` words, not `n`** (`parse.cpp:684`). The schema emits the
  actual word count so no reader re-derives it.
- **`continue flags` carries `Offset == -1`**, because the parser skips allocation for it
  (`parse.cpp:678-679`). It still edits a real word — the earlier declaration of the same
  symbol — so `opsmeta` resolves it and emits `words: 0` alongside. Emitting −1 would have
  left the editor with a widget and nowhere to put the value.

### A21 · Conditionals turned out already lowered — standing

*Phase 2.3.* The phase plan called parameter conditionals "the one genuinely awkward item",
on the assumption that `Flags.choicename` and nested `if` blocks would have to be resolved
downstream. Reading the parser closed both:

- `parse.cpp:1016-1029` desugars `Flags.choicename` into `(Symbol & mask) == value` **while
  parsing**, resolving the choice against that parameter's option string via `sFindFlag`. The
  surviving tree contains only `EOP_BITAND`, `EOP_EQ` and integer literals.
- `parse.cpp:920-921` ANDs an enclosing `if` condition into the inner one, so each parameter
  carries one complete condition and there is no nesting to represent.

What was left was a five-node grammar — binary, unary, int, symbol, `input[n]`. The awkward
part was never the grammar; it was the 82 conditionals in the two gate modules alone, which
is why getting the five nodes right matters.

### A26 · A degradation path must record what it degraded — resolved, standing

*Phase 3, `patches/07`.* Altona handles an unregistered operator class by
substituting `UnknownOp` at read time (`doc.cpp:1854-1863`). Good behaviour, and
the reason a document full of out-of-scope operators still loads with its texture
subgraphs intact.

But the substitution was **not recorded**, and on write `Serialize_` emitted the
substituted name. Any build that did not know every module silently rewrote every
unrecognised operator as `UnknownOp` and damaged the document permanently.

**A graceful-degradation path that discards what it degraded is fine for a reader
and destructive in a writer.** Phase 3 is the first stage that writes, which is
why this surfaced now and not in phase 2.

Fixed by retaining `wOp::ForeignClass`/`ForeignType` and writing those back.
The guarantee is precise: **identity and geometry survive; parameter content of
unregistered operators does not.** The reader discards their parameter words,
strings, link names and array data in four separate places, because `UnknownOp`
declares no storage — and carrying those through is deliberately not done, being
fidelity for render-graph, material and effect operators this port does not
support.

Two things this cost me, both recorded because the error direction matters:

- I first sized it as "two fields and three lines", having looked only at the
  class name and not at what else the reader skips.
- I first called the §4.4 round-trip guarantee "not achievable", which was a
  **misreading of my own phase plan** — the 3.4 gate already scoped it to "the
  subgraphs whose classes we have registered". Re-read the gate before declaring
  it unmeetable.

The immediate payoff was unrelated to writing: retaining the name made
"which modules would I have to register to load this?" answerable, which is how
the phase-3 reachability risk got measured and retired (817 `GenBitmap`
operators reachable across the six documents).

### A27 · Classify errors by cause, not by message — standing

*Phase 3.3.* Loading `example.wz4` with only `basic` registered produces 638
"errors". Reported raw, that number says the port is broken. It is not: two
distinct messages share one root cause.

- `UnknownOp` is declared with **zero inputs** (`basic_ops.ops:239`), so any
  operator it replaced still sits in geometry feeding it — *"too many inputs"*,
  637 times.
- `UnknownOp`'s output type is `AnyType`, which satisfies no typed input, so a
  **real** consumer above it fails — *"input has wrong type"*, once per
  `MakeTexture(BitmapBase)` fed by an unregistered generator.

`wz4gen` therefore classifies by cause: an operator is placeholder fallout if it
**or any of its inputs** is an `UnknownOp`. What is left is 15 connection errors
and 1 unexplained across 7,090 operators, and all of those are properties of the
documents — dangling `Load` names and duplicate store names in decade-old
scratch files.

The first classification attempt keyed on the message and mislabelled the
`MakeTexture` failures as "the ones that matter". Diagnostics that a human will
act on need the residual to be genuinely residual, or the number gets ignored.

### A28 · The metadata is mandatory, not a convenience — standing

*Phase 3.1a.* `wClass` carries `ParaWords` and `ParaStrings` — a *budget* — and
knows nothing else about its parameters. No names, no kinds, no offsets. That
description only ever existed inside the generated `MakeGui`, which `-headless`
omits (A11).

So anything that turns text into parameter words needs the stage-2.3 metadata.
It is not an editor convenience that happened to arrive early; it is **the only
parameter description that survives headless**, and everything above the runtime
depends on it. That reframes 2.3 from "a deliverable for the editor" to "the
layer the CLI and the editor both stand on".

Consequence for structure: `wz4port/wz4t/` holds a *general* JSON reader rather
than one shaped to this schema, because the ImGui editor reads the same files and
would otherwise reimplement it.

### A29 · Validate a format from the consumer side too — standing

*Phase 3.1a.* `opsmeta` validates what it writes (A16). `wz4gen checkmeta`
validates what can be *read*, which is a different question, and it caught
something opsmeta could not: the reader was silently skipping the `array` block,
so it saw 2,667 parameters where the emitter had written 2,728.

**Two counts that should agree, disagreeing by 61, with neither side
complaining** — that is how a bug survives a whole phase. The fix was to load
array rows; the lesson is that a producer's self-check and a consumer's
self-check are not substitutes.

`checkmeta` also asks two questions only a reader would think to ask: does a
`continues` parameter land on a word its owner actually declares, and does every
choice value fit inside its widget's mask once shifted. A choice escaping its
mask would have the editor writing bits belonging to a neighbouring control.

A schema is only proven useful once something reads it in anger. 370 classes,
2,728 parameters, 3,282 choice values, 0 problems — and the parameter count now
matches the emitter's exactly.

### A30 · `sArray::AddMany` does not construct — standing hazard

*Phase 3.1a, found by segfault.* Altona's `sArray::AddMany` returns **raw
memory**: no constructors run. `AddManyInit` is no better — it does
`r[i] = Type()`, an *assignment* into uninitialised storage, so for any element
that owns memory `operator=` reads garbage pointers.

An `sArray<T>` where `T` contains an `sArray` therefore cannot be filled with
`AddMany`. It compiles, and it crashes.

The rule for our code: **element types that own storage are held by pointer**
(`sArray<T *>`, `new T`), with an explicit destructor loop. Plain-old-data
elements — ints, `sPoolString` (a bare pointer), `sString<n>` (a fixed buffer) —
are safe with `AddMany` provided every field is assigned before it is read,
which is why `opsmeta` and `wz4gen`'s tally structs are fine.

---

## Part 3 — where inference lost to measurement

The project rule "verify, don't assume" was not adopted on principle. It was
adopted because of this list.

| Assumption | What measurement showed |
|---|---|
| `sCONFIG_GUID` is only used in makefiles | Used at `types.hpp:2143`. The survey `grep` had silently skipped the file as binary — one bad Latin-1 byte (A5) |
| `graphics.cpp` is droppable for console tools | `types.cpp` references `sRender3DFlush()`. Found at link time (A6) |
| A new `sPLAT_APPLE` is the clean way to add macOS | 21 guards already mean "POSIX desktop"; a new id makes 21 places to get wrong (A1) |
| macOS needs its own copy of `system_linux.cpp` | Four lines and a force-included shim (A3) |
| `doc.hpp` has 18 GUI-touching lines | Four declarations and two by-value members. The by-value ones were the surprise (A8) |
| `sMaterialEnv` comes from `util/shaders.hpp` | `base/graphics.hpp`. Caught by the poison tripwire on its first run (A10) |
| `wz4ops`' parse tree can be reused without a patch | Fully separable, but `Types`/`Ops` were private (A11) |
| One `.ops` guard will be enough | Four, including a dead local and an unused include (A12) |
| Dropping all painting externs is safe | Would have dropped `GenBitmap::Init()` and both `Hit`s — 3 of the 15 are keepers (A13) |
| A green build proves the GUI seam holds | `gui/gui.hpp` parses fine on macOS. Only poisoning proves anything (A10) |
| Conditionals will be the awkward part of the metadata | Already lowered by the parser. The awkward parts were three offset spaces and a float formatter (A20, A18, A21) |
| Grepping the `.ops` files gives a widget census | It counted commented-out operators and missed modifiers in non-canonical order. The parser's own answer differs substantially (A17) |
| Altona's `%f` can be trusted for a data file | Renders `4.0f` as `4.00000023` (A18) |
| Altona's `%g` exists | It does not. An unknown format falls through to PrintInt, so `0.125` printed as `0` (A18) |
| `wClass` describes its own parameters | It carries a word budget and nothing else (A28) |
| `sArray::AddMany` constructs its elements | Raw memory. `AddManyInit` assigns into raw memory. Both crash on element types owning storage (A30) |
| `doc.cpp` has 16 GUI-touching lines | It contains all ~840 lines of `wPaintInfo`. Third time a line count understated coupling (A22) |
| Extracting a declaration is enough to decouple | It compiles; it does not link. The definitions needed extracting too (A23) |
| `script.cpp` might be excludable | `wExecutive::Execute` drives `ScriptContext` directly (A19) |
| A headless build needs a new mechanism to strip painting | `sCOMMANDLINE` already existed and was already defined here (A22) |

---

## Part 4 — latent hazards found but not yet triggered

Recorded here because they are invisible until they bite.

**`wClass::Anims` does not exist.** `output.cpp:1425` emits
`cl->Anims.AddMany(1)->Init(...)` for every entry in `Op::AnimInfos`, but
`wClass` has no `Anims` member. It compiles today only because **the parser
never populates `AnimInfos`** — the `anim` keyword sets `PF_Anim` on a
parameter (`parse.cpp:497`) or an `IE_ANIM` link method (`:783`), and nothing
fills the array. Anyone who makes the parser fill it will get a compile error
in generated code with no obvious cause.

**The extern filter is textual.** A new GUI-only type added to `doc_gui.hpp`
will not be recognised by `ExternIsGuiOnly()` until it is added to the list in
`output.cpp` by hand. The failure is loud (`headless_ops_gate` breaks) but the
cause is not.

**`compat/include/.keep` is load-bearing.** Deleting it breaks A2 in a
confusing way — the include-path retry needs the directory to exist.

**CMake deduplicates bare `-include` flags.** A second `-include` on a target
produces two bare tokens, CMake removes the "duplicate", and the surviving flag
steals the wrong path. The error is
`cannot specify -o when generating multiple output files`, which says nothing
useful. Use the `SHELL:` prefix to keep flag and argument together.

---

## Part 5 — standing architectural risks

**No reference oracle.** There is no working Werkkzeug4 build to diff against,
and the bundled `.exe` is x86 Windows. Golden images from phase 4 will capture
*our* behaviour, not the original's — a plausible, stable port bug would pass.
This is the single largest correctness risk in the project, and it is why A4
(bit-parity) and A11 (one code path for layout-determining emission) are shaped
the way they are. Generating true reference output on a Windows machine remains
worthwhile as a one-off; the `.wz4t` text format (phase 3) exists partly to
make that cheap.

**x86-64 SIMD parity unverified.** `simd_parity` passes on arm64, where it
exercises `sse2neon`. It must also run on x86-64 Linux, where it exercises
native SSE2. The two must agree.

**Metadata schema frozen at version 1, but not yet golden-tested.** Stage 2.3
fixed the JSON the entire editor UI is downstream of, against the complete
widget inventory in `01-existing-model.md` §5.2 rather than against what phase 4
happens to need. It is deterministic and diffable — but `build/meta/` is
gitignored, so nothing yet *fails* when the shape changes unintentionally.
Phase 4 already plans golden outputs and a runner; the schema golden belongs
there.

**Three widget kinds are represented but unexercised.** `bitmask`, `custom` and
`tie` are in the schema because the DSL supports them, but no `.ops` file in the
tree uses any of them — all 383 emitted `ties` arrays are empty. The first real
use should be treated as new code, not as covered ground.

**Upstream footprint is 48 files and will keep growing.** Two categories are
inert (30 encoding-only, 2 genuine language errors); the other 16 are structural
and are the ones a future merge would conflict on. Upstream is a frozen 2014
dump, so this is a bookkeeping cost rather than a merge risk — but the
enumeration in `wz4port/patches/` is the only thing keeping it honest.
