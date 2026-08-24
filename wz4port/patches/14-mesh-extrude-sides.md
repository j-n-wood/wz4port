# Patch 14 — `Extrude` builds side faces on an open boundary

**Files:** 1 upstream (`wz4frlib/wz4_mesh.cpp`), two characters
**Phase:** 6 (geometry), stage 6.7
**Status:** applied
**Wider context:** `docs/08-phase-geometry.md` §6.7, `docs/architecture.md` A54, A56

## Why

`Wz4Mesh::Extrude` decoded the adjacency table with integer division:

```cpp
sInt m = adj[fi].Adjacent[k]/4;   // island growth
sInt n = adj[fi].Adjacent[j]/4;   // boundary edge collection
```

`Adjacent[]` holds `face*4 + vertexIndex`, and a half-edge with **no neighbour at
all** is stored as `-1` (`ConnectFaces`). `-1/4` is `0`, so `n==-1` was never true
and a mesh-boundary edge was misread as "face 0 is my neighbour". No rim edge was
collected, `isl->NumEdges` stayed 0, and no side faces were built.

Everywhere else in the file the field is decoded with `>>2`, which is arithmetic
and yields `-1`.

## The change

`>>2` in both places. That is the whole patch.

## What it actually affects — measured, and much narrower than expected

**`Adjacent[] == -1` means "no neighbour", so only a rim edge lying on a REAL MESH
BOUNDARY was misread.** A rim made of *interior* edges — between a selected face
and an unselected one — decodes correctly either way, because those entries hold a
genuine face index.

That is the difference between the two paths, and it decides everything:

| Selection | Rim | Before | After |
|---|---|---|---|
| all of an open mesh | boundary edges | **broken** — no sides | 1 cap + 4 sides |
| part of a closed mesh | interior edges | correct | unchanged |

**Every `Extrude` in the bundled documents is the second kind.** Measured rather
than assumed: the full `wz4gen sweep -v` report for `example.wz4` — 2,192 lines
including a position-and-topology checksum for all 1,097 evaluated meshes — is
**byte-identical before and after**. The other four documents contain no `Extrude`
at all.

So the compatibility cost is **zero**, and the fidelity argument that delayed this
patch for two rounds was about a trade-off that does not exist.

## What this corrects in the earlier record

Three claims made while planning 6.7, all wrong, all corrected by measurement:

- *"The side-face path has almost certainly never executed."* It executes, and it
  works: eight `Extrude` operators in `example.wz4` produce 62, 7 and 152-face
  results among others. Only the boundary-edge *branch of the rim test* had never
  been taken.
- *"`example.wz4`'s 14 `Extrude` operators will change geometry."* None of them
  changes.
- *"Expect more than a two-character fix."* It was two characters. The reasoning
  behind that warning — patch 10 found two defects in code that had never been
  compiled — was sound, and the conclusion still did not follow.

## Verification

Three cases in `tests/geo/ops_topo.wz4t`, all derivable and all exact:

| Case | Expected | Why |
|---|---|---|
| `p_extrude` | 5 quads | one open quad, 4-edge boundary rim: 1 cap + 4 sides. Rim stays at y=0, cap moves to y=Amount |
| `p_extrude_steps` | 9 quads | same rim at `Steps = 2`: 1 + 2*4. Sides scale with `Steps`, the cap does not, so a one-step case cannot tell whether `Steps` is read |
| `p_extrude_closed` | 10 quads | one face of a closed cube: 6 - 1 + 1 cap + 4 sides, and +x moves to 0.75. The interior-rim path, which worked before — included precisely so a future change to the decode cannot break it silently |

Plus the corpus: 1,386 mesh operators across five documents, 0 violations.

## Invariant

`git status` on `altona_wz4/` shows one file for this patch,
`wz4/wz4frlib/wz4_mesh.cpp`, which patches 10, 11, 12 and 13 also touch.

## Behaviour

`Extrude` now builds side faces when the selected island's rim includes an open
mesh boundary. Extruding a partial selection on a closed mesh is unchanged.
