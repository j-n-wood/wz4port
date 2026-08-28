# Phase 9 — Textures on geometry, via a partial material

## The question this answers

*Does assigning a texture to geometry need a new operator, or can it be a partial implementation of
the material system we skipped?*

**It is the material system, and most of the scaffolding already exists.** A bespoke
`SetTexture(Wz4Mesh, GenBitmap)` operator would duplicate a slot the mesh already has, would not
survive the operator graph the way a material does, and would have no place to go in glTF that a
material does not already describe better.

## What is already there — measured, not assumed

| | state |
|---|---|
| `wz4port/compat/include/wz4_mtrl_headless.hpp` | **concrete** `Wz4Mtrl` and `SimpleMtrl` classes, `wObject`-derived, written in phase 6 as "the smallest surface that compiles" |
| `Wz4MeshCluster::Mtrl` | live and refcounted; materials survive `CopyFrom`, `Add`, and every cluster split — `AddRef`/`Release` compile and run headless today |
| `SimpleMtrl::SetTex(stage, void *tex, tflags)` | exists, deliberately shaped like upstream's, and currently **accepts and drops** the texture |
| `SetMaterial(Wz4Mesh, Wz4Mtrl)` | exists upstream, disabled with `headless = 0;` — and multi-material meshes work through it, since it finds or creates a cluster per material |
| `GenBitmap` | `: BitmapBase : wObject`, so a material can hold one and refcount it |
| cluster → glTF | already one **primitive** per cluster; a per-primitive material needs no restructuring |
| UV orientation | verified: v runs downward, matching glTF. No flip on export |
| `stbi_write_png_to_mem` | **external linkage** (`stb_image_write.h:426`), already compiled in — PNG bytes for GLB embedding need no upstream patch and no temp file |

**The disabling comment on `SetMaterial` states one reason, and it is conditional:**

> *"Registering a phantom `Wz4Mtrl` type instead would be worse than omitting this: nothing headless
> can produce a `Wz4Mtrl`, so the operator would appear in the palette and never be usable."*

That objection dissolves the moment something produces one. This phase makes the type real.

## Scope

**Diffuse colour only.** One `baseColorTexture` per material, `TEXCOORD_0`, REPEAT wrapping. No
metallic-roughness maps, no normal maps, no emissive, no alpha modes, no second UV set. The class
carries three texture stages upstream and this phase fills stage 0.

---

## 9.1 — Make the material real — **done**

162/162 ctest. `wz4gen list` shows the `Wz4Mtrl` tab with `SimpleMtrl.TextureMaterial` under it, and
`palette_insert` — which walks the registry rather than asserting a fixed count — inserted and
connected it along with the other 113.

Three things came out different from the plan below.

**The operator is `TextureMaterial`, not `SimpleMaterial`.** Upstream declares `SimpleMaterial` with
20 parameter words, its metadata *is* loaded here, and `wMetaLibrary::Find` resolves by first match —
so the name bound to upstream's layout, which `wz4gen describe` showed plainly. Binding a 20-word
layout to a 2-word operator writes past the end of its `EditData`. Recorded as **A67**, along with
the correction that the corpus metadata is a deliverable and not the inert coverage its own comment
claimed. The *type* names stay as upstream's: a type carries no parameter layout, so a duplicate
there is cosmetic, and `SetMaterial`'s input must resolve to them.

**`SetBitmap`, not `SetTex`.** The plan said to make `SetTex` store its argument. It should not:
upstream's `Tex[3]` are uploaded GPU textures converted *from* a bitmap, while ours are the source
bitmaps themselves. Overloading one name onto both would leave a later phase reviving the renderer
to untangle which was meant. `SetTex` stays the no-op it was, for upstream's shape; `SetBitmap` is
ours, refcounted, and type-safe with no `void *` cast.

**`Type` is set in the operator body, not the constructor.** `wDocument` fatals if an operator's
output has no `Type` (`doc.cpp:4336`) — checked *after* the body runs, so the body is in time.
Upstream sets it in constructors, but its material classes live in `.cpp` files that include their
own generated ops header; ours is a header-only stand-in that `wz4_mesh_ops.ops` includes, and a
cycle there would be worse.

## 9.1 (original plan text) — Make the material real

**`compat/include/wz4_mtrl_headless.hpp` changes character**, and its header comment must say so: it
stops being a stand-in that only has to compile and becomes this port's partial material. It stays
in `compat/` because `Wz4MeshCluster::Mtrl` is typed `Wz4Mtrl *` and that type has to be the one
this header defines.

- `SimpleMtrl` gains `BitmapBase *Tex[3]`, and `SetTex` **stores and `AddRef`s** instead of
  dropping. The destructor releases. The parameter stays `void *` so the signature still matches
  upstream's shape; the cast happens inside.
- `Wz4Mtrl::Name` already exists and becomes the glTF material name.

**Register the `Wz4Mtrl` type and add a producer**, in a new `wz4port/geo/material_ops.ops` — a
`.ops` in `wz4port/` costs zero upstream changes (A62), and this is the second such module:

```
type Wz4Mtrl { name = "New Wz4 Material"; color = ...; }

operator Wz4Mtrl SimpleMaterial(?GenBitmap)
{
  parameter { colour Colour; flags TexFlags("wrap|clamp"); }
  code { /* new SimpleMtrl, SetTex(0, in0) */ }
}
```

The optional input means an untextured coloured material is expressible too, which the exporter
needs anyway for meshes that never get a texture.

**The type-name hazard, stated once:** headless, `Wz4Mtrl` is *our* class; in an unguarded build it
is upstream's. That divergence already exists — this phase widens it from "a stub with the same
shape" to "a stub that stores something upstream stores differently". It is the same trade patch 10
took, and the guard is the same.

## 9.2 — Re-enable `SetMaterial` — **done**

Two lines: `headless = 0;` deleted, and `#include "geo/material_ops.hpp"` added to the headless
header block for the `Wz4MtrlType` the input names. The operator body was never the problem and was
not touched. Wz4Mesh operators go **46 → 47**; patch 11 carries the amendment.

**A latent `.wz4t` bug fell out of it.** The first `SetMaterial` case failed with *"required input is
missing"*. `SetMaterial`'s material input is a **link** — it names another operator rather than
reading what sits above it — and `wDocument::Connect` only resolves a link when `Select==1`
(`doc.cpp:2763`). Our reader set `LinkName` and left `Select` at 0, so **every link in every `.wz4t`
was inert**. The writer emits the name and nothing else, so a `.wz4` → `.wz4t` → `.wz4` round trip
kept the name and silently lost the connection. Fixed by setting `Select = 1` alongside the name,
which makes the two sides agree: a written link name means an active link.

Still not expressible: `Select==2` ("empty") and `Select>=3` ("use input N"). No bundled document
uses them and they round-trip as 0 — recorded rather than fixed, since inventing syntax for a state
nothing produces would be speculative.

**The suite demanded the coverage, which is the system working.** Registering an operator made
`mesh_ops` fail with *"no case exercises SetMaterial"*, so `ops_mtrl.wz4t` gained a
`Cube → SetMaterial → Transform` chain and `mesh_cases` two entries. `mm_set`'s checksum is
**derived**: attaching a material must not move a vertex, so it has to equal a plain unit Cube's —
and it matches `ops_topo`'s independent `p_in_subdiv` exactly. Checked before locking, not after.

## 9.2 (original plan text) — Re-enable `SetMaterial`

Delete `headless = 0;` and the comment explaining the omission. This is an upstream `.ops` edit in a
file **patch 11 already touches**, so it amends a documented patch rather than adding one.

Verify the selection modes still work headless: `all`, `none`, `selected`, `unselected`, `cluster`.
The body deletes and creates clusters, which is exactly the multi-material path glTF wants.

## 9.3 — Export — **done**

163/163. A material assigned in a document reaches the glTF: one material per primitive from its
cluster, with `baseColorTexture`, a REPEAT sampler and the image as PNG — embedded in a `.glb`'s BIN
chunk, or written beside a `.gltf`. `ninja gltf_samples` now includes `mm_moved.glb` (textured) and
`mf_flat.glb` (coloured).

### The colour space, settled by reading the generator rather than guessing

glTF specifies the two halves differently, which is the whole trap: **`baseColorTexture` is
sRGB-encoded** and **`baseColorFactor` is linear**.

**The texture needs no conversion.** `GetColor64` scales an 8-bit component into a 15-bit range and
applies no transfer function (`wz3_bitmap_code.cpp:213-221`); `CopyTo` narrows back by a plain shift.
A colour authored as mid-grey is stored as mid-grey and displays as mid-grey — display-referred,
which is what sRGB-encoded means. So the PNG carries the authored values and glTF reads them right.

**The factor does.** It comes from a colour picker, so it is display-referred for the same reason,
while the spec wants linear. The test material is `#ff3060c0` — deliberately asymmetric and non-grey,
because white passes either way — and `gltf_roundtrip` computes the expected value **from the spec's
own transfer function** rather than from the writer, so a constant lifted from the code under test
cannot satisfy it. Verified negatively by emitting the raw byte:
`want 0.02956, got 0.18824 (raw/255 would be 0.18824)`.

### Two things the implementation corrected

**Materials are deduplicated by pointer, not by contents.** `SetMaterial` shares one object across
the clusters using it and refcounts it, so pointer identity is exactly the "same material" relation
the document already maintains — `tests/material.cpp` asserts a `Transform` preserves it. Comparing
contents would silently merge two materials a user kept distinct.

**The sidecar is named after the output, not the mesh.** The first version used `mesh->Name`, which
is empty on almost every mesh, so every `.gltf` export in a directory wrote `mesh_tex0.png` over the
last one and every file pointed at whichever finished last. It is now `<stem>_tex0.png`, beside its
own `.gltf` and `.bin`.

### What the goldens showed

One line changed in each of the six, and no `.bin` at all: `roughnessFactor` 0.8 → 1. Untextured
meshes still get the grey default; 1.0 is glTF's own default and the honest value for "no roughness
data", where 0.8 was arbitrary. That the diff was one readable line across six files is the payoff
of keeping the goldens as text.

### Coverage the goldens do not give

None of the six golden stores is textured, so the `.gltf` **sidecar** path had no coverage — the same
gap that left the `.glb` container untested in 8.4. `gltf_material.cmake` exports both containers and
checks the uri names a file that exists beside the `.gltf`, that it starts with the PNG magic, and
that the bytes embedded in the `.glb` are **byte-identical** to the sidecar.

The checker also walks the whole chain: `material → baseColorTexture.index → textures → source →
images → bufferView`. Four hops, four different arrays, and every one can be off by one while the
file still parses and every count still agrees.

## 9.3 (original plan text) — Export

Extend `geo/gltf_write.cpp`. Currently one default material shared by every primitive; instead, one
glTF material **per cluster**, from that cluster's `Mtrl`:

- `materials[i].pbrMetallicRoughness.baseColorFactor` from the material's colour
- `...baseColorTexture.index` when stage 0 is set
- `images` — PNG bytes from `GenBitmap::CopyTo(sImage)` then `stbi_write_png_to_mem`. **GLB**: a
  `bufferView` into the BIN chunk plus `mimeType`. **`.gltf`**: a sidecar `.png` beside the `.bin`,
  named after the material.
- `samplers` — `wrapS`/`wrapT` = **REPEAT (10497)**. This is not a default to shrug at: the Cube's
  UVs run **0..4**, one full tile per face in a continuous band around the four sides, so
  `CLAMP_TO_EDGE` would smear the texture's edge column across three faces of every cube.
- A cluster with no material keeps today's default grey.

**Two colour-space questions to settle by measurement, not assumption:** glTF requires
`baseColorTexture` to be **sRGB-encoded** and `baseColorFactor` to be **linear**. `GenBitmap` is
16-bit with `0x8000 = 1.0` and `CopyTo` narrows by a plain shift — whether its contents are linear or
already sRGB decides whether a conversion belongs on export. Get this wrong and every texture is
visibly too dark or too bright.

## 9.4 — Preview

The 3D viewer samples the texture, so the editor shows what the export will. `wMeshView` already
uploads `TEXCOORD_0`; this adds a GL texture from the cluster's `GenBitmap` and a sampler in the
fragment shader. Deliberately last: the export is the deliverable, and a preview that disagrees with
it is worse than none.

---

## Verification

- **9.1** — `wz4gen list` shows `Wz4Mtrl` with `SimpleMaterial` under it; `wz4gen describe` shows its
  parameters, proving metadata reached the editor panel.
- **9.2** — a `.wz4t` case chaining `Cube → SetMaterial(SimpleMaterial(Perlin))` evaluates, and the
  material survives a `Transform` downstream — that is the property a bespoke operator would not have.
- **9.3** — `gltf_roundtrip` grows assertions: every `baseColorTexture` names a real `texture`, which
  names a real `image`, whose `bufferView` is in range; the sampler is REPEAT; the PNG bytes start
  with the PNG magic. Then a **golden**, since the JSON is diffable. Then the Khronos validator, if
  installed — a textured file is where it earns its keep, because the image/sampler/texture triangle
  is the part of glTF most easily built wrong while remaining self-consistent.
- **9.4** — the existing `editor_differs.cmake` shape: render with and without the texture and
  require the images to differ.
- **Look at it.** A checkerboard on a cube shows tiling, orientation and seams in one glance, and is
  the one test that catches a V flip. `docs/editor.md` gets the user-facing half.

## Effort

9.1 and 9.2 are small — the classes and the operator exist, and the work is storage plus a type
registration. **9.3 is the phase**: images, samplers, textures, per-cluster materials, two container
paths and a colour-space decision. 9.4 is optional and separable.

Upstream footprint: **one line deleted** from `wz4_mesh_ops.ops` (an existing patch). Everything else
is `wz4port/`.
