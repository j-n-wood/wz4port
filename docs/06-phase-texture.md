# Phase 4 — Texture library and test suite

**Goal:** every Werkkzeug4 texture operator runs headlessly on macOS arm64 and Linux x86-64,
with a reviewed per-operator test case and a locked golden image.

This completes the core of **priority 1**. After this phase the generators work; phase 5 puts
a UI on them.

---

## Scope

| Component | LOC | Notes |
|---|---:|---|
| `wz4frlib/wz3_bitmap_code.cpp` | 3,578 | All pixel algorithms |
| `wz4frlib/wz3_bitmap_code.hpp` | 133 | `class GenBitmap` |
| `wz4frlib/wz3_bitmap_ops.ops` | 1,421 | 37 operators, plus algorithm code in `code {}` blocks |
| `wz4frlib/genvector.cpp` / `.hpp` | 249 / 53 | Scanline polygon rasteriser for the `Vector` operator |

Dependencies measured: **zero** graphics, **zero** GUI, **zero** Win32. One external include,
`<emmintrin.h>`, handled by the phase 1 SIMD shim. Only op-framework dependency is `wObject`,
via `GenBitmap : BitmapBase : wObject`.

### Bitmap representation

```cpp
class GenBitmap : public BitmapBase {
  sU64 *Data;                 // one sU64 per pixel = 4 x sU16 channels
  sInt XSize, YSize, Size;
  BitmapAtlas Atlas;
};
```

16 bits per channel, fixed point with `0x8000` = 1.0, plain heap array, entirely CPU.
Conversion out via `CopyTo(sImage*)` (8-bit RGBA) and `CopyTo(sImageI16*)`.

### The 37 operators

**Generators:** `Flat`, `Perlin`, `Cell`, `Gradient`, `Import`, `ImportAnim`, `GlowRect`,
`Dots`, `Atlas`, `Bricks`, `Text`, `Vector`, `Paste`.

**Colour:** `Color`, `Range`, `HSCB`, `Bitcrusher`, `Merge` (22 blend modes), `PreMulAlpha`,
`Mask`, `ColorBalance`.

**Convolution:** `Blur`, `Sharpen`, `Downsample`.

**Resample / warp:** `Rotate`, `RotateMul`, `Twirl`, `Distort`, `Unwrap`, `Bulge`.

**Lighting:** `Normals`, `Light`, `Bump`.

**I/O and conversion:** `Export`, `MakeTexture2`, `MakeWz3Bitmap`.

---

## Stages

### 4.1 — Build `libwz4tex` — **done**

Compile `wz3_bitmap_code.cpp` + `genvector.cpp` + generated `wz3_bitmap_ops` against
`libwz4core`, using the SIMD shim.

Two operators are stubbed initially and revisited in stage 4.5:

- **`Text`** — calls `sFont2D` (`wz3_bitmap_code.cpp:2528`), Altona's OS font wrapper, which
  has GDI and X11 backends but none for macOS.
- **`LoadAtlas`** — uses `sImage` loading paths worth checking separately.

Also note `Bitmap_Inner` is declared `__stdcall` (`wz3_bitmap_code.cpp:17`) but is plain C++;
the calling convention is meaningless on both targets and is simply dropped.

**Gate — passed.** Target is `wz4tex`; recorded as `wz4port/patches/08-texture-library.md`.
It compiles, links, and **runs**: `wz4gen render` evaluates an operator and reports its size
and non-zero pixel count. `Flat`, `Perlin`, `Cell` and `Blur` each produce a full 64×64 bitmap
from `tests/tex/smoke.wz4t` — the first time this port has executed a generator. Four `ctest`
cases, failing on `1 x 1` or `0 of` so an operator that runs but writes nothing cannot pass.

All 34 `GenBitmap` operators register. `example.wz4`'s unknown-class count drops 4,687 → 3,854,
and all six phase-3 document round trips still pass, now comparing far more real parameters.

The survey's dependency measurement held: three MSVC-isms (`__assume`, `__stdcall`,
`__forceinline`) went into the force-included compat header as shims, and the only upstream
change was one line — `<emmintrin.h>` → `"simd_compat.hpp"`, because the real header
hard-errors on arm64.

`GenBitmap::Text` is stubbed behind `#if !WZ4PORT_HAVE_SFONT2D`, leaving the bitmap untouched
so a graph containing it still evaluates. `LoadAtlas` needed no stub.

#### Found here and closed here: `.wz4t` could not express parameter arrays

`Gradient`'s colour stops live in a `array { float Pos; color Color; }` block, and the format
has no syntax for array rows — `02-target-model.md` §4 never defined one. A `Gradient` with no
rows renders black, which is what the smoke test showed before the case was changed.

Scope is bounded: **two** of the 34 texture operators use arrays, `Gradient` and `Vector`.
Corpus-wide it is 13 classes. So 32 operators can have real cases in 4.3 without this, but
those two cannot, and `Gradient` is too central to leave out.

**Done, in this stage** rather than deferred, because 4.3 cannot write a `Gradient` case
without it. Grammar and rationale in `02-target-model.md` §4.2b; `element` blocks inside the
operator body, named after the editor's own group label to avoid colliding with the top-level
`row`. The metadata already carried the array descriptor from 3.1a, so nothing new was needed
there.

Every field of a row is written, unlike an operator's own parameters, because
`SetDefaultsArray` *interpolates float fields between neighbouring rows* — so "the default" for
a row field depends on its neighbours, and omitting one would make the file's meaning depend on
row order.

`wz4t_round_tex` covers it, and the round-trip snapshot now compares array rows word for word.
Verified the `element` blocks really are in the written text rather than being dropped by both
sides — the A33 lesson applied.

Two related format bugs surfaced at the same time, both from `Size = 64, 64` failing:
`Size` is one word holding two controls, so comma-separated values are positional; and a
numeric choice label beats a raw number. Both in `02` §4.2c.

### 4.2 — `wz4gen render` for bitmaps

Complete the `render` command: evaluate a named operator, `CopyTo` an `sImage`, write PNG via
the `stb_image_write.h` already vendored in `altona/main/util/`.

**Gate:** a three-operator `.wz4t` chain renders to a PNG that looks correct.

### 4.3 — Per-operator test cases

One `.wz4t` case per operator, in `wz4port/tests/tex/`. Design rules:

- **Small and fast** — 128×128 or 256×256, no long chains.
- **Visually diagnostic** — the output should make the operator's behaviour obvious. A `Blur`
  case blurs something with hard edges; a `Twirl` case twirls a grid; a `Merge` case shows all
  22 blend modes over a known pair.
- **Deterministic** — explicit seeds everywhere.
- **Parameter coverage** — where an operator has modes, exercise each. `Merge` gets 22 outputs,
  `Cell` gets its inner/outer/cell-colour modes, `Perlin` gets its mode flags.

Each case gets a one-line comment saying what a correct result looks like.

**Gate:** every operator has a case; all render without crashing.

### 4.4 — Golden lock and the runner

Review every output by eye, once, carefully — this is the moment correctness is established
and it deserves proper attention. Then lock the PNGs into `wz4port/tests/tex/golden/`.

Runner: render every case, compare byte-exact against its golden, report failures with a
side-by-side diff image.

Add the **SIMD parity check**: the same suite must produce **bit-identical** output on the
SSE2 path (x86-64 Linux) and the NEON path (arm64 macOS). This is the sharpest correctness
signal available to us and it is nearly free.

**Gate — phase gate.** Full suite green on both platforms, bit-identical between them.

### 4.5 — Font and image import

- **`Text`** — implement `sFont2D`'s glyph rasterisation on FreeType (already installed on
  this host, and available everywhere). Confine to `wz4port/compat/font_freetype.cpp`; do not
  patch Altona's font layer.
- **`Import`/`LoadAtlas`** — verify the `stb_image` path works on both platforms.

Both get test cases. `Text` output will not be pixel-identical to GDI output, so its golden is
established fresh here rather than treated as a port of existing behaviour — noted explicitly
in the test comment.

**Gate:** `Text` renders legible glyphs; `Import` loads PNG and JPG.

---

## The honest limitation

There is no reference build to diff against. The golden images capture **our** behaviour, not
the original's. A port bug that is plausible-looking, visually reasonable and stable would
pass this suite.

What the suite does catch, reliably: regressions from our own later changes, SIMD porting
errors (via bit-parity), crashes, and anything that produces visibly wrong output.

What would close the gap, if a Windows machine becomes available: build the original
`werkkzeug4.exe`, load the same cases converted to `.wz4`, export, and diff. The `.wz4t`
format and the `convert` command exist partly to make that a cheap one-off rather than a
project. Worth doing once; not worth blocking on.

---

## Deliverables

- `wz4port/libwz4tex/`
- `wz4port/compat/font_freetype.cpp`
- `wz4port/tests/tex/` — cases, goldens, runner
- `wz4gen render` complete for bitmaps

## Risks

| Risk | Assessment |
|---|---|
| sse2neon semantic differences on saturating/rounding integer ops | **The main technical risk.** Bit-parity testing against x86 is the mitigation, and it is decisive: either the outputs match or they do not |
| Fixed-point 16-bit arithmetic depends on x86 overflow behaviour | Low. The code is intrinsic-based, not assembly, and the intrinsics have defined semantics |
| `Text` diverges too far from GDI to be useful | Low. Font rasterisation differences are cosmetic; the operator's job is to put glyphs in a bitmap |
| Golden review is done carelessly and locks in a bug | **Real, and process-level rather than technical.** Mitigated by making each case visually diagnostic rather than a soup of operators, and by reviewing them as a deliberate stage rather than in passing |
