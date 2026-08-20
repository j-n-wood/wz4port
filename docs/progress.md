# Progress — session bootstrap

Read this first when picking the project up cold. It records where things
stand, what has been decided and why, and what would otherwise have to be
rediscovered the hard way.

**Last updated:** phase 5, stage 5.3.
**Status:** Phases 1–4 complete and verified; **phase 5 has a working canvas and
the connection rule is fully exercised.** All 34 `GenBitmap` operators run on
macOS arm64, with 93 reviewed test cases and 90 byte-exact goldens that are
bit-identical between the NEON and SSE2 builds. `ctest` is **129 tests** on
arm64.

**`wz4ed` is the editor.** It has a window, a menu bar, a metadata-driven
inspector, and the stacking canvas: blocks coloured by output type, selection,
move and resize by drag with the projected destination framed, fit-to-page,
zoom and pan. Run it as `wz4ed <doc.wz4t>`; add `-shot <file.png>` to render two
frames, screenshot and exit, which is how a GUI gets a regression test here, and
`-select <name>` to pre-select an operator so the panels are exercised too.

**The editor reimplements no rules.** Collision goes through
`wPage::CheckDest`/`CheckMove`, which read `wOp::Select`, and connection goes
through `wDocument::Connect()`. There is no parallel state that could drift from
what the document format enforces — which is what phases 2–4 were for.
`canvas_rules` (30 checks) and `connect_passes` (27) drive exactly that code with
no window.

`Hide` and `Bypass` are editable in the editor (checkboxes, or `H`/`B` on the
selection) and the inspector lists the derived inputs beside them, so watching
`in0` change under a Bypass is a two-second demonstration of the rule.

**Connection guides draw the contact patch, not wires.** This is a contact model,
so a connection has no length — a centre-to-centre line has both endpoints on the
shared edge and renders as nothing. That is the real reason the original draws no
wires. The overlap span along the shared edge is the informative thing, and it is
what a sideways drag destroys.

Next: **5.4**, the operator palette.

**SSE2-vs-NEON parity is real and was runnable here**, contrary to the plan's
assumption that it needed a Linux box: `sh wz4port/tests/tex/parity_x86_64.sh`
cross-compiles an x86-64 slice and runs the suite under Rosetta 2. It found 8
divergences on its first run — all caused by **FMA contraction**, not sse2neon.
`-ffp-contract=off` is now set project-wide *for determinism*; do not remove it.

The renders land in `build/tex-png/`; the goldens are `tests/tex/golden/`, an
image plus a `.txt` checksum line each.

**Two rules that came out of phase 4 and apply to any test added later:**

- **Assert what the viewer will show, not what the buffer holds.** Four
  operators zero the alpha channel, and a transparent PNG displays as plain
  white — indistinguishable from success. `architecture.md` A40.
- **A golden must cover the pipeline's precision, not the artefact's.** The
  pipeline is 16-bit and a PNG is 8-bit, so every golden stores a checksum too.
  7 of the 8 parity divergences had byte-identical images. A43.

**Read `06-phase-texture.md` before touching the texture tests.** 4.3 found four
operators that zero the alpha channel, which makes their PNG render as plain
white and therefore indistinguishable from "filled with white" or "did nothing".
That is why the cases assert on the alpha range and why `wz4gen render` prints
it.

---

## What this project is

Extracting the **procedural generation** half of Farbrausch's Werkkzeug4 from
the `fr_public` code dump and making it run on macOS (Apple Silicon) and
Linux, with a new Dear ImGui editor.

Priority order: **texture generation → geometry generation → animated
geometry**. Sequencer, render graph, materials and post-FX are out of scope —
they are also where every remaining hard platform blocker lives.

Read next, in order:

| Doc | Why |
|---|---|
| `00-overview.md` | Goals, non-goals, tree layout, working conventions |
| `architecture.md` | **Why the structure is shaped like this**, and what was tried and rejected. The decision record |
| `01-existing-model.md` | **How Werkkzeug4 actually works.** The reference document; fully cited |
| `02-target-model.md` | What we are building and every deliberate divergence |
| `03-phase-toolchain.md` … `09-phase-animation.md` | Per-phase plans |

The gotchas below are the ones that cost time *this* session.
`architecture.md` is the durable record: every structural decision, the
measurement behind it, and the alternatives closed off. **Append to it whenever
a structural decision is taken or a structural assumption turns out wrong** —
that is what keeps this file short.

`wz4port/README.md` covers build mechanics and the five non-obvious things
about the build.

---

## Phase status

| Phase | State |
|---|---|
| 0 — Documentation (`docs/00`–`02`) | **Done**, reviewed and approved |
| 1 — Toolchain and portable base | **Done**, gate passed |
| 2 — Headless op runtime + metadata | **Done**, phase gate passed |
| 3 — Text graph format + CLI | **Done**, phase gate passed |
| 4 — Texture library + tests | **Done**, phase gate passed. All 34 operators run |
| 5 — Texture GUI | **In progress.** 5.1–5.3 done, all gates passed |
| 6 — Geometry | Not started |
| 7 — Animated geometry | Not started |

---

## What builds today

```sh
cmake -S wz4port -B wz4port/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C wz4port/build
ctest --test-dir wz4port/build --output-on-failure
```

Clean build from scratch: **0 errors**. Warnings are expected and benign
(Altona overloads global `operator new` as `inline`, and has a
`if(this)` null check).

| Target | What it is |
|---|---|
| `altona_base` | Altona shell subset — types, math, serialize, system, blank renderer |
| `altona_util` | `scanner` + `scanconfig`, the slice the host tools need |
| `wz4ops` | Upstream's `.ops` code generator, native arm64, now with `-headless` |
| `opsmeta` | Ours: `.ops` → metadata JSON, using wz4ops' parser but not its emitter |
| `wz4ops_gate` | Regenerates `basic_ops` + `wz3_bitmap_ops` into `build/generated/` |
| `headless_core_gate` | Compiles `wz4lib/doc_core.hpp` alone, with the GUI poisoned |
| `headless_ops_gate` | Generates and compiles both op modules `-headless`, GUI poisoned |
| `opsmeta_gate` | Emits + validates metadata for all 33 `.ops` modules into `build/meta/` |
| **`wz4core`** | **The operator runtime, GUI-free: doc, build, basic, script, generated basic_ops** |
| **`wz4tex`** | **The texture engine: wz3_bitmap_code + genvector + generated ops** |
| `tex_smoke_*` (5) | Stage 4.1 gate: five operators evaluate; `Flat` uniform, rest structured |
| `tex_chain_*` (4) | **Stage 4.2 gate:** a four-step chain renders to real PNG files (`ctest`) |
| `tex_ops_*` (89) | **Stages 4.3–4.5:** a reviewed case per operator, 90 golden-locked (`ctest`) |
| `tex_merge_identity` | `Merge`'s brightness and hardlight are one algorithm; assert they agree |
| `wz4t` | **Ours.** JSON, the runtime metadata model, and the `.wz4t` reader + writer |
| `wz4t_read` | Stage 3.1b gate: a hand-written case parses and connects (`ctest`) |
| `wz4t_round_*` (4) | Stage 3.2 gate: read→write→read preserves every word (`ctest`) |
| `docround_*` (6) | **Phase 3 gate:** `.wz4`→`.wz4t`→`.wz4` over every document (`ctest`) |
| `wz4gen` | The headless CLI: `list` (`-inputs`), `describe`, `checkmeta`, `convert`, `identity`, `render`, `diff` |
| `core_connect` | Phase 2 gate: links `wz4core`, derives a graph from geometry (`ctest`) |
| `load_*` (6 tests) | Every bundled `.wz4` document must load and be non-empty (`ctest`) |
| `identity_*` (6 tests) | And survive a load/save/reload with every class intact (`ctest`) |
| `checkmeta` | The metadata reads back consistently: 370 classes, 2,728 params (`ctest`) |
| `simd_parity` | Verifies all 43 SSE2 intrinsics against scalar models |
| **`wz4ed`** | **The editor** (phase 5). ImGui + GLFW + GL 3.3, vendored and pinned |
| `imgui` | Vendored ImGui v1.92.9b, built without `altona_flags` — see below |
| `wz4ed_shell` | Stage 5.1 gate: the editor starts, draws and screenshots (`ctest`) |
| `canvas_rules` | Stage 5.2 gate: canvas edits obey `CheckMove`, and rewire the graph |
| `connect_passes` | Stage 5.3 gate: the Hide, Sort and Bypass post-passes |
| `connect_inputs` | And that `wz4gen list -inputs` agrees with the editor's inspector |

`simd_parity`: **70,184 checks, 0 failures** on arm64 via sse2neon.

`opsmeta_gate`: **33 modules, 40 types, 370 classes, 2,728 parameters, 2,729
choice values cross-checked against `sFindFlag`**, 0 failures.

`core_connect`: **11 types, 38 classes registered from `basic`; 14 checks,
0 failures.**

### Files created so far

```
docs/                          00–09 plus this file
wz4port/
  CMakeLists.txt
  README.md
  compat/
    altona_config.hpp          NOT in altona_wz4/ — see gotcha 1
    include/
      .keep                    must exist; see gotcha 1
      wz4port_posix_compat.h   glibc-isms, force-included
      simd_compat.hpp          SSE2 -> NEON
      linux/joystick.h         stub; macOS has no /dev/input/js*
  patches/
    01-pthread-t-casts.md
    02-friend-default-argument.md
    03-latin1-to-utf8.md
    04-doc-headless-split.md   phase 2 stage 2.1
    05-wz4ops-headless.md      phase 2 stage 2.2
    06-doc-cpp-headless.md     phase 2 stage 2.4
    07-retain-foreign-class.md phase 3 — stop writes destroying unknown ops
    08-texture-library.md      phase 4 stage 4.1
    09-guicolor-header.md      phase 4 stage 4.5 — reach the 2D layer headlessly
  compat/altona_missing.cpp    sCheckBreakKey — an upstream POSIX gap
  tools/opsmeta/               phase 2 stage 2.3 — .ops -> metadata JSON
    main.cpp  emit.cpp  json.cpp
    opsmeta.hpp  json.hpp
  tools/wz4gen/main.cpp        phase 3 — the CLI; render/PNG completed in 4.2
  wz4t/                        phase 3 — ours: JSON, metadata, the text format
    json.hpp  json.cpp         a small general JSON reader, plus wFormatFloat
    meta.hpp  meta.cpp         wMetaLibrary — what wClass cannot tell you
    wz4t.hpp                   the .wz4t reader and writer
    wz4t_read.cpp  wz4t_write.cpp
  tests/cases/three_ops.wz4t   stage 3.1b: geometry and connections
  tests/cases/values.wz4t      stage 3.2: value kinds, escapes, non-ASCII text
  tests/tex/smoke.wz4t         stage 4.1: does the texture engine execute?
  tests/tex/chain.wz4t         stage 4.2: a chain whose every step is checkable
  tests/tex/golden_png.cmake   renders one case, checks image AND checksum
  tests/tex/same_png.cmake     asserts two renders are byte-identical
  tests/tex/lock_goldens.cmake the deliberate re-lock step
  tests/tex/parity_x86_64.sh   SSE2-vs-NEON bit parity, via Rosetta
  tests/tex/golden/            90 locked .png + .txt pairs
  tests/tex/ops_text.wz4t      stage 4.5: Text, the one un-lockable case
  tests/tex/ops_import.wz4t      Import (PNG, JPG) and ImportAnim
  tests/tex/data/              checked-in test images + make_anim.wz4t
  compat/font_freetype.cpp     stage 4.5 — sFont2D and the 2D surface
  tests/tex/ops_gen.wz4t       stage 4.3: the generators
  tests/tex/ops_color.wz4t       the pointwise colour operators
  tests/tex/ops_merge.wz4t       Merge, all 12 blend modes
  tests/tex/ops_filter.wz4t      Blur, Sharpen, Downsample
  tests/tex/ops_warp.wz4t        Rotate, Twirl, Unwrap, Bulge, Distort
  tests/tex/ops_light.wz4t       Normals, Light, Bump
  tests/tex/ops_io.wz4t          Export, MakeWz3Bitmap
  tests/
    simd_parity.cpp
    headless_core.cpp          phase 2 stage 2.1 gate
    core_connect.cpp           phase 2 gate — links wz4core, derives a graph
    gui_poison.h               tripwire, force-included into every headless target
  editor/main.cpp              phase 5 — wz4ed: window, panes, menus, panels
  editor/canvas.hpp/.cpp       stage 5.2 — the stacking canvas
  editor/imgui_wz4.hpp         include ImGui through this, never directly (A46)
  tests/editor_shot.cmake      run the editor, screenshot it, check the PNG
  tests/canvas_rules.cpp       stage 5.2 gate, without a window
  tests/connect_passes.cpp     stage 5.3 gate: Hide, Sort, Bypass
  third_party/sse2neon.h       pinned v1.9.1, MIT, 11,222 lines
  third_party/imgui/           pinned v1.92.9b, MIT — core + glfw/gl3 backends
  third_party/glfw/            pinned 3.5.1, zlib — src/include/CMake only
  third_party/VENDORED.md      versions, checksums, and what was pruned
.gitignore                     new, repo root
.claude/settings.local.json    gitignored tool allowlist
```

---

## Upstream footprint

**60 files** (`git diff --name-only 8c8f82c -- altona_wz4`, **run after
staging** — `git diff` does not see untracked files, which is how an earlier
count came out at 56 and missed three additions).

The isolation invariant is that `git status` on `altona_wz4/` must never show
anything not listed in `wz4port/patches/`.

Six categories, worth keeping distinct. Only the last four — 28 files — are
structural; the other 32 are inert (30 encoding-only, 2 genuine clang errors).

**Code changes — 2 files, 5 lines.** Both genuine C++ errors under clang, not
portability preferences.

```
 M base/graphics.hpp      (1 line)  patches/02  friend decl with default arg
 M base/system_linux.cpp  (4 lines) patches/01  pthread_t is a pointer on macOS
```

**Header reorganisation — 3 files edited, 4 added, no code changed.**
`patches/04`. Declarations moved verbatim so the document model no longer
needs the widget toolkit; see stage 2.1 below.

```
 M wz4lib/doc.hpp         now a two-line shim over the halves
 A wz4lib/doc_core.hpp    the document model. No gui, no shaders
 A wz4lib/doc_gui.hpp     wHandle, wPaintInfo, wGridFrameHelper, wCustomEditor
 M gui/listwindow.hpp     sListWindowTreeInfo moved out
 A gui/treeinfo.hpp         ... to here
 M gui/manager.hpp        sGuiTheme moved out
 A gui/theme.hpp            ... to here
```

**Headless operator generation — 9 files.** `patches/05`. `wz4ops` gained a
`-headless` flag; four `.ops`/header sites needed guards or a redirected
include so the generated code stops reaching for the editor.

```
 M tools/wz4ops/{doc.hpp,doc.cpp,main.cpp,output.cpp}   the flag
 M wz4lib/basic.hpp             ) these three are reached from .ops
 M wz4lib/poc.hpp               ) header/code blocks and now include
 M wz4frlib/wz3_bitmap_code.hpp ) doc_core.hpp instead of doc.hpp
 M wz4lib/basic_ops.ops         3 x #ifndef WZ4_HEADLESS
 M wz4frlib/wz3_bitmap_ops.ops  1 x #ifndef WZ4_HEADLESS (an unused include)
```

**Headless runtime — 8 files.** `patches/06`. The painting half of the runtime
is bracketed with upstream's own `#if !sCOMMANDLINE`; three data definitions
were extracted out of the gui library; two genuine clang errors fixed.

```
 M wz4lib/doc.cpp        6 guarded regions, 2 notify calls, palette retarget
 M wz4lib/build.cpp      1 notify call; gui/gui.hpp include removed
 M wz4lib/basic.cpp      3 guarded bodies, 1 guarded + 1 normalised include
 M wz4lib/build.hpp      doc.hpp -> doc_core.hpp
 M wz4lib/doc_core.hpp   declares wNotifyHook
 M wz4lib/script.hpp     extra qualification on a member (clang error)
 M gui/manager.cpp       theme definitions moved out
 A gui/theme.cpp           ... to here
 M gui/color.hpp         PaletteColors becomes a reference to array
 M gui/color.cpp         storage moved out, member bound to it
 A gui/palette.hpp       ) the extracted swatch storage —
 A gui/palette.cpp       ) document data, not gui state
```

**Class identity on write — 2 files.** `patches/07`. `wOp` retains the original
class and type name when it substitutes `UnknownOp`, and writes those back, so a
build that does not know every module cannot damage a document.

```
 M wz4lib/doc_core.hpp   ForeignClass / ForeignType
 M wz4lib/doc.cpp        set on substitution, used on write, copied by CopyFrom
```

**Texture library — 1 file.** `patches/08`. Everything else the pixel engine
needed became a shim in `wz4port/compat/`.

```
 M wz4frlib/wz3_bitmap_code.cpp   <emmintrin.h> -> "simd_compat.hpp";
                                 GenBitmap::Text guarded pending FreeType
```

**Encoding only — 30 files, 72 characters.** Latin-1 → UTF-8, verified
character-identical against the git originals; no semantic change.
See `patches/03`. This unblocked `script.cpp` (clang errors on Latin-1 inside
string literals) and retired the `grep -a` requirement.

---

## Gotchas that cost real time

### 1. `altona_config.hpp` lives in `wz4port/compat/`, not in `altona_wz4/`

`base/types.hpp:67` does `#include "../altona_config.hpp"` and upstream
expects you to create that file inside `altona_wz4/altona/`. We don't.

A quoted include that fails to resolve next to its including file is retried
against each `-I` directory **with the relative path intact**, so with
`compat/include` on the path:

```
compat/include/../altona_config.hpp  ==  compat/altona_config.hpp
```

`compat/include/.keep` exists so that directory is always present. Deleting
it breaks the build in a confusing way.

### 2. Source encoding — RESOLVED, no longer a hazard

*Historical note; nothing to do. Retained because it explains patch 03 and a
wrong finding in the survey.*

30 files were Latin-1 encoded. A single invalid UTF-8 byte made `grep` treat
a file as binary and report nothing — silently. That produced a wrong survey
finding (`sCONFIG_GUID` reported as makefile-only when it is used at
`types.hpp:2143`).

Worse, clang **errors** on Latin-1 inside string literals, which would have
blocked `wz4lib/script.cpp` in phase 2.

All 30 files were converted to UTF-8 (`wz4port/patches/03-latin1-to-utf8.md`),
verified character-identical against their git originals. **`grep -a` is no
longer needed.**

Note for anyone tempted to "clean up" the remaining non-ASCII characters:
**`°` is the dot-product operator in the ASC shader language.** It is real
syntax in ten `.asc` files, registered by `shadercomp/asc_doc.cpp:305` and
tokenised as `case 0xb0:` in `wz4lib/script.cpp:2993`. Do not replace it with
ASCII.

### 3. `wz4ops` must be run with a bare filename from the file's own directory

It derives *both* output paths *and* generated function names from the input
path with the extension stripped. `wz4ops a/b/basic_ops.ops` emits
`void AddTypes_a/b/basic_ops(...)`, which does not compile.

`wz4_add_ops()` in CMakeLists.txt copies each `.ops` into
`build/generated/<subdir>/` and runs the tool there with just the filename,
yielding `AddTypes_basic_ops` / `AddOps_basic_ops` — what the `sREGOPS` macro
(`wz4lib/doc_core.hpp:70-74`) expands to. It also keeps generated output out of
`altona_wz4/`.

### 4. macOS builds as `sPLAT_LINUX`, deliberately

Altona's "LINUX" means "POSIX desktop". 21 guards across the tree spell that
as `(sPLAT_WINDOWS || sPLAT_LINUX)`. Adding a `sPLAT_APPLE` — which the
original plan called for — would mean revisiting every one of them for no
behavioural gain, and the two `pthread_t`-shaped guards in `system.hpp`
(`:370`, `:422`) are already correct for macOS pthreads.

Genuine differences are handled per-file with `__APPLE__` in
`compat/include/wz4port_posix_compat.h` (`lseek64`, `mmap64`, `ftruncate64`,
`stat64`, `strdupa`, `pthread_yield`, `O_LARGEFILE`), force-included into
every TU. `system_linux.cpp` compiles in place, unforked.

### 5. `types.cpp` needs `graphics.cpp`

It references `sRender3DFlush()`, so `graphics.cpp` and `graphics_blank.cpp`
cannot be omitted even for console tools. Discovered at link time after
trying to defer them.

### 6. Altona predates C++11 UDLs

It writes `"..."sTXT(x)` with no separating space, which C++11 parses as a
user-defined literal. Handled with `-Wno-reserved-user-defined-literal`.

### 7. The GUI-free document model is enforced, not merely intended

`wz4lib/doc_core.hpp` must not acquire a `gui/` or `util/shaders.hpp`
dependency. Nothing about the include path prevents that — the real GUI
headers are still on it, and on macOS they even parse cleanly, so a build that
merely succeeds proves nothing.

Two targets compile with `wz4port/tests/gui_poison.h` force-included:
`headless_core_gate` (a TU including only `doc_core.hpp`) and
`headless_ops_gate` (both `-headless` operator modules). That header
`#pragma GCC poison`s `sWindow`, `sGui_` and `sSimpleMaterial`. Every header
in `gui/` reaches `gui/window.hpp`, so those three tokens cover the lot. Break
the invariant and the build fails naming the offending file.

Two things to know if it ever fires:

- `gui/theme.hpp` and `gui/treeinfo.hpp` are ours (extractions, `patches/04`)
  and are *allowed*. They include nothing but `base/`.
- `sMaterialEnv` is declared in **`base/graphics.hpp`**, not in
  `util/shaders.hpp`, despite the name. Poisoning it fails the gate on a
  header we legitimately need. The tripwire caught exactly this on its first
  run.

### 8. `sCOMMANDLINE` already exists, and this port already sets it

`base/types.hpp:605` defines `sCOMMANDLINE` as `sCONFIG_OPTION_SHELL`, which
`altona_flags` has set since phase 1. Altona and `wz4lib` are already sprinkled
with `#if !sCOMMANDLINE` around anything that needs a window — including three
blocks in `doc.cpp` itself.

**Check for it before inventing a mechanism to strip display code.** Stage 2.4
used it for the whole painting half of `doc.cpp` and `basic.cpp` rather than
splitting the files.

### 9. Altona's shell parser: the switch goes *after* the filename

`sGetShellParameter(0,0)` returns the first parameter not attached to a
switch, and a token following `-switch` counts as that switch's parameter. So

```sh
wz4ops -headless basic_ops.ops    # WRONG — prints the usage text
wz4ops basic_ops.ops -headless    # right
```

`wz4_add_ops()` in CMakeLists.txt builds the argument list in that order and
says why.

### 10. CMake deduplicates bare `-include` flags

`altona_flags` force-includes `wz4port_posix_compat.h`. Adding a second
`-include` on a target produces two bare `-include` tokens, CMake removes the
"duplicate", and the surviving flag steals the wrong path — the failure looks
like `error: cannot specify -o when generating multiple output files`, which
says nothing useful. Use the `SHELL:` prefix to keep flag and argument
together:

```cmake
target_compile_options(t PRIVATE "SHELL:-include ${dir}/header.h")
```

---

## Working conventions

Agreed with the user; these are not optional.

- **New code lives in `wz4port/`.** Never inside `altona_wz4/`.
- **Upstream changes are minimal, and every one is recorded** in
  `wz4port/patches/` with rationale. Prefer a shim header or an include-path
  override to a patch; prefer a patch to forking a file.
- **File edits use editor tooling, not shell.** `sed` is denied outright.
  **Python is not used to read or write code at all** — not for editing and
  not for verifying a refactor. Use `git diff`, `diff` and `grep`.
- **Every stage ends at a review gate.** Stop at a demonstrable result,
  `git add` the changes (do **not** commit), and confirm direction before
  starting the next stage.
- **Prefer single shell commands.** Long `&&`/pipe chains trip the permission
  matcher and interrupt the workflow.

---

## Tooling note for a new session

`.claude/settings.local.json` (gitignored) holds a build/inspect allowlist —
`cmake`, `ninja`, `clang++`, `grep`, `git status`/`diff`/`log`, running
binaries from `build/`, and so on. `sed` is denied; `python` is set to ask.

It was written *during* the previous session, after startup, so it never
loaded — the settings watcher only watches directories that existed when the
session began. **A new session will load it normally**, since `.claude/` now
exists. If prompts still appear for allowlisted commands, the file is not
being read; check with `claude --debug`.

Run from the repository root (`/Users/joshwood/code/fr_public`), not from
`altona_wz4/`.

Not allowlisted on purpose: `curl` and other network fetches, `git commit`,
`git push`.

---

## Phase 2 — headless op runtime and metadata

Full plan in `docs/04-phase-headless-core.md`.

### Done: 2.1 — the `doc.hpp` split

The structural change of the whole project. Every operator source file used to
pull Altona's entire widget toolkit through `wz4lib/doc.hpp`, and `doc.hpp`
also included `util/shaders.hpp`, which is *generated* by the `asc` shader
compiler we do not build — so a TU that merely included `doc.hpp` failed
outright on this platform.

`doc.hpp` is now a two-line shim over `doc_core.hpp` (the document model, no
GUI) and `doc_gui.hpp` (`wHandle`, `wPaintInfo`, `wGridFrameHelper`,
`wCustomEditor`). Recorded in `wz4port/patches/04-doc-headless-split.md`.

The predicted main risk — `wType` and `wClass` holding pointers to GUI-facing
functions — cost nothing; forward declarations were enough for every one. The
real surprise was two *by-value* members of GUI-resident plain-data types
(`sGuiTheme` in `wEditOptions`, `sListWindowTreeInfo<>` in `wTreeOp`/`wPage`),
which needed the two verbatim extractions listed under the upstream footprint.

**Gate passed**, and it now runs on every build: `headless_core_gate`. See
gotcha 7.

### Done: 2.2 — headless operator generation

`wz4ops` gained `-headless`. It turned out that ~70% of the generated code is
GUI, wiki text and script bindings: `basic_ops.cpp` goes 6,230 → 1,850 lines,
`wz3_bitmap_ops.cpp` 8,884 → 2,166.

**Open question 1 answered: both.** The plan preferred a separate tool that
reused `wz4ops`' parser, specifically to avoid patching `wz4ops`. Measuring
killed that preference — `Document::Types`/`Ops` are private, so a separate
tool needs a patch anyway, and reimplementing the non-GUI half of
`output.cpp` would duplicate ~600 lines of offset-sensitive emission whose
failure mode is silently misaligned parameter data. So the headless `.cpp`
comes from `wz4ops -headless` (one code path for anything layout-determining)
and the metadata JSON will come from `wz4port/tools/opsmeta` reading the
now-public parse tree.

**Open question 3 answered: a signature rule.** `type` externals are skipped
only when the signature names `wPaintInfo`, `wGridFrameHelper` or
`wCustomEditor` — 12 of the 15 across the two files. A blanket skip would have
dropped `GenBitmap::Init()` (a `wType` virtual the headless build needs) and
both `Hit(wObject *,const sRay &,wHitInfo &)` overrides, which name nothing
outside `doc_core.hpp`. Every skip is printed.

**Gate passed**, and it runs on every build: `headless_ops_gate`.

### Done: 2.3 — metadata emission

`wz4port/tools/opsmeta` (`main.cpp`, `emit.cpp`, `json.cpp`), linking only
`tools/wz4ops/{parse,doc}.cpp`. **Schema frozen at `schemaVersion: 1`**; the
contract is `02-target-model.md` §6, the derivation `04` §2.3. No upstream
change was needed — patch 05 had already made `Document::Types`/`Ops` public.

Two things went beyond the plan, both because they paid for themselves at once:

- **The gate covers all 33 `.ops` modules**, not the two named ones. `opsmeta`
  validates as it goes, so the extra 31 cost milliseconds and lift coverage from
  ~450 parameters to 2,728. It found a crash on its first run that the gate
  modules never hit: the JSON writer had a fixed 16-level depth stack, and
  condition trees nest two levels per expression node. Now dynamic.
- **`opsmeta` validates rather than just emitting**, and writes nothing if a
  check fails. Offsets must not overlap (`wz4ops` checks this only inside the C++
  emitter, which we do not link); every unique choice label must round-trip
  through Altona's own `sFindFlag`; every conditional symbol must resolve.
  Verified with a deliberately broken `.ops`.

Three corrections it forced, all in `docs/architecture.md` A18/A20/A21:

- **The text-based widget census was wrong** — it counted commented-out
  operators and missed modifiers in non-canonical order. Don't grep a DSL when
  its parser is sitting right there.
- **Altona's float formatter is not correctly rounded**: `%.9f` renders `4.0f` as
  `4.00000023`. The emitter uses libc `snprintf`/`strtof` instead.
- **Conditionals were not the awkward part.** The parser already desugars
  `Flags.choicename` and flattens nested `if`. The awkward part was that
  parameters live in **three independent offset spaces**, which the original
  schema sketch collapsed into one.

### Done: 2.4 — `wz4core`, and the phase gate

`patches/06`. The runtime — `doc.cpp`, `build.cpp`, `basic.cpp`, `script.cpp`,
plus `util/image.cpp`, the two extracted `gui/` data files and the generated
headless `basic_ops` — builds as a GUI-free static library, compiled with the
poison header.

**The survey's line count was misleading again.** "`doc.cpp` has 16
GUI-touching lines" counted `sGui->` references and missed that the file
contains *all ~840 lines of the `wPaintInfo` implementation*. One line of it
mattered out of proportion: `new AlphaMtrl` at `doc.cpp:109` is the only use of
the asc-generated `wz4lib/wz4shaders.hpp`.

**Upstream had already built the switch we needed.** `doc.cpp` already carried
three `#if !sCOMMANDLINE` blocks, and `sCOMMANDLINE` is `sCONFIG_OPTION_SHELL`
— which this port has defined since phase 1. The painting half is bracketed
with the same guard rather than moved out; a `.cpp` has no consumers, so the
argument that forced the header split in 2.1 does not apply. **Look for
`sCOMMANDLINE` before inventing a mechanism.**

**Open question 2 answered: `script.cpp` is required.** `wExecutive::Execute`
drives `ScriptContext` directly at `doc.cpp:4050-4200`. It costs nothing
platform-wise and compiled headless first time.

**Extracting a declaration was not enough to link.** 2.1 moved `sGuiTheme`'s
declaration out of `gui/manager.hpp`; the definitions were still in
`gui/manager.cpp`. Two more extractions, of definitions this time:
`gui/theme.cpp` and `gui/palette.{hpp,cpp}`. The palette holds document-format
data that lived as a static member of `sColorPickerWindow`; making that member a
**reference to array** relocated the storage without touching any of its eight
uses. See `architecture.md` A23.

**Phase gate passed.** `core_connect` links `wz4core`, registers `basic`
(11 types, 38 classes), builds a document programmatically and lets `Connect()`
derive the graph from geometry alone — checking the §2.2 rules rather than just
printing them. 14 checks, 0 failures.

`headless_core_gate` was **not** promoted to a linked test as the plan said, on
purpose: its value is compiling a TU that includes *only* `doc_core.hpp`, and
linking `wz4core` would weaken exactly that.

### Deliberately absent from the headless build

Revisit when the new editor exists; recorded in `patches/05` and `patches/06`:

- parameter-panel `actions` — one calls `sSetClipboard`, which lives in
  `base/windows.hpp` and is implemented in `windows_xlib.cpp`, a file phase 1
  excluded;
- the `Screenshot` operator — renders the viewport and compares against a
  reference image. Headless sets `cmd->SetError(...)` instead;
- the 12 `type` externals that paint;
- viewport painting, handle manipulation, theme application, the progress bar,
  and the editor's golden-image comparison (`UnitTest::Test`, which needs
  `App->UnitTestPath`).

`sCheckBreakKey()` is an **upstream POSIX gap**, not something we broke: declared
for every platform, defined only in `system_win.cpp`. Filled by
`wz4port/compat/altona_missing.cpp` returning 0. When `wz4gen` exists, a SIGINT
handler reporting through it is how Ctrl+C should interrupt a long generation.

---

## Known open items

- **No reference oracle.** There is no working Werkkzeug4 build to diff
  against, and the bundled `.exe` is x86 Windows. Golden images from phase 4
  will capture *our* behaviour, not the original's — a plausible, stable port
  bug would pass. Mitigations: SSE2-vs-NEON bit parity, structural assertions
  for meshes, and visually diagnostic per-operator cases. Generating true
  reference output on a Windows machine remains worthwhile as a one-off; the
  `.wz4t` text format (phase 3) exists partly to make that cheap.
- ~~x86-64 side of SIMD parity unverified.~~ **Resolved in 4.4:** an x86-64 slice
  cross-compiles here and runs under Rosetta 2, compiling the real
  `<emmintrin.h>` path rather than sse2neon. All 90 goldens match byte for byte.
  Residual caveat: the instructions are executed by Rosetta's translation, not by
  Intel silicon, so running it once on a real x86-64 Linux box is still worth
  doing.
- ~~`Text` needs FreeType.~~ **Resolved in 4.5:** `compat/font_freetype.cpp`
  implements `sFont2D` and Altona's 2D software surface on FreeType, with no
  patch to Altona's font layer — the opaque `prv` pointer is the seam. `Text3D`
  and `Path3D` still need it *and* a tessellator; they are phase 6/7's, 2
  operators out of 84.
- ~~The runtime has never executed an operator.~~ **Resolved in 4.1/4.2:**
  `wDocument::CalcOp` runs real operator bodies and produces `wObject`s, and as
  of 4.2 the result is written out as a PNG and has been checked by eye. The
  prediction in this item held — `basic`'s operators are nearly all structural,
  so the texture library was indeed where `wExecutive::Execute` first got
  exercised.
- ~~`.wz4` document loading is untested.~~ **Resolved in 3.3:** all six bundled
  documents load, 7,090 operators, and every one is a `ctest` case. The
  serialisation surgery preserved the format.

---

## Phase 3 — text graph format and CLI

Full plan in `docs/05-phase-text-format.md`; target model in
`02-target-model.md` §4.

### Done: 3.3 — `.wz4` interoperation (taken out of order)

The plan puts the reader first. 3.3 went first instead, because phase 2 left
`.wz4` loading untested and stages 2.1/2.4 both had to work to keep the format
byte-compatible — so loading a real document was the cheapest, highest-value
check available, and doing it before building a text format on top de-risks the
rest of the phase.

**All six bundled documents load: 7,090 operators, 111 pages.** Each is now a
`ctest` case. `wz4gen` fails on a zero-operator load as well as on an outright
failure — `sLoadObject` can report success on a file it did not understand.

Errors are classified **by cause, not by message**. Two different messages share
one root cause: `UnknownOp` takes zero inputs, so operators it replaced report
"too many inputs"; and its output type is `AnyType`, so real consumers above it
report "input has wrong type". An operator is placeholder fallout if it or any
of its inputs is an `UnknownOp`. Residual across all six: **15 connection errors
and 1 unexplained**, every one a property of the documents (dangling `Load`
names, six duplicate store names in `screens4/test.wz4`).

### Done: destructive-write fix — `patches/07`

`wOp::Serialize_` substituted `UnknownOp` on read and wrote **that** name back,
so any build not knowing every module silently damaged the document. Since
phase 3 exists to write documents and all six bundled files contain unregistered
classes, every conversion would have been destructive.

`wOp` now retains `ForeignClass`/`ForeignType` and writes those back.
`wz4gen identity` verifies it and is a `ctest` case for all six documents —
4,899 operators and 221 distinct class identities preserved on `example.wz4`,
207 of them classes this build cannot load. Independently confirmed by reading
the *resaved* file back through `wz4gen list`.

**Guarantee is precise: identity and geometry survive a load/save; parameter
content of unregistered operators does not.** The reader discards their words,
strings, links and array data in four places because `UnknownOp` declares no
storage, and carrying that through is deliberately not done — it would be
fidelity for render-graph, material and effect operators that are out of scope.

Two corrections to what I wrote at the end of the last stage, both erring
towards alarm:

- "Two fields and three lines" understated it — I had looked only at the class
  name, not at the four other things the reader skips.
- "The §4.4 guarantee is not achievable" **misread this project's own phase
  plan**: the 3.4 gate already says "limited to the subgraphs whose classes we
  have registered". Re-read the gate before declaring it unmeetable.

### Reachability risk: measured and retired

Retaining the class name made the question answerable. Across the six documents,
**817 `GenBitmap` operators** become reachable once phase 4 registers
`wz3_bitmap`, and 1,424 `Wz4Mesh` for phase 6. In `example.wz4`: `Wz4Mesh` 1,424,
`Wz4Render` 1,299 (out of scope), **`GenBitmap` 624**, materials 779 (out of
scope). A real corpus, not a token one.

### Done: 3.1a — the metadata is readable at runtime

The reader stage split in two, because of a measurement: **`wClass` does not
know its own parameters.** It carries `ParaWords`/`ParaStrings` — a budget — and
nothing else. Names, kinds and offsets only ever existed in the generated
`MakeGui`, which `-headless` omits. So the stage-2.3 metadata is not a
convenience for the text format; it is the only parameter description that
survives headless, and it had to become readable first.

New target `wz4t` (not `libwz4core/wz4t_*.cpp` as the plan said — `wz4core` is
upstream sources compiled in place, and mixing ours in would blur the isolation
seam): a small general JSON reader, and `wMetaLibrary`. General JSON on purpose
— the ImGui editor reads the same files and would otherwise duplicate it.

`wz4gen describe` shows what `wClass` cannot. `wz4gen checkmeta` validates the
whole metadata **from the consumer side**: 370 classes, 2,728 parameters, 3,282
choice values, 13 table widgets, **0 problems**.

**The parameter count is the result worth noting: 2,728 matches opsmeta's own
count exactly**, reached independently by producer and consumer. It did not at
first — the reader silently ignored the `array` block and said 2,667. Two counts
that should agree disagreeing by 61 is how a phase-long bug starts, so the
reader now loads array rows too.

`checkmeta` asks two things opsmeta cannot, both reader questions: does a
`continues` parameter land on a word its owner actually declares, and does every
choice value fit its widget's mask once shifted (a choice escaping its mask would
have the editor writing a neighbour's bits).

**My first version of the check was wrong**, not the metadata: it demanded a
symbol from every parameter and failed on `label "Edit";`, `action "Invert" (1);`
and `fileout "Filename";` — all legal, since the DSL makes both label and name
optional.

Findings for 3.1b: exactly **one** storage-bearing parameter in the whole corpus
has no symbol (`TextObject.TextExport`'s `fileout`), so the reader needs a label
fallback for a single bounded case; and 9 classes leave words unaccounted for,
which is `padding` reserving them and is expected.

### Done: 3.1b — the `.wz4t` reader

`wz4t/wz4t_read.cpp`. Resolves classes through the metadata, runs `SetDefaults`
before applying settings so a file states only what it changes, and writes into
whichever of the three offset spaces the parameter lives in.

**Gate passed:** 19 checks, 0 failures. It checks the *derived graph*, not just
that parsing worked — and it checks that the reader **refuses** a missing header,
unknown version, unknown class, misspelled parameter, too many values, and an op
with no position. A parser that accepts anything would let a hand-written case
quietly test the defaults.

**Two grammar decisions the spec left open**, both now recorded in `02` §4.2:

- **Comments are `//`, not `#`.** The §4.2 example uses `#` as a trailing
  comment marker on one line and as the colour prefix two lines later. Only one
  can hold, and sScanner really does offer `#` comments, so the conflict was
  live. Colours keep `#`.
- **`#aarrggbb` works only because a hex run can be reassembled.** Measured:
  `#ff8040c0` lexes as one NAME, `#08ff0000` as INT + NAME, `#1e500000` as a
  **FLOAT**. All expose exact source text, so concatenating to eight hex digits
  reconstructs the literal. Had that failed, the fallback was a quoted form.

**Round-trip hazard found and closed early:** a freshly constructed `wDocument`
already owns one empty page, because the constructor calls `DefaultDoc()`
(`doc.cpp:2514`). Reading a two-page file gave *three* pages — which in 3.2
would have meant a round trip gaining a stray page every pass. The reader now
takes that default page over for the file's first `page`.

### Done: 3.2 — the writer

`wz4t/wz4t_write.cpp` plus `wz4gen convert`. Canonical output, sorted by `PosY`
then `PosX`, sugar expanded, and **only non-default parameters written** — safe
because `SetDefaults` (from `wz4ops`) and the metadata defaults (from `opsmeta`)
come from the same parse tree.

**Gate passed** over two cases: read → write → read comparing *every parameter
word, string and link*, plus writing twice giving byte-identical text.

Flags decoding needed three guards, all of which fall back to the raw integer:
a `continue flags` parameter puts more widgets on a word its owner declares (so
decoding gathers every widget at that offset, or the continued bits vanish on
write); an **ambiguous** label cannot be written because the reader resolves the
first match; and a label that is not a bare identifier (`16 Samples`) cannot
either.

### The false pass — worth reading before writing another round-trip test

The gate passed on a case containing `café °C — ΔΣ 中文` **while mangling it**:
`café` was stored as `cafÃ©`.

`sLoadText` decodes UTF-8 only when it finds a BOM (`system.cpp:1080`) and
otherwise takes each byte as a character. A hand-written `.wz4t` has no BOM, so
it read as Latin-1; the writer then re-encoded those characters as UTF-8. **That
corruption is idempotent after the first pass**, so read→write→read is perfectly
stable and every comparison passes.

Fixed by decoding UTF-8 in the reader regardless of BOM, and by writing
`sSaveTextUTF8` instead of `sSaveTextAnsi` — the latter truncated each character
to a byte and produced a file `grep` called **binary**, which defeats the whole
point of a text format.

The lasting fix is the test, though: it now asserts an **actual character value**
against a compiler-encoded literal. *A round trip being stable is not the same
as it being correct*, and only the second kind of check tells them apart.

### Done: 3.4 — the CLI, and the phase gate

`wz4gen` has `list`, `describe`, `checkmeta`, `convert`, `identity`, and `render`
stubbed for phase 4 (it loads the graph and resolves the store, then says what
is missing and exits non-zero, rather than pretending to be absent).

**Phase gate passed over all six documents.** On `example.wz4`: 4,899 operators,
418 of them registered, every identity and geometry preserved, every parameter of
every registered operator preserved, and every re-derived connection identical.

Parameters are compared only for registered operators, because Altona discarded
the rest at `.wz4` read time — the plan already scoped the gate that way.

**The reader needed a lenient mode** before `.wz4t` → `.wz4` could work at all:
the writer emits `GenBitmap.Text` for classes this build cannot load, and the
reader rejected them by design. `wWZ4T_ALLOWUNKNOWN` substitutes `UnknownOp` and
remembers the name, mirroring the binary reader. **Off by default** — in a
hand-written case an unknown class is a typo.

### A 1-ULP float drift the gate caught

`example.wz4` failed the first run: `0x3f9e9828 → 0x3f9e9827` on
`ScreenshotProxy.Screenshot`'s `Position`, `Target` and `Zoom`. The writer was
exact, so the loss was `sScanner::ScanFloat()` — **A18 again, on the parsing
side.** Fixed by parsing the token's exact source text through libc.

Worth remembering what that nearly cost: the text diff of two conversions looked
identical, and 5 of 6 documents passed. A phase 4 golden image would have drifted
for a reason nobody would think to look for in the *scanner*.

**Rule of thumb now established twice over: for anything numeric or textual
crossing a format boundary, do not use Altona's conversions.** `wFormatFloat`
and `wParseFloat` in `wz4t/json.hpp` are the ones to use.

---

## Phase 4 — texture library

Full plan in `docs/06-phase-texture.md`.

### Done: 4.1 — `wz4tex` builds, links and runs

`patches/08`. The 3,578-line pixel engine needed exactly what the survey said:
three MSVC-isms as shims in the force-included compat header (`__assume`,
`__stdcall`, `__forceinline`) and **one** upstream line —
`<emmintrin.h>` → `"simd_compat.hpp"`, because the real header hard-errors on
arm64 and takes `xmmintrin.h`/`mmintrin.h` with it.

`GenBitmap::Text` is stubbed behind `#if !WZ4PORT_HAVE_SFONT2D` (no `sFont2D`
backend for macOS); it leaves the bitmap untouched so a graph containing it still
evaluates. Stage 4.5 implements it on FreeType.

**The engine runs.** All 34 `GenBitmap` operators register; `wz4gen render`
evaluates one and reports its size, whether the result is uniform or structured,
and a checksum. Registering the module also dropped `example.wz4`'s unknown-class
count 4,687 → 3,854, and all six phase-3 round trips still pass — now comparing
far more real parameters.

"Non-zero pixels", the first metric tried, is worthless here: alpha is `0xffff`
almost everywhere, so every pixel is non-zero whatever the operator did.

### Done: `.wz4t` array syntax

`Gradient` rendered **black** because its colour stops live in a parameter array
and the format never defined row syntax. Closed in the same stage rather than
deferred, since 4.3 cannot write a `Gradient` case without it.

`element { Pos = 0  Color = #ff000000 }` blocks inside the operator body —
named after the editor's own group label, not `row`, which already means
side-by-side placement at the top level. Grammar in `02` §4.2b.

**Every field of a row is written**, unlike an operator's own parameters,
because `SetDefaultsArray` *interpolates float fields between neighbouring rows*
— so "the default" for a row field depends on its neighbours, and omitting one
would make a file's meaning depend on row order. `wz4t_round_tex_smoke` covers
it, and the round-trip snapshot now compares array rows word for word.

### Done: 4.2 — PNG output

`wz4gen render <doc> -op <name> -out <file.png>`. **No upstream change and no
new patch**: `sImage::SavePNG` was already complete (`util/image.cpp:2744`, doing
the BGRA→RGBA swizzle and calling `stbi_write_png_to_mem`), `image.cpp:23`
already compiles `stb_image_write.h`, and `GenBitmap::CopyTo(sImage*)` sizes the
target itself. The whole stage was ~15 lines in `tools/wz4gen/main.cpp`.

`tests/tex/chain.wz4t` is the gate: `Flat` → `GlowRect` → `Twirl` → `Blur` at
256×256, with **every step named** so each operator renders on its own instead of
being inferred from the end of the chain. Reviewed by eye — uniform dark blue
field; hard-edged white square over it; corners dragged round into spiral arms;
the same shape softened. Four `ctest` cases.

Worth knowing for 4.4: the PNG bytes are reproducible across runs, so a
byte-exact golden comparison will work. Not asserted yet — that is 4.4's job and
asserting it now would pre-empt the review that stage exists to do.

### Done: 4.3 — a reviewed case for every operator

74 cases in seven `ops_*.wz4t` files, covering 31 of the 34 operators. Grouped by
family rather than one file per operator, because **the comparison is the test**:
`ops_filter`'s blur and sharpen read the same brick wall and `ops_merge`'s twelve
modes read the same input pair, so "these two look identical" becomes a
detectable failure. Full table in `06-phase-texture.md`.

`Merge` has **12** blend modes, not the 22 the phase plan claimed in three
places. Corrected.

### The alpha trap — read this before writing another image test

**Four operators zero the alpha channel**: `Color sub`, `Color invert`,
`Merge sub`, `Mask sub`. They operate on all four channels at once, so an opaque
input comes out fully transparent — and **a transparent PNG displays as plain
white**, which is indistinguishable from an operator that filled the image with
white and indistinguishable from one that did nothing.

Three of the four cases were written expecting visible output. The first,
`color_invert`, was reviewed as "a white square" before the cause was found.
Nothing objected: right size, stable checksum, "structured" content, valid PNG.

Closed on both sides:

- `wz4gen render` reports the alpha range and flags `renders blank`. The
  threshold is `amax < 0x0100`, **not** `== 0` — `Mask sub` leaves alpha at
  `0x0001` of `0x7fff`, exactly as invisible as zero, and would pass an equality
  test. `0x0100` is what survives `CopyTo`'s narrowing to 8 bits, which is what
  reaches the file.
- Every case must not render blank unless it declares that it does (`REJECT` in
  `render_png.cmake`). The **negative** assertion is the load-bearing one, since
  all the positive checks passed. The four that legitimately do assert their
  alpha range, and each has a companion case with alpha restored by a trailing
  `Color add #ff000000` so the RGB result is reviewable.

Recorded as `architecture.md` A40, the third instance of A33's shape: when
something else renders the deliverable, correctness lives at that boundary.

### Five things the eye caught that a checksum would not

All five ran, produced plausible images, and were wrong or misdescribed:

- **`Mask`'s input 0 is the MASK**, not one of the two images. Written the way
  the name implies, the case rendered a smooth blue-to-lavender ramp in which one
  of its three inputs made no contribution at all. Now: read the `code {}` block
  before writing any multi-input case — doing that for `Bump` established
  (surface, normals) and it worked first time. `architecture.md` A41.
- **`Perlin`'s `FadeOff` decides whether it looks like Perlin at all.** At the
  default 1 every octave has equal weight and the top one dominates, so the first
  draft looked like television static. 0.5 is the 1/f weighting.
- **`Unwrap`'s `polar2normal` and `normal2polar` are the opposite way round**
  from what the names suggest; `normal2polar` is the dartboard.
- **`Dots`' `Count` is a density**, `Size*Count/4096` — 24 means 96 dots at
  128×128.
- **`Gradient`'s `step` mode** holds each stop until the next, so a stop at
  `Pos=1` has zero width and never appears.

The habit that made those catchable: **write the prediction before looking, and
make it specific enough to be wrong.** A comment saying "a blend of the inputs"
would have been satisfied by every one of them.

`linear` versus `smoothstep` is documented in the case file as genuinely subtle —
they differ by checksum, not visibly. Saying so beats implying the eye can
separate them.

### Layout is semantic, so test files need deliberate gaps

Vertical adjacency **is** connection, so a generator placed directly under the
bottom edge of an unrelated group becomes its consumer and fails with "too many
inputs" — which is what `Bricks` at row 8 did under a `GlowRect` ending at row 8.
An op's default width is 3 (`wz4t_read.cpp:755`), which caught `Atlas` too: a
3-wide consumer under three 3-wide sources overlaps only the first and silently
packs one tile.

`wz4gen describe` now prints array blocks. Without it the tool implied that an
operator with array rows had none — which is how a `Gradient` came to be written
with no stops in 4.1.

### Done: 4.4 — goldens locked, and SSE2/NEON parity actually measured

87 goldens in `tests/tex/golden/`, each an image plus a `.txt` report line.
`ctest` is 119 tests, green on **both** architectures against the same goldens.

**The parity check did not need a Linux box.** An x86-64 slice cross-compiles on
Apple silicon and runs under Rosetta 2, and `simd_compat.hpp` already routes
x86-64 to the real `<emmintrin.h>`. No new harness was needed either — the
checked-in goldens *are* the cross-platform contract. `parity_x86_64.sh`.

**First run: 8 of 87 diverged, and it was not sse2neon.** It was **FMA
contraction**: clang's default `-ffp-contract=fast` fuses `a*b+c` into one FMA
where the target has one (arm64 always; that x86-64 target not), and an FMA
rounds once where two operations round twice. One ULP of float, quantised into a
different 16-bit sample. Every divergent case was float-touching — Perlin's and
GlowRect's `sFPow` gamma tables, `Unwrap`'s coordinates, all the lighting.

`-ffp-contract=off` is in `altona_flags` **for determinism, not speed**. The
comment there says so; without it, it reads like a stray optimisation flag.

`simd_parity` passed on both architectures the whole time. It checks 43
intrinsics against scalar models and structurally could not see this, because
none of it was in the intrinsics. A unit-level parity test does not subsume an
end-to-end one. `architecture.md` A42.

Building the second architecture also found a real bug in our own compat header:
`#define stat64 stat` collides with the x86-64 macOS SDK's `struct stat64`, which
arm64 never declares. Fixed by including `<sys/stat.h>` before the alias.

### A golden must cover the pipeline's precision, not the artefact's

The pipeline is 16 bits per channel; a PNG is 8. So an image-only golden is blind
to anything in the low half of every pixel — and that is not hypothetical:

- `MakeWz3Bitmap` requantises through an 8-bit `sImage`, moving its checksum
  while leaving the PNG **byte-identical** to its source. Image-only, it would
  read as a permanent no-op.
- **7 of the 8 parity divergences had byte-identical images.** Image-only, the
  suite would have reported full parity and the FMA problem would have shipped.

So each golden is `<case>.png` plus `<case>.txt` holding size,
uniform/structured, alpha range and a checksum over all 16 bits — report checked
first. `architecture.md` A43.

**Both failure paths were verified by deliberately breaking them** before the
lock was trusted: a one-hex-digit checksum edit, and a swapped golden image. A
golden that cannot fail is worth nothing, and a green suite cannot tell you which
kind you have. `wz4gen diff` reports differing-pixel count, worst delta per
channel, and writes an amplified difference image.

### Done: 4.5 — Text on FreeType, and the image import paths

**Altona's font layer needed no patch.** `sFont2D` keeps its state behind an
opaque `sFont2DPrivate *prv`, which is exactly the seam needed to implement the
class from outside its own translation unit. Altona declares the whole 2D
software drawing layer in `base/windows.hpp` and defines it only in
`windows.cpp` (GDI) and `windows_xlib.cpp` (X11) — neither of which this build
compiles — so the symbols were simply absent and
`compat/font_freetype.cpp` supplies them.

Two upstream changes, both about *reaching declarations*, neither in the font
layer (`patches/09`):

- **`enum sGuiColor` → `gui/guicolor.hpp`.** `Text` names `sGC_BLACK` and
  `sGC_MAX`, which sat in `gui/window.hpp` beside `sWindow`. The enum has no GUI
  dependency — 22 integers. Relocated, not duplicated, because **phase 5 puts a
  GUI on the texture library** and two definitions would then collide in one
  translation unit. Same shape as patches 04 and 06.
- **Two guarded includes** in `wz3_bitmap_code.cpp`. `base/windows.hpp` pulls
  only `types.hpp` and `serialize.hpp` and never names `sWindow`, so the poison
  tripwire is untroubled — whole-identifier matching is what makes that safe.

`Text` renders legible glyphs first try. Three cases: `"wz4"` centred, a
two-line `"port\n4.5"` for newline handling and `Leading`, and a styled variant
that is visibly oblique and heavier.

**`Text` is the one case in the suite that is not golden-locked.** Its `Font`
parameter is a family *name*, so the glyphs come from whatever font the machine
has — a different file on Linux, and a different file after an OS update. A
byte-exact golden there is a false-failure generator, not a test. It asserts
structurally instead (`wz4_tex_case_nolock`) and is excluded from
`tex-cases.txt` so the lock step cannot pick it up. Vendoring a font would
upgrade it; that is a repository decision, left open rather than taken.

### "Found" is not "links"

`find_package(Freetype)` succeeds when cross-compiling the x86-64 slice and
hands back Homebrew's **arm64-only** dylib; the link then fails with a wall of
undefined `FT_` symbols. Detection now compiles *and links* a two-line program
with the found library, which is the only check that distinguishes the two. The
parity build falls back to the 4.1 stub cleanly — which is why it runs 122 tests
and arm64 runs 125.

Third instance of the same lesson, after A39 and A43: **assert the thing you
need, not a proxy for it.** A path that exists is a proxy.

### Import, and one working directory rather than two

Three golden-locked cases; stb_image is deterministic and the data is committed.

- `import_png` is **byte-identical** to `ops_io`'s `src_bricks`, so
  `GenBitmap → sImage → PNG → stb_image → GenBitmap` loses nothing at 8 bits.
- `import_jpg` differs in 15,893 of 16,384 pixels, worst delta 41 of 255 — the
  JPEG decoder really runs, and really is lossy.
- `import_anim` packs four 32×32 frames into a 64×64 atlas. `LoadAtlas` finds the
  last digits in the filename and increments until a file is missing, so frames
  must be a contiguous run; `data/make_anim.wz4t` regenerates them.

`Import` requires **power-of-two** dimensions, which is why the test image is
128×128.

The tool's working directory is now pinned to `build/tex-png` for **both** the
runner and the lock step. It had been the build root in one and `tex-png` in the
other, which went unnoticed until a case needed to *read* data — then the lock
step could not find files the tests saw perfectly well.

### What the review pass changed

4.4 exists to establish correctness by eye and it earned the slot. Five cases
were not testing anything:

- **`ColorBalance` was invisible** on the shared saturated ramp — every channel
  already clipped at 0 or max, so a per-band lift/gain had nowhere to move. It
  changed the checksum and nothing else. Own greyscale source now. *A test case
  is not a preset.*
- **`Merge`'s pair was blowing out**, so `add` and `addsmooth` read as flat
  white. Cells now peak at `0x90`; costs `mul`/`min`/`max` nothing.
- **`light_point` was a white blob**, then over-corrected to flat grey. A point
  light on a flat plane only varies when it is close to it.
- **`bump_*` wanted the opposite** — a broad light, to show relief rather than
  the light's shape. The two groups deliberately differ.
- **The shared sources were never rendered**, so nothing could be compared to
  them. Six are cases now.

And four descriptions were wrong in ways only the images showed:

- **`brightness` and `hardlight` are the same operation**, written twice with
  different ways of building the same mask — 12 labels, 11 behaviours. Now
  asserted byte-identical by `tex_merge_identity`, which is a free consistency
  check across two different intrinsics.
- **`over` does not reproduce its top layer exactly**: `mulhi_epi16(d,0x7fff)<<1`
  is 0.99997, so 78 of 16384 pixels sit one step off. Measured.
- **`premul alpha` shares `alpha`'s constant** in the mode table; the difference
  is a trailing `PreMulAlpha()`.
- **`scale` is `mul` shifted 11 instead of 15** — 16× the gain, so mid-grey means
  ×8 and saturation.

`gradient_linear` vs `gradient_smooth` is documented as **not** separable by eye,
to stop the next reviewer calling the mode broken.

### Assert on the artefact, not the exit code

The PNG tests could have matched `wz4gen`'s `wrote <path>` line on stdout.
`tests/tex/render_png.cmake` instead deletes any previous output, renders, then
checks the file exists, exceeds 256 bytes, and starts with the PNG signature —
three extra lines of CMake that immediately found something a stdout match never
would: **`SavePNG` does not create its output directory**, so all four tests
failed their first run on a missing `build/tex-png/`.

Same lesson as the phase-3 false pass, where a round trip was stable and wrong.
Both times the test was watching the wrong end of the operation.

### Three diagnosis lessons from 4.1

- **`__forceinline` produced eight errors, six of which named the wrong file.**
  "use of undeclared identifier `sMulShift12`" sent me hunting for missing
  helpers in `util/rasterizer.cpp`; they were defined in `genvector.cpp` itself,
  ten lines above, behind a keyword clang could not parse. Check whether a
  "missing" symbol is defined locally before extracting anything.
- **`Size = 64, 64` is two values for ONE word.** `Size` is a `flags` parameter
  with two controls packed at shifts 0 and 8, so the reader now assigns
  comma-separated values positionally, one per control — which is what `02` §4.2
  always showed. It also removes the label ambiguity, since each value resolves
  within its own control.
- **A numeric choice label beats a raw number.** `Size`'s labels are `"1"` …
  `"8192"` and label `"64"` has control value 6. So a bare `64` means the label,
  and the writer always emits the label rather than the value — writing `8` for
  value 3 would read back as label `"8"`, a different size.

### Still to use from phase 2

- ~~The metadata is the parameter vocabulary.~~ Done in 3.1a — and it turned out
  to be mandatory rather than convenient.
- **`sCheckBreakKey` wants a SIGINT handler.** `wz4port/compat/altona_missing.cpp`
  returns 0 today. Making it report a `SIGINT` flag is how Ctrl+C should
  interrupt a long generation from the CLI, and the executive already polls it
  per command.
