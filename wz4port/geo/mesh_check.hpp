/****************************************************************************/
/***                                                                      ***/
/***   mesh_check — structural facts and invariants for a Wz4Mesh          ***/
/***                                                                      ***/
/****************************************************************************/
//
// Stage 6.3. One implementation, used by two suites that do different jobs:
//
//   Suite A  tests/mesh_cases.cpp — one hand-written case per operator, with
//            numbers chosen so the answer is derivable by hand
//   Suite B  `wz4gen sweep` over the five bundled documents — 39 of the 45
//            operators, with parameter values chosen by the original authors
//
// Suite B can only check invariants, because a demo graph's correct output is
// unknown. Suite A checks values. Neither replaces the other, and the invariants
// below are the part they share.
//
// WHAT IS NOT HERE, AND WHY
//
// Euler characteristic and manifoldness. The phase plan originally listed both,
// and they are the wrong tool at this level: DeleteFace, SplitAlongPlane,
// Splitter, Chunks and Extrude-on-an-open-mesh all legitimately produce
// non-manifold or open meshes, so a global invariant would fire constantly on
// correct output. They belong as per-case assertions on the specific generators
// that actually promise closedness — Cube, Sphere, Torus, Cylinder — and that is
// where wMeshIsClosed below is meant to be used.

#ifndef FILE_WZ4PORT_GEO_MESH_CHECK_HPP
#define FILE_WZ4PORT_GEO_MESH_CHECK_HPP

#include "wz4frlib/wz4_mesh.hpp"

/****************************************************************************/

// Everything a case or a sweep wants to say about a mesh without looking at it.
struct wMeshFacts
{
  sInt Verts;
  sInt Faces;
  sInt Tris;
  sInt Quads;
  sInt OtherArity;          // anything but 3 or 4 — always a defect
  sInt Clusters;
  sInt Degenerate;          // upstream's IsDegenerateFace
  sInt BadVertexIndex;      // face corner outside 0..Verts-1
  sInt BadClusterIndex;     // Face::Cluster outside 0..Clusters-1
  sInt NonFinite;           // vertex position with an inf or a nan in it
  sInt UnusedVerts;         // referenced by no face; not an error, but telling

  sVector31 Lo,Hi;
  sU64 Checksum;            // over positions AND face indices — see the .cpp

  wMeshFacts();

  // The sum of everything that is unambiguously wrong. Zero for a valid mesh,
  // including a legitimately empty one.
  sInt Violations() const;
};

// Measures and returns. Prints nothing.
wMeshFacts wMeshMeasure(Wz4Mesh *mesh);

// Prints the facts as two lines, then one line per violation. Returns
// facts.Violations() so a caller can `if(wMeshReport(...)) fail;`.
sInt wMeshReport(const sChar *label,const wMeshFacts &facts);

// Closed-manifold test, for the per-case use described above: every half-edge
// has exactly one partner running the other way. Also reports whether the
// winding is consistent, which is the half people forget — a mesh with one face
// wound backwards is still edge-paired.
//
// Pairs by vertex POSITION, not by index. A Wz4Mesh splits a position wherever
// the normal or the UVs differ, so every closed primitive in the library has
// unwelded seams and an index-based test calls all of them open. See the .cpp
// for the measurement that established this.
//
// O(n^2) in both vertices and half-edges. For per-case meshes only — never call
// it from the sweep, where meshes reach 786,432 faces.
//
// Returns 1 only if closed AND consistently wound.
sBool wMeshIsClosed(Wz4Mesh *mesh,sInt *openedges=0,sInt *reversed=0);

/****************************************************************************/

#endif  // FILE_WZ4PORT_GEO_MESH_CHECK_HPP
