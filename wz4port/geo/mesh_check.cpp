/****************************************************************************/
/***                                                                      ***/
/***   mesh_check — structural facts and invariants for a Wz4Mesh          ***/
/***                                                                      ***/
/****************************************************************************/

#include "mesh_check.hpp"
#include "base/system.hpp"

/****************************************************************************/

wMeshFacts::wMeshFacts()
{
  Verts = Faces = Tris = Quads = OtherArity = Clusters = 0;
  Degenerate = BadVertexIndex = BadClusterIndex = NonFinite = UnusedVerts = 0;
  Lo.Init(0,0,0);
  Hi.Init(0,0,0);
  Checksum = 0;
}

sInt wMeshFacts::Violations() const
{
  // UnusedVerts is deliberately absent: several operators leave unreferenced
  // vertices behind (Select writes a flag without touching topology; Add merges
  // without compacting), and upstream's own MergeVertices exists precisely
  // because that is a normal intermediate state. It is reported, not counted.
  return OtherArity + Degenerate + BadVertexIndex + BadClusterIndex + NonFinite;
}

/****************************************************************************/

wMeshFacts wMeshMeasure(Wz4Mesh *mesh)
{
  wMeshFacts f;
  if(!mesh)
    return f;

  f.Verts = mesh->Vertices.GetCount();
  f.Faces = mesh->Faces.GetCount();
  f.Clusters = mesh->Clusters.GetCount();

  // Which vertices any face refers to. Sized to the vertex count, so an
  // out-of-range index is detected before it is used as a subscript here.
  sArray<sU8> used;
  if(f.Verts>0)
  {
    used.AddMany(f.Verts);
    for(sInt i=0;i<f.Verts;i++)
      used[i] = 0;
  }

  for(sInt i=0;i<f.Faces;i++)
  {
    const Wz4MeshFace &face = mesh->Faces[i];

    switch(face.Count)
    {
    case 3:  f.Tris++;  break;
    case 4:  f.Quads++; break;
    default: f.OtherArity++; break;
    }

    // Wz4MeshFace::Vertex is sInt[4]. An arity outside 3..4 means something has
    // already written past it, so do not then read Count corners.
    const sInt corners = sClamp<sInt>(face.Count,0,4);
    for(sInt j=0;j<corners;j++)
    {
      const sInt v = face.Vertex[j];
      if(v<0 || v>=f.Verts)
        f.BadVertexIndex++;
      else
        used[v] = 1;
    }

    if(f.Clusters>0)
    {
      if(face.Cluster<0 || face.Cluster>=f.Clusters)
        f.BadClusterIndex++;
    }
    else if(face.Cluster!=0)
    {
      // No clusters at all but a face claiming one. Counted, because the
      // renderer would dereference it.
      f.BadClusterIndex++;
    }

    if(mesh->IsDegenerateFace(i))
      f.Degenerate++;
  }

  if(f.Verts>0)
  {
    // FNV-1a over positions AND face indices. Positions alone are not enough:
    // an operator that rewires topology without moving a vertex — Invert,
    // Triangulate, Dual — would produce an identical checksum on a mesh it had
    // completely rebuilt.
    f.Checksum = 14695981039346656037ULL;

    sBool first = 1;
    for(sInt i=0;i<f.Verts;i++)
    {
      const sVector31 &p = mesh->Vertices[i].Pos;

      if(sIsInfNan(p.x) || sIsInfNan(p.y) || sIsInfNan(p.z))
      {
        f.NonFinite++;
        continue;                 // and keep it out of the bounds
      }

      if(first)
      {
        f.Lo = f.Hi = p;
        first = 0;
      }
      else
      {
        f.Lo.x = sMin(f.Lo.x,p.x); f.Hi.x = sMax(f.Hi.x,p.x);
        f.Lo.y = sMin(f.Lo.y,p.y); f.Hi.y = sMax(f.Hi.y,p.y);
        f.Lo.z = sMin(f.Lo.z,p.z); f.Hi.z = sMax(f.Hi.z,p.z);
      }

      f.Checksum = (f.Checksum ^ sU64(sF32U32(p.x))) * 1099511628211ULL;
      f.Checksum = (f.Checksum ^ sU64(sF32U32(p.y))) * 1099511628211ULL;
      f.Checksum = (f.Checksum ^ sU64(sF32U32(p.z))) * 1099511628211ULL;

      if(!used[i])
        f.UnusedVerts++;
    }

    for(sInt i=0;i<f.Faces;i++)
    {
      const Wz4MeshFace &face = mesh->Faces[i];
      f.Checksum = (f.Checksum ^ sU64(sU32(face.Count))) * 1099511628211ULL;
      const sInt corners = sClamp<sInt>(face.Count,0,4);
      for(sInt j=0;j<corners;j++)
        f.Checksum = (f.Checksum ^ sU64(sU32(face.Vertex[j]))) * 1099511628211ULL;
    }
  }

  return f;
}

/****************************************************************************/

static void PrintVec(const sChar *label,const sVector31 &v)
{
  sPrintF(L"%s %f %f %f",label,v.x,v.y,v.z);
}

sInt wMeshReport(const sChar *label,const wMeshFacts &f)
{
  sPrintF(L"  %-24s %6d v %6d f (%d tri, %d quad",
    label,f.Verts,f.Faces,f.Tris,f.Quads);
  if(f.OtherArity)
    sPrintF(L", %d BAD",f.OtherArity);
  sPrintF(L"), %d cl",f.Clusters);
  if(f.UnusedVerts)
    sPrintF(L", %d unused v",f.UnusedVerts);
  sPrint(L"\n");

  if(f.Verts>0)
  {
    sPrint(L"                           ");
    PrintVec(L"",f.Lo);
    sPrint(L"  ..  ");
    PrintVec(L"",f.Hi);
    sPrintF(L"   %08x%08x\n",sU32(f.Checksum>>32),sU32(f.Checksum));
  }

  // One line per kind of violation, naming the count. A sweep over 54 stores
  // has to say WHICH invariant broke, or the output is unusable.
  if(f.OtherArity)
    sPrintF(L"    VIOLATION  %d face(s) with arity outside 3..4\n",f.OtherArity);
  if(f.BadVertexIndex)
    sPrintF(L"    VIOLATION  %d face corner(s) outside 0..%d\n",
      f.BadVertexIndex,f.Verts-1);
  if(f.BadClusterIndex)
    sPrintF(L"    VIOLATION  %d face(s) with a cluster outside 0..%d\n",
      f.BadClusterIndex,f.Clusters-1);
  if(f.NonFinite)
    sPrintF(L"    VIOLATION  %d vertex position(s) with an inf or nan\n",f.NonFinite);
  if(f.Degenerate)
    sPrintF(L"    VIOLATION  %d degenerate face(s)\n",f.Degenerate);

  return f.Violations();
}

/****************************************************************************/

// Half-edge pairing. A half-edge is (from,to) for consecutive corners of a face;
// a closed, consistently-wound mesh has exactly one (b,a) for every (a,b).
//
// PAIRED BY POSITION, NOT BY VERTEX INDEX, and that is the whole difficulty.
//
// A Wz4Mesh is a render-oriented mesh: a vertex carries a normal and two UV
// pairs as well as a position, so wherever those differ the position is
// duplicated. Cube builds six grids and transforms them into place, and
// MergeVertices welds only within each patch — the six patch borders stay as
// distinct indices because their normals disagree. Measured on Cube(2,3,4):
// index-pairing leaves exactly 72 half-edges unpaired, which is exactly the sum
// of the six patch perimeters, 2*(2+3) + 2*(2+4) + 2*(3+4).
//
// So an index-based test calls every closed primitive in the library open. The
// first version of this function did, and the counts it produced are what
// identified the cause.
//
// Positions are compared with an epsilon rather than exactly: the shared corners
// of two patches are computed through different matrices, so they agree to
// within rounding and not to the bit.
//
// O(n^2) twice over, and that is on purpose: this runs on the handful of small
// per-case meshes that actually promise closedness — never on the sweep, where
// meshes reach 786,432 faces — and a spatial hash would be more code to get
// wrong than the thing it speeds up.

sBool wMeshIsClosed(Wz4Mesh *mesh,sInt *openedges,sInt *reversed)
{
  if(openedges) *openedges = 0;
  if(reversed)  *reversed = 0;
  if(!mesh || mesh->Faces.GetCount()==0)
    return 0;

  const sInt vc = mesh->Vertices.GetCount();
  const sF32 eps = 1e-5f;

  // canon[i] = the lowest index whose position is within eps of vertex i.
  sArray<sInt> canon;
  canon.AddMany(vc);
  for(sInt i=0;i<vc;i++)
  {
    canon[i] = i;
    const sVector31 &p = mesh->Vertices[i].Pos;
    for(sInt k=0;k<i;k++)
    {
      if(canon[k]!=k)
        continue;                     // only compare against representatives
      const sVector31 &q = mesh->Vertices[k].Pos;
      if(sFAbs(p.x-q.x)<eps && sFAbs(p.y-q.y)<eps && sFAbs(p.z-q.z)<eps)
      {
        canon[i] = k;
        break;
      }
    }
  }

  sArray<sInt> from,to;

  for(sInt i=0;i<mesh->Faces.GetCount();i++)
  {
    const Wz4MeshFace &f = mesh->Faces[i];
    const sInt n = sClamp<sInt>(f.Count,0,4);
    for(sInt j=0;j<n;j++)
    {
      const sInt a = f.Vertex[j];
      const sInt b = f.Vertex[(j+1)%n];
      if(a<0 || a>=vc || b<0 || b>=vc)
        return 0;                     // the battery reports this properly
      from.AddTail(canon[a]);
      to.AddTail(canon[b]);
    }
  }

  const sInt count = from.GetCount();
  sInt open = 0,rev = 0;

  for(sInt i=0;i<count;i++)
  {
    sInt opposite = 0,same = 0;
    for(sInt k=0;k<count;k++)
    {
      if(k==i)
        continue;
      if(from[k]==to[i] && to[k]==from[i])
        opposite++;
      // The same half-edge in the same direction on two faces means one of them
      // is wound backwards relative to the other. Edge-pairing alone would not
      // notice, which is why this is counted separately.
      else if(from[k]==from[i] && to[k]==to[i])
        same++;
    }
    if(opposite!=1)
      open++;
    if(same)
      rev++;
  }

  if(openedges) *openedges = open;
  if(reversed)  *reversed = rev;

  return open==0 && rev==0;
}

/****************************************************************************/
