# Patch 10 — make the mesh library includable and compilable headlessly

**Files:** 3 upstream (`wz4frlib/wz4_anim.hpp`, `wz4frlib/wz4_mesh.hpp`,
`wz4frlib/wz4_mesh.cpp`), plus `wz4port/compat/include/wz4_mtrl_headless.hpp`
**Phase:** 6 (geometry), stage 6.1
**Status:** applied — `wz4geo` compiles and links; the ops module is patch 11
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

## Change 2 — `wz4_mesh.hpp`: forward-declare `Wz4Mtrl`, and guard `ConvertFrom`

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

And `void ConvertFrom(class ChaosMesh *)` is guarded, matching change 3b below.

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

### 3b — the wz3 `ChaosMesh` import, also guarded

`Wz4Mesh::ConvertFrom` (lines 839–920, again banner to banner) goes the same way.
This one is not a portability decision: **as shipped the function cannot compile
at all.** `chaosmesh_code.hpp` is commented out of this file's includes at line
12, while the body dereferences `src->Clusters[i]->Material->Material->Flags` and
casts to `Texture2D *`. The non-Windows half of this file has evidently never
been built. Its one caller, the `ConvertFromChaosMesh` operator, is dropped by
patch 11.

### 3c — a stale stub signature, corrected

`MakePath`'s non-Windows stub (`wz4_mesh.cpp:6683`) was missing the
`weldThreshold` parameter that had been added to the declaration and to the
Windows definition, so `#else` branch was a definition of nothing:

```
error: out-of-line definition of 'MakePath' does not match any declaration in 'Wz4Mesh'
```

Corrected to match `wz4_mesh.hpp:290`. Same evidence as 3b, from the other
direction: this file's non-Windows path is untested upstream.

## Change 4 — the headless material, in `wz4port/compat/`

Not an upstream change. `wz4_mtrl_headless.hpp` supplies `Wz4Mtrl` and
`SimpleMtrl` with the smallest surface that compiles: the mesh library names them
in cluster construction and destruction, in the object serialiser, and in
`ConvertFrom`.

It deliberately **does not implement a working `Serialize`**, and the header
explains why at length. Briefly: `Wz4Mesh::Serialize` is unreachable in this build
— nothing calls it, `wObject` declares no virtual `Serialize`, and `.wz4`
documents store operators rather than evaluated meshes. A faithful stub is also
impractical, since the real `SimpleMtrl::Serialize_` does `s.OnceRef()` on three
`Texture2D` handles.

The two overloads therefore carry upstream's own base-class bodies verbatim:
`sFatal(L"no serialize for this material type yet")`. Declaring them is not
optional, which I had assumed wrongly at first — `Wz4Mesh::Serialize`'s cluster
loop streams `c.Mtrl` through `sReader`/`sWriter`, and their templates need the
member to exist on a path never taken:

```
base/serialize.hpp:267: error: no member named 'Serialize' in 'Wz4Mtrl'
```

`SimpleMtrl` inherits them rather than overriding, so the dead path stops loudly
instead of silently misreading a stream.

## Invariant

`git status` on `altona_wz4/` shows exactly three files for this patch:
`wz4frlib/wz4_anim.hpp`, `wz4frlib/wz4_mesh.hpp`, `wz4frlib/wz4_mesh.cpp`.

## The ops module

`wz4_mesh_ops.ops`'s header block includes four other generated op modules, which
the phase survey missed. Of the **47** mesh operators only **two** name a type
from another module, so 45 register with nothing outside the mesh library. That
work — and the `wz4ops` change it needed, because an operator *declaration*
cannot be wrapped in a preprocessor guard — is **patch 11**.
