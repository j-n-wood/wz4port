# Patch 10 — make the mesh library includable and compilable headlessly

**Files:** 3 upstream (`wz4frlib/wz4_anim.hpp`, `wz4frlib/wz4_mesh.hpp`,
`wz4frlib/wz4_mesh.cpp`), plus `wz4port/compat/include/wz4_mtrl_headless.hpp`
**Phase:** 6 (geometry), stage 6.1
**Status:** applied — headers compile; ops module and build wiring outstanding
**Wider context:** `docs/08-phase-geometry.md`, whose structural section was
rewritten from measurement before any of this was written

## Why

`wz4_mesh.hpp` could not be included at all in a headless build. The chain is

```
wz4_mesh.hpp -> wz4_mtrl2.hpp -> wz4_mtrl2_shader.hpp   DOES NOT EXIST
                              -> wz4lib/doc.hpp -> doc_gui.hpp -> util/shaders.hpp
             -> wz4_anim.hpp  -> wz4lib/doc.hpp -> (same)
```

`wz4_mtrl2_shader.hpp` is generated from `wz4_mtrl2_shader.asc` by the `asc`
shader compiler, which this port does not build and which `CLAUDE.md` lists as
out of scope. Same shape as the phase-2 blocker that split `doc.hpp`.

Everything here is guarded on `WZ4PORT_HEADLESS_MTRL`, so the unguarded build is
byte-for-byte the original.

## Change 1 — `wz4_anim.hpp`: `doc.hpp` → `doc_core.hpp`

`doc.hpp` means "the document model AND the gui". This header uses none of the
gui half — checked for `wPaintInfo`, `wHandle`, `wGridFrameHelper`,
`wCustomEditor` and `sSimpleMaterial`, none present. Identical to what patch 06
did for `wz3_bitmap_code.hpp`.

Not guarded, because it is correct unconditionally.

## Change 2 — `wz4_mesh.hpp`: forward-declare `Wz4Mtrl`

The header's only use of materials is `Wz4Mtrl *Mtrl` in `Wz4MeshCluster` — a
**pointer**. So a forward declaration is sufficient and the full header is not
needed even in principle.

This is smaller than the plan expected. `08-phase-geometry.md` proposed extracting
`Wz4Mtrl` into a new upstream `wz4_mtrl_iface.hpp`, on the pattern of patches 04,
06 and 09. Measurement made that unnecessary: those extractions existed because
the headless side needed the *definitions* (`sGuiTheme` by value, `sGuiColor`'s
enumerators). Here it needs only a name.

Also adds `#include "util/image.hpp"`, because `Displace()` takes an
`sImageI16 *` that the header had been getting transitively through
`wz4_mtrl2.hpp`. Declaring what you use is right either way; relying on a
transitive include is how a header comes to depend on the whole world.

## Change 3 — `wz4_mesh.cpp`: the renderer is guarded, not moved

Lines 4,424–5,287 — `ChargeWire`, `ChargeSolid`, `ChargeBBox`, `Charge`,
`BeforeFrame`, `Render`, `RenderInst`, `RenderBone`, `RenderBoneInst` — are
wrapped in `#if !WZ4PORT_HEADLESS_MTRL`.

The boundaries are the `/*** Painting ***/` and `/*** Generators ***/` banner
comments, so they are exact. And the property that makes this possible was
measured rather than assumed:

> **Zero** references to `sGeometry`, `sVertexFormat`, `sMaterial`, `sTexture`,
> `sCBuffer`, `sSetTarget` or `sDrawRange` exist in this file outside that region.

**The plan said to MOVE the region into a new `wz4_mesh_render.cpp`. A guard is
used instead.** The project rule is to prefer a shim to a patch and a patch to
forking a file, and moving 864 lines is nearer a fork than a patch: it risks
transcription, it creates an upstream file that must be kept in step with the
original, and it buys nothing the guard does not. The plan's second reason for
moving — pimpl the GPU handles out of the header — evaporated once the header
proved to need only a forward declaration.

Flipping the flag brings the renderer back in one step, which is what a later
phase wanting a real renderer would want.

## Change 4 — the headless material, in `wz4port/compat/`

Not an upstream change. `wz4_mtrl_headless.hpp` supplies `Wz4Mtrl` and
`SimpleMtrl` with the smallest surface that compiles: the mesh library names them
in cluster construction and destruction, in the object serialiser, and in
`ConvertFrom`.

It deliberately **does not implement `Serialize`**, and the header explains why at
length. Briefly: `Wz4Mesh::Serialize` is unreachable in this build — nothing calls
it, `wObject` declares no virtual `Serialize`, and `.wz4` documents store
operators rather than evaluated meshes. A faithful stub is also impractical, since
the real `SimpleMtrl::Serialize_` does `s.OnceRef()` on three `Texture2D` handles.
Leaving it alone means the inherited `Wz4Mtrl::Serialize` applies, which does
`sFatal(L"no serialize for this material type yet")` — a loud stop rather than a
desynced stream, if the dead path ever comes alive.

## Invariant

`git status` on `altona_wz4/` shows exactly three files for this patch:
`wz4frlib/wz4_anim.hpp`, `wz4frlib/wz4_mesh.hpp`, `wz4frlib/wz4_mesh.cpp`.

## Still outstanding for 6.1

`wz4_mesh_ops.ops`'s header block includes four other generated op modules —
`wz4lib/poc_ops.hpp`, `chaosmesh_ops.hpp`, `wz4_anim_ops.hpp`,
`wz4_mtrl2_ops.hpp` — plus `wz4_mtrl2.hpp`. The plan did not mention this.

Measured, the exposure is small: of the **47** mesh operators, only **two** name a
type from another module — `ConvertFromChaosMesh(ChaosMesh)` and
`SetMaterial(Wz4Mesh,Wz4Mtrl)`. So 45 register with nothing outside the mesh
library, and the two get the same treatment `GenBitmap.Text` got in phase 4.1:
guarded out, documented, revisited later. Patch 05's `#ifndef WZ4_HEADLESS`
guards in `wz3_bitmap_ops.ops` are the precedent.
