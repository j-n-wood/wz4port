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

## Also in this file: two comment-only TODO markers

`Wz4Mesh::Extrude`'s two `Adjacent[...]/4` decodes carry `TODO(6.7)` comments
pointing at the fix and at `docs/08-phase-geometry.md` §6.7. No code changes —
they exist so the defect is findable from the code rather than only from the
docs, and they are listed here to keep the `altona_wz4/` isolation invariant
honest.

## Why this one was fixed immediately, and the Extrude defect is stage 6.7

Stage 6.3 found two upstream faults. Both are being fixed; only this one could be
fixed **without weighing anything**, which is the difference worth recording.

A crash cannot be authored against, so guarding it changes no document's
appearance and there is nothing to trade off. The Extrude defect produces
deterministic *output*, and `example.wz4`'s 14 `Extrude` operators were authored
against it — so fixing it does change what those documents render, which is a
decision rather than a repair. It is stage 6.7, taken deliberately.

My first version of this patch concluded the Extrude defect should be left alone
permanently, on the grounds that matching the 2014 tool was the goal. That was
wrong and is corrected in `architecture.md` A54: fidelity to the original binary
is a tie-breaker for genuine ambiguity, not a veto over an operator that cannot
do its job. An extrude with no side faces is a decode error in a code path that
has never executed, not a design anyone chose.

## Invariant

`git status` on `altona_wz4/` shows one file for this patch,
`wz4/wz4frlib/wz4_mesh.cpp`, which patches 10, 11 and 12 also touch.

## Behaviour

`BakeAnim` on a mesh with no skeleton passes the mesh through unchanged. Asserted
by `mesh_ops`' `a_bakeanim` case: 6 quads in, 6 quads out, bounds intact.
