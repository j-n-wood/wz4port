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

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

Requires clang (or gcc), CMake ≥ 3.20 and Ninja.

Dear ImGui and GLFW are **vendored** in [`third_party/`](third_party/) and need
no installation. FreeType is an **optional system package**: without it
`GenBitmap.Text` and `Text3D` stay stubbed and everything else builds. OpenGL
comes from the platform; without it the editor is skipped and the headless tools
still build. See [`third_party/VENDORED.md`](third_party/VENDORED.md).

## Current state — phases 1–8 complete

**The Werkkzeug operator runtime builds and runs with no GUI, no graphics API
and no window system.** On top of it: a text graph format, texture and mesh
generation, an ImGui editor with a 3D preview, and glTF export.

```sh
wz4gen list                        # registered operators
wz4gen list doc.wz4 -stores        # what is in a document
wz4gen describe GenBitmap.Perlin   # the full parameter description
wz4gen convert doc.wz4 doc.wz4t    # and back
wz4gen render doc.wz4t -op name -out mesh.glb     # or .gltf, .obj, .png
wz4ed doc.wz4t -meta build/meta                   # the editor
```

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

The table above lists the phase 1–3 gates; the suite has grown to 161 tests and
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
