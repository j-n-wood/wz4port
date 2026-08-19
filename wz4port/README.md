# wz4port

Portable Werkkzeug4 generators for macOS (Apple Silicon) and Linux.

All new code lives here. `altona_wz4/` is upstream and is compiled in place;
see [patches/](patches/) for the complete list of changes made to it.

Planning and reference documentation is in [`../docs/`](../docs/) —
start with `00-overview.md`, then `01-existing-model.md` (how Werkkzeug4
works) and `02-target-model.md` (what we are building).

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

Requires clang (or gcc), CMake ≥ 3.20 and Ninja. No external libraries yet.

## Current state — phase 1 complete

| Target | What it is |
|---|---|
| `altona_base` | Altona's shell subset: types, math, serialisation, system, blank renderer |
| `altona_util` | The scanner slice the host tools need |
| `wz4ops` | Upstream's `.ops` code generator, built natively |
| `wz4ops_gate` | Regenerates `basic_ops` and `wz3_bitmap_ops` into `build/generated/` |
| `simd_parity` | Verifies all 43 SSE2 intrinsics against scalar models (`ctest`) |

Not yet built: the headless op runtime, the texture library, the CLI, the
editor. Those are phases 2 onward.

## Three things that are not obvious

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

### 3. `wz4ops` must be run with a bare filename

`wz4ops` derives *both* its output paths *and* its generated function names
from the input path with the extension stripped. Invoking it as
`wz4ops a/b/basic_ops.ops` emits `void AddTypes_a/b/basic_ops(...)`, which
does not compile.

The CMake `wz4_add_ops()` function therefore copies each `.ops` into
`build/generated/<subdir>/` and runs the tool there with just the filename,
which yields `AddTypes_basic_ops` / `AddOps_basic_ops` — the names the
`sREGOPS` macro (`wz4lib/doc.hpp:51-56`) expands to. It also keeps generated
output out of `altona_wz4/`.

## Layout

```
compat/          altona_config.hpp, POSIX shim, stub headers
  include/       on the include path; see note 1 above
patches/         every change made to altona_wz4/, with rationale
build/           generated + compiled output (gitignored)
```
