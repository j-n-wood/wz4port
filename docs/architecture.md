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
  phase 4 ▸ / 6        │  wz4tex ✓ (34 ops)   ·   libwz4geo      │
                       └─────────────────────────────────────────┘
                                          │
                       ┌─────────────────────────────────────────┐
  phase 3 ✓            │  wz4t — json, meta, .wz4t read/write     │
                       │  wz4gen — list describe convert identity │
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
| The texture engine evaluates to a real bitmap | `tex_smoke_*` — five operators; `Flat` must be uniform, the rest structured |
| Array rows survive a `.wz4t` round trip | `wz4t_round_tex` — rows compared word for word |
| Connection-from-geometry behaves as documented | `core_connect`'s 14 checks against `01-existing-model.md` §2.2 |
| The `.wz4` document format still reads | `load_*` — all six bundled documents, non-empty, via `ctest` |
| A load/save by this build does not damage a document | `identity_*` — class identity tally preserved, all six documents |
| The metadata reads back consistently | `checkmeta` — offsets, `continues` owners, choice masks, all 33 modules |
| `.wz4t` parses, connects, and refuses bad input | `wz4t_read` — 19 checks, 6 of them rejections |
| `.wz4t` round-trips every parameter word, and text is correct | `wz4t_round_*` — two cases, including a non-ASCII assertion against a literal |
| `.wz4` → `.wz4t` → `.wz4` preserves identity, geometry, parameters and the graph | `docround_*` — all six bundled documents |

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

### A31 · A reader that accepts anything is worse than none — standing

*Phase 3.1b.* `.wz4t` exists to hold test cases a reviewer can read. If the
parser silently ignored a misspelled parameter name, the case would quietly test
the operator's *default* instead of the value the author wrote, and it would pass
for the wrong reason forever.

So the reader refuses, and the gate tests the refusals as first-class cases: a
missing header, an unknown version, an unknown class, a misspelled parameter,
too many values for a parameter, an op with no position. Six of the nineteen
checks in `wz4t_read` are about rejection.

The general point for the phases that follow: for a format whose purpose is
testing, **the error paths are part of the contract**, not an afterthought.

### A32 · A fresh `wDocument` already owns a page — standing hazard

*Phase 3.1b.* `wDocument`'s constructor calls `DefaultDoc()` (`doc.cpp:2514`,
`:2609`), which appends one default-named page and connects. So a document is
never empty, and reading a two-page file naively yields three pages.

Caught in the reader; it matters for the **writer**. Unaddressed, a
`.wz4` → `.wz4t` → `.wz4` round trip would gain a stray empty page on every pass
— a slow corruption that a single round trip would not reveal. The reader now
takes the default page over for the file's first `page`, provided it is still
empty and untouched.

Worth generalising: when a round trip is the acceptance criterion, **look for
state the constructor creates**, not just state the format carries.

### A33 · A stable round trip is not a correct one — standing

*Phase 3.2.* The `.wz4t` round-trip gate compared every parameter word, string
and link across read → write → read, and required two writes to be
byte-identical. It passed on a case containing `café °C — ΔΣ 中文` **while
storing `cafÃ©`**.

`sLoadText` decodes UTF-8 only on finding a BOM (`system.cpp:1080`); otherwise
each byte becomes one character. A hand-written file has no BOM, so it read as
Latin-1 — and the writer then re-encoded those characters as UTF-8. **The
corruption is idempotent after the first pass.** Every comparison in the gate is
between pass 1 and pass 2, so every comparison agreed.

Fixed by decoding UTF-8 in the reader regardless of BOM (`sLoadFile` +
`sCopyStringFromUTF8`) and writing `sSaveTextUTF8`. `sSaveTextAnsi` truncated
each character to a byte and produced output `grep` reported as **binary** —
which alone defeats a format whose purpose is being read and diffed.

The durable fix is in the test: it now asserts an **actual character value**
against a literal the compiler encodes, independently of any file.

**Generalise this.** A round-trip test compares the system against itself, so it
cannot see an error the system applies consistently. Any such test needs at
least one assertion against a value from outside the loop. The same reasoning
already applies elsewhere in this project — it is why `opsmeta` cross-checks
choice decoding against Altona's own `sFindFlag` (A16) rather than against
itself, and why the absence of a reference oracle (Part 5) is the standing risk
it is.

### A34 · Do not use Altona's numeric or text conversions across a format boundary — standing

*Established in 2.3, confirmed twice more in 3.2 and 3.4.* Three independent
failures, one cause:

| Facility | What it does | Found in |
|---|---|---|
| `sFormatStringBuffer` `%f` | Not correctly rounded: `4.0f` prints as `4.00000023` | 2.3 (A18) |
| `sFormatStringBuffer` `%g` | Does not exist; falls through to `PrintInt`, so `0.125` prints as `0` | 3.1a |
| `sLoadText` | Decodes UTF-8 only with a BOM; otherwise byte-per-character | 3.2 (A33) |
| `sSaveTextAnsi` | Truncates each character to a byte; output reads as binary | 3.2 |
| `sScanner::ScanFloat` | Not correctly rounded: loses one ULP on a full-precision value | 3.4 |

None of these matter inside Altona, where the values are display text. All of
them matter at a boundary where a value must survive a write and a read.

`wz4port/wz4t/json.hpp` holds the replacements — `wFormatFloat`, `wParseFloat` —
and the readers decode UTF-8 themselves. **Use those.**

The 3.4 instance is the instructive one. A one-ULP change is invisible in a diff
of the text, and 5 of the 6 documents passed anyway; only comparing raw
parameter words caught it. Phase 4's golden images would have drifted for a
reason nobody would have thought to look for in the *scanner*.

### A35 · Graceful degradation is a mode, not a default — standing

*Phase 3.4.* `.wz4t` → `.wz4` was impossible until the reader could accept a
class this build cannot load, because the writer emits those operators by name
and the reader rejected them (A31, deliberately).

The resolution is a flag, `wWZ4T_ALLOWUNKNOWN`, **off by default**:

| Caller | Mode | Because |
|---|---|---|
| hand-written case | strict | an unknown class is a typo, and silence would let the case test nothing |
| `wz4gen convert`, document round-trip | lenient | the unknown classes are real operators from out-of-scope subsystems |

Same input, same parser, opposite correct answers — so leniency belongs to the
*caller's intent*, not to the format. Worth remembering when the editor loads a
user's document (lenient) versus a regression case (strict).

### A36 · A packed choice word takes one value per control, and labels win — standing

*Phase 4.1.* `GenBitmap.Size` is a single `flags` word holding **two** controls,
at shifts 0 and 8, each offering the labels `"1"` … `"8192"`. That one parameter
breaks two naive assumptions at once:

- **`Size = 64, 64` is two values for one word**, not two words. The reader
  assigns comma-separated values positionally, one per control — which is what
  `02-target-model.md` §4.2 showed all along. It also dissolves the label
  ambiguity that the writer previously had to bail out of (A16): each value
  resolves within its own control, so two controls sharing a label is fine.
- **A numeric label is a label, not a number.** Label `"64"` has control value 6.
  So a bare `64` in the text means the label, and the writer must always emit the
  **label** — emitting the raw value 3 for `"8"` would read back as label `"8"`
  and silently change the texture size.

Both rules are shared between reader and writer through one `wGatherWidgets`
helper, because getting them to disagree is exactly how a round trip corrupts
data while looking fine. That helper takes the parameter *list* explicitly —
an array row is a separate word space with its own list, and searching the wrong
one silently finds no widgets at all.

### A38 · A row's "default" depends on its neighbours, so rows state everything — standing

*Phase 4.1.* `.wz4t` omits any operator parameter equal to its default, which is
safe because the reader runs `SetDefaults` first and both sides read the same
metadata.

Array rows cannot work that way. `wOp::AddArray` calls the generated
`SetDefaultsArray`, which sets each field's default and then **linearly
interpolates every float field between the neighbouring rows**
(`output.cpp:681-696`) — the behaviour that makes inserting a gradient key land
halfway between its neighbours instead of at a default. So "the default" for a
row field is a function of the rows around it.

Omitting a row field would therefore make a file's meaning depend on row order,
and a round trip could shuffle values without changing any text. The writer
emits **every** field of every row instead.

Generalisable: default-omission is only sound where the default is a constant.
Check that before shortening any serialised form.

### A37 · Check for a local definition before extracting a "missing" symbol — standing

*Phase 4.1.* `genvector.cpp` produced eight errors: two "unknown type name
`__forceinline`" and six "use of undeclared identifier `sMulShift12`". I searched
the tree, found `sMulShift12` defined only as a file-local function in a
*different* translation unit (`util/rasterizer.cpp`), concluded the file had
never compiled, and started designing a header extraction.

Both helpers were defined in `genvector.cpp` itself, ten lines above the first
use. Clang could not parse the definitions because the return type was preceded
by an unknown keyword, so the call sites failed too. **One cause, eight errors,
and the six loudest pointed at the wrong file.**

The habit worth keeping: fix the *first* error and re-run before believing the
rest. The same shape appeared in phase 2 — patch 02's five bogus "private
member" errors all came from one rejected friend declaration.

### A39 · A test asserts on the artefact, not on the exit code — standing

*Phase 4.2.* The PNG tests could have been four `PASS_REGULAR_EXPRESSION`
matches on `wz4gen`'s `wrote <path>` line. That would have been cheaper and
wrong: `sImage::SavePNG` returning true is a statement about a function call,
not about what is on the disk.

`tests/tex/render_png.cmake` deletes any previous output first, runs the render,
then checks the file exists, is over 256 bytes, and begins with the PNG
signature. The extra checks are three lines of CMake and they immediately caught
something a stdout match never would have: **`SavePNG` does not create the
output directory**, so the first run of all four tests failed on a missing
`build/tex-png/`. Real behaviour, found by asserting on the artefact.

Same shape as A33, where a round trip passed while mangling text. Both times the
test was checking the wrong end of the operation.

### A40 · Assert on what the viewer will show, not on what the buffer holds — standing

*Phase 4.3.* Four operators — `Color sub`, `Color invert`, `Merge sub`,
`Mask sub` — take the alpha channel to zero, because they operate on all four
channels at once. The bitmap is fine; the *PNG* is fully transparent, and a
transparent PNG **displays as plain white**.

So the reviewable artefact is indistinguishable from an operator that filled the
image with white, and indistinguishable from one that did nothing. Three of the
four cases were written expecting visible output and the first was reviewed as "a
white square" before the cause was found. Nothing in the pipeline objected: the
size was right, the checksum was stable, the content was "structured", the file
was a valid PNG.

Two rules came out of it:

1. **The tool reports the alpha range**, so a blank render explains itself
   instead of looking like a boring image.
2. **Every case must not render blank unless it declares that it does.** The
   negative assertion is the load-bearing one — `REJECT` in
   `render_png.cmake` — because the positive checks all passed.

The threshold matters and is not `== 0`. `Mask sub` leaves alpha at `0x0001` of
`0x7fff`, which is exactly as invisible as zero and passes an equality test. The
test is `amax < 0x0100`: what survives `CopyTo`'s narrowing to 8 bits, because
that is what reaches the file.

Generalisable, and the third instance of the same shape after A33 and A39: when
the deliverable is *rendered* by something else — a viewer, a browser, a
terminal — correctness lives at that boundary, not at the buffer.

**Why the engine does this, established later in phase 5.** None of the
arithmetic kernels special-case alpha; they operate on all four 16-bit lanes at
once. `BI_SUB` is `_mm_subs_epu16(a,b)` and `BI_ADD` is `_mm_adds_epi16(a,b)`
(`wz3_bitmap_code.cpp:770` and `:760`). For two opaque inputs alpha is `0x7fff`
on both sides, so **`sub` always yields zero alpha** while **`add` survives by
luck**: signed saturation clamps at `0x7fff`, which happens to be exactly the
value that means opaque.

And it went unnoticed for the life of the original tool because **its preview
ignored alpha**: `wPaintInfo::PaintTex2D` draws through a plain
`sSimpleMaterial` with no blend flags (`doc.cpp:129`), with the alpha view as a
separate toggle. An artist saw the RGB subtraction they expected.

**And alpha is attached deliberately, by an operator whose job that is.**
`Merge`'s `alpha` mode masks with `{0xffff,0xffff,0xffff,0}`
(`wz3_bitmap_code.cpp:808`): it keeps input 0's three colour lanes, clears its
alpha, and substitutes input 1's *luminance*. That is literally "add an alpha
channel to an RGB image". Measured from outside, `merge_alpha` against its own
input reads `b 0, g 0, r 0, a 195` — every colour channel byte-identical, alpha
the only thing that moved.

`premul alpha` is the same constant plus a trailing `PreMulAlpha()`, so the two
are not the duplicate the mode table suggests but a designed pair: attach an alpha
channel, or attach one and premultiply it for compositing.

This was corroborated independently by the user's recollection of working with
Werkkzeug4 — that alpha-related examples were treated specially, adding an alpha
channel to an RGB image as an explicit step. Worth recording, because a
first-hand memory of how a dead tool was *used* is not recoverable from the source
and it is what turns three separate code readings into one coherent design.

So the honest reading is not "four operators are broken" but **alpha is not a
carried channel in this engine — it is attached late and on purpose**, and the
original's presentation matched that. The editor's preview therefore defaults to
`rgb` as the original did, with `rgba` and `alpha` a click away. Showing a
perfectly good RGB result as an empty pane is accurate and useless.

### A41 · Verify an operator's input order before writing the case, not after — standing

*Phase 4.3.* `Mask(a,b,mask)` is really `Mask(mask,a,b)`: the code does
`out = GRAY(in0)` and blends `in1` with `in2` by it. Written the way the name
implies, the case rendered a smooth blue-to-lavender ramp — a perfectly
plausible image in which one of the three inputs made no contribution at all.

It was caught by eye, not by any assertion, and only because the case's comment
had predicted "red to blue" specifically enough to be contradicted. A comment
saying "a blend of the inputs" would have passed.

Two habits followed, and both paid immediately:

- For every multi-input operator, read the `code {}` block before writing the
  case. Doing that for `Bump` established (surface, normals) rather than the
  reverse, and it worked first time.
- **Write the prediction before looking at the output**, precisely enough to be
  wrong. Five cases in this stage were corrected because the image contradicted
  a specific claim; a vaguer claim would have been satisfied by all of them.

Related: A36's lesson about packed choice words, and the same "measure it"
instinct as Part 3.

### A42 · Determinism is a compiler flag, and the default is against you — standing

*Phase 4.4.* Building an x86-64 slice and running the texture suite against the
arm64-generated goldens diverged on 8 of 87 cases. The suspect was sse2neon. It
was not sse2neon.

clang defaults to `-ffp-contract=fast`, which fuses `a*b+c` into a single FMA
wherever the target has one. **arm64 always has one; the x86-64 target did not.**
An FMA rounds once where the two separate operations round twice, so the two
architectures computed float results one ULP apart — and the texture engine
quantises floats into 16-bit fixed point, which turns one ULP into a different
sample. Every divergent case was float-touching: two `sFPow` gamma tables,
`Unwrap`'s coordinate maths, all the lighting.

`-ffp-contract=off` is now set project-wide in `altona_flags`, **for determinism
rather than performance**, and the comment there says so — otherwise it reads
like a stray optimisation flag and someone removes it.

Two general points:

- A cross-architecture bit-parity claim is a claim about the *compiler
  configuration*, not only about the source. It cannot be inherited from "we use
  the same code".
- `simd_parity` — 43 intrinsics against scalar models — passed on both
  architectures throughout. It structurally could not see this, because none of
  it was in the intrinsics. **A unit-level parity test does not subsume an
  end-to-end one**; here the end-to-end test was the only one that could fail.

### A43 · A golden must cover the pipeline's precision, not the artefact's — standing

*Phase 4.4.* The pipeline is 16 bits per channel; the PNG is 8. So a golden PNG
is blind to any change confined to the low half of every pixel.

`MakeWz3Bitmap` is the proof: it requantises its input through an 8-bit `sImage`,
which moves the checksum from `a7af9504d91e1410` to `dbb910e7e4b14596` while
leaving the written PNG **byte-identical** to its source. An image-only golden
would have called that operator a permanent no-op.

It was not a hypothetical. **7 of the 8 cross-architecture divergences above had
byte-identical images** and were caught only by the checksum. An image-only
golden would have reported full parity and the FMA problem would have shipped.

So each golden is an image *plus* a locked report line carrying a checksum over
all 16 bits, and the report is checked first. Generalisable: when the stored
artefact is lower-precision than the computation, store a digest of the
computation too.

Corollary, learned by breaking it deliberately: **verify that a golden can
fail.** Both paths were tested — a one-hex-digit edit to a checksum, and a
swapped image — before the lock was trusted. A golden that cannot fail is worth
nothing, and nothing else in a green suite tells you which kind you have.

### A44 · An opaque private pointer is an implementation seam — standing

*Phase 4.5.* `sFont2D` was the last platform blocker in the texture library:
GDI and X11 backends, none for macOS. The expectation was a patch to Altona's
font layer.

It needed none. The class is declared as

```cpp
class sFont2D { struct sFont2DPrivate *prv; public: /* methods */ };
```

and its methods are *defined* only in `base/windows.cpp` and
`base/windows_xlib.cpp` — neither of which this build compiles. So the symbols
were simply absent, and `wz4port/compat/font_freetype.cpp` defines them,
including `sFont2DPrivate` itself. The header is untouched; the whole backend
lives outside `altona_wz4/`.

Worth generalising, because this codebase uses the idiom widely: **a
forward-declared private struct plus out-of-line methods is a substitutable
backend, whether or not anyone intended it as one.** Before patching a platform
layer, check whether its implementation is merely *missing* rather than *wrong* —
missing is much easier to replace than wrong.

The two upstream changes 4.5 did need were about *reaching declarations*, not
implementing anything: relocating `enum sGuiColor` out of a GUI header, and two
guarded includes. Recorded as `patches/09`.

### A45 · A dependency is "present" only if it links — standing

*Phase 4.5.* `find_package(Freetype)` succeeds when cross-compiling the x86-64
slice and returns Homebrew's **arm64-only** dylib. Configuration reports success,
compilation succeeds, and the link fails with a wall of undefined `FT_` symbols.

Detection now compiles *and links* a two-line program against the found library.
That is the only check that can tell "a header and a file exist" from "this
dependency works for this target", and it makes the optional-dependency fallback
actually function: the parity build drops to the stubbed `Text` cleanly instead
of failing to build at all.

Third instance of one shape, after A39 (assert the artefact, not the exit code)
and A43 (assert the pipeline's precision, not the artefact's): **assert the thing
you need, never a proxy for it.** A found path is a proxy. An exit code is a
proxy. A lower-precision artefact is a proxy.

### A48 · A working build is not evidence that the commit builds — standing

*Phase 5.4.* A new file was named `editor/edit_ops.cpp`. `.gitignore` excludes
`*_ops.cpp` and `*_ops.hpp`, because that is what `wz4ops` and `opsmeta` generate
and they land in several directories. So `git add -A` **silently dropped it**.

Everything kept working: the file was on disk, the build compiled it, all 130
tests passed. The commit would have been broken for everyone else, and nothing in
the local loop could have said so — the build reads the working tree, not the
index.

Two habits from it:

- **After staging new files, check the index and not the disk.** `git status`
  showing a clean tree is the *symptom* here, not the reassurance:
  `git ls-files <dir>` is what actually answers "will this build after a clone".
- **Rename rather than add a negation.** The ignore pattern is broad on purpose;
  a source file whose name trips it is a trap for the next person too. The file
  is now `docedit.cpp`, and the reason is written at the top of it.

Same shape as A39 and A45 once more: the local build is a *proxy* for the
committed build, and this is the case where the proxy and the thing disagree.

**It happened again in 6.3a**, to `tests/mesh_ops.cpp` — the natural name for a
per-operator suite, and it matches `*_ops.cpp` exactly as `edit_ops.cpp` did.
Caught the same way: the file was absent from `git status --short` after
`git add -A` while 141 tests passed off the untracked copy. Now
`tests/mesh_cases.cpp`.

Twice is a pattern, and the habit above was not enough on its own — noticing an
*absence* in a 16-line status listing is not a reliable check. What actually
catches it is asking the question the other way round:

```sh
git status --porcelain --ignored=matching <dir> | grep '^!!'
```

Anything listed there that is not generated output is a file the commit will not
carry. Worth running before staging any stage that adds source files, because the
two names that have tripped it — `edit_ops.cpp`, `mesh_ops.cpp` — are both the
*obvious* name for what they contain, so a third is likely.

### A49 · Prefer a snapshot to an inverse — standing

*Phase 5.7.* `07-phase-texture-gui.md` specified command-pattern undo: an inverse
per operation. It was rejected on contact with the code, and the reasoning
generalises.

**Upstream already had a whole-page serialiser.** `wPage::Serialize` covers the
ops array and, through `wOp::Serialize`, every operator's parameter words,
strings, link names and array rows. It exists for the clipboard, so it is code
that already works and is already exercised on the format that matters. The
serialise-to-memory idiom came from `sSetClipboardObject`
(`base/windows.hpp:92`).

An inverse per command is a **correctness surface that grows with every editing
feature**, and it is wrong in precisely the combinations nobody tests — undo a
resize that also broke a connection, undo a paste that partly collided. A
snapshot cannot be wrong about what it captured. It can only be wrong about
*what it captures*, which is one question asked once and answerable by a test.

The trade is memory, and it is worth measuring before assuming it matters: the
entire history of the undo test session was **2.4 KB**. A 37-operator page is a
couple of KB. Capped at 64 states, this is free.

So the risk moves from arithmetic to **completeness**, and that is a better place
for it — "does the snapshot include array rows" is a checkable question, where
"is the inverse of every operation correct under every combination" is not.

Two implementation notes worth carrying:

- `sReader::ArrayNew` asserts its target is empty (`serialize.hpp:196`); it fills
  a freshly constructed object. Restoring into a live page must clear first.
- Restoring **replaces every object**, so any pointer held across a restore
  dangles. Hold indices. The editor drops its selection and re-evaluates the
  preview on a bumped revision rather than comparing an address that may have
  been reused.

And the coalescing came free: deferring the snapshot until
`ImGui::IsAnyItemActive()` is false turns a whole drag gesture into one entry,
for both a slider and a canvas drag, with no per-widget gesture tracking. When
the toolkit already knows something, ask it.

### A46 · Altona macro-defines `new`, so third-party headers need a shield — standing

*Phase 5.1.* `base/types.hpp:1763` ends with

```cpp
#define new sDEFINE_NEW
```

a 2005 memory-tracking idiom over a keyword. Any header included afterwards that
*declares* an operator new is mangled beyond repair. `imgui.h` declares one, for
placement new, and the result was four cascading parse errors inside ImGui saying
things like "function cannot return function type" — none of which points at the
macro.

Fixed in `wz4port/editor/imgui_wz4.hpp`, which wraps the ImGui includes in
`#pragma push_macro("new")` / `#undef new` / `pop_macro`. All editor code includes
ImGui through it and never directly.

Include order alone would also work — ImGui before Altona in a single translation
unit — and that was the first instinct. Rejected because it is **unenforceable**:
the rule would be "ImGui first, in every editor source file, forever", and the
punishment for forgetting is a wall of errors in third-party code. A wrapper
states the hazard once, in the place that owns it.

Generalisable to this codebase: it macro-defines a keyword, so **any** new
third-party header is a candidate for the same treatment. Check for macro
collisions before blaming the library.

### A47 · A process-global allocator cannot outlive-mismatch its process — standing

*Phase 5.1.* Altona replaces the global `operator new` and `delete`
(`base/types.hpp:1713`). On macOS that interposes for **every dylib in the
process**, and Altona unregisters its memory handlers as it shuts down after
`sMain` returns. Static destructors run after that, free through the still-live
interposed `delete`, and `sFreeMem_` finds no handler owning the pointer and calls
`sFatal` (`base/types.cpp:4776`).

Result: four `FATAL ERROR: pointer ... seems not to belong to any sMemoryHandler`
lines on every clean, successful exit of the editor. Nothing is corrupted —
Altona detects the foreign pointer and refuses it — but a tool must not print
FATAL ERROR when it succeeded.

No earlier tool in this port hit it, because none linked a dylib that owns C++
objects. The GUI frameworks are the first.

The editor now ends the process at the bottom of `sMain` instead of unwinding
through teardown. The cost is that this one binary gets no leak report from
Altona at exit; every other tool still does, which is where that check earns its
keep anyway.

Two things worth carrying forward:

- The ordering is **not ours to fix**. It is a property of interposing an
  allocator whose lifetime is shorter than the process, and the fix is to stop
  the process while the allocator is still alive.
- The message went to stdout **with a zero exit code**, so every check that
  looked only at the status passed. `editor_shot.cmake` now greps for
  `FATAL ERROR` explicitly. Same shape as A39: the status is a proxy.

### A50 · A `.ops` operator declaration cannot be preprocessor-guarded — standing

*Phase 6.1.* Patch 05 established `#ifndef WZ4_HEADLESS` in `.ops` files as the
way to exclude something from a headless build, and it works for four cases. It
cannot exclude an **operator**, for two independent reasons:

- `code` and `header` blocks are verbatim C++ and pass straight through, which is
  why a guard inside one works. `operator` and `type` are *parsed*
  (`parse.cpp:37-75`), and a `#` line at that level is a syntax error. Only an
  operator's body can be guarded — what patch 05 did to `Screenshot`.
- The body is usually not the problem anyway. What breaks is the **registration**,
  because an input type is emitted as a bare global owned by another module:
  `in[0].Type = Wz4MtrlType;`. Guarding the body leaves that line behind.

So `wz4ops` grew a per-operator `headless = 0;` directive (patch 11). The general
shape is worth carrying: **the unit of exclusion has to match the unit the
generator emits.** A guard placed inside verbatim text can only remove verbatim
text; anything the generator *derives* — registration tables, offsets, type
references — needs the generator to know.

Two rejected alternatives, both recorded because each looked cheaper:

- **A new bit in the `flags = ` choice list.** Those map to runtime `wCF_` bits
  the document model reads. This is a directive to the generator, and giving it a
  runtime bit would misdescribe it.
- **Registering a phantom `Wz4Mtrl` type** so the symbol resolves. Worse than
  omitting the operator: nothing headless can produce a `Wz4Mtrl`, so
  `SetMaterial` would sit in the palette permanently unusable and the phantom
  would show up in the inspector as a real type. *An operator that cannot work is
  worse than an operator that is honestly absent.*

### A51 · A generated header's macros are part of its contract — standing

*Phase 6.1.* Patch 05 emitted `#define WZ4_HEADLESS 1` into the generated `.cpp`
only, which was sufficient for every guard it wrote — they were all in `code`
blocks, and `code` goes to the `.cpp`. `header` goes to the `.hpp`, and **the
`.hpp` is read by every consumer of the module**, not just by its own `.cpp`.

Guarding the mesh module's header block therefore worked in the generated file and
failed in `wz4_mesh.cpp`, which includes the same header and does not define the
macro. The error names the `.ops` line, which is the confusing part — the guard
looks correct because it *is* correct, just evaluated in a translation unit that
never heard of the flag:

```
wz4_mesh_ops.ops:11:10: fatal error: 'wz4lib/poc_ops.hpp' file not found
```

Generalisation: when a generator emits a configuration macro, the macro belongs
wherever the generated code that tests it can be *included*, not wherever it is
compiled. Emitting it in one of a matched `.hpp`/`.cpp` pair is a latent bug
waiting for the second consumer.

### A52 · Untested platform branches rot silently — standing

*Phase 6.1.* Two upstream defects in `wz4_mesh.cpp` were found by the simple act
of compiling it for a non-Windows target for the first time:

- `Wz4Mesh::ConvertFrom` dereferences an **incomplete type** — `chaosmesh_code.hpp`
  is commented out of the includes at line 12 while the body reads
  `src->Clusters[i]->Material->Material->Flags`.
- `MakePath`'s `#else` stub lost track of the real signature: `weldThreshold` was
  added to the declaration and to the Windows definition, never to the stub, so
  the non-Windows branch defined a function that matched nothing.

Neither is a porting difficulty. Both are code that **has never been compiled**,
in a file whose Windows path is exercised constantly. Worth expecting more of the
same in every remaining `#if sPLATFORM==sPLAT_WINDOWS ... #else` in this tree:
the `#else` arms are the unvisited half of the dump, and a compiler is the only
thing that has ever looked at them.

### A53 · A self-consistent test can pass on the wrong data entirely — standing

*Phase 6.2.* `mesh_obj` took its output directory from `sGetShellParameter(0,1)`,
copied from `wz4gen`, whose positional 0 is its **command** and whose files are
therefore at index 1. There is no command in a test binary, so the argument was
silently ignored, the path fell back to `.`, and every OBJ landed in the build
root instead of `build/obj/`.

**All fifteen assertions still passed.** They had to: the test wrote to
`objpath`, read back from `objpath`, and compared the two. Nothing in it ever
referred to where the file was *supposed* to be, so the wrong answer was
perfectly self-consistent. It was found by listing the directory.

This is a different failure from A39 ("the status is a proxy"). There the
assertion was too weak; here the assertions were strong and *all of them were
about the same wrong object*. The distinction matters because more assertions
would not have helped.

Two things that do help, both applied:

- **Make the input non-optional.** The test now fails with a usage message if the
  directory argument is missing, so the fallback that hid the bug is gone.
- **Check at least one thing against an outside reference** — here, that the
  directory ctest was told about is the directory the files are in. A test whose
  every claim is relative to its own state can only prove internal consistency.

Third instance of the Altona shell-parameter API biting: `sGetShellInt` versus
`sGetShellParameterInt` in 5.6, the switch/filename ordering in `wz4ops`, and now
the positional index. The API is easy to call in a way that compiles, runs, and
means nothing.

### A54 · Fidelity to the original tool is a tie-breaker, not a goal — standing, **amended**

*Phase 6.3.* Two genuine upstream faults surfaced in one afternoon:

- **`Extrude` decodes the adjacency table with `/4`** where the rest of
  `wz4_mesh.cpp` uses `>>2` (`:3033`, `:3111`). A boundary half-edge is stored as
  `-1` (`ConnectFaces`, `:1290`), and `-1/4` is `0`, so `n==-1` can never be true
  and every rim edge is misread as adjoining face 0. Consequence: extruding a
  selected open quad builds **no side faces at all** — the cap just translates by
  `Amount` along its normal.
- **`BakeAnim` dereferences a null `Skeleton`** (`:1754`) and segfaults on any
  generated mesh.

**As first written, this entry concluded that only the second should be fixed**,
on the grounds that `Extrude` produces deterministic output which `example.wz4`'s
14 `Extrude` operators were authored against, so changing it would make the port
disagree with the tool the demos were built with.

**That was wrong, and it is the mistake worth keeping the entry for.** The
distinction it draws is real — a deterministic wrong *answer* is part of a tool's
observable behaviour, a *crash* is not — but it answers the wrong question.
"Would this change what the 2014 binary produced" is a **tie-breaker for
ambiguity**, not a veto. An extrude that cannot build sides is not a design
anyone chose; it is a two-character decode error in a code path that has never
executed. Inheriting it because the demos inherited it mistakes the reference
corpus for the specification.

The corrected rule, in order:

1. **Is the behaviour something a user would recognise as intended?** Phase 4's
   alpha handling was (A40) — four operators zeroing alpha turned out to be
   "alpha is attached late, on purpose". `Extrude`'s missing sides are not.
2. **If not, fix it**, and state what changes. Here: 14 operators in
   `example.wz4` produce different geometry. Accepted; scheduled as stage 6.7.
3. **Fidelity decides only what is left** — where the tree is ambiguous and there
   is no way to tell a choice from an accident.

One practical note survives unchanged: a crash can be fixed without weighing
anything, because nothing can be authored against it.

**Postscript, stage 6.7: the trade-off did not exist.** The fix is `>>2` in two
places, and the full sweep report for `example.wz4` — 2,192 lines including a
checksum for every one of 1,097 evaluated meshes — is **byte-identical before and
after**. `Adjacent[] == -1` means "no neighbour *at all*", so only a rim edge on a
real mesh boundary was misread; every `Extrude` in the bundled documents extrudes
a partial selection of a *closed* mesh, whose rim is interior edges that decoded
correctly all along. Nothing changed, and the two rounds spent weighing fidelity
against correctness were spent on a cost of zero.

Which is the sharper lesson, and it is A56.

### A55 · An operator's first real input is where its bugs are — standing

*Phase 6.3.* `BakeAnim` had been "working" for the whole port: it registered, it
linked, it appeared in the palette, and the corpus sweep over five documents
reported it among the operators that *could not run* — because its inputs came
from unregistered modules, so the crash was never reached. It took a
hand-written case handing it a plain `Cube` to find that it segfaults on every
mesh a generator produces.

Three findings in this stage came the same way, and none was reachable by
inspection:

| Operator | What only an input revealed |
|---|---|
| `BakeAnim` | segfault on a null skeleton |
| `TransformEx` | `Flags` default to `0x33` — uv0 to uv0 — so it moves **texture coordinates** and leaves positions untouched |
| `Extrude` | silently requires `Select` upstream (`f->Select>=0.5f`, `:3011`), and then builds no sides anyway |

`TransformEx` is the one to remember, because nothing about it looks wrong: the
operator runs, reports success, and returns a mesh identical to its input. The
same shape as `Perlin`'s `FadeOff` default in 4.3.

The lesson is about test *design*, not diligence: the corpus sweep (Suite B) has
far more coverage — 1,386 operators against 46 — and found none of these, because
a sweep can only check invariants over inputs it did not choose. Breadth finds
crashes in code paths; a chosen input finds *semantics*. Both suites, always.

### A56 · Measure the cost of a change before arguing about whether to pay it — standing

*Phase 6.7.* The `Extrude` adjacency defect got two rounds of deliberation about
whether fixing it was worth breaking fidelity with the 2014 tool: an entry in this
document, a paragraph in a patch, a deferred stage with a stated compatibility
cost, and a test case written as a change-detector.

**The cost was zero.** The fix is two characters, and the full sweep report for
`example.wz4` — 2,192 lines, a checksum for each of 1,097 evaluated meshes — is
byte-identical before and after. The other four documents contain no `Extrude` at
all. One command, run *first*, would have collapsed the whole argument:

```sh
wz4gen sweep <doc> -v > before.txt    # then apply the fix, and diff
```

Why the cost was zero is the part worth understanding, because it was knowable
too: `Adjacent[] == -1` means "no neighbour **at all**", so the bad decode only
misread a rim edge on a *real mesh boundary*. Every `Extrude` in the bundled
documents extrudes part of a *closed* mesh, where the rim is interior edges
carrying genuine face indices — which decoded correctly either way. The defect
only ever broke open meshes. That is also why it survived a decade.

Three specific claims made while planning the change, all wrong:

| Claimed | Measured |
|---|---|
| "the side-face path has almost certainly never executed" | it executes, and works — 62, 7 and 152-face results in `example.wz4` |
| "`example.wz4`'s 14 `Extrude` operators will change geometry" | none of them changes |
| "expect more than a two-character fix" | two characters |

The generalisation is not "argue less". It is that **the blast radius of a change
is usually cheaper to measure than to reason about**, and this project already had
the instrument — the corpus sweep built one stage earlier, whose whole purpose is
running every operator in the reference documents. Reaching for a principle when a
measurement is one command away is the same error as A39 and A45 wearing different
clothes: preferring the available proxy to the actual thing.

### A57 · A vendored loader is generated, and generated means stripped — standing

*Phase 6.4.* The 3D preview needed GL 3.3 entry points the editor had never used.
ImGui vendors a loader, `imgui_impl_opengl3_loader.h`, whose implementation is
behind `#ifdef IMGL3W_IMPL` while its declarations are unconditional — so
including it *without* that define binds to the copy already inside
`libimgui.a`, already initialised by `ImGui_ImplOpenGL3_Init`. That part worked,
and it is a genuinely good trick: no new dependency, no second loader, no
per-platform `#if`.

The plan then said it "gives every GL 3.x entry point". **It gives what ImGui
references and nothing else.** It is produced by `gl3w_gen.py --ref
imgui_impl_opengl3.cpp`, which the backend states plainly, and the split is
exactly what that implies:

| | |
|---|---|
| present | shaders, programs, uniforms, VAOs, buffers, `glDrawElements`, `glPolygonMode` |
| absent | the whole framebuffer/renderbuffer family, `glUniform3fv`, `glDepthFunc`, `glDrawArrays`, and a dozen GL 1.1 enums |

Supplementing it was right (thirteen `glfwGetProcAddress` pointers) and editing
it was not: it is *generated*, so a local edit becomes a fork that the next
dependency bump silently reverts. **The general rule: a vendored file that is
generated is a build artefact, not source. Extend beside it, never inside it.**

Also worth carrying: the failure mode was a wall of `use of undeclared
identifier` spread over three rebuild cycles, because each fix revealed the next
missing symbol. Grepping the header for the full list of what I needed *before*
writing the code would have collapsed three cycles into one.

### A58 · The dangerous graphics bug is the one with no error — standing

*Phase 6.4.* Two bugs in the mesh viewer, and neither produced an error message,
a failed check, or a GL error:

- **A hand-fused view-projection matrix had three sign errors**, negating the `w`
  row. Every vertex had `w < 0` and was clipped. The pane rendered **black**, and
  everything else looked healthy: the upload report was correct, the toolbar was
  correct, the framebuffer was complete. Rewritten as separate view and
  projection matrices multiplied explicitly — longer, and the mistake is not
  available in that form. *Do not hand-fuse matrices; the operation is free and
  the debugging is not.*
- **Releasing the object from `CalcOp` broke the next frame.** Frame 1 uploaded
  23 vertices, frame 2 reported `meshview: empty`. The document's cache and that
  pointer are not independent references. `wPreview::Upload` — the tested
  precedent in the same editor — does not release, which is the kind of thing to
  check *before* reasoning from first principles about who owns what.

The shared lesson is about where to look, not about matrices or refcounts. In a
render path, "nothing appeared" is the default symptom of almost every mistake,
so the useful instrument is not the error output — there isn't any — but a report
of what the code *believed* it did. `meshview:` prints its vertex and triangle
counts on every upload, and that line is what distinguished "the mesh never
arrived" from "the mesh arrived and the camera is wrong". Both bugs were found by
reading it against the screenshot.

Which is why the gate asserts that line and not the image, and why the wireframe
case exists at all: a screenshot of a *checked checkbox* is not evidence that the
checkbox does anything.

### A59 · A47 has two ends, and one of them exits zero — standing

*Phase 6.6a.* A47 recorded that Altona's process-global allocator unregisters its
handlers before static destructors run. Stage 6.6 hit that hazard **twice, from
opposite ends**, in code written a day apart:

- a file-scope `sTextBuffer` allocates in its **constructor**, which runs *before*
  the handlers are registered — `sVERIFY(h)` fires inside `sAllocMem_`;
- file-scope `sArray`s free in their **destructors**, which run *after* the
  handlers are gone — `sFreeMem_` prints `FATAL ERROR: pointer ... seems not to
  belong to any sMemoryHandler`.

The second is the dangerous one, because **it exits 0**. The test printed a fatal
error and passed. A47 already noted that property for the editor and
`editor_shot.cmake` greps the string for exactly that reason, but the lesson had
been filed as "the editor's teardown problem" rather than as a property of every
binary that links Altona.

Two things follow, both applied:

- **No Altona container at file scope, ever.** Not a pointer allocated in `sMain`
  as a workaround — in this case nothing needed a growable container at all, and a
  fixed array removed the hazard class rather than routing around it once more.
- **Assert it, do not remember it.** Every test phase 6 added carries
  `FAIL_REGULAR_EXPRESSION "FATAL ERROR"`. ctest supports it natively, so a
  convention that has to be recalled became a check that cannot be forgotten.

The general shape is A39 again in a new costume: the exit code is a *proxy* for
"the run was clean", and this is a case where the proxy and the thing disagree.
The remedy is the same — assert on the output the program actually produced.

### A60 · A one-line plan for a large region is a guess about its shape — standing

*Phase 6.6.* The phase plan said of `Text3D` and `Path3D`: "reimplement on
FreeType outline extraction plus a tessellator". Measured, that describes **two of
the six things** in the 770-line Windows-only block:

| | |
|---|---|
| platform-specific | glyph outlines (`GetGlyphOutlineW`), tessellation (`glu32`) |
| **portable, merely guarded** | Bézier flattening; the 150-line SVG path parser; text layout; all 160 lines of `Finish2DExtrusionOp` — which has *zero* references to `glu`, `HDC`, `HFONT`, `__stdcall` or `GLYPH` |

That changes the work from "reimplement" to "replace the tessellator handle with
a sink interface and add a FreeType branch", which shares the portable four
instead of copying them. Duplicating the parser and flattening into `wz4port/`
would have been a 200-line partial fork — and it is what a plan phrased as
"reimplement" invites.

Third time in this phase a survey line has understated or misdescribed a region:
6.1's materials exposure, 6.3's operator inventory (three of the "47" are not
operators), and now this. The pattern is not carelessness in the original survey —
it is that **a one-line summary of a large region records what the author
noticed, and what gets noticed is the platform-specific part**, because that is
what looks like work. The portable part is invisible precisely because it is
unremarkable. Worth measuring the *ratio* before planning any region of this size.

### A61 · Test the condition the consumer tests — standing

*Phase 7.2.* New rig invariants in `geo/mesh_check.cpp` immediately fired on every
`Text3D` and `Path3D` mesh: *"196 skinned vertex(es) naming a joint outside
0..-1"*. The meshes were correct; **the check was wrong.**

It identified an unskinned vertex as one with a negative `Index[0]`. But
`wMeshTess::AddVertex` builds vertices with `sClear`, so `Index[0]` is **0** — and
0 is a perfectly good joint number. Upstream's own `Wz4MeshVertex::Skin`
(`wz4_mesh.cpp:188`) tests

```cpp
if(Index[0]<0 || Index[0]>=max)     // max = the joint count
```

so on a mesh with no skeleton *every* vertex is unskinned by construction, and
the negative index never has to occur.

The invariant had been invented alongside the check rather than read off the code
it protects, and the two then disagreed. **An invariant is a claim about what a
consumer requires; derive it from the consumer.** The cheapest way to get it right
was to copy `Skin`'s own predicate — which is what the fix does, guarded on
`Joints > 0`.

Worth noting what caught it: four *existing, unrelated* cases from stage 6.6.
A new check that only ever ran against new data would have looked clean. This is
an argument for adding invariants to a shared battery rather than to the test
that motivated them.

### A62 · New operators can live in `wz4port/`, and that was not obvious — standing

*Phase 7.2.* All 33 `.ops` modules in the dump live in two upstream directories,
so adding an operator looked like it had to mean patching an upstream `.ops` —
against the project's own "new code goes in `wz4port/`" rule.

It does not. `wz4_add_ops` **copies the file into the build tree and runs the
generator there** (`CMakeLists.txt:224-226`), so the tool never sees the original
path. `wz4port/geo/animate_ops.ops` is the first non-port operator in the project
and it cost **zero upstream changes**.

Two constraints, both discovered rather than assumed:

- The file must be named `<t>_ops.ops`, because `sREGOPS(t,s)` expands to
  `AddTypes_##t##_ops` (`doc_core.hpp:70`).
- A module needs `wz4_add_meta` as well as `wz4_add_ops`, or the operator
  registers but has **no parameters in the editor panel** — the panel is
  generated from metadata, and the corpus glob only covers upstream directories.

The general point: a convention observed across every existing example ("`.ops`
files live upstream") is not the same as a constraint. This one was worth testing,
and testing it removed the only reason phase 7 looked like it needed upstream
surgery.

### A63 · A rig in this build has no hierarchy, for the same reason it had no motion — standing

*Phase 7.5.* The skeleton overlay was planned as "a point per joint and a line to
its parent". The lines never appeared, and the data is why:
`Wz4AnimJoint::Init` sets `Parent = -1` (`wz4_anim.cpp:55`) and **`Deform` never
assigns it**. The only code in the tree that writes `Parent` is the merge remap
(`wz4_mesh.cpp:1030`) and `LoadWz3MinMesh` (`:7388`) — an **import** path, and no
`.wz3`/`.xsi`/`.lwo` asset exists here.

This is the phase's founding finding repeating one level up. Phase 7 exists
because every channel a registered operator can build is a `Wz4ChannelConstant`,
so **motion** only ever came from import. **Hierarchy** turns out to have come
from exactly the same place. `Deform` is a deformation tool — a set of control
joints laid along a segment, weighted into the mesh — and neither a chain nor an
animation. The user identified this from their own use of it before the code
confirmed it.

The design consequence is the part worth keeping. `Deform`'s joints *do* lie
along a line, so connecting them would have looked correct in every screenshot —
and would have drawn a hierarchy the data does not contain. **A viewer that
invents structure is worse than one that shows none**, because the invented
version is indistinguishable from the real thing precisely when it is wrong. So
the overlay draws what is there (joint axes, plus a parent line when a parent
exists) and the status line separates `joint(s)` from `bone(s)`, making "no bones"
legible as flat data rather than a broken overlay.

The general point: when a feature's data turns out to be absent, the choice is
between showing the absence and simulating the feature. Simulating it costs
nothing today and destroys the viewer's credibility the first time someone
imports a real rig and cannot tell the drawn hierarchy from the inferred one.

### A64 · An oracle can be reconstructed, and that is worth a dependency to avoid — standing

*Phase 8.* glTF export looked like the obvious place to vendor a library: fx/gltf, or at least
nlohmann/json. The reason not to turned out to be about **testing**, not about size or licence.

`tests/mesh_obj.cpp` is a real test of `SaveOBJ` rather than a self-consistent one because upstream's
`LoadOBJ` shares no code with it. A writer checked by its own reader agrees with itself no matter how
wrong both are. There is no `LoadGLTF` in this tree, so that oracle does not come free — and a single
vendored library used for both directions would not have supplied one either. It would have supplied
the *appearance* of one.

It was reconstructed instead, from two implementations that already existed and had never met:

- `wz4t/json.hpp` — a general JSON **reader**, written for the metadata runtime in phase 3.
- `tools/opsmeta/json.hpp` — a deterministic JSON **writer**, written for reviewable metadata output.

The writer moved to `wz4t/json_write.cpp` and the exporter emits through it; the test reads back
through the reader. Neither knows the other exists. Both already solve the float round-trip problems
(A18, A34) that any JSON emitter in this tree would otherwise hit.

Three supporting facts, none of which would have been decisive alone: `curl` is not allowlisted, so
both libraries would have had to be vendored by hand; fx/gltf throws unconditionally with no
`JSON_NOEXCEPTION` escape and Altona builds `-fno-exceptions`; and we only ever *write*, which is the
half a glTF library adds least to.

The general point, and the reason this is an entry rather than a note: **"is there an independent
implementation to check this against?" is a question about the dependency, not just about the test.**
A library that supplies both sides of a round trip removes the very thing that made the round trip
worth running. Phase 6 declined libtess2 on effort grounds; this declined fx/gltf on evidentiary
ones, which is the stronger argument and the one to reach for first.

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
| A hex colour literal is one token | `#08ff0000` is INT+NAME, `#1e500000` is a FLOAT. Reassembled from exact source text (3.1b) |
| A fresh `wDocument` is empty | Its constructor calls `DefaultDoc()` and it already owns a page (A32) |
| `sLoadText` reads UTF-8 | Only with a BOM; otherwise byte-per-character. Silent mojibake on hand-written files (A33) |
| A passing round-trip test means the data survived | Not if the error is idempotent. `café` → `cafÃ©` passed every comparison (A33) |
| `sScanner::ScanFloat` round-trips a float | Loses one ULP. Invisible in a text diff; 5 of 6 documents passed anyway (A34) |
| `doc.cpp` has 16 GUI-touching lines | It contains all ~840 lines of `wPaintInfo`. Third time a line count understated coupling (A22) |
| Extracting a declaration is enough to decouple | It compiles; it does not link. The definitions needed extracting too (A23) |
| `script.cpp` might be excludable | `wExecutive::Execute` drives `ScriptContext` directly (A19) |
| A headless build needs a new mechanism to strip painting | `sCOMMANDLINE` already existed and was already defined here (A22) |
| `wz4_mesh.hpp` needs `Wz4Mtrl` extracted into its own header | It needs a forward declaration. Its one use is a pointer (6.1) |
| The mesh library's materials exposure is the render section | Also `ConvertFrom` and the object serialiser, in three separate places (6.1) |
| A stand-in that overrides nothing inherits upstream's fatal `Serialize` | There is nothing to inherit from — `wObject` has no `Serialize`, and the streaming templates require the member to exist (6.1) |
| A `#ifndef WZ4_HEADLESS` guard can drop an operator | Only verbatim text can be guarded; the registration is derived (A50) |
| One `#define` in the generated `.cpp` is enough | The `.hpp` is read by every consumer of the module, not just its own `.cpp` (A51) |
| `wz4_mesh.cpp`'s non-Windows path is untried but sound | Two things in it have never compiled: an incomplete-type dereference and a stub whose signature drifted (A52) |
| The 47 mesh operators are the exposure to measure | 45 are clean; the survey's own count of *includes* (five foreign) overstated it by an order of magnitude (6.1) |
| The phase plan's operator inventory is the list to work from | Three of the 47 it names — `CalcNormals`, `CalcTangents`, `Weld` — are `Wz4Mesh` *methods*, not operators. Corrected from the live registry (6.3) |
| A closed primitive's half-edges pair by vertex index | They do not: a `Wz4Mesh` splits a position wherever normals differ, so `Cube(2,3,4)` leaves exactly 72 unpaired — the six patch perimeters (6.3) |
| Sweeping every store covers a document's operators | `example.wz4` has 54 stores yielding 15 meshes, and 1,180 mesh operators. The interesting ones are mid-graph (6.2) |
| `TransformEx` with default flags transforms positions | It transforms **uv0**; `Flags` default to `0x33`. The mesh comes out identical and the operator reports success (A55) |
| `Extrude` extrudes | Only faces already selected (A55) — and it built no sides on an OPEN mesh, because of the `/4` decode (A54) |
| Fixing the `Extrude` decode changes 14 operators in `example.wz4` | It changes nothing: 2,192 lines of checksummed sweep report, byte-identical (A56) |
| The `Extrude` side-face path has never executed | It executes and works; only the boundary-edge branch of the rim test never ran (A56) |
| ImGui's vendored loader covers GL 3.3 | It covers what ImGui references. Framebuffers, `glUniform3fv` and `glDepthFunc` are absent (A57) |
| A mesh's GL buffers are copies, so the source object can be released | Frame 2 came back empty — the document's cache is not an independent reference (A58) |
| `Text3D`/`Path3D` need reimplementing on FreeType plus a tessellator | Two of six parts are platform-specific; the parser, flattening, layout and all of `Finish2DExtrusionOp` are portable (A60) |
| A47 is the editor's teardown problem | It is every Altona binary's, and the destructor end exits 0 while printing a fatal error (A59) |
| Skeletal animation may be entangled with the sequencer | Zero script references; `Evaluate` takes time as a plain argument. The phase's "main risk" did not exist (7.0) |
| A skinned mesh needs an imported asset | `Deform` builds a skeleton with weights procedurally, and has been in the suite since 6.3 unnoticed (7.0) |
| Porting the animation module makes animation work | It makes *skinning* work. Every procedurally-creatable channel is CONSTANT; nothing in scope can move a joint (7.0) |
| `Wz4ChannelLinear` is implemented and merely never constructed | Implemented *incompletely* — no `CopyTo()`, no `Serialize()`, so `Wz4Skeleton::CopyFrom` would `sFatal` (7.2) |
| A new operator means patching an upstream `.ops` | `wz4_add_ops` copies to the build tree first, so a `.ops` in `wz4port/` works — zero upstream changes (A62) |
| An unskinned vertex has `Index[0] < 0` | It has `Index[0] >= joint count`; `sClear` leaves it at 0, which is a valid joint (A61) |
| A skeleton is a chain, so bones can be drawn to parents | `Init` sets `Parent = -1` and `Deform` never assigns it. Hierarchy, like motion, only ever came from import (A63) |
| glTF export needs a JSON library | A reader and a writer already existed in-tree, in separate files — and using them keeps the round-trip oracle a vendored library would have destroyed (A64) |
| A cube is enough to test a coordinate conversion | It is symmetric in z, so omitting the mirror ENTIRELY passes every check. The asymmetric case is the only one that decides it (8.3) |

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

**Upstream footprint is 68 files and will keep growing.** Two categories are
inert (30 encoding-only, 2 genuine language errors); the rest are structural and
are the ones a future merge would conflict on. Upstream is a frozen 2014 dump, so
this is a bookkeeping cost rather than a merge risk — but the enumeration in
`wz4port/patches/` is the only thing keeping it honest.
