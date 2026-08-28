/****************************************************************************/
/***                                                                      ***/
/***   glTF 2.0 export for Wz4Mesh — wz4port, phase 8                      ***/
/***                                                                      ***/
/****************************************************************************/
//
// Writes what a generator produces: positions, normals, tangents, both UV sets,
// indices, and the cluster structure as separate primitives. That is strictly
// more than SaveOBJ carries — OBJ drops the second UV set, the tangents and the
// clusters, and rounds to five decimals.
//
// NO JSON DEPENDENCY, AND THAT IS THE POINT. The JSON goes out through
// wz4t/json_write.hpp and the tests read it back through wz4t/json.hpp, which are
// two separate implementations sharing no code. That is the same relationship
// that makes tests/mesh_obj.cpp a real test of SaveOBJ rather than a
// self-consistent one — a vendored library used for both directions would have
// forfeited it. See docs/10-phase-gltf.md for the full argument, including why
// fx/gltf was considered and rejected.
//
// NOT WRITTEN, ON PURPOSE
//
//   skins and animations   nothing in this build produces a rig with hierarchy
//                          or motion (architecture.md A63). The layout below
//                          leaves room for both.
//   materials and textures the material system is out of scope, and Wz4Mtrl is
//                          only forward-declared in the headless build, so there
//                          is nothing to read. One default PBR material is
//                          emitted so the file shades sanely.
//   COLOR_0                Color0/Color1 do not exist: WZ4MESH_LOWMEM is
//                          unconditionally 1 (wz4_mesh.hpp:11).
//   Draco, quantisation, KHR_* extensions

#ifndef FILE_WZ4PORT_GEO_GLTF_WRITE_HPP
#define FILE_WZ4PORT_GEO_GLTF_WRITE_HPP

#include "base/types2.hpp"
#include "wz4frlib/wz4_mesh.hpp"

/****************************************************************************/

struct wGltfStats
{
  sInt Verts;               // every vertex is written, used or not — see below
  sInt Tris;                // after quads are fanned
  sInt Prims;               // one per non-empty cluster
  sInt UnusedVerts;         // referenced by no face; reported, never removed
  sInt Degenerate;          // legal in glTF, so a warning rather than a refusal
  sInt Materials;           // one per primitive
  sInt Textures;            // distinct bitmaps, deduplicated by pointer
  sDInt JsonBytes;
  sDInt BinBytes;

  wGltfStats();
};

/****************************************************************************/

// One texture, encoded, for a caller that has to write it out.
//
// Only .gltf produces these: glTF cannot put image bytes inside a JSON file, so
// each texture becomes a PNG beside it. A .glb has somewhere to put them — a
// bufferView into its BIN chunk — so it emits none and this array comes back
// empty. That asymmetry is the format's, not ours.
// A raw owned pointer rather than an sArray member, deliberately. Altona's
// sArray hands out uninitialised memory from AddMany and relocates it wholesale
// in Grow, so an element type that owns heap storage through a member container
// would be copied bitwise and then double-freed. sString is a plain buffer and
// is safe; sArray inside an sArray is not.
//
// The caller owns Png and releases it with wFreeGltfSidecars.
struct wGltfSidecar
{
  sString<128> Name;        // file name only, as the uri in the JSON
  sU8 *Png;
  sInt Len;
};

void wFreeGltfSidecars(sArray<wGltfSidecar> &sidecars);

// Formats into memory, so the formatter is testable without touching a disk —
// the same split as wWriteWz4t / wWriteWz4tFile (wz4t_write.cpp:435).
//
// `binuri` names the sidecar buffer for .gltf output. Pass 0 for .glb, where the
// buffer is a chunk of the same file and carries no uri.
//
// THE MESH IS NOT MUTATED. MergeVertices() would drop the unused vertices, but
// callers hand us an object borrowed from the document's cache, and mutating one
// of those broke the next frame once already (architecture.md A58). Unused
// vertices are exported and counted, not silently removed.
//
// Returns 0 and prints why if the mesh cannot be represented.
sBool wWriteGltf(sTextBuffer &json,sArray<sU8> &bin,
  sArray<wGltfSidecar> &sidecars,Wz4Mesh *mesh,
  const sChar *binuri,wGltfStats *stats=0);

// Writes .gltf plus its .bin sidecar, or a single .glb, chosen by the path's
// extension. Creates neither directory — callers make it, because a writer that
// silently fails to open is how the 4.2 PNG runner passed on a directory that
// did not exist (architecture.md A39).
sBool wWriteGltfFile(const sChar *path,Wz4Mesh *mesh,wGltfStats *stats=0);

/****************************************************************************/

#endif  // FILE_WZ4PORT_GEO_GLTF_WRITE_HPP
