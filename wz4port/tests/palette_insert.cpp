/****************************************************************************/
/***                                                                      ***/
/***   Stage 5.4 gate — every operator the palette offers can be inserted  ***/
/***                                                                      ***/
/****************************************************************************/
//
// The gate is "every registered operator is reachable from the palette and can
// be inserted". Reachability is a property of the palette's filtering, which is
// three flag tests; insertability is a property of the runtime, and it is the
// half that can actually break — a class whose SetDefaults crashes, or whose
// Init needs something the editor does not provide, would look fine in a list
// and fail on click.
//
// So this walks the whole registry and inserts every offerable class for real,
// through wInsertOp — the SAME function the palette calls. That is why insert
// and delete were factored into editor/edit_ops.cpp: testing a copy of them
// would test the copy.
//
// It also pins the two behaviours that make the palette usable rather than
// merely present:
//
//   * repeated insertion at an advancing cursor builds a CONNECTED stack, which
//     is the whole reason the original advances the cursor by one row
//   * an insert into occupied cells is refused and displaces nothing

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "base/system.hpp"
#include "docedit.hpp"

/****************************************************************************/

// All four modules, in the order wz4gen uses: GenBitmap derives from BitmapBase
// and Wz4Mesh from MeshBase, so basic must register its types first.
// The same four modules the editor registers, so "every offerable class inserts"
// means every class the palette actually offers. The mesh modules joined in stage
// 6.5: this test is class-driven, so extending it to 45 more operators was
// registration and one assertion, which is the point of having written it that
// way.
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

// The palette's own filter, kept in step with editor/palette.cpp. If these ever
// disagree the test is offering a different set from the UI, so the numbers are
// printed rather than only compared.
static sBool Insertable(wClass *cl)
{
  if(!cl || !cl->OutputType)     return 0;
  if(cl->Flags & wCF_HIDE)       return 0;
  if(cl->Flags & wCF_CONVERSION) return 0;
  if(!cl->Extract.IsEmpty())     return 0;
  return 1;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"palette_insert: stage 5.4 gate\n\n");

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];
  page->Ops.Clear();

  sPrintF(L"%d types, %d classes registered\n\n",
    Doc->Types.GetCount(),Doc->Classes.GetCount());

  // --- every offerable class inserts ---------------------------------------

  sInt offered = 0, inserted = 0, hidden = 0;

  for(sInt i=0;i<Doc->Classes.GetCount();i++)
  {
    wClass *cl = Doc->Classes[i];
    if(!Insertable(cl))
    {
      // Named rather than counted. An exclusion list that is only a number
      // cannot be audited, and "the palette is missing an operator" is exactly
      // the complaint this test exists to answer.
      sPrintF(L"  skip  %-24s%s%s%s\n",cl->Name,
        (cl->Flags & wCF_HIDE)       ? L" hide"       : L"",
        (cl->Flags & wCF_CONVERSION) ? L" conversion" : L"",
        cl->Extract.IsEmpty()        ? L""            : L" extract");
      hidden++;
      continue;
    }
    offered++;

    // A fresh page each time, so one class cannot be blocked by the last one's
    // block and so a crash names the class that caused it.
    page->Ops.Clear();

    wStackOp *op = wInsertOp(page,cl,0,0);
    if(!op)
    {
      sPrintF(L"  FAIL  %s could not be inserted into an empty page\n",cl->Name);
      Failures++;
      continue;
    }
    if(op->Class!=cl)
    {
      sPrintF(L"  FAIL  %s inserted as the wrong class\n",cl->Name);
      Failures++;
      continue;
    }

    // Connect has to survive it too: a class that inserts but breaks the
    // connection pass is no more usable than one that will not insert.
    Doc->Connect();
    inserted++;
  }

  sPrintF(L"  %d offered, %d inserted, %d filtered out\n",
    offered,inserted,hidden);
  Check(offered>0,L"the palette offers something at all");
  Check(inserted==offered,L"every offered class inserted and connected");

  // 34 GenBitmap operators is the number phase 4 established, and 45 Wz4Mesh the
  // number 6.1 did. Counted per output type rather than in total, because a
  // single total would still pass if one module lost operators while another
  // gained them.
  sInt genbitmap = 0,wz4mesh = 0;
  for(sInt i=0;i<Doc->Classes.GetCount();i++)
  {
    wClass *cl = Doc->Classes[i];
    if(!cl->OutputType)
      continue;
    if(sCmpString(cl->OutputType->Symbol,L"GenBitmap")==0)
      genbitmap++;
    if(sCmpString(cl->OutputType->Symbol,L"Wz4Mesh")==0)
      wz4mesh++;
  }
  sPrintF(L"  %d classes output GenBitmap, %d output Wz4Mesh\n",
    genbitmap,wz4mesh);
  Check(genbitmap==34,L"all 34 GenBitmap operators are registered");
  // 46 since phase 7 — 45 upstream plus our own AnimateBones.
  Check(wz4mesh==46,L"all 45 upstream Wz4Mesh operators, plus AnimateBones");

  // --- repeated insertion builds a connected stack -------------------------

  sPrint(L"\nrepeated insertion at an advancing cursor builds a stack\n");
  {
    page->Ops.Clear();
    wClass *nop = Doc->FindClass(L"Nop",0);
    Check(nop!=0,L"Nop is registered");

    if(nop)
    {
      // What the editor does: insert, then advance the cursor by the block's
      // height. Four times.
      sInt y = 0;
      wStackOp *made[4];
      for(sInt i=0;i<4;i++)
      {
        made[i] = wInsertOp(page,nop,0,y);
        y += made[i] ? made[i]->SizeY : 1;
      }
      Doc->Connect();

      Check(made[0] && made[1] && made[2] && made[3],
        L"four inserts at an advancing cursor all succeeded");

      if(made[0] && made[3])
      {
        Check(made[0]->Inputs.GetCount()==0,L"the first has no input");
        Check(made[1]->Inputs.GetCount()==1 && made[1]->Inputs[0]==made[0],
          L"the second reads the first");
        Check(made[2]->Inputs.GetCount()==1 && made[2]->Inputs[0]==made[1],
          L"the third reads the second");
        Check(made[3]->Inputs.GetCount()==1 && made[3]->Inputs[0]==made[2],
          L"and the fourth reads the third — a connected chain");
      }

      // The point of advancing by height rather than by one: a taller block
      // still leaves the next insert touching it.
      Check(made[1] && made[1]->PosY==1,L"the second landed one row down");
    }
  }

  // --- an insert into occupied cells is refused ----------------------------

  sPrint(L"\nan insert with no room is refused, and displaces nothing\n");
  {
    page->Ops.Clear();
    wClass *nop = Doc->FindClass(L"Nop",0);
    if(nop)
    {
      wStackOp *first = wInsertOp(page,nop,4,4);
      Check(first!=0,L"the first insert succeeds");

      const sInt count = page->Ops.GetCount();
      wStackOp *clash = wInsertOp(page,nop,5,4);   // overlaps by two cells
      Check(clash==0,L"an overlapping insert returns nothing");
      Check(page->Ops.GetCount()==count,L"and adds nothing to the page");
      if(first)
        Check(first->PosX==4 && first->PosY==4,
          L"and the block already there has not moved");

      // Exactly clear of it is fine, which is what proves the refusal above was
      // about the overlap and not about the position.
      wStackOp *ok = wInsertOp(page,nop,7,4);
      Check(ok!=0,L"an insert just clear of it succeeds");
    }
  }

  // --- delete ---------------------------------------------------------------

  sPrint(L"\ndelete removes the selection and rewires what is left\n");
  {
    page->Ops.Clear();
    wClass *nop = Doc->FindClass(L"Nop",0);
    if(nop)
    {
      wStackOp *a = wInsertOp(page,nop,0,0);
      wStackOp *b = wInsertOp(page,nop,0,1);
      wStackOp *c = wInsertOp(page,nop,0,2);
      Doc->Connect();
      Check(a && b && c && c->Inputs.GetCount()==1 && c->Inputs[0]==b,
        L"a chain of three, c reading b");

      // Select b and nothing else. wInsertOp leaves only the block it just made
      // selected, which after three inserts is c — so the selection has to be
      // set explicitly here. The first version of this test called
      // wDeleteSelection immediately and asserted it removed nothing: it removed
      // c, and the two assertions after it were then reading a deleted operator.
      for(sInt i=0;i<page->Ops.GetCount();i++)
        page->Ops[i]->Select = (page->Ops[i]==b) ? 1 : 0;

      Check(wDeleteSelection(page)==1,L"deleting b removes exactly one operator");
      Doc->Connect();

      Check(page->Ops.GetCount()==2,L"a and c remain");
      Check(c->Inputs.GetCount()==0,
        L"and c has no input, because the row above it is now empty");
      Check(a->Inputs.GetCount()==0,L"a is untouched");
    }
  }

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
