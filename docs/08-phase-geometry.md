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

**Corrected after 6.1, from the live registry** (`wz4gen list`) rather than from the survey's
reading of the `.ops` file. The survey listed three operators that **do not exist** — `CalcNormals`,
`CalcTangents` and `Weld`. All three are `Wz4Mesh` *methods*, called from inside other operators'
bodies; none is exposed as an operator. That is why the survey's own count came to 47 by a
different route than the file does.

The real inventory is 47 declared, of which 45 register headlessly:

**Generators (9):** `Cube`, `Grid`, `Sphere`, `Cylinder`, `Torus`, `Disc`, `Text3D`, `Path3D`,
`Import`.

**Transform (14):** `Transform`, `TransformRange`, `TransformEx`, `TransformMatrix`,
`TransformNonLinear`, `Mirror`, `Center`, `Multiply`, `MultiplyNew`, `Bend`, `Deform`,
`Normalize`, `Randomize`, `Noise`.

**Topology (15):** `Subdivide`, `Extrude`, `Bevel`, `Facette`, `Crease`, `UnCrease`, `Dual`,
`Splitter`, `Triangulate`, `Invert`, `Heal`, `SplitAlongPlane`, `Chunks`, `RandomizeChunks`,
`DeleteFace`.

**Attributes (5):** `Displace`, `ExtrudeNormal`, `Select`, `SelectGrow`, `BakeAnim`.

**Combining (1):** `Add` — the only variadic operator, `(*?Wz4Mesh)`.

**Export (1):** `Export`.

9 + 14 + 15 + 5 + 1 + 1 = **45 registered**, plus the 2 dropped headlessly
(`ConvertFromChaosMesh`, `SetMaterial` — patch 11) = 47 declared.

**CSG** (in `wz4_bsp`, a separate module and not yet built): `Polyhedron`, `FromMesh`,
`SliceAndDice`, `BSPToMesh`.

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

**This section was written in detail before the code, after measuring what the tree offers.**
The largest stage in the phase, and the one whose design decides whether the phase is worth
anything.

#### What changes versus phase 4

Two things, and they pull in opposite directions.

**In our favour: meshes admit structural assertions that images do not.** Phase 4's problem was
that a texture operator's output is only checkable by eye, so 4.3 had to review 90 images and 4.4
froze whatever they showed. A mesh has properties that are *derivable* — face counts, arity,
bounds, Euler characteristic, manifoldness, winding consistency. Those can be asserted before
anyone looks at anything, and they fail loudly when an algorithm is wrong rather than merely
different.

**Against us: there is a real reference corpus, and ignoring it would be a mistake.** Measured
after 6.2, with the mesh module registered:

| Document | Mesh operators |
|---|---:|
| `demos/example/example.wz4` | **39** |
| `wz4/screens4/test.wz4` | 15 |
| `demos/the_cube/fr-062_party.wz4` | 12 |
| `demos/easterparty/teaser1.wz4` | 12 |
| `demos/easterparty/teaser2.wz4` | 12 |

`example.wz4` alone uses **39 of the 45** registered operators, with parameter values chosen by
the original authors rather than by us. That is the closest thing this port has ever had to a
reference build, and phase 4 had no equivalent. The six it does not use are `Center`, `Heal`,
`Mirror`, `Path3D`, `SelectGrow` and `TransformRange`.

#### The design: two suites, doing different jobs

Neither alone is sufficient, and the reason is worth stating because it decides the whole stage.

**Suite A — hand-written cases, `tests/geo/ops_*.wz4t`.** One case per operator, minimal, with
numbers chosen so the answer is *derivable by hand*. A `Cube` at tesselation 1 has 6 faces and
spans -0.5..0.5, and nothing about that is a matter of opinion. This is where correctness is
asserted.

**Suite B — the bundled corpus, swept.** Evaluate every mesh store in the five documents above
and require each to produce a mesh that passes the same invariant battery. This costs almost
nothing to author and covers parameter combinations we would never think to write. It is where
*crashes* and *assertion failures inside upstream algorithms* will be found — the failure mode
Suite A is worst at, because Suite A only ever asks each operator the easy question.

Suite B cannot replace Suite A: a demo graph's output is unknown, so the sweep can only check
invariants, never values. Suite A cannot replace Suite B: 45 minimal cases exercise 45 code paths
out of hundreds.

#### The invariant battery

Applied by both suites, in `wz4port/tests/mesh_check.cpp` so there is one implementation:

| Check | Why it earns its place |
|---|---|
| No degenerate faces | Upstream has the predicate (`IsDegenerateFace`); it is the classic symptom of a tesselation off-by-one |
| Every face index in range | A face referencing vertex N of an N-vertex mesh is the single most common mesh bug, and it corrupts silently |
| Every face arity 3 or 4 | `Wz4MeshFace::Vertex[4]` is fixed; anything else means memory was written past it |
| Finite positions | One NaN from a division propagates through every later operator and shows as nothing at all |
| Cluster indices in range | `Face::Cluster` indexes `Clusters`; out of range is a crash waiting for the renderer |
| Bounds not absurd | A generator that emits garbage usually emits *huge* garbage |

**Euler characteristic and manifoldness are deliberately NOT in the battery.** They are in the
plan's original wording and they are the wrong tool here: most of these operators legitimately
produce non-manifold or open meshes (`DeleteFace`, `SplitAlongPlane`, `Splitter`, `Chunks`,
`Extrude` on an open mesh), so a global invariant would fire constantly on correct output. They
belong as *per-case* assertions in Suite A, on the specific cases where closedness is actually
promised — `Cube`, `Sphere`, `Torus`, `Cylinder` — and that is where they go.

#### Goldens

Locked the same way phase 4's were, and for the same reason: a checksum over vertex positions and
face indices, written alongside the OBJ, so a change that is invisible in a viewer still fails.
`wz4gen render` already reports the position checksum (6.2).

**The order matters and phase 4 established it the hard way.** Cases first, *reviewed*, then
locked. Locking earlier freezes whatever the code does today, which is exactly the failure
`06-phase-texture.md` warns about. So 6.3 splits:

- **6.3a** — the battery, Suite B (the corpus sweep), and Suite A's cases with structural
  assertions only. No goldens.
- **6.3b** — review the OBJ output, then lock checksums.

6.3a is where bugs will surface. 6.3b is bookkeeping, and doing it second is what makes it
meaningful.

#### Known obstacles, measured

- **`Text3D` and `Path3D` produce empty meshes** and warn (patch 12). They were `sFatal` — which
  aborts, and would have killed any sweep over `example.wz4` and its six `Text3D` operators. Fixed
  before this stage rather than worked around in it.
- **`Import` needs files** that are not in the tree. Its cases can only assert that a missing file
  is refused cleanly; a real import case needs an OBJ we write ourselves, which 6.2 now makes
  possible.
- **`BakeAnim` needs a skeleton**, which needs `wz4_anim`'s `BoneChain`. Registered since 6.1, so
  this is authorable, but it is the one case whose input is another module.
- **`wz4_bsp.cpp` is not built**, so the four CSG operators are out of scope for this stage. If a
  Suite A case for `SplitAlongPlane` or `Chunks` turns out to need it, that is a finding, not a
  plan change.

**Gate for 6.3a — met.**

```
Suite A   45 of 45 registered mesh operator(s) exercised
          48 case(s), 483 check(s), 0 failure(s)   (46 before stage 6.7 added two)
Suite B   1,386 mesh operators across 5 documents, 0 violations
          36 distinct operator classes, with the authors' own parameter values
```

Coverage is asserted against the **live registry**, not counted from the table:
`mesh_ops` collects every mesh operator appearing in a case file and names any registered class
that appears in none. Counting rows by hand is the proxy this project keeps getting caught by
(A39, A53).

#### What 6.3a found

Four things, and the interesting part is *which suite found them*. Suite B has thirty times the
coverage and found none of them — a sweep can only check invariants over inputs it did not choose.
Breadth finds crashes in code paths; a chosen input finds semantics (A55).

| Finding | |
|---|---|
| **`BakeAnim` segfaults** on any mesh with no skeleton — every generated mesh | fixed, patch 13 |
| **`Extrude` builds no side faces on an open mesh**: `/4` where the file elsewhere uses `>>2`, so a boundary half-edge stored as -1 decodes to face 0 | fixed in 6.7, patch 14 |
| **`TransformEx` defaults to uv0 → uv0**, so it moves texture coordinates and returns a mesh identical to its input, reporting success | case states `pos, pos` |
| **Our own `.wz4t` reader** subscripted a value list before its bounds test — unreachable until a case wrote a partial list | fixed, plus a regression case |

I first deferred the `Extrude` fix on fidelity grounds — `example.wz4`'s 14 `Extrude` operators
were authored against the broken behaviour, so changing it makes this port disagree with the tool
the demos were built with. **That was the wrong call, and the user overruled it.** Fidelity to a
2014 binary is a sensible *default* for resolving ambiguity, not a goal that outranks an operator
doing its job: an extrude that cannot build sides is not a design choice anyone made.

And then the premise turned out to be false as well. **The fix changes nothing in any bundled
document** — measured, byte-identical checksums — because the defect only ever affected *open*
meshes and every `Extrude` in those documents works on a closed one. See 6.7 below and A56: the
cost of the change was one command away from being known, and two rounds went into weighing a
trade-off that did not exist.

The general rule the two findings illustrate still holds — a deterministic wrong *answer* is part
of the tool's behaviour and a *crash* is not — it just does not settle the question on its own,
and it is worth less than a measurement.

#### Two design points that did not survive contact

- **Closedness pairs half-edges by POSITION, not by vertex index.** A `Wz4Mesh` splits a position
  wherever the normal or UVs differ, so every closed primitive in the library has unwelded seams.
  Index-pairing called `Cube(2,3,4)` open with exactly 72 unpaired half-edges — which is exactly
  the sum of its six grid patches' perimeters, `2*(2+3) + 2*(2+4) + 2*(3+4)`. That arithmetic is
  what identified the cause.
- **The plan's operator inventory was wrong.** `CalcNormals`, `CalcTangents` and `Weld` are
  `Wz4Mesh` *methods* called from inside other operators, not operators. The list above is now
  taken from `wz4gen list`.

#### Derivations that came out exactly right

Worth recording, because they are what makes the suite worth more than a golden:

- `Bevel` on a cube: **6** shrunk faces + **12** edge quads + **8** corner triangles = 26 faces,
  18 quads, 8 tris. All three numbers from the cube's own counts.
- `Dual` of a cube is the **octahedron**: 8 corners become 8 triangles, 6 faces become 6 vertices.
- `ExtrudeNormal` by 0.1: a cube's averaged corner normal is `(±1,±1,±1)/√3`, so each axis gains
  `0.1/1.7320508` and the extent becomes ±0.5577350 — measured to the last digit.
- `SplitAlongPlane` at `x=0`: misses the two faces perpendicular to x, halves the other four,
  6 + 4 = 10.
- `Sphere(6,4)`: 24 faces, `2*6` pole triangles, `6*(4-2)` quads; y spans the full diameter while
  x and z reach only `0.5*cos(30°)` because no vertex lands on the axis.

**6.3b remains:** review the OBJ output and lock checksums.

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

### 6.7 — `Extrude` builds side faces — **done**

**The defect.** `Wz4Mesh::Extrude` decodes the adjacency table with integer division in two
places:

```cpp
sInt m = adj[fi].Adjacent[k]/4;   // wz4_mesh.cpp:3033 — island growth
sInt n = adj[fi].Adjacent[j]/4;   // wz4_mesh.cpp:3111 — boundary edge collection
```

`Adjacent[]` holds `face*4 + vertexIndex`, and a boundary half-edge is stored as **-1**
(`ConnectFaces`, `:1290`). `-1/4` is `0`, so:

- at `:3111` the test `if(n==-1 || faceIsland[n]!=isli)` never sees a boundary, and every rim edge
  is misread as adjoining face 0 — so `isl->NumEdges` stays 0, no edge loop is built, and no side
  faces are generated;
- at `:3033` the same decode makes island growth spuriously pull face 0 into every island it
  touches a boundary from.

Everywhere else in the file the field is decoded with `>>2`, which yields -1 for a boundary
because the shift is arithmetic.

**The fix** is `>>2` in both places, and that was the whole patch — patch 14.

#### The defect is far narrower than the plan assumed

`Adjacent[] == -1` means "no neighbour **at all**", so the bad decode only ever misread a rim edge
lying on a **real mesh boundary**. A rim made of *interior* edges — between a selected face and an
unselected one — holds genuine face indices and decoded correctly either way.

| Selection | Rim is | Before | After |
|---|---|---|---|
| all of an **open** mesh | boundary edges | **broken**, no sides | 1 cap + 4 sides |
| part of a **closed** mesh | interior edges | correct | unchanged |

Every `Extrude` in the bundled documents is the second kind. That is why the defect survived a
decade, and it is why the compatibility cost turned out to be **zero**.

#### Three things the plan got wrong, all corrected by one measurement

Running the sweep before and after — `wz4gen sweep example.wz4 -v`, 2,192 lines including a
checksum for each of 1,097 evaluated meshes — gives a **byte-identical** diff. The other four
documents contain no `Extrude` at all.

| Plan said | Measured |
|---|---|
| the side-face path has almost certainly never executed | it executes and works — 62, 7 and 152-face results in `example.wz4` |
| `example.wz4`'s 14 `Extrude` operators will change geometry | none of them changes |
| expect more than a two-character fix | two characters |

Recorded as A56. The instrument that settled it was the corpus sweep built one stage earlier,
whose entire purpose is running every operator in the reference documents — so the measurement was
one command away throughout.

**Gate — met.** Three derivable cases, all exact:

| Case | | |
|---|---:|---|
| `p_extrude` | 5 quads | one open quad, four boundary edges: 1 cap + 4 sides. Rim stays at y=0, cap moves to y=Amount |
| `p_extrude_steps` | 9 quads | same rim at `Steps = 2`: 1 + 2×4. Sides scale with `Steps`, the cap does not — a one-step case cannot tell whether `Steps` is read at all |
| `p_extrude_closed` | 10 quads | one face of a closed cube: 6 − 1 + 1 cap + 4 sides, +x out to 0.75, still closed. The interior-rim path, included so a future change to the decode cannot break it silently |

Plus the corpus: 1,386 mesh operators across five documents, 0 violations. 141/141 ctest.

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
