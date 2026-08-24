# Patch 12 — `MakeText`/`MakePath` warn instead of aborting

**Files:** 1 upstream (`wz4frlib/wz4_mesh.cpp`)
**Phase:** 6 (geometry), stage 6.3
**Status:** applied
**Precedent:** patch 08, which did the same for `GenBitmap::Text`
**Wider context:** `docs/08-phase-geometry.md` §6.3

## Why

The non-Windows arms of `Wz4Mesh::MakeText` and `Wz4Mesh::MakePath` called

```cpp
sFatal(L"Wz4Mesh::MakeText() only for windows...");
```

`sFatal` aborts the process. In a library function reached from an operator body,
that is the wrong severity: **one `Text3D` operator takes down the whole
document**, so nothing else in it can be evaluated, tested or previewed either.
`example.wz4` has six of them among its 39 mesh operators.

Patch 10 already had to correct `MakePath`'s signature here, because this `#else`
arm had never been compiled at all. This is the second thing wrong with it, and
the same evidence: nobody has run this path.

## The change

Both stubs now print one line and return, leaving the mesh empty. The operator
bodies then run `CalcNormalAndTangents()` over zero faces, which is harmless —
verified, not assumed:

```
txt: Wz4Mesh.Text3D
Wz4Mesh::MakeText: not implemented on this platform, mesh left empty (hund.)
  0 vertices, 0 faces (0 tri, 0 quad), 0 clusters
exit=0
```

This is exactly what `GenBitmap::Text` does and for the same stated reason
(`wz3_bitmap_code.cpp:2517`, patch 08): *"leaves the bitmap untouched rather than
failing, so a graph containing a Text operator still evaluates and everything
downstream of it can be seen."*

## Why this is the right severity, not merely the convenient one

The port already treats "we do not support this operator" as a **recoverable**
condition everywhere else it arises, and each of those was a deliberate decision:

- patch 07 makes `wOp` retain a foreign class rather than rewriting it as
  `UnknownOp`, so a build that does not know a module cannot damage a document.
- patch 05 makes the `Screenshot` operator call `cmd->SetError(...)` headlessly
  rather than failing the build or the run.
- patch 08 leaves `GenBitmap.Text` a no-op.

An abort is the one response that cannot be contained by the caller, and it
punishes the 44 operators that do work for the sake of the one that does not.

The cost is that a `Text3D` in a graph now produces an *empty* mesh rather than
stopping. That is visible — the warning prints on every call, and the reported
counts are zeros — and it is the same trade phase 4 accepted for two full stages
before 4.5 implemented the real thing.

## Invariant

`git status` on `altona_wz4/` shows one file for this patch,
`wz4/wz4frlib/wz4_mesh.cpp`, which patches 10 and 11 also touch.

## Behaviour

`Text3D` and `Path3D` evaluate to empty meshes and say so. Stage 6.6 replaces
both with a FreeType outline extractor plus a tessellator.
