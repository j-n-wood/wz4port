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

### 4.4 — Golden lock and the runner — **done**

Review every output by eye, once, carefully — this is the moment correctness is established
and it deserves proper attention. Then lock the PNGs into `wz4port/tests/tex/golden/`.

Runner: render every case, compare byte-exact against its golden, report failures with a
side-by-side diff image.

Add the **SIMD parity check**: the same suite must produce **bit-identical** output on the
SSE2 path and the NEON path. This is the sharpest correctness signal available to us and it
is nearly free.

**Gate — phase gate, passed.** 87 cases locked in `tests/tex/golden/`, **119 ctest tests**
green on arm64 **and** on x86-64, bit-identical between them. The chain cases and the shared
source bitmaps are now cases too, which is where 74 became 87.

#### The parity check was runnable here after all, and it found real divergence

The plan assumed x86-64 needed a Linux box. It does not: an x86-64 slice cross-compiles on
Apple silicon and runs under Rosetta 2, and `compat/include/simd_compat.hpp` already resolves
x86-64 to the real `<emmintrin.h>` rather than sse2neon. So the comparison is available on
this machine — `tests/tex/parity_x86_64.sh`.

No separate harness was needed. The goldens *are* the cross-platform contract: they are
checked in, they were produced by the NEON path, and every case compares byte-for-byte. Build
the other architecture, run the same suite.

**First run: 8 of 87 cases diverged.** Every one was float-touching — Perlin's and GlowRect's
`sFPow` gamma tables, `Unwrap`'s coordinate maths, and all five lighting cases.

The cause was **not sse2neon**. It was **FMA contraction**: clang defaults to
`-ffp-contract=fast` and fuses `a*b+c` into a single FMA where the target has one — arm64
always does, this x86-64 target does not — and an FMA rounds once where two operations round
twice. One ULP of float, quantised by the engine into a different 16-bit sample.
`-ffp-contract=off` in `CMakeLists.txt` makes the two architectures agree exactly. It is there
for determinism, not speed, and the comment says so.

`simd_parity` passed throughout, on both architectures. It checks 43 intrinsics against scalar
models and could not see any of this, because none of it was in the intrinsics. **The
operator-level comparison caught what the intrinsic-level one structurally cannot.**

Honest caveat: the SSE2 *code path* is genuine, which is the thing being verified, but the
instructions are executed by Rosetta's translation rather than by Intel silicon. Running this
once on a real x86-64 Linux box is still worth doing.

Also found, and fixed, by building the second architecture: **`#define stat64 stat` in our own
compat header collides with the x86-64 macOS SDK's own `struct stat64`**, which arm64 does not
declare. The header now includes `<sys/stat.h>` before defining the alias.

#### The golden is the image *and* the checksum

`MakeWz3Bitmap` forced this. It requantises its input through an 8-bit `sImage`, so its
checksum moves — `a7af9504d91e1410` to `dbb910e7e4b14596` — while the written PNG stays
**byte-identical** to its source. An image-only golden would call that operator a no-op
forever.

So each golden is a `.png` plus a `.txt` holding the tool's report line: size,
uniform/structured, alpha range, and a checksum over all 16 bits of every pixel. The report is
checked first, because it is the stronger of the two.

That mattered immediately: **7 of the 8 parity divergences had byte-identical images** and were
caught only by the checksum. An image-only golden would have reported full parity.

Both directions of the runner were verified by deliberately breaking them: a one-hex-digit edit
to a locked checksum fails with both values printed, and a swapped golden image fails with
`wz4gen diff` output — "16359 of 16384 pixels differ, worst channel delta 128 of 255" — plus a
normalised difference image. A golden that cannot fail is worth nothing.

`wz4gen diff <a> <b> [-out <c>]` is new: differing pixel count, worst delta per channel, and an
amplified difference image scaled to the worst delta.

Locking is `tests/tex/lock_goldens.cmake`, run deliberately and never as part of a build. It
renders every case in `build/tex-cases.txt` — emitted by CMake, so the list cannot drift from
the tests — straight into `golden/`.

#### What the review pass changed

The stage exists to establish correctness by eye, and it earned its place. Five cases were
changed because looking at them showed they were not testing anything:

- **`ColorBalance` was invisible.** The shared saturated ramp has every channel already clipped
  at 0 or maximum, so a lift/gain per tonal band had nowhere to move — even at the extremes of
  the range the output was indistinguishable from the source while still changing the checksum.
  It now has its own greyscale source, and the grade is obvious. *A test case is not a preset.*
- **`Merge`'s input pair was blowing out.** With both inputs reaching full brightness, `add` and
  `addsmooth` saturated across most of the frame and read as flat white. The cells now peak at
  `0x90`, which costs `mul`/`min`/`max` nothing and makes all twelve legible.
- **`light_point` was a white blob**, then over-corrected to a near-flat grey. A point light on
  a *flat* plane only varies strongly when it is close to it; the three Light cases now share
  `z = 0.15` and read as three obviously different shapes.
- **`bump_*` wanted the opposite** — a broad light, so the relief is visible across the frame
  rather than inside a small cone. The two groups deliberately differ, and the file says why.
- **The shared sources were not rendered at all**, so nothing could be compared against them.
  `src_ramp`, `src_gray`, `src_cells`, `src_bricks`, `src_height` and `src_white` are cases now.

And four descriptions were wrong in ways only the images revealed:

- **`Merge`'s `brightness` and `hardlight` are the same operation**, written twice with two
  different ways of building the same mask (`_mm_srli_epi16` versus `_mm_and_si128`). Twelve
  labels, eleven behaviours. Their outputs must be byte-identical, and `tex_merge_identity`
  asserts it — a free consistency check on two different intrinsics.
- **`over` does not reproduce its top layer exactly.** The alpha multiply is
  `mulhi_epi16(d,0x7fff) << 1`, a factor of 0.99997, so 78 of 16384 pixels land one 8-bit step
  away. Measured, not guessed.
- **`premul alpha` maps to the same `BI_ALPHA` constant as `alpha`** in the mode table, which
  reads like a bug until you notice the trailing `out->PreMulAlpha()`.
- **`Color`'s `scale` is `mul` with the descale shifted 11 instead of 15** — the same operation
  with 16× the gain, so a mid-grey Color means ×8 and saturation, not ×0.5.

The operand convention behind all the Merge comments is now written down in the case file:
`Bitmap_Inner` loads `b` from its 2nd argument and `a` from its 5th, so input 0 is the bottom
layer and input 1 is the top.

One pair is documented as **not** reviewable by eye: `gradient_linear` and `gradient_smooth`
differ by checksum but not visibly, because three stops compress each span to 30% of the width.
Saying so protects the next reviewer from concluding the mode is broken.

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

That limit is narrower after 4.4 than it looks, and it is worth being precise about which half
closed:

- **Still open.** Nothing here says our `Perlin` matches Farbrausch's `Perlin`. Every value in
  the goldens is ours.
- **Closed.** Whether the *arm64 translation* of the pixel kernels matches the *x86-64* original
  semantics is no longer a matter of trust: 87 cases are bit-identical across the two
  architectures, checksums included. The SSE2 path compiled from the real `<emmintrin.h>` and
  the NEON path through sse2neon produce the same bytes. Since the algorithms are integer fixed
  point, that is a strong statement — there is no tolerance being hidden anywhere.

So the residual risk is concentrated in one place: an operator that was *always* being driven
wrongly by us — a misread parameter, a wrong input order — rather than one that drifted in
translation. 4.3 found four of exactly that kind by eye (`Mask`'s input order, `Perlin`'s
`FadeOff`, `Unwrap`'s mode names, `Dots`' density), which is the argument for the review stage
having been a stage.

What the suite catches reliably: regressions from our own later changes, SIMD and
floating-point porting errors (via bit-parity, which found FMA contraction), crashes, and
anything that produces visibly wrong output.

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
