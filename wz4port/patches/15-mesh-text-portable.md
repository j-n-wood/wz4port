# Patch 15 — open the 2D extrusion path to a non-Windows tessellator

**Files:** 1 upstream (`wz4frlib/wz4_mesh.cpp`), **101 insertions, 0 deletions**
**Phase:** 6 (geometry), stage 6.6b
**Status:** applied — `Path3D` in 6.6b, `MakeText` on FreeType in 6.6c
**Wider context:** `docs/08-phase-geometry.md` §6.6, `docs/architecture.md` A60

## Why

`Text3D` and `Path3D` lived inside `#if sPLATFORM==sPLAT_WINDOWS`, and the phase
plan described the work as "reimplement on FreeType plus a tessellator". Measuring
the region showed that describes **two of the six things in it**:

| | | |
|---|---|---|
| glyph outlines | `GetGlyphOutlineW`, a `TTPOLYGONHEADER` walk | Windows |
| tessellation | `glu32`, four callbacks | Windows |
| Bézier flattening | recursive subdivision to a tolerance | portable |
| the SVG-like path parser | ~150 lines | portable |
| text layout | pen advance, newlines | portable |
| `Finish2DExtrusionOp` | 160 lines: adjacency, degenerate flipping, the extrusion and its walls | portable |

`Finish2DExtrusionOp` was checked directly: **zero** references to `glu`, `HDC`,
`HFONT`, `__stdcall` or `GLYPH`.

Copying the portable four into `wz4port/` would have been a 200-line partial fork.
The project rule prefers a patch to a fork, so instead the region opens under
`WZ4PORT_TESS2D` and the tessellator handle becomes ours.

## Zero deletions, and that is the design

`git diff --stat` reports **101 insertions and 0 deletions**. Not one existing
line was modified — the 350 lines of parser, flattening, layout and extrusion are
byte-identical.

That is achieved by mapping the GLU *call names* onto the sink rather than
rewriting twelve call sites:

```cpp
#define GLUtesselator            wMeshTess
#define gluNewTess()             (new wMeshTess)
#define gluTessBeginPolygon(t,d) (t)->BeginPolygon(d)
#define gluTessBeginContour(t)   (t)->BeginContour()
#define gluTessEndContour(t)     (t)->EndContour()
#define gluTessEndPolygon(t)     (t)->EndPolygon()
#define gluTessNormal(t,x,y,z)   ((void)0)
#define gluTessCallback(t,w,f)   ((void)0)
```

The two no-ops **drop** their arguments rather than evaluating them, which is what
lets `tess3DBeginCB` and the other three callbacks stay undefined in this build.

A deliberate trade: a macro over a third-party API name is normally a smell, and
here it buys 350 lines that cannot be broken by touching them. It is confined to
one region of one file and undone by the `#endif`.

## The four changes

1. **The outer guard** becomes `#if sPLATFORM==sPLAT_WINDOWS || WZ4PORT_TESS2D`,
   with the glu32/GDI declarations keeping their own inner Windows guard.
2. **`tess3DAddPoint`** gets a one-line `WZ4PORT_TESS2D` branch. The sink creates
   the mesh vertex itself, so the Bézier helpers are untouched — they still read
   `Vertices.GetTail().Pos` for the segment start.
3. **The four GLU callbacks** are guarded out. They exist only because glu32
   *streams* triangles; a sink that owns the mesh appends complete faces. The
   trailing empty face they left behind goes too, which is why `MakePath`'s
   `Faces.RemTail()` is guarded — removing the last face would now delete a real
   triangle.
4. **`MakeText` gets three arms**, on an inner guard: the Windows one unchanged,
   a FreeType one added in 6.6c, and a warning stub when no font backend is built
   (`WZ4PORT_HAVE_SFONT2D`, the same flag `GenBitmap.Text` uses). `MakePath` needs
   no font, which is why it went first: it isolates a new tessellator against a
   real operator before a font is added on top.

   The FreeType arm keeps the Windows structure — same layout loop, per-character
   temp mesh, `Finish2DExtrusionOp`, chunk handling — and swaps only the glyph
   walk. Both feed the SAME Bézier subdivision helpers, so the platforms flatten
   curves identically rather than merely similarly. It ends with the same extra
   `RemoveDegenerateFaces()`, because every glyph with a counter is a bridged
   hole.

## One addition that is not just a guard

`MakePath` calls `RemoveDegenerateFaces()` a second time, after
`Finish2DExtrusionOp`, under `WZ4PORT_TESS2D`.

A hole is tessellated by **bridging** it to its outer contour, which leaves two
coincident edges — that is what a bridge is. `Finish2DExtrusionOp` opens by
flipping degenerate triangles using `Adjacency()`, and a coincident pair makes
that edge look like it has four incident faces, so the flip can emit a triangle
with a repeated index. Measured on a 4×4 square with a 2×2 hole: **4 such faces
out of 24, all zero-area**. They contribute nothing, so removing them is safe.

This is a genuine cost of ear-clipping-with-bridges versus the sweep-line
tessellator glu32 used, which creates real vertices at intersections and so never
produces a coincident pair. Recorded rather than hidden; see `geo/tess2d.hpp`.

## Verification

`Path3D` on `"M 0 0 L 1 0 L 1 1 z"` extruded 0.1 gives a triangular prism —
**2 triangular caps + 3 quad walls = 5 faces**, z spanning exactly the extrude
depth, closed.

With a hole, `"M 0 0 L 4 0 L 4 4 L 0 4 z M 1 1 L 1 3 L 3 3 L 3 1 z"` extruded
0.5: tess2d gives 8 ring triangles (4 outer + 4 hole + 2 bridge vertices, minus
2), of which 2 are the bridge's zero-area slivers, so 6 real per cap and 12 for
both, plus 4 outer and 4 inner wall quads — **20 faces**, closed. Every number
derived.

## Invariant

`git status` on `altona_wz4/` shows one file for this patch,
`wz4/wz4frlib/wz4_mesh.cpp`, which patches 10, 11, 12 and 13 also touch.

## Behaviour

Both operators produce geometry instead of warning, when FreeType is present.
Without it, `Text3D` keeps patch 12's warning and `Path3D` still works — it needs
no font.

Two differences from the Windows original, both consequences of ear clipping
rather than a sweep line, and both stated in `geo/tess2d.hpp`:

- a self-intersecting `Path3D` string is not handled;
- bridged holes leave zero-area faces that need the extra `RemoveDegenerateFaces()`.

Neither affects glyphs, which are non-self-intersecting with properly nested
counters.
