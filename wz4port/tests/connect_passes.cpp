/****************************************************************************/
/***                                                                      ***/
/***   Stage 5.3 gate — the three post-passes of the connection rule       ***/
/***                                                                      ***/
/****************************************************************************/
//
// docs/01-existing-model.md §2.2 describes the connection rule as adjacency plus
// horizontal overlap, followed by three post-passes. core_connect covers the
// adjacency, and canvas_rules covers what happens when a drag changes it. The
// post-passes were untested, and they are not cosmetic — each one changes which
// operator feeds which:
//
//   Hide    (doc.cpp:2926)  inputs with Hide set are dropped from consumers
//   Sort    (doc.cpp:2946)  inputs ordered left-to-right by PosX
//   Bypass  (doc.cpp:2963)  a bypassed block is spliced out, passing its own
//                           in0 through; with no inputs, the slot disappears
//
// The editor does not reimplement any of this — editor/main.cpp calls
// Doc->Connect() — so what is being pinned here is upstream behaviour that the
// editor now exposes through Hide and Bypass toggles and draws as guides.
//
// Two orderings inside the implementation are worth knowing, because a
// reimplementation would plausibly get them wrong:
//
//   * Hide removes with RemAt, which does NOT preserve order. It is only safe
//     because the sort pass runs afterwards and puts things back.
//   * Bypass runs AFTER the sort and removes with RemAtOrder, so it never
//     re-sorts. That turns out not to matter, and the reason is geometric —
//     see the comment on the ordering test below.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "base/system.hpp"

/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
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

static wStackOp *AddOp(wPage *page,const sChar *classname,
  sInt x,sInt y,sInt w,sInt h=1)
{
  wClass *cl = Doc->FindClass(classname,0);
  if(!cl)
  {
    sPrintF(L"  FAIL  no such operator class <%s>\n",classname);
    Failures++;
    return 0;
  }
  wStackOp *op = new wStackOp;
  op->Init(cl);
  op->PosX = x;
  op->PosY = y;
  op->SizeX = w;
  op->SizeY = h;
  page->Ops.AddTail(op);
  return op;
}

static wPage *FreshPage()
{
  delete Doc;
  Doc = new wDocument;
  wPage *page = Doc->Pages[0];
  page->Ops.Clear();
  return page;
}

static sInt IndexOfInput(wOp *op,wOp *want)
{
  for(sInt i=0;i<op->Inputs.GetCount();i++)
    if(op->Inputs[i]==want)
      return i;
  return -1;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"connect_passes: stage 5.3 gate\n\n");

  // --- hide ----------------------------------------------------------------

  sPrint(L"hide drops an input, and the block stays on the page\n");
  {
    wPage *page = FreshPage();
    wStackOp *a = AddOp(page,L"Nop",0,0,3);
    wStackOp *b = AddOp(page,L"Nop",3,0,3);
    wStackOp *c = AddOp(page,L"Nop",0,1,6);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==2,L"two inputs before hiding anything");

    b->Hide = 1;
    Doc->Connect();
    Check(c->Inputs.GetCount()==1,L"hiding b leaves one input");
    Check(c->Inputs.GetCount()==1 && c->Inputs[0]==a,L"and it is a");
    Check(page->Ops.GetCount()==3,
      L"b is still on the page — Hide is not delete");

    // Hidden blocks still derive their own inputs; only consumers ignore them.
    Check(b->Inputs.GetCount()==0,L"a hidden block still has its own input list");

    b->Hide = 0;
    Doc->Connect();
    Check(c->Inputs.GetCount()==2,L"unhiding restores the input");
  }

  // --- hide interacts with sort -------------------------------------------

  sPrint(L"\nhide removes without preserving order, and sort repairs it\n");
  {
    wPage *page = FreshPage();
    wStackOp *l = AddOp(page,L"Nop",0,2,3);
    wStackOp *m = AddOp(page,L"Nop",3,2,3);
    wStackOp *r = AddOp(page,L"Nop",6,2,3);
    wStackOp *c = AddOp(page,L"Nop",0,3,9);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==3,L"three inputs");
    Check(c->Inputs[0]==l && c->Inputs[1]==m && c->Inputs[2]==r,
      L"ordered left to right");

    // Hiding the MIDDLE one is the interesting case: the hide pass uses RemAt,
    // which fills the gap with the last element rather than shifting, so
    // without the later sort pass the survivors would come out [l, r] or
    // [r, l] depending on the array's internals.
    m->Hide = 1;
    Doc->Connect();
    Check(c->Inputs.GetCount()==2,L"hiding the middle input leaves two");
    Check(c->Inputs.GetCount()==2 && c->Inputs[0]==l && c->Inputs[1]==r,
      L"and they are still ordered left to right");
  }

  // --- bypass --------------------------------------------------------------

  sPrint(L"\nbypass splices a block out, passing its own in0 through\n");
  {
    wPage *page = FreshPage();
    wStackOp *a = AddOp(page,L"Nop",0,0,3);
    wStackOp *bp = AddOp(page,L"Nop",0,1,3);
    wStackOp *c = AddOp(page,L"Nop",0,2,3);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==1 && c->Inputs[0]==bp,
      L"c reads bp before the bypass");

    bp->Bypass = 1;
    Doc->Connect();
    Check(c->Inputs.GetCount()==1,L"c still has exactly one input");
    Check(c->Inputs.GetCount()==1 && c->Inputs[0]==a,
      L"but it is now a — bp is spliced out");
    Check(page->Ops.GetCount()==3,
      L"bp is still on the page — Bypass is not delete");

    bp->Bypass = 0;
    Doc->Connect();
    Check(c->Inputs.GetCount()==1 && c->Inputs[0]==bp,L"clearing it restores bp");
  }

  sPrint(L"\nbypassing a block with no inputs removes the slot entirely\n");
  {
    wPage *page = FreshPage();
    wStackOp *bp = AddOp(page,L"Nop",0,1,3);
    wStackOp *c = AddOp(page,L"Nop",0,2,3);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==1,L"c reads bp");

    bp->Bypass = 1;
    Doc->Connect();
    Check(c->Inputs.GetCount()==0,
      L"bypassing an input-less block leaves c with no inputs at all");
  }

  sPrint(L"\nand it removes only that slot, not the others\n");
  {
    wPage *page = FreshPage();
    wStackOp *keep = AddOp(page,L"Nop",0,1,3);
    wStackOp *bp = AddOp(page,L"Nop",3,1,3);
    wStackOp *c = AddOp(page,L"Nop",0,2,6);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==2,L"two inputs");

    bp->Bypass = 1;
    Doc->Connect();
    Check(c->Inputs.GetCount()==1,L"one survives");
    Check(c->Inputs.GetCount()==1 && c->Inputs[0]==keep,L"and it is the right one");
  }

  // --- bypass and ordering -------------------------------------------------

  sPrint(L"\nbypass substitution cannot break the left-to-right order\n");
  {
    // The bypass pass runs AFTER the sort and never re-sorts, so substituting
    // one operator for another in a slot could in principle leave the list out
    // of order. It cannot, and the reason is geometric: a bypassed block's own
    // input must overlap the bypassed block's horizontal span, while its sibling
    // slots must lie outside that span — they are separate inputs of the same
    // consumer on the same row. So the substitute always sorts into the slot it
    // replaces.
    //
    // Asserted rather than assumed, because it is the kind of invariant a
    // reimplementation would break by re-sorting (harmless) or by splicing
    // before sorting (not harmless).
    wPage *page = FreshPage();
    wStackOp *w = AddOp(page,L"Nop",6,0,3);   // feeds bp, at bp's right end
    wStackOp *bp = AddOp(page,L"Nop",0,1,9);  // wide, so w overlaps it
    wStackOp *z = AddOp(page,L"Nop",9,1,3);   // sibling slot, to bp's right
    wStackOp *c = AddOp(page,L"Nop",0,2,12);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==2 && c->Inputs[0]==bp && c->Inputs[1]==z,
      L"before the bypass, c reads bp then z");

    bp->Bypass = 1;
    Doc->Connect();
    Check(c->Inputs.GetCount()==2,L"still two inputs after the bypass");
    Check(IndexOfInput(c,w)==0,
      L"the substitute takes the slot it replaced, still first");
    Check(IndexOfInput(c,z)==1,L"and the sibling is still second");
  }

  // --- comments never participate ------------------------------------------

  sPrint(L"\ncomments never participate in the graph\n");
  {
    wPage *page = FreshPage();
    wStackOp *note = AddOp(page,L"Comment",0,0,3);
    wStackOp *c = AddOp(page,L"Nop",0,1,3);
    if(Failures) goto done;

    Doc->Connect();
    Check(c->Inputs.GetCount()==0,
      L"a comment directly above a block is not an input of it");
    Check(note->Inputs.GetCount()==0,L"and a comment has no inputs either");
  }

done:
  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
