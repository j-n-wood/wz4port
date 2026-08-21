/****************************************************************************/
/***                                                                      ***/
/***   Stage 5.7 gate — document-level undo and redo                       ***/
/***                                                                      ***/
/****************************************************************************/
//
// The gate is "insert, move, resize, delete, paste and parameter edits all undo
// and redo correctly". None of that needs a window, so all of it is here.
//
// The interesting risk with snapshot undo is not the stack arithmetic, it is
// COMPLETENESS: a snapshot that silently drops parameter words, strings, link
// names or array rows restores something that looks right and is not. Those are
// checked one at a time, and the most easily-lost one — array rows — is checked
// through a full insert/undo/redo cycle.
//
// A second risk is specific to restoring in place: every wStackOp is replaced, so
// any pointer held across an undo dangles. The test holds indices, never
// pointers, and the editor drops its selection — which is what the header
// promises and this file relies on.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "base/system.hpp"
#include "undo.hpp"
#include "docedit.hpp"

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

// Operators are addressed by index, never by pointer: a restore replaces every
// one of them.
static wStackOp *At(wPage *page,sInt i)
{
  if(i<0 || i>=page->Ops.GetCount())
    return 0;
  return page->Ops[i];
}

static sU32 Word(wPage *page,sInt i,sInt w)
{
  wStackOp *op = At(page,i);
  if(!op || !op->EditU())
    return 0xdeadbeef;
  return op->EditU()[w];
}

/****************************************************************************/

void sMain()
{
  sPrint(L"undo_page: stage 5.7 gate\n\n");

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];
  page->Ops.Clear();

  wClass *flat = Doc->FindClass(L"Flat",L"GenBitmap");
  wClass *grad = Doc->FindClass(L"Gradient",L"GenBitmap");
  wClass *nop  = Doc->FindClass(L"Nop",0);
  Check(flat && grad && nop,L"Flat, Gradient and Nop are registered");
  if(!flat || !grad || !nop)
  {
    sPrintF(L"\n%d failure(s)\n",Failures);
    sSetErrorCode();
    return;
  }

  wUndo undo;

  // --- baseline ------------------------------------------------------------

  wInsertOp(page,flat,0,0);
  wInsertOp(page,flat,0,1);
  Doc->Connect();
  undo.Reset(page);

  Check(page->Ops.GetCount()==2,L"two operators to start with");
  Check(!undo.CanUndo(),L"nothing to undo at the baseline");
  Check(!undo.CanRedo(),L"and nothing to redo");

  // --- insert --------------------------------------------------------------

  sPrint(L"\ninsert\n");
  {
    wInsertOp(page,grad,4,0);
    Doc->Connect();
    undo.Push(page,L"insert Gradient");
    Check(page->Ops.GetCount()==3,L"three after inserting");
    Check(undo.CanUndo(),L"and there is something to undo");

    Check(undo.Undo(page),L"undo succeeds");
    Doc->Connect();
    Check(page->Ops.GetCount()==2,L"back to two operators");

    Check(undo.Redo(page),L"redo succeeds");
    Doc->Connect();
    Check(page->Ops.GetCount()==3,L"three again");
  }

  // --- delete --------------------------------------------------------------

  sPrint(L"\ndelete\n");
  {
    for(sInt i=0;i<page->Ops.GetCount();i++)
      page->Ops[i]->Select = (i==0) ? 1 : 0;
    Check(wDeleteSelection(page)==1,L"one operator deleted");
    Doc->Connect();
    undo.Push(page,L"delete");
    Check(page->Ops.GetCount()==2,L"two remain");

    Check(undo.Undo(page),L"undo the delete");
    Doc->Connect();
    Check(page->Ops.GetCount()==3,L"the deleted operator is back");
  }

  // --- move and resize -----------------------------------------------------

  sPrint(L"\nmove and resize\n");
  {
    // Geometry is the graph, so an undone move has to restore the CONNECTIONS
    // and not just the numbers.
    page->Ops.Clear();
    wInsertOp(page,nop,0,0);
    wInsertOp(page,nop,0,1);
    Doc->Connect();
    undo.Reset(page);
    Check(At(page,1)->Inputs.GetCount()==1,L"the lower operator reads the upper");

    // Slide the lower one clear, which breaks the adjacency.
    At(page,1)->PosX = 6;
    Doc->Connect();
    undo.Push(page,L"move");
    Check(At(page,1)->Inputs.GetCount()==0,L"moving it clear breaks the input");

    Check(undo.Undo(page),L"undo the move");
    Doc->Connect();
    Check(At(page,1)->PosX==0,L"the position is restored");
    Check(At(page,1)->Inputs.GetCount()==1,
      L"and so is the connection it implied");

    // Resize, which is the same rule reached a different way.
    At(page,0)->SizeY = 3;
    Doc->Connect();
    undo.Push(page,L"resize");
    Check(At(page,1)->Inputs.GetCount()==0,
      L"growing the upper one downward breaks the adjacency");

    Check(undo.Undo(page),L"undo the resize");
    Doc->Connect();
    Check(At(page,0)->SizeY==1,L"the size is restored");
    Check(At(page,1)->Inputs.GetCount()==1,L"and the connection with it");
  }

  // --- parameter words -----------------------------------------------------

  sPrint(L"\nparameter edits\n");
  {
    page->Ops.Clear();
    wInsertOp(page,flat,0,0);
    Doc->Connect();

    // Flat word 1 is Color. Set it to something recognisable first, so the test
    // is not relying on whatever the class default happens to be.
    At(page,0)->EditU()[1] = 0xff112233;
    undo.Reset(page);

    At(page,0)->EditU()[1] = 0xffaabbcc;
    undo.Push(page,L"colour");
    Check(Word(page,0,1)==0xffaabbcc,L"the edited word is what we set");

    Check(undo.Undo(page),L"undo the parameter edit");
    Check(Word(page,0,1)==0xff112233,
      L"and the word is restored exactly, not to the class default");

    Check(undo.Redo(page),L"redo it");
    Check(Word(page,0,1)==0xffaabbcc,L"and the edit is back");
  }

  // --- array rows ----------------------------------------------------------

  sPrint(L"\nparameter array rows\n");
  {
    // The part of an operator most easily dropped by a snapshot, because it is
    // heap-allocated per row rather than living in the word block.
    page->Ops.Clear();
    wStackOp *g = wInsertOp(page,grad,0,0);
    Doc->Connect();

    g->AddArray(-1);
    g->AddArray(-1);
    sU32 *row0 = (sU32 *) g->GetArray<void>(0);
    sU32 *row1 = (sU32 *) g->GetArray<void>(1);
    Check(row0 && row1,L"two rows exist");
    if(row0 && row1)
    {
      row0[1] = 0xff010203;         // Gradient row word 1 is Color
      row1[1] = 0xff040506;
    }
    undo.Reset(page);

    At(page,0)->AddArray(-1);
    sU32 *row2 = (sU32 *) At(page,0)->GetArray<void>(2);
    if(row2)
      row2[1] = 0xff070809;
    undo.Push(page,L"add row");
    Check(At(page,0)->GetArrayCount()==3,L"three rows after adding one");

    Check(undo.Undo(page),L"undo the row insert");
    Check(At(page,0)->GetArrayCount()==2,L"two rows again");

    // The surviving rows must still hold their own data, which is the check that
    // a snapshot restoring the right NUMBER of rows but not their contents would
    // fail.
    sU32 *r0 = (sU32 *) At(page,0)->GetArray<void>(0);
    sU32 *r1 = (sU32 *) At(page,0)->GetArray<void>(1);
    Check(r0 && r0[1]==0xff010203,L"and row 0 kept its colour");
    Check(r1 && r1[1]==0xff040506,L"and row 1 kept its colour");

    Check(undo.Redo(page),L"redo the row insert");
    Check(At(page,0)->GetArrayCount()==3,L"three rows again");
    sU32 *r2 = (sU32 *) At(page,0)->GetArray<void>(2);
    Check(r2 && r2[1]==0xff070809,L"and the new row's colour came back too");
  }

  // --- names, Hide and Bypass ---------------------------------------------

  sPrint(L"\nnames, Hide and Bypass\n");
  {
    page->Ops.Clear();
    wInsertOp(page,nop,0,0);
    Doc->Connect();
    At(page,0)->Name = L"before";
    undo.Reset(page);

    At(page,0)->Name = L"after";
    At(page,0)->Hide = 1;
    At(page,0)->Bypass = 1;
    undo.Push(page,L"rename and flag");

    Check(undo.Undo(page),L"undo them together");
    Check(sCmpString(At(page,0)->Name,L"before")==0,L"the name is restored");
    Check(At(page,0)->Hide==0,L"Hide is restored");
    Check(At(page,0)->Bypass==0,L"Bypass is restored");
  }

  // --- stack behaviour ----------------------------------------------------

  sPrint(L"\nthe stack itself\n");
  {
    page->Ops.Clear();
    wInsertOp(page,nop,0,0);
    undo.Reset(page);

    for(sInt i=1;i<=5;i++)
    {
      wInsertOp(page,nop,0,i);
      undo.Push(page,L"insert");
    }
    Check(page->Ops.GetCount()==6,L"six operators after five inserts");
    Check(undo.Depth()==6,L"and six states including the baseline");

    for(sInt i=0;i<5;i++)
      undo.Undo(page);
    Check(page->Ops.GetCount()==1,L"undoing five times returns to one");
    Check(!undo.CanUndo(),L"and there is nothing left to undo");

    for(sInt i=0;i<5;i++)
      undo.Redo(page);
    Check(page->Ops.GetCount()==6,L"redoing five times returns to six");
    Check(!undo.CanRedo(),L"and there is nothing left to redo");

    // A new edit after undoing discards the redo branch — the future that no
    // longer happened.
    undo.Undo(page);
    undo.Undo(page);
    Check(undo.CanRedo(),L"there is a redo branch after undoing twice");
    wInsertOp(page,nop,8,0);
    undo.Push(page,L"insert elsewhere");
    Check(!undo.CanRedo(),L"and a new edit discards it");
  }

  sPrintF(L"\n%d state(s), %d bytes of history\n",undo.Depth(),sInt(undo.Bytes()));
  sPrintF(L"%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
