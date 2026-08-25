/****************************************************************************/
/***                                                                      ***/
/***   Stage 6.2 gate — the OBJ a mesh writes is a mesh that reads back    ***/
/***                                                                      ***/
/****************************************************************************/
//
// The plan's gate is "a Cube round-trips to a valid OBJ that opens in a mesh
// viewer". There is no viewer in a test, and "the file is non-empty" is exactly
// the kind of proxy this project has been caught by before (architecture.md
// A39, A43). So the oracle is upstream's own OBJ *parser*: write the mesh, read
// it back, and require the geometry to survive.
//
// That is a real check rather than a self-consistent one, because LoadOBJ and
// SaveOBJ share no code — the reader is a full sScanner grammar that validates
// every index against the counts it has seen, and rejects a face with more than
// four corners. A file it accepts is a file that says what it appears to say.
//
// WHAT IS AND IS NOT EXPECTED TO SURVIVE
//
// Face count and arity, and the bounding box. Not the vertex count: the writer
// emits one vertex per face corner and the reader re-merges them, so the number
// on the far side is a property of MergeVertices, not of the file. It is
// reported, and asserted only to be in the range the topology allows.
//
// Positions are compared with an epsilon, not bit-exactly, and deliberately:
// sScanner::ScanFloat loses one ULP (architecture.md A34), so requiring equality
// here would be asserting something known to be false.
//
// The negative case at the end is the one that makes the rest mean anything. A
// parser that accepted everything would pass every assertion above.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "wz4frlib/wz4_mesh.hpp"
#include "base/system.hpp"
#include "docedit.hpp"

/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(wz3_bitmap,0);
    sREGOPS(wz4_anim,0);
    sREGOPS(wz4_mesh,0);
    sREGOPS(animate,0);
  }
}

/****************************************************************************/

static sInt Failures = 0;

static void Check(sBool cond,const sChar *what)
{
  if(cond)
  {
    sPrintF(L"  ok    %s\n",what);
  }
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

struct wMeshFacts
{
  sInt Verts,Faces,Tris,Quads,Degenerate;
  sVector31 Lo,Hi;

  wMeshFacts() { Verts=Faces=Tris=Quads=Degenerate=0; Lo.Init(0,0,0); Hi.Init(0,0,0); }
};

static wMeshFacts Measure(Wz4Mesh *mesh)
{
  wMeshFacts f;
  f.Verts = mesh->Vertices.GetCount();
  f.Faces = mesh->Faces.GetCount();

  for(sInt i=0;i<f.Faces;i++)
  {
    if(mesh->Faces[i].Count==3) f.Tris++;
    if(mesh->Faces[i].Count==4) f.Quads++;
    if(mesh->IsDegenerateFace(i)) f.Degenerate++;
  }

  if(f.Verts>0)
  {
    f.Lo = f.Hi = mesh->Vertices[0].Pos;
    for(sInt i=1;i<f.Verts;i++)
    {
      const sVector31 &p = mesh->Vertices[i].Pos;
      f.Lo.x = sMin(f.Lo.x,p.x); f.Hi.x = sMax(f.Hi.x,p.x);
      f.Lo.y = sMin(f.Lo.y,p.y); f.Hi.y = sMax(f.Hi.y,p.y);
      f.Lo.z = sMin(f.Lo.z,p.z); f.Hi.z = sMax(f.Hi.z,p.z);
    }
  }
  return f;
}

static void Report(const sChar *label,const wMeshFacts &f)
{
  sPrintF(L"  %-8s %d vertices, %d faces (%d tri, %d quad), %d degenerate\n",
    label,f.Verts,f.Faces,f.Tris,f.Quads,f.Degenerate);
  sPrintF(L"           bounds %f..%f  %f..%f  %f..%f\n",
    f.Lo.x,f.Hi.x,f.Lo.y,f.Hi.y,f.Lo.z,f.Hi.z);
}

static sBool SameBounds(const wMeshFacts &a,const wMeshFacts &b,sF32 eps)
{
  return sFAbs(a.Lo.x-b.Lo.x)<eps && sFAbs(a.Hi.x-b.Hi.x)<eps
      && sFAbs(a.Lo.y-b.Lo.y)<eps && sFAbs(a.Hi.y-b.Hi.y)<eps
      && sFAbs(a.Lo.z-b.Lo.z)<eps && sFAbs(a.Hi.z-b.Hi.z)<eps;
}

// Evaluates one store and round-trips it. Returns 0 only for a setup failure —
// assertion failures are counted in Failures and reported by the caller's exit.
static sBool RoundTrip(wPage *page,const sChar *classname,const sChar *objpath,
  sInt expectfaces)
{
  wClass *cl = Doc->FindClass(classname,L"Wz4Mesh");
  if(!cl)
  {
    sPrintF(L"  FAIL  no class %s\n",classname);
    Failures++;
    return 0;
  }

  page->Ops.Clear();
  wOp *op = wInsertOp(page,cl,0,0);
  Doc->Connect();

  wObject *obj = Doc->CalcOp(op);
  if(!obj)
  {
    sPrintF(L"  FAIL  %s did not evaluate\n",classname);
    Failures++;
    return 0;
  }

  Wz4Mesh *before = (Wz4Mesh *)obj;
  const wMeshFacts a = Measure(before);
  Report(L"in",a);
  if(expectfaces>=0)
    Check(a.Faces==expectfaces,L"the generator produced the expected face count");
  else
    Check(a.Faces>0,L"the generator produced faces");

  Check(before->SaveOBJ(objpath)!=0,L"SaveOBJ reports success");

  // SaveOBJ returns 1 without checking that anything landed, so the file is
  // read for itself before the parser is given a chance to be blamed for it.
  sDInt size = 0;
  sU8 *bytes = sLoadFile(objpath,size);
  Check(bytes!=0 && size>0,L"and the file exists and is not empty");
  delete[] bytes;

  Wz4Mesh after;
  const sBool loaded = after.LoadOBJ(objpath);
  Check(loaded!=0,L"LoadOBJ accepts it — every index is in range");

  if(loaded)
  {
    const wMeshFacts b = Measure(&after);
    Report(L"out",b);

    Check(b.Faces==a.Faces,L"the face count survives the round trip");
    Check(b.Tris==a.Tris && b.Quads==a.Quads,L"and so does every face's arity");
    Check(SameBounds(a,b,1e-4f),L"and the bounding box, to within a float ULP");
    Check(b.Degenerate==0,L"and nothing degenerated in the process");

    // The writer emits one vertex per corner; the reader merges. So the honest
    // bound is "no more than the corners written, no fewer than the corners a
    // fully-merged mesh needs" — which for a quad mesh is at least a quarter.
    Check(b.Verts>0 && b.Verts<=a.Faces*4,
      L"and the vertex count is within what merging can produce");
  }

  obj->Release();
  return 1;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"mesh_obj: stage 6.2 gate\n\n");

  // Positional 0, not 1: wz4gen's first positional is its COMMAND, which is why
  // its files are at index 1. There is no command here. Getting this wrong once
  // already cost a silent pass — the test wrote into the build root, and every
  // assertion still held because they all used the same wrong path.
  const sChar *dir = sGetShellParameter(0,0);
  if(!dir)
  {
    sPrint(L"usage: mesh_obj <output-directory>\n");
    sSetErrorCode();
    return;
  }

  sString<1024> cubepath,spherepath,badpath;
  sSPrintF(cubepath,L"%s/roundtrip_cube.obj",dir);
  sSPrintF(spherepath,L"%s/roundtrip_sphere.obj",dir);
  sSPrintF(badpath,L"%s/roundtrip_bad.obj",dir);

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];

  sPrint(L"a Cube round-trips through OBJ\n");
  RoundTrip(page,L"Cube",cubepath,6);

  // A second shape, because a cube is the case most likely to work by accident:
  // eight distinct positions, all axis-aligned, every coordinate the same
  // magnitude. A sphere has a real vertex spread and, at the poles, faces that
  // upstream's own generator may make triangular.
  sPrint(L"\nand so does a Sphere\n");
  RoundTrip(page,L"Sphere",spherepath,-1);

  // --- the negative case ----------------------------------------------------

  sPrint(L"\nand the reader is not simply permissive\n");
  {
    // A face index one past the end of the vertex list. This is the exact
    // failure a broken writer produces — off-by-one on the 1-based indices — so
    // it is the one worth proving the reader catches.
    const sChar *bad =
      L"v 0 0 0\n"
      L"v 1 0 0\n"
      L"v 0 1 0\n"
      L"f 1 2 4\n";

    Check(sSaveTextAnsi(badpath,bad)!=0,L"wrote a deliberately broken OBJ");

    Wz4Mesh junk;
    Check(junk.LoadOBJ(badpath)==0,
      L"and LoadOBJ REJECTS an out-of-range face index");
  }

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
