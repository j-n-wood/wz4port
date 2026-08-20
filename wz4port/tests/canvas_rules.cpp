/****************************************************************************/
/***                                                                      ***/
/***   Stage 5.2 gate — canvas edits obey the document's own rules         ***/
/***                                                                      ***/
/****************************************************************************/
//
// The canvas is a GUI, and half of its gate — "a document's layout renders
// recognisably" — is verified by looking at a screenshot. The other half,
// "blocks can be moved and resized within the original's collision rules", is
// not something a screenshot can show, so it is tested here instead.
//
// The editor does not reimplement those rules: editor/canvas.cpp calls
// wPage::CheckDest and wPage::CheckMove directly, and they read wOp::Select as
// the selection. So this exercises exactly the code the canvas drives, with the
// same inputs a drag would produce, and additionally checks the part that makes
// the model what it is: that a legal move REWIRES THE GRAPH, because in
// Werkkzeug the geometry is the graph.
//
// Rules under test, from docs/01-existing-model.md §2.2 and §2.4:
//
//   connection    A is an input of B iff A.PosY + A.SizeY == B.PosY and their
//                 horizontal spans overlap; inputs sorted left-to-right
//   collision     any overlap is rejected; ALL-OR-NOTHING across the whole
//                 selection; nothing is ever displaced or nudged
//   comments      may overlap anything
//   bounds        0 <= x, x+w < wPAGEXS, likewise y

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "base/system.hpp"

/****************************************************************************/

void RegisterWZ4Classes()
{
  // Braces matter: sREGOPS expands to more than one statement, so a loop body
  // without them leaves the rest outside the loop and referring to a dead `i`.
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

static void SelectOnly(wPage *page,wStackOp *a,wStackOp *b=0)
{
  for(sInt i=0;i<page->Ops.GetCount();i++)
    page->Ops[i]->Select = 0;
  if(a) a->Select = 1;
  if(b) b->Select = 1;
}

// What the canvas does on release of a legal drag.
//
// The legality check is repeated here rather than trusted, because the first
// version of this test applied a move CheckMove had already refused: the
// assertion about the refusal failed, the move went through anyway, and the
// three assertions after it still passed on the wrong geometry. A helper that
// cannot apply an illegal move makes that class of mistake impossible.
static void ApplyMove(wPage *page,sInt dx,sInt dy)
{
  if(!page->CheckMove(dx,dy,0,0,1))
  {
    sPrintF(L"  FAIL  refusing to apply an illegal move (%d,%d)\n",dx,dy);
    Failures++;
    return;
  }
  for(sInt i=0;i<page->Ops.GetCount();i++)
  {
    wStackOp *op = page->Ops[i];
    if(op->Select)
    {
      op->PosX += dx;
      op->PosY += dy;
    }
  }
  Doc->Connect();
}

static sBool HasInput(wOp *op,wOp *want)
{
  for(sInt i=0;i<op->Inputs.GetCount();i++)
    if(op->Inputs[i]==want)
      return 1;
  return 0;
}

/****************************************************************************/

void sMain()
{
  sPrint(L"canvas_rules: stage 5.2 gate\n\n");

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];
  page->Ops.Clear();

  //   x: 0    3    6
  // y=0   [ a ][ b ]
  // y=1   [   c    ]     6 wide, so it consumes both
  // y=3   [ d ]          a clear row below, connected to nothing
  //
  // Nop is a `basic` operator that takes any input, which is what makes it
  // usable for every case below.
  wStackOp *a = AddOp(page,L"Nop",0,0,3);
  wStackOp *b = AddOp(page,L"Nop",3,0,3);
  wStackOp *c = AddOp(page,L"Nop",0,1,6);
  wStackOp *d = AddOp(page,L"Nop",0,3,3);
  if(Failures)
  {
    sPrintF(L"\n%d failure(s)\n",Failures);
    sSetErrorCode();
    return;
  }

  Doc->Connect();

  sPrint(L"the derived graph before any edit\n");
  Check(c->Inputs.GetCount()==2,L"c has two inputs");
  Check(HasInput(c,a) && HasInput(c,b),L"c's inputs are a and b");
  Check(c->Inputs.GetCount()==2 && c->Inputs[0]==a,
    L"and they are sorted left to right");
  Check(d->Inputs.GetCount()==0,L"d is connected to nothing");

  sPrint(L"\ncollision: any overlap is refused\n");

  SelectOnly(page,d);
  Check(page->CheckMove(0,1,0,0,1),L"d may move into free cells");
  Check(!page->CheckMove(0,-2,0,0,1),L"d may not move onto c");

  SelectOnly(page,a);
  Check(!page->CheckMove(3,0,0,0,1),L"a may not move onto b");

  sPrint(L"\nall-or-nothing across the selection\n");

  // a and d together, one cell left: a would leave the page. The whole move must
  // be refused even though d's half is perfectly legal.
  SelectOnly(page,a,d);
  Check(!page->CheckMove(-1,0,0,0,1),
    L"a two-block move is refused entirely when one would leave the page");

  // Same shape, but the illegal half is a collision rather than a bound.
  SelectOnly(page,b,d);
  Check(!page->CheckMove(0,1,0,0,1),
    L"and refused entirely when one would overlap another block");

  // With only the legal member selected the same delta passes, which is what
  // proves the refusals above were about the other block, not the delta.
  SelectOnly(page,d);
  Check(page->CheckMove(0,1,0,0,1),L"the same delta is legal for d alone");

  sPrint(L"\nselected blocks do not collide with themselves\n");

  // A whole-selection move keeps the relative layout, so a sliding onto b's
  // cells while b slides out of them must be legal. `move` is the flag that
  // tells CheckDest to skip selected blocks.
  //
  // The delta has to put a destination ON another selected block for this to
  // test anything: the first attempt moved both down four rows into empty space,
  // where move=0 and move=1 agree, so the assertion was vacuous and failed.
  SelectOnly(page,a,b);
  Check(page->CheckMove(3,0,0,0,1),
    L"a may slide onto b's cells while b slides out of them");
  Check(!page->CheckMove(3,0,0,0,0),
    L"and the same move is refused with move=0, which skips nothing");

  sPrint(L"\npage bounds\n");
  SelectOnly(page,a);
  Check(!page->CheckMove(-1,0,0,0,1),L"nothing may move to x < 0");
  Check(!page->CheckMove(0,-1,0,0,1),L"nothing may move to y < 0");
  Check(!page->CheckMove(wPAGEXS,0,0,0,1),
    L"nothing may move past the right edge");
  Check(!page->CheckMove(0,wPAGEYS,0,0,1),L"nothing may move past the bottom");

  sPrint(L"\ncomments may overlap anything\n");
  wStackOp *note = AddOp(page,L"Comment",0,0,6);
  if(note)
  {
    SelectOnly(page,note);
    Check(page->CheckMove(0,1,0,0,1),
      L"a comment may be moved onto other blocks");
    page->Ops.RemOrder(note);
  }

  sPrint(L"\nresize is gated by the same rule\n");
  SelectOnly(page,d);
  Check(page->CheckMove(0,0,3,0,1),L"d may grow right into free cells");
  SelectOnly(page,a);
  Check(!page->CheckMove(0,0,3,0,1),L"a may not grow right into b");

  // --- the part that matters: geometry IS the graph -------------------------

  sPrint(L"\na legal move rewires the graph\n");

  // b one cell right: its span becomes 4..7, still overlapping c's 0..6, so the
  // connection survives.
  SelectOnly(page,b);
  Check(page->CheckMove(1,0,0,0,1),L"b may move one cell right");
  ApplyMove(page,1,0);
  Check(c->Inputs.GetCount()==2,L"c still has two inputs after a 1-cell slide");

  // Now slide b right until its span clears c's. b is at x=4 and 3 wide; c
  // spans 0..6. At x=6 the spans touch but do not overlap, so the connection
  // must drop.
  //
  // Sliding b DOWN would be the obvious way to break the adjacency, and it is
  // what the first version of this test did — but c is directly below b, so the
  // move is a collision and CheckMove refuses it. Horizontal span is the half of
  // the rule that can actually be exercised here, and it is the more interesting
  // half anyway.
  SelectOnly(page,b);
  Check(page->CheckMove(2,0,0,0,1),L"b may slide two cells right");
  ApplyMove(page,2,0);
  Check(c->Inputs.GetCount()==1,
    L"sliding b clear of c's span drops it as an input");
  Check(HasInput(c,a) && !HasInput(c,b),L"and the input that remains is a");

  // Back again: spans overlap once more, input restored.
  SelectOnly(page,b);
  ApplyMove(page,-2,0);
  Check(c->Inputs.GetCount()==2,L"sliding b back restores the input");
  Check(HasInput(c,b),L"and it is b again");

  // And the vertical half of the rule, on a block that has room to move: d sits
  // at row 3 with nothing above it. c occupies row 1, so its bottom edge is 2 —
  // moving d up one row makes d's top edge 2 as well, and c becomes its input.
  Check(d->Inputs.GetCount()==0,L"d starts with no inputs");
  SelectOnly(page,d);
  Check(page->CheckMove(0,-1,0,0,1),L"d may move up one row");
  ApplyMove(page,0,-1);
  Check(d->Inputs.GetCount()==1 && HasInput(d,c),
    L"and touching c's bottom edge makes c an input of d");

  sPrint(L"\nnothing is ever displaced\n");

  // The defining property of this collision model: a refused move leaves every
  // block exactly where it was.
  const sInt ax = a->PosX, ay = a->PosY;
  SelectOnly(page,d);
  page->CheckMove(0,-2,0,0,1);      // refused above; must have no side effect
  Check(a->PosX==ax && a->PosY==ay,L"a checked move never moves anything");

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();

  delete Doc;
  Doc = 0;
}

/****************************************************************************/
