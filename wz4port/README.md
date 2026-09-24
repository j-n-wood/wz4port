# wz4port

Portable Werkkzeug4 generators for macOS (Apple Silicon) and Linux.

All new code lives here. `altona_wz4/` is upstream and is compiled in place;
see [patches/](patches/) for the complete list of changes made to it.

Planning and reference documentation is in [`../docs/`](../docs/) —
start with `00-overview.md`, then `01-existing-model.md` (how Werkkzeug4
works) and `02-target-model.md` (what we are building). The six notes below
are the build mechanics; `../docs/architecture.md` is the full decision record
behind them, including the alternatives that were rejected.

**To use the editor, see [`../docs/editor.md`](../docs/editor.md)** — the panes,
the shortcuts, and glTF export.

## Build

### Prerequisites

A C++ compiler (clang or gcc), **CMake ≥ 3.20** and **Ninja**. On macOS:

```sh
xcode-select --install          # clang, and the OpenGL framework
brew install cmake ninja        # required
brew install freetype           # optional — see below
```

Dear ImGui and GLFW are **vendored** in [`third_party/`](third_party/) and need
no installation. FreeType is an **optional system package**: without it
`GenBitmap.Text` and `Text3D` stay stubbed and everything else builds. OpenGL
comes from the platform; without it the editor is skipped and the headless tools
still build. See [`third_party/VENDORED.md`](third_party/VENDORED.md).

The day-to-day platform is macOS on Apple silicon. Linux is a target, but a
native Linux build has not been run yet; there, GLFW additionally needs the X11
or Wayland development headers.

### Configure, build, test

From the repository root:

```sh
cmake -S wz4port -B wz4port/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C wz4port/build
ctest --test-dir wz4port/build --output-on-failure
```

or, equivalently, from inside `wz4port/`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
ctest --test-dir build --output-on-failure
```

`cmake` prints what it found. On a full build the last lines include
`FreeType links: GenBitmap.Text is enabled` and `editor: wz4ed will be built`;
if either says otherwise, that feature is being skipped, not failing. Warnings
from Altona's sources are expected; errors are not.

A few of the tests open a window, which needs a graphical session. Over ssh or
in headless CI, configure with `-DWZ4_GUI_TESTS=OFF`.

### If it fails

| Symptom | Cause and fix |
|---|---|
| `command not found: cmake` (or `ninja`) | Not installed, or not on `PATH` in this shell. Homebrew's tools are in `/opt/homebrew/bin`; a shell or IDE terminal that has not run `brew shellenv` will not see them. |
| `does not appear to contain CMakeLists.txt` | `-S` is not pointing at `wz4port/`. The two forms above differ only in the working directory. |
| `CMake was unable to find a build program corresponding to "Ninja"` | `cmake` was found but `ninja` was not (`which ninja`). Install it, then delete `build/` — a failed configure leaves a cache behind. |
| `Does not match the generator used previously` | `build/` was configured with another generator. Delete it (`rm -rf build`) and configure again. |
| `attempt to use a poisoned identifier` | A GUI header reached the headless core — a real error in a change, not a setup problem. See note 4 below. |

## Running it

The build leaves everything in `build/`; nothing is installed. The two programs
are **`build/wz4gen`** (headless: inspect, convert and render documents) and
**`build/wz4ed`** (the editor). Put `build/` on your `PATH`, or call them by path
as below.

Both need the operator metadata that the build writes to `build/meta/`. That
path is compiled into them, so `-meta` is only needed to use metadata from
somewhere else.

Try it from `wz4port/`, with the example documents in `tests/`:

```sh
./build/wz4gen list                                            # every registered operator
./build/wz4gen list tests/tex/smoke.wz4t -stores               # what a document contains
./build/wz4gen describe GenBitmap.Perlin                       # one operator's parameters
./build/wz4gen render tests/tex/smoke.wz4t -op noise -out noise.png   # a texture
./build/wz4gen render tests/geo/gen.wz4t -op cube -out cube.glb       # a mesh, as glTF
./build/wz4ed tests/geo/gen.wz4t                               # open it in the editor
```

`-op` names a **store** — an operator given a name inside the document; `list
<doc> -stores` shows them. `render` without `-out` evaluates and reports but
writes nothing. More documents to open are in `tests/geo/` and `tests/tex/`.

**Switches go after the filename.** Altona's command-line parser takes the token
after a `-switch` as that switch's argument, so `wz4ed -wire doc.wz4t` treats
the document as the argument to `-wire` and opens nothing.

### `wz4gen`

| Command | What it does |
|---|---|
| `list` | every registered operator, by output type |
| `list <doc>` | the operators in a document, with a class tally. Switches: `-stores` store names, `-pages` pages, `-inputs` each operator's derived inputs, `-unknown` unregistered classes, `-errors` connection and calc errors |
| `describe <Class>` | the full parameter description of one operator, e.g. `GenBitmap.Perlin` |
| `render <doc> -op <store> [-out <file>]` | evaluate one store and report it. `-out` by extension: `.png` for a texture; `.obj`, `.gltf` (with a `.bin` beside it) or `.glb` for a mesh |
| `convert <in> <out>` | `.wz4` ↔ `.wz4t`, direction from the extensions |
| `sweep <doc> [-v] [-classes]` | evaluate every store and check every mesh against the invariant battery |
| `diff <a.png> <b.png> [-out <diff.png>]` | compare two images: how much, where, and a difference image |
| `checkmeta [-verbose]` | load all the metadata and check it hangs together |
| `identity <doc.wz4> <scratch.wz4>` | load, save, reload, and check every operator kept its class |

Every command that reads a `.wz4t` also takes `-meta <dir>`. `wz4gen` with no
arguments prints this list.

### `wz4ed`

```sh
./build/wz4ed [<document.wz4t>] [switches]
```

| Switch | Effect |
|---|---|
| `-select <store>` | select that operator at startup |
| `-export <file.glb>` | export the selected operator as glTF and exit (needs `-select`) |
| `-shot <file.png>` | render, save the frame as a PNG, and exit |
| `-frames <n>` | render *n* frames and exit |
| `-time <pct>` | animation time, 0–100 |
| `-wire`, `-bbox`, `-guides` | start with wireframe / bounding box / connection guides on |
| `-nobones`, `-notex` | start with the skeleton overlay / texture off |
| `-meta <dir>` | use metadata from another directory |
| `-help` | usage |

The panes, keyboard shortcuts and glTF export are in
[`../docs/editor.md`](../docs/editor.md).

## Current state — phases 1–8 complete

**The Werkkzeug operator runtime builds and runs with no GUI, no graphics API
and no window system.** On top of it: a text graph format, texture and mesh
generation, an ImGui editor with a 3D preview, and glTF export.

| Target | What it is |
|---|---|
| `altona_base` | Altona's shell subset: types, math, serialisation, system, blank renderer |
| `altona_util` | The scanner slice the host tools need |
| **`wz4core`** | **The operator runtime: doc, build, basic, script, generated `basic_ops`** |
| `wz4ops` | Upstream's `.ops` code generator, built natively, with `-headless` |
| `opsmeta` | **Ours.** `.ops` → metadata JSON, using wz4ops' parser but not its emitter |
| `wz4ops_gate` | Regenerates `basic_ops` and `wz3_bitmap_ops` into `build/generated/` |
| `headless_core_gate` | Compiles `wz4lib/doc_core.hpp` alone, with the GUI poisoned |
| `headless_ops_gate` | Generates and compiles both op modules `-headless`, GUI poisoned |
| `opsmeta_gate` | Emits and validates metadata for all 33 `.ops` modules into `build/meta/` |
| **`wz4t`** | **Ours. JSON reader + writer, the runtime metadata model, and the `.wz4t` reader + writer** |
| `wz4tex` | The texture library: `wz3_bitmap`, and `GenBitmap.Text` when FreeType links |
| `wz4geo` | The mesh library: `wz4_anim`, `wz4_mesh`, plus our 2D tessellator for `Text3D`/`Path3D` |
| **`wz4geochk`** | **Ours. The mesh invariant battery and the glTF writer** |
| **`wz4gen`** | **Ours. The headless CLI** |
| **`wz4ed`** | **Ours. The editor — see [`../docs/editor.md`](../docs/editor.md)** |
| `core_connect` | Phase 2 gate: links `wz4core`, derives a graph from geometry (`ctest`) |
| `wz4t_read`, `wz4t_round_*` | Stage 3.1b/3.2 gates over hand-written cases (`ctest`) |
| `docround_*` | Phase 3 gate: `.wz4` → `.wz4t` → `.wz4` over six documents (`ctest`) |
| `load_*`, `identity_*` | Every bundled document loads, and survives a load/save (`ctest`) |
| `checkmeta` | The metadata reads back consistently (`ctest`) |
| `simd_parity` | Verifies all 43 SSE2 intrinsics against scalar models (`ctest`) |

The table above lists the phase 1–3 gates; the suite has grown to 164 tests and
`ctest --test-dir build` runs all of them. `../docs/progress.md` is the current
state in detail.

Still out of scope, and not merely deferred: the sequencer, the render graph, the
material system, post-processing, audio, video, packfiles and networking. See
`../docs/00-overview.md`.

## Six things that are not obvious

### 1. `altona_config.hpp` lives here, not in `altona_wz4/`

`base/types.hpp:67` does `#include "../altona_config.hpp"`, and upstream
expects you to create that file inside `altona_wz4/altona/`. We do not.

A quoted include that fails to resolve next to the including file is retried
against each `-I` directory **with the relative path intact**. So with
`compat/include` on the include path:

```
compat/include/../altona_config.hpp  ==  compat/altona_config.hpp
```

`compat/include/.keep` exists to guarantee that directory is present. This is
why `altona_wz4/` needs no config file and stays clean.

### 2. `system_linux.cpp` is compiled unmodified on macOS

Altona's `sPLAT_LINUX` really means "POSIX desktop" — 21 guards across the
tree spell that as `(sPLAT_WINDOWS || sPLAT_LINUX)`. Introducing a separate
`sPLAT_APPLE` would mean revisiting every one of them for no behavioural
gain, so macOS builds as `sPLAT_LINUX`.

The glibc-isms that macOS lacks (`lseek64`, `mmap64`, `stat64`, `strdupa`,
`pthread_yield`, …) are supplied by `compat/include/wz4port_posix_compat.h`,
force-included into every translation unit. `compat/include/linux/joystick.h`
is a stub for the same reason — macOS has no `/dev/input/js*`, so the
joypad scan simply finds nothing.

Only four lines could not be handled this way; see `patches/01`.

### 3. `wz4ops` must be run with a bare filename, and switches go last

`wz4ops` derives *both* its output paths *and* its generated function names
from the input path with the extension stripped. Invoking it as
`wz4ops a/b/basic_ops.ops` emits `void AddTypes_a/b/basic_ops(...)`, which
does not compile.

Separately: Altona's shell parser treats the token after a `-switch` as that
switch's first parameter, so `wz4ops -headless x.ops` leaves
`sGetShellParameter(0,0)` empty and prints the usage text. Write
`wz4ops x.ops -headless`.

The CMake `wz4_add_ops()` function therefore copies each `.ops` into
`build/generated/<subdir>/` and runs the tool there with just the filename,
which yields `AddTypes_basic_ops` / `AddOps_basic_ops` — the names the
`sREGOPS` macro (now `wz4lib/doc_core.hpp:70-74`) expands to. It also keeps
generated output out of `altona_wz4/`.

### 4. The GUI-free document model is enforced by a poisoned build

Phase 2 split `wz4lib/doc.hpp` into `doc_core.hpp` (the document model, no
GUI) and `doc_gui.hpp` — see `patches/04`. Nothing about the include path
keeps `doc_core.hpp` GUI-free afterwards: the real `gui/` headers are still on
it and, on macOS, they parse cleanly. A green build would prove nothing.

So `tests/gui_poison.h` is force-included into the `headless_core_gate` and
`headless_ops_gate` targets and nowhere else. It `#pragma GCC poison`s
`sWindow`, `sGui_` and `sSimpleMaterial` — three tokens that between them
cover every header in `gui/` and the generated `util/shaders.hpp`. Reintroduce
a GUI dependency and the build fails naming the file.

`gui/theme.hpp` and `gui/treeinfo.hpp` are the two pure-data extractions
`doc_core.hpp` is allowed to use; they include nothing but `base/`.

### 5. `sCOMMANDLINE` is already defined here, and already does a lot of work

`base/types.hpp:605` defines `sCOMMANDLINE` as `sCONFIG_OPTION_SHELL`, which
`altona_flags` sets. Altona and `wz4lib` already guard window-dependent code
with `#if !sCOMMANDLINE` — including the painting half of `wz4lib/doc.cpp` and
three function bodies in `basic.cpp`, which is how `wz4core` is GUI-free without
those files being split. See `patches/06`.

Check for this before adding a mechanism to strip display code.

### 6. `wz4ops_gate` looks redundant next to `headless_ops_gate`. It is not.

`wz4ops_gate` regenerates the operator modules *without* `-headless` into
`build/generated/`, and nothing compiles them. It exists to prove the flag is
inert when it is off: the two generated trees must differ only in what
`-headless` suppresses, and the pre-patch output was diffed against it. Delete
it and that guarantee goes with it. See `patches/05`.

## Layout

```
compat/          altona_config.hpp, POSIX shim, stub headers
  include/       on the include path; see note 1 above
  altona_missing.cpp   sCheckBreakKey — declared everywhere, defined only for Windows
patches/         every change made to altona_wz4/, with rationale
tools/opsmeta/   .ops -> metadata JSON
tools/wz4gen/    the headless CLI
wz4t/            JSON, runtime metadata, .wz4t read/write
tests/           gates + gui_poison; cases/ holds hand-written .wz4t
build/           compiled output (gitignored)
  generated/     wz4ops output, non-headless — the inertness proof, note 5
  generated-headless/  wz4ops -headless output, compiled by headless_ops_gate
  meta/          opsmeta output; wz4lib/ and wz4frlib/ are the gate modules,
                 corpus/ is the other 31 (coverage, nothing consumes it)
```
