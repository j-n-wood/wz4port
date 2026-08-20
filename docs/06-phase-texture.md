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

**Colour:** `Color`, `Range`, `HSCB`, `Bitcrusher`, `Merge` (12 blend modes), `PreMulAlpha`,
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
It compiles, links, and **runs**: `wz4gen render` evaluates an operator and reports its size,
whether the result is uniform or structured, and a checksum. `Flat`, `Perlin`, `Cell` and
`Blur` each produce a full 64×64 bitmap from `tests/tex/smoke.wz4t` — the first time this port
has executed a generator. Five `ctest` cases asserting `uniform` for `Flat` and `structured`
for the rest, so an operator that ran but silently produced a flat fill cannot pass.

("Non-zero pixels", the obvious metric, is useless here: alpha is `0xffff` almost everywhere,
so every pixel is non-zero whatever happened.)

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

### 4.2 — `wz4gen render` for bitmaps — **done**

Complete the `render` command: evaluate a named operator, `CopyTo` an `sImage`, write PNG via
the `stb_image_write.h` already vendored in `altona/main/util/`.

**Gate — passed.** `tests/tex/chain.wz4t` is `Flat` → `GlowRect` → `Twirl` → `Blur` at
256×256, every step named so each operator can be rendered on its own instead of being
inferred from the end of the chain. Reviewed by eye: a uniform dark blue field; a hard-edged
white square composited over it; the square's corners dragged round into spiral arms; the
same shape with its edges softened. Four `ctest` cases via `tests/tex/render_png.cmake`.

This stage was much smaller than planned, because the survey of what needed writing was
wrong in our favour: **`sImage::SavePNG` is already complete** (`util/image.cpp:2744` — it
does the BGRA→RGBA swizzle and calls `stbi_write_png_to_mem`), `image.cpp:23` already
compiles `stb_image_write.h`, and `GenBitmap::CopyTo(sImage*)` sizes the target itself. The
change was confined to `tools/wz4gen/main.cpp`; no upstream change, no new patch.

`Flat` rendering as blue rather than orange is the end-to-end check on channel order —
`#aarrggbb` in the text through `GenBitmap`'s 16-bit fixed point to the PNG's byte order —
which nothing before this stage could have caught.

The output is byte-identical across runs. That is not yet asserted; 4.4 is where it becomes a
golden, and asserting it here would pre-empt the review that stage exists to do.

#### Found here: the tests must check the file, not the exit code

The first run failed all four cases on a missing `build/tex-png/` — `SavePNG` does not create
its output directory. A `PASS_REGULAR_EXPRESSION` on the tool's `wrote <path>` line would not
have found it, and would equally have passed on a zero-byte or truncated file. The runner
deletes any previous output, then checks existence, size and the PNG signature. Recorded as
`architecture.md` A39, and it is the same lesson as A33.

### 4.3 — Per-operator test cases — **done**

One `.wz4t` case per operator, in `wz4port/tests/tex/`. Design rules:

- **Small and fast** — 128×128 or 256×256, no long chains.
- **Visually diagnostic** — the output should make the operator's behaviour obvious. A `Blur`
  case blurs something with hard edges; a `Twirl` case twirls a grid; a `Merge` case shows all
  12 blend modes over a known pair.
- **Deterministic** — explicit seeds everywhere.
- **Parameter coverage** — where an operator has modes, exercise each. `Merge` gets 12 outputs,
  `Cell` gets its inner/outer/cell-colour modes, `Perlin` gets its mode flags.

Each case gets a comment saying what a correct result looks like.

**Gate — passed.** **74 cases** over seven files, covering **31 of the 34** operators; `Import`,
`ImportAnim` and `Text` are 4.5's, as planned. All render, all reviewed by eye.

Grouped by family rather than one file per operator, because the comparison is the test:
`ops_filter.wz4t`'s blur and sharpen read the *same* brick wall, and `ops_merge.wz4t`'s twelve
modes read the same input pair, so "these two look identical" is a detectable failure. One
file per operator would have made every source subtly different.

| File | Cases | Covers |
|---|---:|---|
| `ops_gen.wz4t` | 16 | Flat, Perlin ×3, Cell ×3, Gradient ×3, GlowRect ×2, Dots, Bricks, Vector, Atlas |
| `ops_color.wz4t` | 19 | Color ×6 (+2 alpha-restored), Range ×3, HSCB, Bitcrusher, ColorBalance, PreMulAlpha, Mask ×4 |
| `ops_merge.wz4t` | 13 | Merge, all 12 modes (+1 alpha-restored) |
| `ops_filter.wz4t` | 4 | Blur, Sharpen, Downsample ×2 |
| `ops_warp.wz4t` | 9 | Rotate ×2, RotateMul, Twirl, Unwrap ×3, Bulge, Distort |
| `ops_light.wz4t` | 11 | Normals ×5, Light ×3, Bump ×3 |
| `ops_io.wz4t` | 2 | Export, MakeWz3Bitmap |

`ctest` is **109 tests** in total, up from 35.

`Merge` has **12** modes, not the 22 this document claimed in three places
(`wz3_bitmap_ops.ops:676` — a 4-bit field with twelve labels). Corrected above.

#### Found here: four operators zero the alpha channel, and a blank PNG looks white

`Color sub`, `Color invert`, `Merge sub` and `Mask sub` all take alpha to zero (or, in Mask's
case, to `0x0001`) because they operate on all four channels. The saved PNG is then fully
transparent — which **displays as plain white** and is indistinguishable by eye from an
operator that filled the bitmap with white or did nothing at all.

Three of the four were written expecting visible output, and the first draft of `color_invert`
was reviewed as a white square before the cause was understood. This is precisely the
"plausible, stable, wrong" failure the *honest limitation* section below warns about, arriving
in stage 4.3 rather than 4.4.

Closed on both sides:

- `wz4gen render` now reports the alpha range and flags `renders blank`. The threshold is
  `amax < 0x0100`, not `== 0`, because that is what survives `CopyTo`'s narrowing to 8 bits —
  `Mask sub`'s `0x0001` is exactly as invisible as zero and would have passed an equality test.
- Every case must NOT render blank unless it says so (`REJECT` in `render_png.cmake`). The four
  that legitimately do assert their alpha range explicitly, and each has a companion case with
  alpha restored by a trailing `Color add #ff000000` so the RGB result is actually reviewable.

#### Five things the review corrected that a checksum would not have

Each of these was a case that ran, produced a plausible image, and was wrong or misdescribed:

- **`Mask`'s input 0 is the mask**, not one of the two images (`out = GRAY(in0)`, then blend
  in1/in2 by it). Written the way the name implies, the case produced a blue-to-lavender ramp
  with one of its inputs absent from the output entirely.
- **`Perlin`'s `FadeOff` decides whether it looks like Perlin noise at all.** At the default 1
  every octave has equal weight, the highest dominates, and the result is white noise. The
  first draft of the case looked like static.
- **`Unwrap`'s `polar2normal` and `normal2polar` are the other way round** from the reading
  their names suggest; `normal2polar` is the one that produces a dartboard.
- **`Dots`' `Count` is a density**, `Size*Count/4096`, so 24 means 96 dots at 128×128.
- **`Gradient`'s `step` mode** holds each stop's colour until the next, so a stop at `Pos=1` has
  zero width and never appears — the case showed two bands where it claimed three.

`linear` versus `smoothstep` is recorded in the case file as a known-subtle pair: they differ
by checksum but not visibly at 128×128 with three stops. Saying so is better than implying the
eye can separate them.

#### Also found: layout is semantic, so test files need deliberate gaps

Vertical adjacency *is* connection, so a generator placed directly under the bottom edge of an
unrelated group becomes its consumer and fails with "too many inputs". `Bricks` at row 8 under
a `GlowRect` ending at row 8 did exactly that. An op's default width is 3
(`wz4t_read.cpp:755`), which also caught `Atlas`: a 3-wide consumer under three 3-wide sources
overlaps only the first and silently packs one tile.

`wz4gen describe` now prints array blocks. Without it the tool implied that an operator with
array rows had none — which is how a `Gradient` came to be written with no stops in 4.1.

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
