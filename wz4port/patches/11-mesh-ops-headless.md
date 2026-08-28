# Patch 11 — dropping an operator from the headless registry, and the mesh ops module

**Files:** 4 in `tools/wz4ops/`, 3 upstream (`wz4frlib/wz4_mesh_ops.ops`,
`wz4frlib/wz4_mesh_obj.cpp`, `wz4frlib/wz4_mesh_lwo.cpp`), plus
`wz4port/compat/mesh_xsi_stub.cpp`
**Phase:** 6 (geometry), stage 6.1
**Status:** applied — `wz4geo` builds, links and passes `mesh_register`
**Extends:** patch 05, which added `wz4ops -headless`
**Wider context:** patch 10 (the mesh library itself), `docs/08-phase-geometry.md`

## Why

Patch 10 made `wz4_mesh.hpp` includable and `wz4_mesh.cpp` compilable. That is
not enough to *register* the library: the operators come from
`wz4_mesh_ops.ops`, whose header block includes four other generated op modules
and whose operator list names two types this build cannot have.

Measured before deciding anything: of the **47** mesh operators, exactly **two**
name a type from another module.

| Operator | Foreign type | Owned by |
|---|---|---|
| `ConvertFromChaosMesh(ChaosMesh)` | `ChaosMesh` | `chaosmesh_ops` |
| `SetMaterial(Wz4Mesh,Wz4Mtrl)` | `Wz4Mtrl` | `wz4_mtrl2_ops` |

So 45 register against nothing outside the mesh library. The two get the
treatment `GenBitmap.Text` got in phase 4.1 — dropped, documented, revisited.

## Why a preprocessor guard could not do this

Patch 05 guarded four things in `.ops` files with `#ifndef WZ4_HEADLESS`, and
that was the obvious first move here too. It does not work, for two separate
reasons, and both are worth recording because they look like oversights.

**1. The grammar has no top-level `#if`.** `code` and `header` blocks are
verbatim C++, which is why a guard inside one passes straight through to the
output. `operator` and `type` are parsed. `parse.cpp:37-75` dispatches on the
keyword and a `#` line at that level is a syntax error, so an operator
*declaration* cannot be guarded — only its body, which is what patch 05 did to
`Screenshot`.

**2. The body is not the problem anyway.** `SetMaterial`'s code block compiles
perfectly well against patch 10's headless `Wz4Mtrl`. What cannot compile is the
*registration*, because an input type is emitted as a bare global from the owning
module's generated header:

```cpp
in[0].Type = Wz4MtrlType;         // declared in wz4_mtrl2_ops.hpp
```

Guarding the body would leave that line behind. The operator has to go, or the
whole materials module has to build — and it cannot, since `wz4_mtrl2.hpp` needs
the asc-generated `wz4_mtrl2_shader.hpp`.

The rejected third option was to **register a phantom `Wz4Mtrl` type** so the
symbol resolves. That is worse than omitting the operator: nothing headless can
produce a `Wz4Mtrl`, so `SetMaterial` would sit in the palette permanently
unusable, and the type would appear in the inspector as a real thing.

## Change 1 — `tools/wz4ops/`: a per-operator opt-out

```
 M doc.hpp     Op::Headless
 M doc.cpp     Headless = 1 in Op::Op()
 M parse.cpp   the `headless = <int>;` keyword in an operator body
 M output.cpp  the skips, and the .hpp #define
```

Written in the `.ops` as:

```
operator Wz4Mesh SetMaterial(Wz4Mesh,Wz4Mtrl)
{
  headless = 0;
  ...
}
```

Under `-headless`, `OutputOps` skips the whole operator — parameter struct,
helper struct, code body, `Cmd`, `Def` — and `OutputMain` skips its `wClass`
registration. `OutputAnim` needed nothing: it is already `!Headless` at the call
site.

A new *flag* in the existing `flags = ` choice list was rejected. Those map onto
runtime `wCF_` bits that the document model reads; this is a directive to the
generator, and giving it a runtime bit would be a lie about what it is.

Consistent with patch 05, **every skip is printed**:

```
wz4ops -headless: skipping Wz4Mesh::BeginEngine (gui type in signature)
wz4ops -headless: skipping Wz4Mesh::Paint (gui type in signature)
wz4ops -headless: skipping Wz4Mesh::Wireframe (gui type in signature)
wz4ops -headless: skipping operator Wz4Mesh.ConvertFromChaosMesh (headless = 0)
wz4ops -headless: skipping operator Wz4Mesh.SetMaterial (headless = 0)
```

Those first three are patch 05's `ExternIsGuiOnly()` working unchanged on a
module it had never seen: `Init`, `Exit` and `Hit` are kept, the three painting
externs dropped, with no new rule needed.

## Change 2 — `-headless` now defines `WZ4_HEADLESS` in the generated `.hpp` too

Patch 05 emitted `#define WZ4_HEADLESS 1` at the top of the generated `.cpp`
only, because the guards it needed were all in `code` blocks. `header` blocks go
to the `.hpp`, and the `.hpp` is read by **every consumer of the module**, not
just the generated `.cpp`.

Found the direct way. `wz4_mesh_ops.ops` puts its includes in a `header` block,
so guarding them left the `.cpp` fine and broke `wz4_mesh.cpp`, which includes
the same generated header and does not define the macro:

```
wz4_mesh_ops.ops:11:10: fatal error: 'wz4lib/poc_ops.hpp' file not found
```

The `#define` is now emitted in the `.hpp` as well, wrapped in `#ifndef` so the
`.cpp`'s own copy is not a redefinition. Still entirely inside `if(Headless)`.

## Change 3 — `wz4_mesh_ops.ops`: the header block and the two directives

Four of the five foreign includes are guarded out, and the headless material
takes their place — the type block declares `SimpleMtrl *DefaultMtrl`, which
lands in the generated `.hpp`, so the stand-in has to be visible there:

```cpp
#ifndef WZ4_HEADLESS
#include "wz4lib/poc_ops.hpp"           // unused here; poc needs util/shaders.hpp
#include "wz4frlib/chaosmesh_ops.hpp"   // only ConvertFromChaosMesh, headless = 0
#include "wz4frlib/wz4_anim_ops.hpp"    // unused here
#include "wz4frlib/wz4_mtrl2_ops.hpp"   // only SetMaterial, headless = 0
#include "wz4frlib/wz4_mtrl2.hpp"
#else
#include "wz4_mtrl_headless.hpp"
#endif
```

`wz4_anim_ops.hpp` is genuinely unused: nothing in the file names an animation
*type*, and the animation *classes* arrive through `wz4_mesh.hpp`. The module is
built anyway — `wz4_anim_ops.ops` has no foreign dependency at all, its two
`Wz4Skeleton` operators cost nothing, and `wz4_anim.cpp` has to link because the
mesh engine calls into it.

## Change 4 — the two import/export files, and the one that could not follow

`wz4_mesh_obj.cpp` and `wz4_mesh_lwo.cpp` both include `wz4_mtrl2_ops.hpp` and
**use nothing from it** — checked for `Mtrl`, `Texture2D` and `Wz4MtrlType`
across both files, zero occurrences. Guarding the include is the whole change,
and it is the same case as patch 05's `poc_ops.hpp` in `wz3_bitmap_ops.ops`.
`SaveOBJ` living in the first of them is what stage 6.2 needs.

`wz4_mesh_xsi.cpp` is different in kind, and the difference is measurable rather
than a matter of taste: across 2,142 lines it *constructs* `SimpleMtrl` objects,
creates `Texture2D` objects, reads `mtrl->Tex[i]` and passes `sMTRL_*` render
state. It needs materials to exist for real.

So `Wz4Mesh::LoadXSI` comes from `wz4port/compat/mesh_xsi_stub.cpp`, which
returns `sFALSE` after saying why. That keeps the change out of `altona_wz4/`
entirely — a shim in preference to a patch, which is the project's stated order
— and leaves `Import` registered and working for OBJ and LWO.

## Why this is safe

- **The generator change is inert when `-headless` is off.** All three hunks in
  `output.cpp` sit inside `if(Headless ...)`, so the non-headless path cannot
  reach them. Confirmed on the output too: the non-headless `wz4_mesh_ops.cpp`
  still emits `Wz4MeshCmdConvertFromChaosMesh`, `...Gui...`, `...Bind...` and
  `...Def...` for both dropped operators, while the headless one contains zero
  occurrences of either name. `wz4ops_gate` now covers the mesh and anim modules
  as well, so it keeps proving this.
- **The suppression is never silent** — five lines printed, quoted above.
- **The registry is checked, not assumed.** `mesh_register` asserts 45 operators
  output `Wz4Mesh`, asserts both dropped operators absent *by name*, and then
  evaluates a `Cube` and measures the mesh: 6 faces, all quads, 24 vertices,
  bounds exactly -0.5..0.5 on every axis.
- **The existing suite is unaffected:** 133/133, including the 90 byte-exact
  texture goldens, which every module recompiled for.

## Invariant

`git status` on `altona_wz4/` shows exactly these files for this patch:
`altona/tools/wz4ops/{doc.hpp,doc.cpp,parse.cpp,output.cpp}`,
`wz4/wz4frlib/wz4_mesh_ops.ops`, `wz4/wz4frlib/wz4_mesh_obj.cpp`,
`wz4/wz4frlib/wz4_mesh_lwo.cpp` — plus patch 10's three.

## Behaviour

Two mesh operators are absent from the headless build, and XSI import refuses.
`SetMaterial` is the one to revisit first: its body already compiles, so it needs
only a materials type to exist, and it is the operator a later materials phase
would re-enable for free.

## Amendment, phase 9.2 — `SetMaterial` is back

That prediction held exactly, and the re-enabling cost two lines.

`wz4port/geo/material_ops.ops` registers the `Wz4Mtrl` and `SimpleMtrl` types
that `wz4_mtrl2_ops` would have, and gives them a producer
(`SimpleMtrl.TextureMaterial`). With the type no longer phantom, this patch's
`headless = 0;` on `SetMaterial` was removed and the headless header block gained
`#include "geo/material_ops.hpp"` for the `Wz4MtrlType` its input names. The
operator body was never touched, in this patch or since — it always compiled
against `compat/include/wz4_mtrl_headless.hpp`.

So the file's diff against upstream shrinks: the guarded include block stays, and
the `headless = 0;` on `SetMaterial` is gone. `ConvertFromChaosMesh` keeps its
own, since `ChaosMesh` remains unregistered.

Measured: Wz4Mesh operators go 46 → 47, `mesh_register` now asserts SetMaterial
is PRESENT, and `tests/material.cpp` checks a material attached this way survives
a `Transform` downstream as the same object rather than a copy.
