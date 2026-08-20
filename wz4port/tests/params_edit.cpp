/****************************************************************************/
/***                                                                      ***/
/***   Stage 5.5 gate — metadata-driven edits reach the generator          ***/
/***                                                                      ***/
/****************************************************************************/
//
// The gate is "every parameter of every texture operator is editable, and edits
// reach the generator". The panel is generated from metadata, so "editable"
// reduces to one question: does the offset the metadata gives for a parameter
// actually address that parameter's storage?
//
// A wrong offset is the failure mode that matters, and it is invisible in a
// screenshot — the panel would show a plausible number, edit the wrong word, and
// the picture would change in some other way. So this writes through the
// metadata exactly as editor/params.cpp does and then RENDERS, checking the
// bitmap that comes out.
//
// Three things are established:
//
//   1. A packed flags word written through its metadata shift and mask changes
//      the thing it names — Flat's Size is two controls in one word, so setting
//      it to 32,32 must produce a 32 x 32 bitmap and nothing else.
//   2. A value edit changes the render at all.
//   3. Doc->Change invalidates DOWNSTREAM operators, not only the edited one,
//      which is what makes editing a Perlin update the Blur that reads it.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz3_bitmap_code.hpp"
#include "base/system.hpp"
#include "meta.hpp"
#include "docedit.hpp"

#ifndef WZ4_META_DIR_STR
#define WZ4_META_DIR_STR L"meta"
#endif

/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(wz3_bitmap,0);
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

// The metadata parameter for a symbol, so the test addresses storage the same
// way the panel does rather than by hardcoding an offset.
static const wMetaParam *FindParam(const wMetaClass *mc,const sChar *symbol)
{
  for(sInt i=0;i<mc->Params.GetCount();i++)
    if(sCmpString(mc->Params[i]->Symbol,symbol)==0)
      return mc->Params[i];
  return 0;
}

// Writes one control of a packed choice word, preserving its neighbours. This is
// the same masked read-modify-write DrawFlagsWidget does, and it is the operation
// most likely to be wrong.
static void SetChoice(wOp *op,const wMetaParam *p,sInt widget,sInt value)
{
  if(!p || widget>=p->Widgets.GetCount())
    return;
  const wMetaWidget *w = p->Widgets[widget];
  sU32 *word = op->EditU() + p->Offset;
  *word = (*word & ~sU32(w->Mask)) | ((sU32(value) << w->Shift) & sU32(w->Mask));
}

struct wShot
{
  sInt X,Y;
  sU64 Sum;
  wShot() { X = Y = 0; Sum = 0; }
};

// Renders an operator and summarises the bitmap. Same FNV the render command
// uses, over all 16 bits of every pixel.
static wShot Render(wOp *op)
{
  wShot s;
  wObject *obj = Doc->CalcOp(op);
  if(!obj)
    return s;

  GenBitmap *bm = (GenBitmap *) obj;
  s.X = bm->XSize;
  s.Y = bm->YSize;
  for(sInt i=0;i<bm->Size;i++)
    s.Sum = s.Sum*1099511628211ULL ^ bm->Data[i];
  return s;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"params_edit: stage 5.5 gate\n\n");

  wMetaLibrary meta;
  if(!meta.LoadDirectory(WZ4_META_DIR_STR))
  {
    sPrint(L"params_edit: no metadata\n");
    sSetErrorCode();
    return;
  }

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];
  page->Ops.Clear();

  wClass *flatcl = Doc->FindClass(L"Flat",L"GenBitmap");
  wClass *blurcl = Doc->FindClass(L"Blur",L"GenBitmap");
  Check(flatcl!=0,L"Flat is registered");
  Check(blurcl!=0,L"Blur is registered");
  if(!flatcl || !blurcl)
  {
    sPrintF(L"\n%d failure(s)\n",Failures);
    sSetErrorCode();
    return;
  }

  const wMetaClass *flatmc = meta.Find(L"GenBitmap",L"Flat");
  Check(flatmc!=0,L"and Flat has metadata");
  if(!flatmc)
  {
    sPrintF(L"\n%d failure(s)\n",Failures);
    sSetErrorCode();
    return;
  }

  // Flat at the top, Blur directly beneath it, so Blur reads Flat.
  wOp *flat = wInsertOp(page,flatcl,0,0);
  wOp *blur = wInsertOp(page,blurcl,0,1);
  Doc->Connect();
  Check(blur && blur->Inputs.GetCount()==1 && blur->Inputs[0]==flat,
    L"Blur reads Flat");

  // --- 1. a packed flags word, written through its metadata ----------------

  sPrint(L"\na packed choice word written through metadata addresses the right field\n");

  const wMetaParam *size = FindParam(flatmc,L"Size");
  Check(size!=0,L"Flat has a Size parameter in the metadata");
  if(size)
  {
    Check(size->Widgets.GetCount()==2,
      L"and it declares two controls in one word");

    // Choice value 6 is the label "64" and 5 is "32" — the labels are powers of
    // two and the VALUE is the exponent, which is exactly the trap that caught
    // the .wz4t reader in phase 4 (architecture.md A36). Setting the two
    // controls independently proves the shift and mask are right.
    SetChoice(flat,size,0,5);        // x = 32
    SetChoice(flat,size,1,5);        // y = 32
    Doc->Change(flat);

    wShot s = Render(flat);
    Check(s.X==32 && s.Y==32,L"setting both controls to \"32\" renders 32 x 32");

    // Now change ONE control. If the mask were wrong the other would move too.
    SetChoice(flat,size,0,6);        // x = 64, y untouched
    Doc->Change(flat);
    s = Render(flat);
    Check(s.X==64 && s.Y==32,
      L"changing only the first control gives 64 x 32 — the mask is right");

    SetChoice(flat,size,0,7);
    SetChoice(flat,size,1,7);
    Doc->Change(flat);
    s = Render(flat);
    Check(s.X==128 && s.Y==128,L"and \"128\" gives 128 x 128");
  }

  // --- 2. a value edit changes the render ----------------------------------

  sPrint(L"\na value edit changes what the generator produces\n");

  const wMetaParam *color = FindParam(flatmc,L"Color");
  Check(color!=0,L"Flat has a Color parameter");
  if(color)
  {
    sU32 *word = flat->EditU() + color->Offset;

    *word = 0xff000000;
    Doc->Change(flat);
    const wShot black = Render(flat);

    *word = 0xffffffff;
    Doc->Change(flat);
    const wShot white = Render(flat);

    Check(black.Sum!=white.Sum,
      L"black and white produce different bitmaps");
    Check(black.X==white.X && black.X==128,
      L"and the size is unaffected by a colour edit");
  }

  // --- 3. the change propagates downstream ---------------------------------

  sPrint(L"\nDoc->Change invalidates downstream operators too\n");
  {
    // This is the property that makes the editor feel connected rather than
    // per-operator: editing a source must update everything that reads it. If
    // Change only dirtied the edited operator, Blur would keep serving a cached
    // bitmap of the OLD colour and the panel would appear to do nothing.
    sU32 *word = flat->EditU() + color->Offset;

    *word = 0xff000000;
    Doc->Change(flat);
    const wShot before = Render(blur);

    *word = 0xffff0000;
    Doc->Change(flat);
    const wShot after = Render(blur);

    Check(before.Sum!=after.Sum,
      L"editing Flat changes what Blur renders");
    Check(after.X==128,L"and Blur still has the right size");
  }

  // --- every texture operator's parameters are addressable -----------------

  sPrint(L"\nevery parameter of every texture operator has usable metadata\n");
  {
    // "Editable" for a generated panel means: the metadata names a kind the
    // panel can draw, and the offset is inside the operator's storage. A
    // parameter failing either would be a blank row or a stray write.
    sInt classes = 0,params = 0,badoffset = 0,badkind = 0;

    for(sInt i=0;i<Doc->Classes.GetCount();i++)
    {
      wClass *cl = Doc->Classes[i];
      if(!cl->OutputType || sCmpString(cl->OutputType->Symbol,L"GenBitmap")!=0)
        continue;
      const wMetaClass *mc = meta.Find(L"GenBitmap",cl->Name);
      if(!mc)
        continue;
      classes++;

      for(sInt k=0;k<mc->Params.GetCount();k++)
      {
        const wMetaParam *p = mc->Params[k];
        params++;

        // The kinds editor/params.cpp draws. Anything else renders as a
        // "no editor" line, which is honest but is not editable.
        const sBool known =
          p->Kind==L"float"   || p->Kind==L"int"     || p->Kind==L"color"  ||
          p->Kind==L"flags"   || p->Kind==L"radio"   || p->Kind==L"string" ||
          p->Kind==L"filein"  || p->Kind==L"fileout" || p->Kind==L"char"   ||
          p->Kind==L"link"    || p->Kind==L"action"  || p->Kind==L"strobe" ||
          p->Kind==L"group"   || p->Kind==L"label";
        if(!known)
        {
          sPrintF(L"    %s.%s: no editor for kind \"%s\"\n",
            cl->Name,p->Symbol,p->Kind);
          badkind++;
        }

        if(p->Space==wMS_WORDS)
        {
          const sInt end = p->Offset + sMax(1,p->Words);
          if(p->Offset<0 || end>cl->ParaWords)
          {
            sPrintF(L"    %s.%s: words %d..%d outside 0..%d\n",
              cl->Name,p->Symbol,p->Offset,end,cl->ParaWords);
            badoffset++;
          }
        }
        else if(p->Space==wMS_STRINGS)
        {
          if(p->Offset<0 || p->Offset>=cl->ParaStrings)
          {
            sPrintF(L"    %s.%s: string %d outside 0..%d\n",
              cl->Name,p->Symbol,p->Offset,cl->ParaStrings);
            badoffset++;
          }
        }
      }
    }

    sPrintF(L"  %d classes, %d parameters\n",classes,params);
    Check(classes==34,L"all 34 texture operators have metadata");
    Check(badoffset==0,L"every parameter's offset is inside its storage");
    Check(badkind==0,L"and every parameter's kind has an editor");
  }

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
