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
    Wz4MeshFace *face = Mesh->Faces.AddMany(1);
    face->Init(3);
    face->Vertex[0] = Indices[i];
    face->Vertex[1] = Indices[i+1];
    face->Vertex[2] = Indices[i+2];
  }

  return tris;
}

/****************************************************************************/
