# Phase 6 — Geometry

**Goal:** the Werkkzeug4 mesh operators run headlessly and are editable in the GUI, with a 3D
preview. This is **priority 2**.

---

## Scope

| Component | LOC | Notes |
|---|---:|---|
| `wz4frlib/wz4_mesh.cpp` | 7,406 | The largest file in the repo; all mesh algorithms |
| `wz4frlib/wz4_mesh.hpp` | 323 | `Wz4Mesh`, `Wz4MeshCluster` |
| `wz4frlib/wz4_mesh_ops.ops` | 2,171 | 47 operators, plus substantial algorithm code |
| `wz4frlib/wz4_bsp.cpp` | 1,675 | BSP/CSG, `SliceAndDice` fracturing |
| `wz4frlib/bspline.cpp` | 585 | Cubic B-spline helper |
| `wz4frlib/wz4_mesh_obj.cpp` | 231 | Wavefront OBJ import/export |
| `wz4frlib/wz4_mesh_lwo.cpp` | 462 | LightWave import |
| `wz4frlib/wz4_mesh_xsi.cpp` | 2,142 | SoftImage dotXSI import |

`wz4_bsp`, `bspline`, `wz4_anim` and all three importers measure **zero** graphics, GUI, Win32
and SIMD dependencies. They come across untouched.

### Data structure

```cpp
struct Wz4MeshVertex { sVector31 Pos; sVector30 Normal, Tangent; sF32 BiSign,
                       U0,V0,U1,V1; sS16 Index[4]; sF32 Weight[4]; sF32 Select; sInt Temp; };
struct Wz4MeshFace   { sInt Cluster; sU8 Count /*3 or 4*/, Select, Selected;
                       sInt Vertex[4]; sInt Temp; };
struct Wz4MeshFaceConnect { sInt Adjacent[4]; };   // half-edge tag = face*4 + vertIndex
```

A polygon soup with half-edge adjacency computed on demand. Triangles and quads.

### The 47 operators

**Generators:** `Cube`, `Grid`, `Sphere`, `Cylinder`, `Torus`, `Disc`, `Text3D`, `Path3D`,
`Import`.

**Transform:** `Transform`, `TransformRange`, `TransformEx`, `TransformMatrix`,
`TransformNonLinear`, `Mirror`, `Center`, `Multiply`, `MultiplyNew`, `Bend`, `Deform`,
`Normalize`, `Randomize`, `Noise`.

**Topology:** `Subdivide`, `Extrude`, `Bevel`, `Facette`, `Crease`, `UnCrease`, `Dual`,
`Splitter`, `Triangulate`, `Invert`, `Heal`, `Weld`, `SplitAlongPlane`, `Chunks`,
`RandomizeChunks`, `Add`, `DeleteFace`.

**Attributes:** `CalcNormals`, `CalcTangents`, `Displace`, `ExtrudeNormal`, `Select`,
`SelectGrow`, `SetMaterial`, `BakeAnim`.

**CSG** (in `wz4_bsp`): `Polyhedron`, `FromMesh`, `SliceAndDice`, `BSPToMesh`.

---

## The structural changes

**This section was rewritten after measuring, before any code was written.** The survey's
central simplifying assumption was wrong, and the correction changes what has to be patched.
What follows is what the tree actually says.

### Confirmed exactly as surveyed

`wz4_mesh.cpp` is banner-delimited and the boundaries are where the survey said:

| Lines | Content | Disposition |
|---|---|---|
| 1–4,423 | Components, serialisation, clusters, connectivity, transforms, topology, selection | **Take as-is** |
| **4,424–5,287** | `ChargeWire`/`ChargeSolid`/`ChargeBBox`/`Charge`/`BeforeFrame`/`Render`/`RenderInst`/`RenderBone*` | **Split out** into `wz4_mesh_render.cpp` |
| 5,288–5,854 | `MakeGrid`/`Cube`/`Torus`/`Disc`/`Sphere`/`Cylinder` | **Take as-is** |
| **5,855–6,650** | `MakeText`/`MakePath` | **Stub, then 6.6.** Win32 GDI + `glu32` |
| 6,651–7,406 | Plane splitting, wz3 min-mesh loading | **Take as-is** |

The split points are the `/*** Painting ***/` and `/*** Generators ***/` banners, so they are
exact rather than approximate. And the claim that matters held under measurement:

> **Zero** references to `sGeometry`, `sVertexFormat`, `sMaterial`, `sTexture`, `sCBuffer`,
> `sSetTarget` or `sDrawRange` exist outside lines 4,424–5,287.

`bspline.cpp` includes nothing but its own header and comes across untouched, as surveyed.

### Wrong: "`Wz4Mtrl` becomes an opaque handle the library never dereferences"

It is dereferenced, in three places outside the render section:

- **Serialisation** (`wz4_mesh.cpp:483–509`) does `new SimpleMtrl`, `c.Mtrl->Prepare()` and
  refcount traffic. A cluster's material is part of the mesh's *serialised form*.
- **Copy and merge** (`:567`, `:639`, `:1081`, `:1170`) do `AddRef`/`Release`.
- **`:902`** constructs a `SimpleMtrl` and calls `SetMtrl(...Flags)`.

So meshes do not merely carry material *assignments*; the mesh library constructs and prepares
concrete materials.

### Wrong by omission: three headers cannot be included headlessly at all

The survey measured `.cpp` dependencies and missed the `.hpp` chain, which is where phase 2's
equivalent problem lived too:

- **`wz4_mtrl2.hpp`** includes `wz4frlib/wz4_mtrl2_shader.hpp`, which **does not exist in the
  tree** — it is generated from `wz4_mtrl2_shader.asc` by the `asc` shader compiler, which this
  port does not build and which `CLAUDE.md` lists as out of scope. It also includes
  `wz4lib/doc.hpp`.
- **`wz4lib/doc.hpp`** includes `doc_gui.hpp` *unconditionally* (patch 04 split the file but kept
  `doc.hpp` meaning "both"), so anything reaching it needs the GUI and the generated shader
  library.
- **`wz4_anim.hpp`**, which `wz4_mesh.hpp` also includes, reaches `wz4lib/doc.hpp` as well.
- **`wz4_mesh_obj.cpp`** includes the generated `wz4_mtrl2_ops.hpp`.

This is the same shape as the phase-2 blocker where `doc.hpp` reached `util/shaders.hpp`, and it
has the same three-way answer: redirect an include, extract a dependency-free declaration, or
provide a headless implementation.

### The corrected work list, and its precedents

| # | Change | Precedent | Status |
|---|---|---|---|
| 1 | `wz4_anim.hpp`: `doc.hpp` → `doc_core.hpp` | patch 06 did exactly this for `wz3_bitmap_code.hpp` | **done** |
| 2 | ~~Extract `Wz4Mtrl` into `wz4_mtrl_iface.hpp`~~ → **forward-declare it** | — | **done, smaller** |
| 3 | ~~Move lines 4,424–5,287 into `wz4_mesh_render.cpp`~~ → **guard them** | patch 05's `#ifndef WZ4_HEADLESS` | **done, smaller** |
| 4 | A headless concrete material in `wz4port/compat/` | new | **done** |
| 5 | `wz4_mesh_ops.ops`'s four cross-module includes | patch 05 | outstanding |
| 6 | `wz4_mesh_obj.cpp`'s materials-ops include, for import | as (5) | outstanding |

Two of those turned out **smaller than planned**, and both for the same reason —
the plan assumed a dependency that measurement did not support:

- **(2) needs only a forward declaration.** `wz4_mesh.hpp`'s single use of
  materials is `Wz4Mtrl *Mtrl` — a pointer. The patch-04/06/09 extractions
  existed because headless code needed the *definitions* (`sGuiTheme` by value,
  `sGuiColor`'s enumerators); here it needs a name. It does need
  `#include "util/image.hpp"` added, because `Displace()` takes an `sImageI16 *`
  it had been getting transitively through the materials header.
- **(3) is a guard, not a move.** The project rule prefers a patch to forking a
  file, and moving 864 lines is nearer a fork: it risks transcription and creates
  an upstream file to keep in step. The plan's other reason for moving — pimpl the
  GPU handles out of the header — evaporated with (2). Flipping
  `WZ4PORT_HEADLESS_MTRL` brings the renderer back in one step.

**Checkpoint reached:** `wz4_mesh.hpp` compiles headlessly, and the existing
132-test suite is unaffected.

### Also missed by the survey: the ops module's cross-module dependencies

`wz4_mesh_ops.ops`'s header block includes four *other* generated op modules —
`wz4lib/poc_ops.hpp`, `chaosmesh_ops.hpp`, `wz4_anim_ops.hpp`,
`wz4_mtrl2_ops.hpp` — plus `wz4_mtrl2.hpp` again.

Measured, the exposure is small. Of the **47** mesh operators, exactly **two**
name a type from another module:

- `ConvertFromChaosMesh(ChaosMesh)` — the wz3 legacy path
- `SetMaterial(Wz4Mesh,Wz4Mtrl)` — materials

So **45 of 47 register with nothing outside the mesh library**, and the two get the
treatment `GenBitmap.Text` got in 4.1: guarded out, documented, revisited later.

One related find: `Wz4Mesh::ConvertFrom(ChaosMesh *)` dereferences an **incomplete
type** — `chaosmesh_code.hpp` is commented out of `wz4_mesh.cpp`'s includes
(`:12`) while the body reads `src->Clusters[i]->Material->Material->Flags`. As
shipped, this file does not compile. It has to be guarded regardless of what this
port wants.

`Wz4Mtrl` itself is headless-compatible: it derives from `wObject` (which we compile) and its
virtuals use only `base/graphics.hpp` and `base/math.hpp` types, both of which build. Only
`SimpleMtrl` is not — it holds `SimpleShader*`, `Texture2D*` and a `sCBuffer<SimpleShaderVPara>`
from the missing generated header.

### The one decision without a precedent

Serialisation names `SimpleMtrl` concretely, so the headless build needs *something* by that name.
Two options:

**(a) A headless `SimpleMtrl` in `wz4port/compat/`** — satisfying the constructor, `Prepare()`,
`SetMtrl()` and the pure virtuals, and doing nothing. The two definitions never meet, because the
real one lives in a translation unit this build does not compile, so unlike the `sGuiColor` case
in patch 09 duplication here cannot collide.

**(b) Guard the material branches out** with `#if`, so meshes load without materials.

**(a) is chosen** — but the first rationale written here was wrong and is worth correcting rather
than quietly replacing, because a future reader would otherwise believe more works than does.

The claim was that (a) "keeps the serialised form intact". It does not, and it does not need to.
`Wz4Mesh::Serialize` is **unreachable in this build, and arguably in the whole dump**: nothing
calls it, and it cannot be reached generically either — `wObject` declares no virtual `Serialize`,
and `AddRef`/`Release` are non-virtual inlines. Mesh *objects* are computed at runtime; `.wz4`
documents store operators and parameters, not evaluated meshes.

A faithful stub is not merely unnecessary but impractical: `SimpleMtrl::Serialize_`
(`wz4_mtrl2.cpp:779`) does `s.OnceRef(Tex[i])` on three `Texture2D` handles, so reading one
faithfully would drag in the texture object type and the render library behind it.

So the requirement on the headless material is only that it **compile**. And the correct behaviour
if the dead path ever comes alive is already there for free: `Wz4Mtrl::Serialize` is inherited and
does `sFatal(L"no serialize for this material type yet")` — a loud stop rather than a desynced
stream, which is exactly what you want from a path nobody has exercised. The headless material
therefore does **not** override it.

Recorded as `wz4port/patches/10-mesh-headless.md`. **Not patch 04** — the survey predates patches
04 through 09.

---

## Stages

### 6.1 — Split and build `wz4geo` — **done**

Order was the order the dependencies force: headers first, then the headless material, then the
renderer, then the ops module. Getting a clean compile of `wz4_mesh.hpp` alone was the first
checkpoint and was worth reaching before touching the `.cpp` — the phase-2 equivalent
(`headless_core_gate`) earned its keep by being a compile-only target.

`Text3D` and `Path3D` needed no stubbing after all: they **register and link**, because their
Windows-only bodies already had an `#else` arm that calls `sFatal`. Correcting one stale signature
in that arm was the whole cost (patch 10, change 3c). They will still refuse at *runtime* until
6.6, which is the same position `GenBitmap.Text` was in after 4.1 — but the operators are present,
so a graph containing one loads and every other operator in it evaluates.

What the plan did not anticipate, and cost the most: **an operator declaration cannot be
preprocessor-guarded.** `wz4ops` gained a per-operator `headless = 0;` directive
(patch 11, `architecture.md` A50), and `-headless` now defines `WZ4_HEADLESS` in the generated
`.hpp` as well as the `.cpp` (A51).

`wz4_bsp.cpp` is **not** in the library yet. Nothing in the 45 registered operators referenced it
at link time, so it stays out until something needs it — 6.3 will say, since the CSG and fracturing
operators are the ones that would pull it in.

`wz4_mesh_xsi.cpp` stays out too, and is the one importer that cannot simply follow: across its
2,142 lines it *constructs* materials and textures rather than mentioning them. `LoadXSI` comes
from `wz4port/compat/mesh_xsi_stub.cpp` and refuses. The OBJ and LWO readers and the OBJ writer
compile and link, and `Import`/`Export` register — but none of them has been *run* yet. 6.2
exercises the writer; a reader case belongs with 6.3.

**Gate — met.** `wz4geo` compiles and links headlessly on macOS arm64; `tests/mesh_register.cpp`
asserts 45 of 47 operators register with `ConvertFromChaosMesh` and `SetMaterial` absent *by name*,
then evaluates a `Cube`: 6 faces, all quads, 24 vertices, bounds exactly -0.5..0.5 on every axis.
133/133 ctest.

### 6.2 — OBJ export and `wz4gen render` for meshes — **done**

`wz4gen` now registers `wz4_anim` and `wz4_mesh` and links `wz4geo`. `render` grew a mesh branch
that dispatches on the output extension, as `convert` does.

What the mesh branch reports is deliberately richer than the bitmap branch's "uniform or
structured, plus a checksum" — an image tells you almost nothing without being looked at, a mesh
tells you a great deal:

```
cube_wide: Wz4Mesh.Transform
  24 vertices, 6 faces (0 tri, 6 quad), 1 clusters
  min -2 -0.5 -0.5
  max 2 0.5 0.5
  checksum e340d418a0000000
  wrote obj/cli_cube_wide.obj (2306 bytes)
```

Every line is something a reviewer can check against what the operator claims: counts, arity,
degenerate-face count (upstream has the predicate), bounds, and a position checksum for 6.3 to
lock. Floats go through `wFormatFloat`, not Altona's `%f` (A18).

**The gate said "opens in a mesh viewer", which a test cannot do.** The oracle used instead is
upstream's own `LoadOBJ` — a full `sScanner` grammar that shares no code with `SaveOBJ` and
validates every index against the counts it has seen. Write, read back, require the geometry to
survive. `tests/mesh_obj.cpp`:

- **Cube:** 6 quads in, 6 quads out, bounds identical, 24 vertices both sides.
- **Sphere:** 96 faces — **24 triangles at the poles and 72 quads** — and the arity split survives
  exactly. This is the case worth having: a cube is the shape most likely to round-trip by
  accident, since every coordinate has the same magnitude.
- **A negative case.** A face index one past the end — the exact off-by-one a broken 1-based
  writer produces — must be *rejected*. Without this, none of the above means anything: a parser
  that accepted everything would pass every positive assertion.

Positions are compared with an epsilon, not bit-exactly, because `sScanner::ScanFloat` loses one
ULP (A34) — requiring equality would be asserting something known to be false.

`mesh_render_cli` covers the one thing the round-trip cannot: that `render` reaches the writer at
all. It matches the **bounds** as well as the counts, on a `Cube → Transform` chain scaled 4x in x
only, so it fails if the chain did not connect, if `Transform` was skipped, or if the scale landed
on the wrong component.

**Gate — met.** 135/135 ctest.

### 6.3 — Per-operator test cases

Same discipline as phase 4, with an important addition: meshes admit **structural assertions**
that images do not. Each case checks:

- Vertex and face counts.
- Bounding box.
- Topology invariants — closed-manifold where expected, Euler characteristic, no degenerate
  faces, consistent winding.
- Then a golden OBJ, byte-compared.

This is a materially stronger correctness signal than the texture suite gets, and it partly
compensates for the absence of a reference build.

**Gate:** every mesh operator has a case; structural assertions pass; goldens reviewed.

### 6.4 — 3D preview

A small forward renderer in the editor: vertex/index buffers from `Wz4Mesh`, flat or simple
Lambert shading, orbit/dolly/pan camera, wireframe toggle, grid, bounding box.

Explicitly **not** Altona's renderer and **not** Werkkzeug materials. A viewer, not an engine.

**Gate:** meshes display, camera controls work, wireframe toggles.

### 6.5 — Editor integration

Mesh operators appear in the palette and panel automatically — that is what metadata-driven UI
buys us; the work here is preview routing by result type (`base2d` vs `base3d`, per
`01-existing-model.md` §7) rather than new UI.

**Gate — phase gate.** Open a document, build a mesh graph, edit parameters, see the mesh
update in 3D. Export to OBJ.

### 6.6 — Text3D and Path3D

`MakeText` uses `GetGlyphOutlineW` to fetch glyph outlines and `glu32` to tessellate.
Reimplement on FreeType outline extraction (`FT_Outline`) plus a tessellator — libtess2 or
earcut, both small and permissive.

Deferred to last deliberately: two operators, real work, and everything else must not wait
on it.

**Gate:** `Text3D` produces correct extruded geometry for a simple string.

---

## Deliverables

- `wz4port/patches/10-mesh-headless.md`
- `wz4port/compat/mtrl_headless.cpp` — the concrete material the serialiser names
- `wz4geo` target
- `wz4port/tests/geo/` — cases, structural assertions, golden OBJs
- 3D preview in the editor
- OBJ export in `wz4gen`

## Risks

| Risk | Assessment |
|---|---|
| ~~The render split is less clean than the banners suggest~~ | **Retired by measurement.** Zero graphics references outside lines 4,424–5,287, and the boundaries are banner comments |
| The material dependency is deeper than the three call sites found | **The live risk, replacing the one above.** The GPU members *do* reach serialisation — that was the survey's stated worry and it turned out to be true. Three call sites are known; a fourth found late would mean revisiting the headless-material decision |
| `Subdivide`/`Extrude`/`Bevel` are subtle and fail in ways structural assertions miss | Moderate. These are the hardest algorithms in the set. Mitigated by visual inspection alongside assertions |
| XSI importer proves unusable without test assets | Low impact. OBJ and LWO cover the need; XSI is legacy |
| Tessellation for `Text3D` diverges from GLU | Low. Both produce valid triangulations; only the triangulation differs, not the silhouette |
