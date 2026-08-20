# Progress — session bootstrap

Read this first when picking the project up cold. It records where things
stand, what has been decided and why, and what would otherwise have to be
rediscovered the hard way.

**Last updated:** phase 3, stage 3.3 + the class-identity fix.
**Status:** Phases 1 and 2 complete and verified. The operator runtime links and
runs headless. **Phase 3 stage 3.3 done first (out of order, deliberately): all
six bundled `.wz4` documents load** — 7,090 operators, 111 pages — confirming the
phase-2 serialisation surgery preserved the format. A destructive-write bug found
there is fixed (`patches/07`). Stages 3.1 (reader) and 3.2 (writer) next.

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
| 3 — Text graph format + CLI | **In progress.** 3.3 done (first, deliberately). 3.1/3.2 next |
| 4 — Texture library + tests | Not started |
| 5 — Texture GUI | Not started |
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
| `wz4gen` | The headless CLI. `list` works; `describe`/`convert`/`render` are phase 3 |
| `core_connect` | Phase 2 gate: links `wz4core`, derives a graph from geometry (`ctest`) |
| `load_*` (6 tests) | Every bundled `.wz4` document must load and be non-empty (`ctest`) |
| `identity_*` (6 tests) | And survive a load/save/reload with every class intact (`ctest`) |
| `simd_parity` | Verifies all 43 SSE2 intrinsics against scalar models |

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
  compat/altona_missing.cpp    sCheckBreakKey — an upstream POSIX gap
  tools/opsmeta/               phase 2 stage 2.3 — .ops -> metadata JSON
    main.cpp  emit.cpp  json.cpp
    opsmeta.hpp  json.hpp
  tools/wz4gen/main.cpp        phase 3 — the headless CLI: list, identity
  tests/
    simd_parity.cpp
    headless_core.cpp          phase 2 stage 2.1 gate
    core_connect.cpp           phase 2 gate — links wz4core, derives a graph
    gui_poison.h               tripwire, force-included into every headless target
  third_party/sse2neon.h       pinned v1.9.1, MIT, 11,222 lines
.gitignore                     new, repo root
.claude/settings.local.json    gitignored tool allowlist
```

---

## Upstream footprint

**59 files** (`git diff --name-only 8c8f82c -- altona_wz4`, **run after
staging** — `git diff` does not see untracked files, which is how an earlier
count came out at 56 and missed three additions).

The isolation invariant is that `git status` on `altona_wz4/` must never show
anything not listed in `wz4port/patches/`.

Five categories, worth keeping distinct. Only the last three — 27 files — are
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
- **x86-64 side of SIMD parity unverified.** `simd_parity` passes on arm64.
  It should also be run on x86-64 Linux, where it exercises native SSE2 — the
  two must agree.
- **`Text` / `Text3D` / `Path3D` operators** need FreeType (and a tessellator
  for the 3D ones). Deferred; 3 operators out of 84.
- **The runtime has never executed an operator.** Phase 2 proved the runtime
  *links* and that the graph is derived correctly, but `core_connect` never calls
  `wDocument::CalcOp`, so no operator body has run and no `wObject` has been
  produced. The `basic` module's operators are almost all structural (`Nop`,
  `Group`, `Store`, `Load`), so the first real execution comes with the texture
  library in phase 4 — that is where `wExecutive::Execute` gets exercised.
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

### Still to use from phase 2

- **The metadata is the parameter vocabulary.** `.wz4t` needs to name parameters
  and values; `build/meta/*.json` already carries every symbol, kind, choice
  label and default. Read it rather than re-deriving.
- **`sCheckBreakKey` wants a SIGINT handler.** `wz4port/compat/altona_missing.cpp`
  returns 0 today. Making it report a `SIGINT` flag is how Ctrl+C should
  interrupt a long generation from the CLI, and the executive already polls it
  per command.
