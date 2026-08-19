# Phase 1 — Toolchain and portable base

**Goal:** Altona's non-graphics core builds under clang on macOS arm64 and on Linux x86-64,
and the `wz4ops` code generator runs natively.

**Why first:** nothing else can be attempted until `.ops` files can be compiled to C++ on
this host, and this phase is small, mechanical, and proves the ground is solid before any
structural work begins.

---

## Starting position

Verified by syntax-checking with clang 17 on arm64, using the flags from Altona's own
`base/Makefile.linux.boot`:

- `base/types.cpp`, `types2.cpp`, `serialize.cpp`, `math.cpp`, `system.cpp`,
  `system_linux.cpp`, `graphics.cpp`, `graphics_blank.cpp` all produce **exactly one error**:
  `'../altona_config.hpp' file not found`.
- That file is gitignored upstream and created by a documented setup step.
- The Latin-1 source files compile without complaint; the high bytes are all in comments.

So the language-level porting work is close to zero. The work is build plumbing and a small
system layer.

---

## What changed during implementation

Four things turned out differently from the plan above. Recorded here because
this document is the plan of record.

**No `sPLAT_APPLE`.** The plan called for adding a platform id. Measurement
showed 21 guards across the tree spell "POSIX desktop" as
`(sPLAT_WINDOWS || sPLAT_LINUX)`, so a new platform would have meant
revisiting every one of them for no behavioural gain. macOS now builds as
`sPLAT_LINUX`, and the two `pthread_t` sites in `system.hpp` that branch on
it (`:370`, `:422`) are already correct for macOS pthreads. **Upstream change
avoided entirely.**

**No `compat/system_osx.cpp`.** The plan called for a copy of
`system_linux.cpp` (2,274 lines) adapted for macOS. Instead the glibc-isms
are handled by a force-included shim
(`compat/include/wz4port_posix_compat.h`: `lseek64`, `mmap64`, `ftruncate64`,
`stat64`, `strdupa`, `pthread_yield`, `O_LARGEFILE`) plus a stub
`compat/include/linux/joystick.h`. That reduced the upstream change from a
2,274-line fork to **four lines** — see `wz4port/patches/01`.

**`altona_config.hpp` never enters `altona_wz4/`.** A quoted include that
fails to resolve next to its including file is retried against each `-I`
directory with the relative path intact, so `compat/include` on the include
path makes `#include "../altona_config.hpp"` resolve to
`compat/altona_config.hpp`. Verified before relying on it.

**Two upstream patches were unavoidable**, both genuine language/type errors
rather than portability preferences: the four `pthread_t` casts (`patches/01`)
and a friend declaration carrying a default argument (`patches/02`).

Three smaller corrections to the analysis above:

- `sCONFIG_GUID` **is** required. The earlier claim that it appears only in
  makefiles came from a `grep` that silently skipped `types.hpp` as binary —
  the exact Latin-1 trap documented in `01-existing-model.md`. It is used at
  `types.hpp:2143` and defined only for iOS. Now supplied by our config header.
- `graphics.cpp` and `graphics_blank.cpp` **are** needed even for the host
  tools: `types.cpp` references `sRender3DFlush()`.
- Altona writes `"..."sTXT(x)` with no separating space, which C++11 reads as
  a user-defined literal. Handled with `-Wno-reserved-user-defined-literal`.

## Stages

Each stage is separately reviewable.

### 1.1 — Config header and CMake skeleton

- `wz4port/compat/altona_config.hpp`, derived from `altona_wz4/altona/altona_config_sample.hpp`
  with every SDK disabled (`sCONFIG_SDK_DX9`, `DX11`, `CG`, `XSI`, `GECKO` all 0).
- `wz4port/CMakeLists.txt` with an `altona_base` target compiling the shell subset:
  `types.cpp`, `types2.cpp`, `serialize.cpp`, `math.cpp`, `system.cpp`, `graphics.cpp`,
  `graphics_blank.cpp`, `input2.cpp`, plus the platform system file.
- Compile options mirroring the upstream Linux bootstrap: `-fno-exceptions -fno-rtti
  -fshort-wchar -fno-strict-aliasing`, and the defines `sCONFIG_RENDER_BLANK`,
  `sCONFIG_OPTION_SHELL`, plus a `sCONFIG_GUID` value.
- Include path arranged so `compat/` precedes upstream, giving us an override mechanism that
  avoids editing upstream files where a header shim will do.

**Gate:** `altona_base` compiles on macOS arm64 with the Linux system file, even if it does
not yet link or run.

### 1.2 — Platform identity

Upstream patch, additive:

- Add `sPLAT_APPLE` to the platform enum, `altona/main/base/types.hpp:37-46`. The slot is
  already reserved with the comment *"not implemented. intendet for MacOS X"*.
- Set `sCONFIG_SYSTEM_OSX` / `sPLATFORM` in the selection block at `types.hpp:211-228`, which
  currently falls macOS through to the Linux branch.
- Extend the `sPLATFORM==sPLAT_WINDOWS || sPLATFORM==sPLAT_LINUX` guards in
  `base/windows.hpp` at lines 94, 111, 128, 149 — the templated clipboard helpers, which
  would otherwise silently compile to no-ops.

Recorded as `wz4port/patches/01-plat-apple.md`.

**Gate:** platform macros resolve correctly; a trivial program reports the right platform.

### 1.3 — macOS system layer

`wz4port/compat/system_osx.cpp`, modelled on `altona/main/base/system_linux.cpp` (2,274 LOC).
That file is pthreads, mmap, dirent, fnmatch, poll and signal — the large majority compiles on
macOS unchanged.

Scope is the **shell subset only**: no window system, no input devices, no sound. Specifically
needed: memory, file I/O (including the memory-mapped path), directory enumeration, threads
and synchronisation, timing, and the debug/log output path.

Known divergences to handle:

- `<linux/joystick.h>` — excluded; no input devices in scope.
- `/proc` usage — replace with `sysctl` or stub.
- `syslog` semantics — stub to `stderr`.
- `clock_gettime(CLOCK_MONOTONIC)` is available on modern macOS; no fallback needed.

Lives in `compat/`, not upstream: it is a new file, so nothing is patched.

**Gate:** a console program using Altona file I/O, threads and timing runs correctly on macOS.

### 1.4 — Wide-character correctness

`-fshort-wchar` makes `wchar_t` 2 bytes, which is what Altona assumes and what keeps `L"..."`
literals working. It is unsafe only where Altona calls libc wide-character functions, because
the system's own `wchar_t` is 4 bytes.

There are exactly **three** such call sites:

| File | Line | Call |
|---|---|---|
| `base/system_linux.cpp` | 101 | `wcstombs` |
| `base/system_linux.cpp` | 174 | `mbstowcs` |
| `base/windows_xlib.cpp` | 297 | `wcstombs` |

All three convert between Altona strings and UTF-8. Altona already ships its own converters
for exactly this — `sCopyStringToUTF8` / `sCopyStringFromUTF8`, declared at
`base/types.hpp:1030-1031`. Replace the three calls.

This avoids the alternative, which would be switching `sCONFIG_UNICODETYPE` to `char16_t` and
rewriting every `L"..."` literal in the codebase to `u"..."` — thousands of edits for no gain.

Recorded as `wz4port/patches/02-widechar.md`.

**Gate:** round-trip a non-ASCII string through Altona's UTF-8 conversion and back.

### 1.5 — SIMD shim

`wz4port/compat/simd_compat.hpp`:

```cpp
#if defined(__aarch64__) || defined(_M_ARM64)
  #define SSE2NEON_SUPPRESS_WARNINGS 1
  #include "sse2neon.h"
#else
  #include <emmintrin.h>
#endif
```

Vendor `sse2neon.h` (MIT) into `wz4port/third_party/`.

The texture generator uses 338 intrinsic calls across 43 distinct intrinsics, **all SSE2
integer operations** (`_mm_mulhi_epi16`, `_mm_adds_epi16`, `_mm_shufflelo_epi16`, and so on).
sse2neon covers all of them. There is no inline assembly anywhere in wz4.

**Done.** `sse2neon.h` is vendored at `wz4port/third_party/sse2neon.h`,
**pinned to tag v1.9.1** (MIT, 11,222 lines) rather than tracking master, so
the phase 4 bit-parity check compares against a fixed translation and both
platforms agree. Homebrew also ships 1.9.1, but vendoring keeps the build
reproducible from the repository alone and identical on Linux.

`wz4port/tests/simd_parity.cpp` checks **all 43** intrinsics against
independent scalar models — not merely that they compile. Coverage is the
full edge-value cross product (0, 1, 0x7fff, 0x8000, 0x8001, 0xffff, 0x00ff,
0xff00) plus a 2,000-iteration deterministic pseudorandom sweep, weighted
toward the cases where a NEON translation could plausibly differ without
looking wrong: saturating add/sub (signed and unsigned), rounding average,
multiply-high, `madd`, saturating pack, and arithmetic right shift.

Result on arm64: **70,184 checks, 0 failures.**

The test carries a `-DWZ4PORT_SIMD_MUTATE` switch that drops the rounding
term from the `_mm_avg_epu16` model. Building with it yields 2,058 failures
out of 2,064 cases — the six survivors being those where the sum is even and
rounding cannot matter. This exists because a parity test that cannot fail
would lend false confidence to everything in phase 4 that rests on it.

**Gate:** passed on arm64. The x86-64 half of the comparison belongs to
phase 4, where the same generators must produce bit-identical *texture*
output on both architectures.

### 1.6 — Build `wz4ops`

`wz4ops` (3,407 LOC) depends only on `base` and `util`, and upstream already ships a Linux
bootstrap makefile for it, so this should be a straightforward CMake target over the original
sources. The `util` slice needed is small — `scanner.cpp` and `scanconfig.cpp`, per upstream's
`util/Makefile.linux.boot`.

**Gate — phase gate.** `wz4ops` runs natively and regenerates `wz3_bitmap_ops.cpp/.hpp` and
`basic_ops.cpp/.hpp`. Byte-compare the output against a second host if one is available.

---

## Deliverables

- `wz4port/CMakeLists.txt`, `wz4port/compat/{altona_config.hpp, system_osx.cpp, simd_compat.hpp}`
- `wz4port/third_party/sse2neon.h`
- `wz4port/patches/01-plat-apple.md`, `02-widechar.md`
- Targets: `altona_base`, `altona_util_slice`, `wz4ops`

## Risks

| Risk | Assessment |
|---|---|
| `system_osx.cpp` larger than expected | Moderate. The Linux file is 2,274 LOC but much is I/O and threading that is POSIX-identical. Scope is deliberately the shell subset |
| `-fshort-wchar` breaks something unforeseen | Low. Only three libc wide-char sites exist; Altona is explicitly designed not to call the OS outside `base` |
| Altona's global `operator new` overload conflicts with libc++ | Low-moderate. Altona2 hit this (`throw()` vs `noexcept`); Altona 1 did not surface it in syntax checks, but it may appear at link time. Fix is one line if so |
| sse2neon semantic mismatch on saturating/rounding ops | Low, and the phase 1.5 test is specifically there to catch it early |
