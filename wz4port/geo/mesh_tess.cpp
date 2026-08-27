/****************************************************************************/
/***                                                                      ***/
/***   mesh_tess — the tessellation sink Text3D and Path3D drive           ***/
/***                                                                      ***/
/****************************************************************************/

#include "mesh_tess.hpp"
#include "base/system.hpp"

/****************************************************************************/

wMeshTess::wMeshTess()
{
  Mesh = 0;
  Error = L"";
}

void wMeshTess::BeginPolygon(Wz4Mesh *mesh)
{
  Mesh = mesh;
  Error = L"";
  Indices.Clear();
  Tess.Begin();
}

void wMeshTess::BeginContour()
{
  Tess.BeginContour();
}

void wMeshTess::AddVertex(const sVector31 &pos)
{
  if(!Mesh)
    return;

  Wz4MeshVertex *vert = Mesh->Vertices.AddMany(1);
  sClear(*vert);
  vert->Pos = pos;
  vert->Normal.z = -1.0f;

  // The tag is the mesh vertex index, so the triangles that come back out are
  // already indices into the mesh — no mapping step, and no chance of one being
  // wrong.
  Tess.AddVertex(pos.x,pos.y,Mesh->Vertices.GetCount()-1);
}

void wMeshTess::EndContour()
{
  Tess.EndContour();
}

sInt wMeshTess::EndPolygon()
{
  if(!Mesh)
    return 0;

  Indices.Clear();
  const sInt tris = Tess.End(Indices);
  if(tris<0)
  {
    Error = Tess.GetError();
    return -1;
  }

  for(sInt i=0;i+2<Indices.GetCount();i+=3)
  {
    // REVERSED, and this is load-bearing.
    //
    // tess2d normalises everything to counter-clockwise rings — a good internal
    // invariant, and the wrong thing to hand to this consumer. Upstream's
    // Finish2DExtrusionOp opens with a triangulation cleanup pass
    // (wz4_mesh.cpp:6182):
    //
    //     sVector30 n = (v2-v0) % (v1-v0);
    //     if(n.z < 1e-6f)   // try to flip it
    //
    // Note the operand order: for a COUNTER-CLOCKWISE triangle that cross
    // product points at -z, so the condition holds for every well-formed
    // triangle we produce and the pass edge-flips the lot. An edge flip swaps a
    // shared edge for the opposite diagonal, which is how a correct annulus
    // becomes triangles spanning non-adjacent contour points, overlapping, with
    // half of them inverted — z-fighting in every counter and hole.
    //
    // The pass is written for GLU's convention: clockwise caps, which is also
    // what AddVertex's Normal.z = -1 above already assumes, since a clockwise
    // triangle in xy has a geometric normal of -z. So the seam adapts to the
    // consumer rather than the consumer being patched.
    //
    // It went unnoticed because a cap with no interior edges cannot be flipped:
    // a lone triangle has no adjacent face, so every hole-free case — g_path3d,
    // a triangular prism — came out perfect while every glyph counter was
    // scrambled.
    Wz4MeshFace *face = Mesh->Faces.AddMany(1);
    face->Init(3);
    face->Vertex[0] = Indices[i];
    face->Vertex[1] = Indices[i+2];
    face->Vertex[2] = Indices[i+1];
  }

  return tris;
}

/****************************************************************************/
