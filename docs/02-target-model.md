# The target model — what we are building

Read `01-existing-model.md` first. This document says what we keep, what we change, and why.

---

## 1. Shape

Four layers, with a hard boundary between the first three and the last:

```
  editor/          Dear ImGui application — canvas, panel, preview
  ─────────────────────────────────────────────  no headless code depends on anything above
  tools/wz4gen     headless CLI: render, convert, list, describe
  libwz4tex        texture operators          libwz4geo   geometry operators
  libwz4core       headless op runtime: wObject/wType/wClass/wCommand/wExecutive/wDocument
  compat/          altona_config.hpp, sse2neon shim, system_osx
  ─────────────────────────────────────────────
  altona/main/base + util slice                (upstream, essentially unmodified)
```

The rule that keeps this honest: **`libwz4core` and the operator libraries must build and
their tests must pass with no GUI, no window system, and no graphics API linked.** The CLI is
not a convenience wrapper on the editor; the editor is a front-end on the CLI's libraries.

---

## 2. Decisions

### 2.1 Keep Altona's base runtime

`sArray`, `sString`, `sVector31`, `sMatrix34`, `sReader`/`sWriter` appear on nearly every line
of the generators, and they carry the `.wz4` format. They are also already cross-platform:
Altona's core compiles under clang on arm64 today.

Converting to STL would be a rewrite of 12,000 lines of algorithm code with no functional
gain and every opportunity for silent numerical drift. We keep it.

What this costs us: a non-idiomatic C++ dialect at the library boundary, and `sChar` being
`wchar_t` under `-fshort-wchar`. Both are contained — see §5.

### 2.2 Keep the `.ops` DSL, and make it emit metadata

The DSL already describes, for all 374 operators: name, output type, input types and arity,
every parameter's name, widget kind, range, step, default and conditional visibility, plus
palette column, keyboard shortcut and preview mode.

Today `wz4ops` burns all of that into hand-emitted Altona GUI calls. We add a second output:
**a JSON sidecar describing the same metadata as data.**

This is the highest-leverage decision in the project. The ImGui property panel and the whole
operator palette are then *generated from data at runtime*, so 374 operators get a working UI
with no per-operator UI code, and adding an operator upstream costs nothing.

### 2.3 Reproduce the stacking canvas faithfully

Not a wired node editor. The grid-of-adjacent-blocks model is genuinely good — it makes
layout and dataflow the same act — and reproducing it means `.wz4` documents round-trip
losslessly. It is also *less* work than integrating a node-editor library, because there are
no links to store, route or hit-test.

Implementation is a custom canvas on ImGui's `DrawList`: draw rectangles, snap to a
24 × 16 px cell grid, and derive connections with the exact `ConnectStack` algorithm. We
deliberately do **not** use imgui-node-editor, whose data model is the wrong shape.

### 2.4 A text graph format is a deliverable, not a convenience

There is no reference build to diff against. A Windows binary exists but will not run on this
arm64 host, and reconstructing a working build is its own project. So the test suite *is* the
correctness argument, and it has to be reviewable by a human.

`.wz4t` (§4) exists so each operator gets a small, hand-written, readable case whose output is
obviously right or wrong by eye. Reviewed once, then locked as a golden image and diffed
automatically forever after.

### 2.5 Our own preview renderer

Altona's OpenGL backend has 19 unimplemented entry points, and its shader pipeline depends on
NVIDIA Cg, discontinued in 2012. Finishing either is a large project in service of code we
are otherwise not using.

The editor draws its own preview: a textured quad for 2D, a small forward renderer for
meshes. This is why the material system can be dropped wholesale rather than ported.

---

## 3. Divergences from the original

| Aspect | Existing | Target | Why |
|---|---|---|---|
| Base runtime | Altona containers/strings/math | Keep | Rewrite risk with no gain |
| Op definitions | `.ops` → C++ | Keep, plus JSON metadata | Data-driven UI for 374 ops |
| Op runtime | GUI-coupled header | Split headless | Only 18 lines of `doc.hpp` touch the GUI |
| Canvas | Grid of adjacent blocks | Reproduce faithfully | Good model, lossless round-trip, less work |
| Document format | Tagged binary `.wz4` | Read/write `.wz4`, **plus** text `.wz4t` | Reviewable test cases |
| **Undo** | Per-op, single level, panel only | **Document-level undo/redo** | A real gap; cheap in a new UI |
| Renderer | D3D9 / D3D11 / partial GL2 | Our own preview renderer | Avoids the GL backend and dead Cg toolchain |
| Shaders | ASC → HLSL/Cg | None in scope | Nothing headless needs them |
| Materials, sequencer, post-FX, audio, video | Present | **Dropped** | Out of scope, and where every hard blocker lives |
| `Text` / `Text3D` / `Path3D` ops | Win32 GDI + GLU | FreeType + tessellator, or stubbed initially | 3 operators of 84; not worth blocking on |
| Platform | Win32 + X11 | macOS (arm64) + Linux | The point of the exercise |

Everything in the "existing" column that is not listed is preserved exactly, including the
connection rule, type system, automatic conversion insertion, cache semantics,
`passinput`/`passoutput` in-place mutation, and the `.wz4` format.

---

## 4. The `.wz4t` text format

### 4.1 Goals

Human-readable and human-writable; lossless against the parts of `.wz4` we support;
diff-friendly; and short enough that a per-operator test case fits on a screen.

### 4.2 Canonical form

Positions and sizes are explicit, in grid cells. This is what the converter emits.

```
wz4t 1

page "textures"

op GenBitmap.Perlin at 4,2 size 3x1 {
  Size    = 256, 256
  Freq    = 4, 4
  Octaves = 5
  Seed    = 1
}

op GenBitmap.Blur at 4,3 size 3x1 {
  Order  = 1
  Size   = 0.02, 0.02
  Amplify = 1.0
}

op GenBitmap.Color at 4,4 size 3x1 {
  name  = result                  # store name — this op is addressable by name
  Mode  = mul
  Color = #ff8040c0
}
```

- Operators are identified as `OutputType.ClassName`, matching how `.wz4` identifies them, so
  the mapping is exact and ambiguity is impossible.
- **Connections are not written down.** They are derived from geometry by the same rule as the
  original. That is the point: the format tests the rule.
- `name = ` sets the store name. `hide` / `bypass` are bare flags.
- Parameter names are the `.ops` parameter names verbatim.
- Values: numbers, comma-separated vectors, `#aarrggbb` colours, quoted strings, and
  identifiers for `flags`/`radio` choices (falling back to integers when a choice has no name).

### 4.3 Sugar for hand-written cases

Requiring a test author to compute grid coordinates would make the suite tedious and
error-prone. A `stack` block assigns positions automatically, top to bottom:

```
stack at 4,2 {
  op GenBitmap.Perlin { Size = 256,256  Freq = 4,4  Octaves = 5 }
  op GenBitmap.Blur   { Size = 0.02,0.02 }
  op GenBitmap.Color  { name = result  Mode = mul  Color = #ff8040c0 }
}
```

Each operator is placed directly beneath the previous one at width 3, forming a chain. A
`row { }` block places operators side by side at the same Y, for feeding a multi-input
consumer; the consumer's width is then set explicitly. Sugar is expanded on read; the writer
always emits canonical form, so `convert` round-trips are stable.

### 4.4 Round-trip guarantee

`.wz4` → `.wz4t` → `.wz4` must preserve every operator, its geometry, its parameters and its
store name; and re-deriving connections from the result must reproduce the original input
lists exactly. This is a phase 3 gate and a permanent test.

---

## 5. Platform strategy

Contained in `wz4port/compat/`, with as little upstream change as possible.

| Concern | Approach |
|---|---|
| `altona_config.hpp` | A new file we own. Upstream expects it and gitignores it; creating it is setup, not modification |
| Build | Hand-written CMake for a narrow subset. `makeproject` is **not** ported — we do not need a general project generator, and `asc` is not needed because nothing headless uses shaders |
| Platform id | Add `sPLAT_APPLE` and extend the `sPLAT_WINDOWS \|\| sPLAT_LINUX` guards. Additive; upstream patch |
| System layer | `compat/system_osx.cpp` modelled on `system_linux.cpp` — pthreads, mmap, dirent, poll. Shell subset only; no window system |
| Wide characters | Keep `-fshort-wchar` so `L"..."` stays 2-byte. Replace the **three** libc `wcstombs`/`mbstowcs` sites with Altona's own `sCopyStringToUTF8`/`sCopyStringFromUTF8`. Those three are the only reason `-fshort-wchar` is unsafe; fixing them avoids a mass `L""` → `u""` rename across the codebase |
| SIMD on arm64 | `compat/simd_compat.hpp` maps `<emmintrin.h>` to vendored `sse2neon.h`. All 43 distinct intrinsics used are SSE2 integer operations, fully covered |
| Source encoding | **Nothing to do.** clang accepts the 30 Latin-1 files as-is; the high bytes are all in comments. Verified |
| Endianness | Nothing to do. `.wz4` is little-endian and both targets are |

### Upstream patches

Every change to `altona_wz4/` is recorded in `wz4port/patches/` with its rationale. The
expected complete set:

1. `sPLAT_APPLE` and the two guard extensions — additive.
2. The three wide-char call sites.
3. The `doc.hpp` headless split — the significant one.
4. The `wz4_mesh.cpp` render split (phase 6).

`git status` on `altona_wz4/` showing anything else is a defect.

---

## 6. Operator metadata schema

Emitted by `wz4port/tools/opsmeta`, one JSON document per `.ops` module, into `build/meta/`.
**Fixed in phase 2 stage 2.3 at `schemaVersion: 1`.** The derivation, the corpus census behind
it and the traps it avoids are in `04-phase-headless-core.md` §2.3; this section is the
contract.

### 6.1 Guarantees

- **Sufficient to build a parameter panel with no per-operator code.** Every widget kind of
  §5.2 in `01-existing-model.md`, with ranges, steps, defaults, choice bit layouts, array
  descriptors and conditional-visibility expressions.
- **Addressing is unambiguous.** A parameter says which of the three offset spaces it lives in
  (`words`, `strings`, `links`), what its offset is, and how many words it consumes. The
  reader never re-derives a size or an offset — including for `char[n]`, which occupies
  `(n+1)/2` words rather than `n`, and for `continue flags`, whose offset is resolved to the
  variable it shares.
- **Nothing needs re-parsing.** Choice strings arrive decomposed into widgets with shift, mask
  and per-choice values. The raw `options` string travels too, for presentation details the
  decomposition drops, but no reader has to interpret it.
- **Conditionals arrive as a tree**, already lowered: `Flags.choicename` is desugared to
  `(symbol & mask) == value` and nested `if` blocks are ANDed flat, both by the upstream
  parser. Each `symbol` node carries the referenced parameter's offset, space and kind.
- **Palette placement needs no inference.** `column` is the effective value; the upstream
  parser applies its signature-based default before an explicit `column = N;` can override it.
- **Stable and diffable.** Source order, fixed key order, two-space indent, pure ASCII
  (anything outside 0x20–0x7e is `\uXXXX`), no timestamps, no absolute paths. Floats print at
  the shortest precision that round-trips to the same float32.
- **A reader must reject an unknown `schemaVersion`** rather than guess.

### 6.2 Shape

Per module: `schemaVersion`, `module`, `priority`, `types[]`, `classes[]`.

Per type: `symbol`, `label`, `parent`, `virtual`, `color`, `flags[]`, `gui[]`,
`columnHeaders[]` (each `{column, label}` — the array is sparse, so the index travels with the
text).

Per class: `name`, `label`, `outputType`, `tabType`, `outputClass`, `column`, `shortcut`,
`gridColumns`, `extract`, `flagBits`, `flags[]`, `hasCode`, `paraWords`, `paraStrings`,
`arrayWords`, `fileInMask`, `fileOutMask`, `fileInFilter`, `inputs[]`, `actionIds[]`,
`parameters[]`, `array{}`, `ties[]`.

Per input: `type`, `optional`, `weak`, `varargs`, `method`, `linkSymbol`, `defaultOpType`,
`defaultOpName`.

Per parameter: `kind`, `symbol`, `label`, `space`, `offset`, `words`, `layout`
(`scalar`/`vector`/`array`), `count`, `ctype`, `continues`, `rebuildOnChange`, `modifiers[]`,
`condition{}`, plus per-kind fields — `min`/`max`/`step`/`rstep`/`logStep`/`defaults` for
numbers, `format` for hex ints, `channels` for colours, `options`/`widgets[]` for choice
kinds, `capacity` for `char`, `lines` for text, `method` for links, `actionId` for actions.

An emitted module is the authoritative example; `build/meta/wz4frlib/wz3_bitmap_ops.json` is
the one to read.

### 6.3 What is represented but untested

`bitmask`, `custom` and `tie` are emitted because the DSL supports them, but **no `.ops` file
in the tree uses any of them** — see the census in `04` §2.3. They are untested by
construction, and the first real use should be treated as new code rather than as coverage.

### 6.4 The original sketch

Kept for comparison. The three things it got wrong were a single `offset` (there are three
spaces), no word count (so `char[n]` would be misread), and no `layout` (so a vector and a
2-element array were indistinguishable).

```json
{
  "module": "wz3_bitmap",
  "types": [{
    "symbol": "GenBitmap",
    "label": "wz3 Bitmap",
    "parent": "BitmapBase",
    "color": "0xffc040c0",
    "flags": ["uncache"],
    "gui": ["base2d"],
    "columnHeaders": ["generator", "filters", "special", "samplers"]
  }],
  "classes": [{
    "name": "Flat",
    "outputType": "GenBitmap",
    "tab": "GenBitmap",
    "column": 0,
    "shortcut": "f",
    "flags": ["passoutput"],
    "inputs": [],
    "paraWords": 2,
    "parameters": [
      { "kind": "flags", "name": "Size", "offset": 0, "widgets": [
          { "shift": 0, "choices": ["1","2","4","…","8192"], "default": 8 },
          { "shift": 8, "choices": ["1","2","4","…","8192"], "default": 8 } ] },
      { "kind": "color", "name": "Color", "offset": 1,
        "channels": "rgba", "default": "0xff000000" }
    ]
  }]
}
```

---

## 7. What "done" looks like per goal

**Texture generation (priority 1).** An ImGui application on macOS and Linux that opens a
`.wz4` or `.wz4t` document, shows the operator stack on a grid canvas, lets you add, move,
resize, hide and bypass blocks with connections derived from geometry, edits every parameter
through a data-driven panel, and previews the resulting bitmap with pan, zoom, tile and alpha.
Backed by a headless library and a per-operator golden-image suite.

**Geometry (priority 2).** The same, for `Wz4Mesh`: 47 generator and modifier operators, CSG,
mesh import, OBJ export, and a 3D preview with orbit and wireframe.

**Animated geometry (priority 3).** Skeletons, channels, `BakeAnim`, vertex skinning, and a
timeline scrubber sufficient to see the animation — not the demo sequencer.
