/****************************************************************************/
/***                                                                      ***/
/***   Stage 6.1 gate — the geometry library registers and evaluates       ***/
/***                                                                      ***/
/****************************************************************************/
//
// The gate is "wz4geo compiles and links headlessly, 45 of 47 operators
// register, and a Cube evaluates to a mesh with the expected counts".
//
// Compiling and linking is proved by this file existing as a target. What it
// adds is the two things a build cannot show:
//
//   1. The REGISTRY is complete and honest. 45 Wz4Mesh operators present, and
//      the two that patch 11 drops absent by name — not "roughly the right
//      number", because a miscount either way is the interesting failure. A
//      silently missing operator would look identical to a correct build until
//      someone tried to use it.
//   2. A generator actually RUNS. Registration is a table; evaluation is the
//      mesh engine. Cube goes through MakeGrid six times, Add, Transform and
//      CalcNormalAndTangents, so it exercises rather more than its own body.
//
// The mesh assertions are structural, and chosen to be checkable without a
// golden: face count, face arity, and the bounding box. The bounding box is the
// one that would catch a Transform or a vertex-layout error — Cube's default
// Scale is 1 and its matrix centres the cube on the origin, so the extent is
// exactly -0.5..0.5 in all three axes and nothing about that is approximate.
// Golden meshes are stage 6.3's job, after the outputs have been looked at.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "wz4frlib/wz4_mesh.hpp"
#include "base/system.hpp"
#include "docedit.hpp"

/****************************************************************************/

// Order matters: Wz4Mesh derives from basic's MeshBase and two mesh operators
// take a bitmap input, so basic and wz3_bitmap must register their types first.
// sREGOPS runs types on pass 0 and operators on pass 1.

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(wz3_bitmap,0);
    sREGOPS(wz4_anim,0);
    sREGOPS(wz4_mesh,0);
    sREGOPS(animate,0);
    sREGOPS(material,0);
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

static sBool HasClass(const sChar *name)
{
  return Doc->FindClass(name,L"Wz4Mesh")!=0;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"mesh_register: stage 6.1 gate\n\n");

  Doc = new wDocument;

  // --- the type system ------------------------------------------------------

  sPrint(L"the mesh types are registered\n");

  wType *meshtype = Doc->FindType(L"Wz4Mesh");
  wType *basetype = Doc->FindType(L"MeshBase");
  Check(meshtype!=0,L"Wz4Mesh is a registered type");
  Check(basetype!=0,L"MeshBase is a registered type");
  Check(meshtype && meshtype->Parent==basetype,
    L"and Wz4Mesh derives from MeshBase across module boundaries");
  Check(Doc->FindType(L"Wz4Skeleton")!=0,L"Wz4Skeleton is registered too");

  if(!meshtype)
  {
    sPrintF(L"\n%d failure(s)\n",Failures);
    sSetErrorCode();
    return;
  }

  // --- the operator count ---------------------------------------------------

  sPrint(L"\n45 of the 47 mesh operators register\n");

  sInt meshops = 0;
  for(sInt i=0;i<Doc->Classes.GetCount();i++)
  {
    wClass *cl = Doc->Classes[i];
    if(cl->OutputType==meshtype)
      meshops++;
  }
  // 47 since phase 9.2: 45 upstream that always registered, plus AnimateBones
  // from wz4port's own geo/animate_ops.ops, plus SetMaterial — which is upstream
  // but stayed unregistered until material_ops.ops supplied the Wz4Mtrl type its
  // input names.
  sPrintF(L"  %d operators output Wz4Mesh\n",meshops);
  Check(meshops==47,
    L"45 upstream, plus our AnimateBones, plus SetMaterial output Wz4Mesh");
  Check(HasClass(L"AnimateBones"),
    L"and AnimateBones is one of them — a wz4port module registered alongside "
    L"the upstream ones");

  // Named, not just counted. ConvertFromChaosMesh is still a patch 11 drop, and
  // the reason is its input type rather than its body.
  Check(!HasClass(L"ConvertFromChaosMesh"),
    L"ConvertFromChaosMesh is absent (input type ChaosMesh)");

  // SetMaterial was the other drop until phase 9.2. It came back when
  // wz4port/geo/material_ops.ops registered the Wz4Mtrl type its input needs —
  // the omission was never about the operator body, which compiled against the
  // headless material all along.
  Check(HasClass(L"SetMaterial"),
    L"SetMaterial is PRESENT again (phase 9.2 registered its Wz4Mtrl input)");

  // And a spread of what must be present: a generator, a filter, a
  // multi-input operator, and the two that take a bitmap.
  Check(HasClass(L"Cube"),L"Cube is present");
  Check(HasClass(L"Sphere"),L"Sphere is present");
  Check(HasClass(L"Transform"),L"Transform is present");
  Check(HasClass(L"Add"),L"Add is present");
  Check(HasClass(L"Select"),L"Select is present (bitmap input)");
  Check(HasClass(L"Displace"),L"Displace is present (bitmap input)");

  // These two needed no stub, unlike GenBitmap.Text in 4.1: the Windows-only
  // tesselator path already had an #else arm that calls sFatal, so they register
  // and link and will refuse at runtime until 6.6. Asserted because "they are
  // present" is the part that makes a graph containing one still loadable.
  Check(HasClass(L"Text3D"),L"Text3D is present (refuses at runtime until 6.6)");
  Check(HasClass(L"Path3D"),L"Path3D is present (likewise)");

  // Import links against the OBJ and LWO readers; XSI comes from
  // compat/mesh_xsi_stub.cpp and refuses. Export links against SaveOBJ, which
  // stage 6.2 wires into wz4gen.
  Check(HasClass(L"Import"),L"Import is present (OBJ and LWO)");
  Check(HasClass(L"Export"),L"Export is present");

  // --- a Cube evaluates -----------------------------------------------------

  sPrint(L"\na Cube evaluates to a mesh\n");

  wClass *cubecl = Doc->FindClass(L"Cube",L"Wz4Mesh");
  Check(cubecl!=0,L"Cube has a class");
  if(cubecl)
  {
    wPage *page = Doc->Pages[0];
    page->Ops.Clear();

    wOp *cube = wInsertOp(page,cubecl,0,0);
    Doc->Connect();

    wObject *obj = Doc->CalcOp(cube);
    Check(obj!=0,L"and it evaluates to an object");
    if(!obj && cube->CalcErrorString)
      sPrintF(L"        %s\n",cube->CalcErrorString);

    if(obj)
    {
      Check(obj->IsType(meshtype),L"which is a Wz4Mesh");

      Wz4Mesh *mesh = (Wz4Mesh *) obj;
      sPrintF(L"  %d vertices, %d faces, %d clusters\n",
        mesh->Vertices.GetCount(),mesh->Faces.GetCount(),
        mesh->Clusters.GetCount());

      Check(mesh->Faces.GetCount()==6,L"with 6 faces at the default tesselation");

      sInt quads = 0;
      for(sInt i=0;i<mesh->Faces.GetCount();i++)
        if(mesh->Faces[i].Count==4)
          quads++;
      Check(quads==mesh->Faces.GetCount(),L"and every face is a quad");

      Check(mesh->Vertices.GetCount()>=8,L"and at least 8 vertices");
      Check(mesh->Clusters.GetCount()==1,L"and exactly one cluster");

      // A generated mesh's cluster carries NO material — AddDefaultCluster
      // (wz4_mesh.cpp:622) leaves Mtrl null and the renderer substitutes the
      // type's DefaultMtrl at draw time. So the thing worth asserting is that
      // DefaultMtrl exists: it is the headless stand-in, and its presence is
      // what proves the type block's Init ran and SimpleMtrl is constructible.
      if(mesh->Clusters.GetCount()>=1)
        Check(mesh->Clusters[0]->Mtrl==0,
          L"whose material is null, as upstream leaves it");
      Check(Wz4MeshType->DefaultMtrl!=0,
        L"and the type's DefaultMtrl is the headless stand-in, constructed");

      // Cube's matrix divides by the tesselation and offsets by -Scale/2, so at
      // Scale 1 the mesh occupies exactly -0.5..0.5. An off-by-one in the vertex
      // indices, a missed Transform or a wrong vertex stride all show up here.
      if(mesh->Vertices.GetCount()>0)
      {
        sVector31 lo = mesh->Vertices[0].Pos;
        sVector31 hi = lo;
        for(sInt i=1;i<mesh->Vertices.GetCount();i++)
        {
          const sVector31 &p = mesh->Vertices[i].Pos;
          lo.x = sMin(lo.x,p.x); hi.x = sMax(hi.x,p.x);
          lo.y = sMin(lo.y,p.y); hi.y = sMax(hi.y,p.y);
          lo.z = sMin(lo.z,p.z); hi.z = sMax(hi.z,p.z);
        }
        sPrintF(L"  bounds %f..%f  %f..%f  %f..%f\n",
          lo.x,hi.x,lo.y,hi.y,lo.z,hi.z);

        const sF32 eps = 1e-5f;
        Check(sFAbs(lo.x+0.5f)<eps && sFAbs(hi.x-0.5f)<eps &&
              sFAbs(lo.y+0.5f)<eps && sFAbs(hi.y-0.5f)<eps &&
              sFAbs(lo.z+0.5f)<eps && sFAbs(hi.z-0.5f)<eps,
          L"and it spans exactly -0.5..0.5 on every axis");
      }
    }
  }

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
