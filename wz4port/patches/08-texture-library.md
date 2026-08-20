# Patch 08 — compile the texture library

**Files:** 1 upstream (`wz4frlib/wz3_bitmap_code.cpp`), plus three shims in
`wz4port/compat/`
**Phase:** 4 (texture library), stage 4.1
**Status:** applied
**Wider context:** `docs/architecture.md` A36

## Why

`wz3_bitmap_code.cpp` is the whole 3,578-line pixel engine behind all 34
`GenBitmap` operators. The phase-4 survey measured its dependencies as zero
graphics, zero GUI, zero Win32, one external include. That held up — the file
needed **three** things, and only one of them was a patch.

## Shims, not patches — three MSVC-isms

All three went into `wz4port/compat/include/wz4port_posix_compat.h`, which is
force-included into every translation unit. None is an upstream change.

| Spelling | Used at | Translation |
|---|---|---|
| `__assume(false)` | `wz3_bitmap_code.cpp:637` | `__builtin_assume` — same promise, different compiler |
| `__stdcall` | six functions in `wz3_bitmap_code.cpp` | empty. Altona's own `sSTDCALL` is already empty for POSIX (`base/types.hpp:573`); this file writes the raw keyword instead |
| `__forceinline` | `genvector.cpp:19`, `:24` | `inline`. Altona maps `sINLINE` to it only on MSVC (`base/types.hpp:292`) |

The `__forceinline` one is worth a note about diagnosis. It produced eight
errors: two "unknown type name" and six "use of undeclared identifier
`sMulShift12`". I went looking for the missing helpers, found them defined only
as file-local functions in a *different* translation unit
(`util/rasterizer.cpp`), and was about to extract them into a shared header —
before checking `genvector.cpp` itself, where both are defined at lines 19-27.
Clang could not parse the definitions because the return type was preceded by an
unknown keyword, so every call site then failed too. **One cause, eight errors,
and the six loudest ones pointed at the wrong file.**

## The one patch: `<emmintrin.h>`

```cpp
- #include <emmintrin.h>
+ #include "simd_compat.hpp"
```

On arm64 the real header hard-errors — *"This header is only meant to be used on
x86 and x64 architecture"* — and takes `xmmintrin.h` and `mmintrin.h` down with
it. `wz4port/compat/include/simd_compat.hpp` selects sse2neon there and the real
intrinsics everywhere else.

**An include-path override was considered and rejected.** A
`compat/include/emmintrin.h` using `#include_next` would need no upstream change
at all, which is the order this project normally prefers. But it would shadow a
system header for *every* translation unit in the build, to fix one line in one
file. Same reasoning as patch 04's rejection of shadowing `gui/listwindow.hpp`:
the rule is the least surprising thing that works, and a one-line include change
in a file patches 05 and 06 already touch is less surprising than that.

## `GenBitmap::Text` is stubbed

Guarded with `#if !WZ4PORT_HAVE_SFONT2D`, which is undefined and therefore 0.
The body needs Altona's `sFont2D` — an OS font wrapper with GDI and X11 backends
and none for macOS — plus a 2D render target this build never creates.

The stub leaves the bitmap untouched rather than failing, so a graph containing a
`Text` operator still evaluates and everything downstream of it can be inspected.
Define `WZ4PORT_HAVE_SFONT2D=1` when `compat/font_freetype.cpp` lands in stage
4.5.

Four lines added, wrapping the existing body untouched.

## Verified

- `wz4tex` compiles and links; `wz4gen` registers **all 34 `GenBitmap`
  operators** alongside `basic`.
- **The engine runs.** `wz4gen render` evaluates an operator and reports its
  size and non-zero pixel count — `Flat`, `Perlin`, `Cell` and `Blur` each
  produce a full 64×64 bitmap. This is the first time anything in this port has
  executed a generator. Four `ctest` cases, which fail on `1 x 1` or `0 of` so
  that an operator which runs but writes nothing cannot pass.
- Registering the module dropped `example.wz4`'s unknown-class count from 4,687
  to 3,854, and **all six phase-3 document round trips still pass** — now
  comparing far more real parameters than before.

## Behaviour

None on Windows or for a GUI build: every change is either a shim active only
where the MSVC keyword is absent, or guarded by a macro that is off by default.
