# Patch 13 — `Wz4Mesh::BakeAnim` guards a null skeleton

**Files:** 1 upstream (`wz4frlib/wz4_mesh.cpp`)
**Phase:** 6 (geometry), stage 6.3
**Status:** applied
**Wider context:** `docs/08-phase-geometry.md` §6.3, `docs/architecture.md` A55

## Why

`Wz4Mesh::BakeAnim` dereferenced `Skeleton` on its first line:

```cpp
void Wz4Mesh::BakeAnim(sF32 time)
{
  sMatrix34 *bonemat,*basemat;
  Wz4MeshVertex *v;

  sInt max = Skeleton->Joints.GetCount();     // <- null on any unskinned mesh
```

`Skeleton` is 0 on every mesh a generator produces. So the `BakeAnim` operator
**segfaults** on a plain `Cube`:

```
$ wz4gen render ops_attr.wz4t -op a_bakeanim
a_bakeanim: Wz4Mesh.BakeAnim
exit=139
```

```
stop reason = EXC_BAD_ACCESS (code=1, address=0x28)
frame #0: sStaticArray<Wz4AnimJoint>::GetCount(this=0x0000000000000018)
```

Found by stage 6.3's per-operator case, which is the first thing that ever gave
this operator an input it had not been hand-fed in an editor.

## The change

```cpp
  if(!Skeleton)
    return;
```

Baking no animation is exactly a no-op, so returning is both safe and correct.

## Why this one is fixed, when the Extrude defect in the same stage is not

Stage 6.3 found two upstream faults and treated them differently. The distinction
is worth stating, because "fix upstream bugs" and "stay faithful to the original
tool" pull against each other and the rule that separates them is simple:

**Can a working document depend on the current behaviour?**

- **The Extrude adjacency defect** (`/4` where the file elsewhere uses `>>2`, so a
  boundary half-edge stored as -1 decodes to face 0 — `wz4_mesh.cpp:3033`,
  `:3111`) produces *output*. Wrong-looking output, but deterministic, identical
  on every compiler since integer division has always truncated toward zero, and
  `example.wz4`'s 14 `Extrude` operators were authored against it. Changing it
  would make this port disagree with the tool the demos were built with.
  **Left alone**, asserted as-is, and recorded.
- **This one** produces a *crash*. Nothing can be authored against a segfault, so
  no document's appearance can change. There is no fidelity argument to weigh.

## Invariant

`git status` on `altona_wz4/` shows one file for this patch,
`wz4/wz4frlib/wz4_mesh.cpp`, which patches 10, 11 and 12 also touch.

## Behaviour

`BakeAnim` on a mesh with no skeleton passes the mesh through unchanged. Asserted
by `mesh_ops`' `a_bakeanim` case: 6 quads in, 6 quads out, bounds intact.
