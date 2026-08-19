# wz4port

Portable Werkkzeug4 generators for macOS (Apple Silicon) and Linux.

All new code lives here. `altona_wz4/` is upstream and is compiled in place;
see [patches/](patches/) for the complete list of changes made to it.

Planning and reference documentation is in [`../docs/`](../docs/) —
start with `00-overview.md`, then `01-existing-model.md` (how Werkkzeug4
works) and `02-target-model.md` (what we are building). The five notes below
are the build mechanics; `../docs/architecture.md` is the full decision record
behind them, including the alternatives that were rejected.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

Requires clang (or gcc), CMake ≥ 3.20 and Ninja. No external libraries yet.

## Current state — phases 1 and 2 complete

**The Werkkzeug operator runtime builds and runs with no GUI, no graphics API
and no window system.** `core_connect` proves it end to end: it constructs a
document programmatically and lets the runtime derive the graph from block
geometry alone.

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
| `core_connect` | Phase 2 gate: links `wz4core`, derives a graph from geometry (`ctest`) |
| `simd_parity` | Verifies all 43 SSE2 intrinsics against scalar models (`ctest`) |

Not yet built: the texture library, the `.wz4t` text format, the CLI, the editor.

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
tests/           simd_parity, headless_core, core_connect + gui_poison
build/           compiled output (gitignored)
  generated/     wz4ops output, non-headless — the inertness proof, note 5
  generated-headless/  wz4ops -headless output, compiled by headless_ops_gate
  meta/          opsmeta output; wz4lib/ and wz4frlib/ are the gate modules,
                 corpus/ is the other 31 (coverage, nothing consumes it)
```
