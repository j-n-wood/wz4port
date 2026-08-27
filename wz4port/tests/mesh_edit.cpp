/****************************************************************************/
/***                                                                      ***/
/***   Stage 6.5 — the phase gate, headless                                ***/
/***                                                                      ***/
/****************************************************************************/
//
// The phase gate is "open a document, build a mesh graph, edit parameters, see
// the mesh update in 3D, export to OBJ". The 3D half is wz4ed_mesh and
// wz4ed_mesh_wire, which need a window. This is everything else, and everything
// else is where the assertions can be exact.
//
// It builds the graph THROUGH THE EDITOR'S OWN FUNCTIONS — wInsertOp, the
// metadata offsets, Doc->Change, Doc->CalcOp — rather than through a .wz4t file.
// That is the difference from mesh_cases: there the graph is authored and the
// question is whether each operator computes the right answer; here the graph is
// BUILT, and the question is whether the editing path works at all.
//
// Four things it pins, in the order a user would hit them:
//
//   1. inserting two operators one below the other CONNECTS them, because
//      connection comes from geometry and nothing else
//   2. an edit through the metadata offset changes what the generator produces
//   3. an edit UPSTREAM invalidates the operator downstream of it — the property
//      that makes the editor feel connected rather than per-operator
//   4. Export writes an OBJ that LoadOBJ accepts
//
// Undo is not re-tested here: undo_page already covers every edit kind, and the
// mechanism is page snapshots, which cannot care what an operator computes.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "base/system.hpp"
#include "mesh_check.hpp"
#include "meta.hpp"
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

// Addressed through the metadata, exactly as editor/params.cpp does it, so a
// wrong offset fails here the same way it would in the panel. Hardcoding the
// word index would test nothing about the path the editor actually uses.
static const wMetaParam *FindParam(const wMetaClass *mc,const sChar *symbol)
{
  if(!mc)
    return 0;
  for(sInt i=0;i<mc->Params.GetCount();i++)
    if(sCmpString(mc->Params[i]->Symbol,symbol)==0)
      return mc->Params[i];
  return 0;
}

static void SetFloats(wOp *op,const wMetaParam *p,sF32 a,sF32 b,sF32 c)
{
  if(!p)
    return;
  sF32 *f = (sF32 *)(op->EditU() + p->Offset);
  f[0] = a; f[1] = b; f[2] = c;
}

static void SetInts(wOp *op,const wMetaParam *p,sInt a,sInt b,sInt c)
{
  if(!p)
    return;
  sInt *v = (sInt *)(op->EditU() + p->Offset);
  v[0] = a; v[1] = b; v[2] = c;
}

static void SetChoice(wOp *op,const wMetaParam *p,sInt widget,sInt value)
{
  if(!p || widget>=p->Widgets.GetCount())
    return;
  const wMetaWidget *w = p->Widgets[widget];
  sU32 *word = op->EditU() + p->Offset;
  *word = (*word & ~sU32(w->Mask)) | ((sU32(value) << w->Shift) & sU32(w->Mask));
}

static void SetString(wOp *op,const wMetaParam *p,const sChar *text)
{
  if(!p || p->Offset>=op->EditStringCount || !op->EditString[p->Offset])
    return;
  sTextBuffer *tb = op->EditString[p->Offset];
  tb->Clear();
  tb->Print(text);
}

// Evaluates and measures. Returns a zeroed struct if nothing came out, which the
// callers check for rather than reading stale numbers.
static wMeshFacts Eval(wOp *op,wType *meshtype)
{
  wMeshFacts f;
  wObject *obj = Doc->CalcOp(op);
  if(!obj)
  {
    sPrint(L"        evaluation produced nothing\n");
    if(op->CalcErrorString)
      sPrintF(L"        %s\n",op->CalcErrorString);
    return f;
  }
  if(!obj->IsType(meshtype))
  {
    sPrint(L"        result is not a Wz4Mesh\n");
    return f;
  }
  return wMeshMeasure((Wz4Mesh *)obj);
}

/****************************************************************************/

void sMain()
{
  sPrint(L"mesh_edit: stage 6.5 phase gate, headless half\n\n");

  const sChar *metadir = sGetShellParameter(0,0);
  const sChar *outdir = sGetShellParameter(0,1);
  if(!metadir || !outdir)
  {
    sPrint(L"usage: mesh_edit <meta-directory> <output-directory>\n");
    sSetErrorCode();
    return;
  }

  wMetaLibrary meta;
  if(!meta.LoadDirectory(metadir))
  {
    sPrintF(L"mesh_edit: no metadata in <%s>\n",metadir);
    sSetErrorCode();
    return;
  }

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];
  page->Ops.Clear();

  wType *meshtype = Doc->FindType(L"Wz4Mesh");
  wClass *cubecl = Doc->FindClass(L"Cube",L"Wz4Mesh");
  wClass *xfcl = Doc->FindClass(L"Transform",L"Wz4Mesh");
  wClass *expcl = Doc->FindClass(L"Export",L"Wz4Mesh");
  Check(meshtype && cubecl && xfcl && expcl,
    L"Wz4Mesh, Cube, Transform and Export are all registered");
  if(!meshtype || !cubecl || !xfcl || !expcl)
  {
    sPrintF(L"\n%d failure(s)\n",Failures);
    sSetErrorCode();
    return;
  }

  const wMetaClass *cubemc = meta.Find(L"Wz4Mesh",L"Cube");
  const wMetaClass *xfmc = meta.Find(L"Wz4Mesh",L"Transform");
  const wMetaClass *expmc = meta.Find(L"Wz4Mesh",L"Export");
  Check(cubemc && xfmc && expmc,L"and all three have metadata");

  // --- 1. building the graph connects it ------------------------------------

  sPrint(L"\ninserting two operators one below the other connects them\n");

  wOp *cube = wInsertOp(page,cubecl,0,0);
  wOp *xf = wInsertOp(page,xfcl,0,1);
  Check(cube!=0 && xf!=0,L"both inserted");
  Doc->Connect();
  Check(xf && xf->Inputs.GetCount()==1 && xf->Inputs[0]==cube,
    L"and Transform reads Cube, from the geometry alone");

  // --- 2. an edit changes what the generator produces -----------------------

  sPrint(L"\nan edit through the metadata reaches the generator\n");

  const wMetaParam *xfscale = FindParam(xfmc,L"Scale");
  Check(xfscale!=0,L"Transform has a Scale parameter in the metadata");

  {
    // Default Transform is identity, so the chain must still be the cube.
    const wMeshFacts before = Eval(xf,meshtype);
    Check(before.Faces==6,L"the default chain is the cube, 6 quads");
    Check(sFAbs(before.Lo.x+0.5f)<1e-4f && sFAbs(before.Hi.x-0.5f)<1e-4f,
      L"spanning -0.5..0.5");

    SetFloats(xf,xfscale,2.0f,3.0f,4.0f);
    Doc->Change(xf);

    const wMeshFacts after = Eval(xf,meshtype);
    Check(after.Faces==6,L"after scaling it is still 6 quads");
    Check(sFAbs(after.Lo.x+1.0f)<1e-4f && sFAbs(after.Hi.x-1.0f)<1e-4f &&
          sFAbs(after.Lo.y+1.5f)<1e-4f && sFAbs(after.Hi.y-1.5f)<1e-4f &&
          sFAbs(after.Lo.z+2.0f)<1e-4f && sFAbs(after.Hi.z-2.0f)<1e-4f,
      L"and spans -1..1, -1.5..1.5, -2..2 — the scale reached the vertices");

    // Non-uniform on purpose: a scale applied to all three axes from x's value
    // would pass a uniform test and fail this one.
    Check(after.Checksum!=before.Checksum,
      L"and the mesh actually changed, bit for bit");
  }

  // --- 3. an upstream edit invalidates downstream ----------------------------

  sPrint(L"\nediting the SOURCE updates the operator below it\n");
  {
    const wMetaParam *tess = FindParam(cubemc,L"Tesselate");
    Check(tess!=0,L"Cube has a Tesselate parameter");

    const wMeshFacts before = Eval(xf,meshtype);

    SetInts(cube,tess,2,3,4);
    Doc->Change(cube);            // the CUBE, not the Transform

    const wMeshFacts after = Eval(xf,meshtype);

    // 2*(2*3 + 2*4 + 3*4) = 52, the same derivation ops_gen.wz4t uses. Asserted
    // on the DOWNSTREAM operator: if Change only dirtied the edited op, the
    // Transform would keep serving a cached 6-face mesh and the editor would
    // appear to do nothing.
    Check(before.Faces==6 && after.Faces==52,
      L"Cube(2,3,4) gives 52 quads, seen through the Transform below it");
    Check(sFAbs(after.Hi.x-1.0f)<1e-4f,
      L"and the Transform's own scale is still applied on top");
  }

  // --- 4. Export writes an OBJ that reads back ------------------------------

  sPrint(L"\nExport writes an OBJ, and LoadOBJ accepts it\n");
  {
    wOp *exp = wInsertOp(page,expcl,0,2);
    Check(exp!=0,L"Export inserted below the Transform");
    Doc->Connect();
    Check(exp && exp->Inputs.GetCount()==1 && exp->Inputs[0]==xf,
      L"and reads it");

    sString<1024> path;
    sSPrintF(path,L"%s/mesh_edit.obj",outdir);

    SetString(exp,FindParam(expmc,L"Filename"),path);
    // FormatType wz4|obj — obj is 1. Set through the metadata widget, so a
    // changed choice list fails here rather than writing the wrong format.
    SetChoice(exp,FindParam(expmc,L"FormatType"),0,1);
    Doc->Change(exp);

    // Export writes as a side effect of evaluating, which is worth stating: the
    // editor exports by selecting the operator, not by a menu command.
    const wMeshFacts f = Eval(exp,meshtype);
    Check(f.Faces==52,L"Export passes the mesh through unchanged");

    sDInt size = 0;
    sU8 *bytes = sLoadFile(path,size);
    Check(bytes!=0 && size>0,L"and the file exists and is not empty");
    delete[] bytes;

    Wz4Mesh back;
    const sBool loaded = back.LoadOBJ(path);
    Check(loaded!=0,L"LoadOBJ accepts what Export wrote");
    if(loaded)
    {
      const wMeshFacts b = wMeshMeasure(&back);
      sPrintF(L"        read back %d vertices, %d faces\n",b.Verts,b.Faces);
      Check(b.Faces==f.Faces,L"with the same face count");
      Check(b.Violations()==0,L"and no invariant violations");
    }
  }

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
