/****************************************************************************/
/***                                                                      ***/
/***   mesh_tess — the tessellation sink Text3D and Path3D drive           ***/
/***                                                                      ***/
/****************************************************************************/
//
// Stage 6.6b. This is what replaces `GLUtesselator *` in wz4_mesh.cpp's 2D
// extrusion path, and it is deliberately shaped to make that a small change:
// the call sequence is the same one glu32 wanted, so the upstream diff is a type
// name and five call sites rather than a restructure.
//
//     gluNewTess()                    ->  wMeshTess (a local)
//     gluTessBeginPolygon(tess,mesh)  ->  tess.BeginPolygon(mesh)
//     gluTessBeginContour(tess)       ->  tess.BeginContour()
//     gluTessVertex(tess,coords,data) ->  inside tess.AddVertex(pos)
//     gluTessEndContour(tess)         ->  tess.EndContour()
//     gluTessEndPolygon(tess)         ->  tess.EndPolygon()
//
// THE FOUR GLU CALLBACKS DISAPPEAR. glu32 streamed triangles out through
// GLU_TESS_BEGIN/VERTEX/COMBINE/EDGE_FLAG callbacks, which is why the original
// has `tess3DBeginCB` and friends and why it ends up with a trailing empty face
// to remove. A sink that owns the mesh writes complete faces directly, so the
// begin/vertex bookkeeping and the RemTail that cleaned up after it both go.
//
// The COMBINE callback has no counterpart and needs none: glu32 called it when
// the sweep created a new vertex at an intersection, and this tessellator never
// creates vertices — it only emits triangles over the ones it was given. That is
// the same restriction as tess2d's, stated once more: self-intersecting input is
// out of scope. See geo/tess2d.hpp.
//
// AddVertex creates the Wz4Mesh vertex as well as registering the point, exactly
// as tess3DAddPoint did, so the Bézier subdivision helpers keep working
// unchanged — they read `mesh->Vertices.GetTail().Pos` for the segment start.

#ifndef FILE_WZ4PORT_GEO_MESH_TESS_HPP
#define FILE_WZ4PORT_GEO_MESH_TESS_HPP

#include "tess2d.hpp"
#include "wz4frlib/wz4_mesh.hpp"

/****************************************************************************/

class wMeshTess
{
public:
  wMeshTess();

  void BeginPolygon(Wz4Mesh *mesh);
  void BeginContour();

  // Appends a vertex to the mesh AND to the current contour. The mesh vertex
  // gets the -Z normal the 2D path expects; Finish2DExtrusionOp overwrites it
  // for the extruded sides.
  void AddVertex(const sVector31 &pos);

  void EndContour();

  // Tessellates and appends triangles to the mesh. Returns the triangle count,
  // or -1 on failure — which the caller must report: a silently untessellated
  // glyph is an invisible character.
  sInt EndPolygon();

  const sChar *GetError() const { return Error; }

private:
  wTess2D Tess;
  Wz4Mesh *Mesh;
  sArray<sInt> Indices;
  const sChar *Error;
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_GEO_MESH_TESS_HPP
