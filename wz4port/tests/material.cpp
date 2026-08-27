/****************************************************************************/
/***                                                                      ***/
/***   material — the phase 9.1 gate                                       ***/
/***                                                                      ***/
/****************************************************************************/
//
// One claim: a texture reaches a material and is still there afterwards.
//
// That is the whole of stage 9.1, and it is not something the surrounding
// machinery would notice on its own. `wz4gen sweep` walks MESH operators and
// says so plainly when a document yields none — a material document is invisible
// to it. `mesh_cases` measures meshes. So a material that registered, evaluated,
// and quietly dropped its bitmap would pass every existing test in the suite.
//
// The assertions are therefore about the bitmap POINTER surviving, and about the
// negative case: a material with no texture input must have no texture, not a
// stale one and not a crash. Without that second half the first proves only that
// something non-null is present.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz3_bitmap_code.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "wz4frlib/wz4_mesh.hpp"
#include "base/system.hpp"

#include "wz4_mtrl_headless.hpp"
#include "meta.hpp"
#include "wz4t.hpp"

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
    sPrintF(L"  ok    %s\n",what);
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

// Evaluates one store and returns it as a SimpleMtrl, or 0 with the failure
// already counted.
static SimpleMtrl *Eval(const sChar *store)
{
  wOp *op = Doc->FindStore(store);
  if(!op)
  {
    sPrintF(L"  FAIL  no store called \"%s\"\n",store);
    Failures++;
    return 0;
  }

  wObject *obj = Doc->CalcOp(op);
  if(!obj)
  {
    sPrintF(L"  FAIL  \"%s\" did not evaluate\n",store);
    Failures++;
    return 0;
  }

  // IsType against the registered type, not a C++ cast: a wrong registration
  // would otherwise be reinterpreted rather than reported.
  wType *t = Doc->FindType(L"SimpleMtrl");
  if(!t || !obj->IsType(t))
  {
    sPrintF(L"  FAIL  \"%s\" is not a SimpleMtrl\n",store);
    Failures++;
    return 0;
  }
  return (SimpleMtrl *)obj;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"material: phase 9.1 gate\n\n");

  const sChar *doc = sGetShellParameter(0,0);
  const sChar *metadir = sGetShellParameter(0,1);
  if(!doc || !metadir)
  {
    sPrint(L"usage: material <doc.wz4t> <metadir>\n");
    sSetErrorCode();
    return;
  }

  wMetaLibrary meta;
  if(!meta.LoadDirectory(metadir))
  {
    sPrintF(L"material: %s\n",meta.GetError());
    sSetErrorCode();
    return;
  }

  Doc = new wDocument;
  if(!wReadWz4t(doc,meta))
  {
    sPrint(L"material: the case did not parse\n");
    sSetErrorCode();
    delete Doc;
    return;
  }
  Doc->Connect();

  // The registration itself. Both types have to exist, and the derived one has
  // to actually derive — SetMaterial's `Wz4Mtrl` input resolves through that
  // relationship in 9.2, so a flat pair of unrelated types would pass a naive
  // "does the type exist" check and fail then.
  sPrint(L"[types]\n");
  wType *base = Doc->FindType(L"Wz4Mtrl");
  wType *simple = Doc->FindType(L"SimpleMtrl");
  Check(base!=0,L"the Wz4Mtrl type is registered");
  Check(simple!=0,L"and SimpleMtrl is too");
  if(simple)
    Check(simple->Parent==base,L"and SimpleMtrl derives from Wz4Mtrl");

  sPrint(L"\n[textured]\n");
  {
    SimpleMtrl *m = Eval(L"m_textured");
    if(m)
    {
      Check(m->GetBitmap(0)!=0,L"the texture reached stage 0 and is still there");
      Check(m->GetBitmap(1)==0 && m->GetBitmap(2)==0,
        L"and stages 1 and 2 are empty, not aliases of it");
      Check(m->Colour==0xffffffff,L"the colour parameter arrived");
      Check(m->Wrap==1,L"and \"repeat\" means wrap");

      // The bitmap has to be a usable GenBitmap, not merely non-null: the
      // exporter will read its pixels, and a pointer to the wrong type would
      // only show up there.
      wType *bt = Doc->FindType(L"GenBitmap");
      Check(bt && m->GetBitmap(0)->IsType(bt),L"and it is a GenBitmap");
      if(bt && m->GetBitmap(0)->IsType(bt))
      {
        GenBitmap *bm = (GenBitmap *)m->GetBitmap(0);
        Check(bm->XSize>0 && bm->YSize>0 && bm->Data!=0,
          L"with real pixels behind it");
        sPrintF(L"        %d x %d\n",bm->XSize,bm->YSize);
      }
    }
  }

  sPrint(L"\n[untextured]\n");
  {
    // The negative half. The input is optional so that this case exists, and
    // without it "the texture is present" proves nothing about whether the
    // operator ever looks at its input.
    SimpleMtrl *m = Eval(L"m_flat");
    if(m)
    {
      Check(m->GetBitmap(0)==0,L"no input means NO texture, not a stale one");
      Check(m->Colour==0xff3060c0,L"the colour still arrived");
      Check(m->Wrap==0,L"and \"clamp\" means clamp");
    }
  }

  delete Doc;
  Doc = 0;

  sPrintF(L"\nmaterial: %d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
