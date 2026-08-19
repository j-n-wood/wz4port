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

## The one structural change

`wz4_mesh.cpp` is banner-delimited, and the boundaries are already exactly where we need them:

| Lines | Content | Disposition |
|---|---|---|
| 1–4,424 | Components, serialisation, clusters, connectivity, transforms, topology, selection | **Take as-is.** Pure CPU; only 3 renderer touches, all `Doc->IsPlayer` flags |
| **4,425–5,288** | `ChargeWire`/`ChargeSolid`/`ChargeBBox`/`Charge`/`BeforeFrame`/`Render`/`RenderInst`/`RenderBone*` | **Split out.** All 18 `sGeometry`/`sVertexFormat`/`sMaterial` references live here |
| 5,289–5,854 | `MakeGrid`/`Cube`/`Torus`/`Disc`/`Sphere`/`Cylinder` | **Take as-is.** Pure CPU |
| **5,855–6,650** | `MakeText`/`MakePath` — Font3D and SVG path | **Reimplement or stub.** Win32 GDI (`GetGlyphOutlineW`) + `glu32`, already platform-guarded with an `sFatal` fallback |
| 6,651–7,406 | Plane splitting, wz3 min-mesh loading | **Take as-is** |

Move 4,425–5,288 into `wz4_mesh_render.cpp` and pimpl the GPU handles in `Wz4MeshCluster`
(`sGeometry *Geo[2]; sGeometry *InstanceGeo[4]; Wz4Mtrl *Mtrl;`). Mechanical — the section
boundary is already a comment banner.

Recorded as `wz4port/patches/04-mesh-render-split.md`.

**Materials are out of scope**, so `Wz4Mtrl` becomes an opaque handle the headless library
never dereferences. Meshes carry material *assignments*; nothing renders them with a
Werkkzeug material.

---

## Stages

### 6.1 — Split and build `libwz4geo`

Perform the render split; build `wz4_mesh.cpp` (remainder), `wz4_bsp.cpp`, `bspline.cpp` and
the three importers against `libwz4core`.

`Text3D` and `Path3D` are stubbed initially — two operators of 47, not worth blocking on.

**Gate:** `libwz4geo` compiles and links headlessly on macOS arm64.

### 6.2 — OBJ export and `wz4gen render` for meshes

`wz4_mesh_obj.cpp` already exports OBJ. Wire it into `wz4gen render` by output extension.

**Gate:** a `Cube` operator round-trips to a valid OBJ that opens in a mesh viewer.

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

- `wz4port/patches/04-mesh-render-split.md`
- `wz4port/libwz4geo/`
- `wz4port/tests/geo/` — cases, structural assertions, golden OBJs
- 3D preview in the editor
- OBJ export in `wz4gen`

## Risks

| Risk | Assessment |
|---|---|
| The render split is less clean than the banners suggest | Low-moderate. The 18 graphics references are all in one region; the risk is `Wz4MeshCluster`'s GPU members leaking into serialisation, which needs checking early |
| `Subdivide`/`Extrude`/`Bevel` are subtle and fail in ways structural assertions miss | Moderate. These are the hardest algorithms in the set. Mitigated by visual inspection alongside assertions |
| XSI importer proves unusable without test assets | Low impact. OBJ and LWO cover the need; XSI is legacy |
| Tessellation for `Text3D` diverges from GLU | Low. Both produce valid triangulations; only the triangulation differs, not the silhouette |
