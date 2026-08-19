/****************************************************************************/
/***                                                                      ***/
/***   Phase 2 gate — the operator runtime runs with no GUI                ***/
/***                                                                      ***/
/****************************************************************************/

// Links libwz4core, registers the `basic` operator module, builds a document
// programmatically, and lets wDocument::Connect() derive the graph from block
// geometry alone. Then checks the derived input lists.
//
// The point is not that these particular assertions are interesting. It is that
// getting this far requires the whole runtime — type registry, class registry,
// operator construction, parameter storage, page management and the connection
// builder — to work with no window system, no graphics API and no widget
// toolkit linked in.
//
// The connection rule under test (01-existing-model.md §2.2):
//
//     A is an input of B iff A.PosY + A.SizeY == B.PosY
//     and their horizontal spans overlap.
//     B's inputs are then sorted left-to-right by PosX.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "base/system.hpp"

/****************************************************************************/

// The module registration hook. wDocument's constructor calls this (doc.cpp:2464)
// and every wz4 application defines it — it is the intended seam for choosing
// which operator libraries exist, not a stub. We register only `basic`.

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)           // two passes: types first, then operators
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

// Adds a stack operator of the named class at a grid rectangle. Nothing here
// says anything about connections — that is the whole point.

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

static void PrintInputs(wOp *op,const sChar *label)
{
  sString<256> line;
  line.PrintF(L"    %s <- ",label);
  if(op->Inputs.GetCount()==0)
  {
    line.Add(L"(nothing)");
  }
  else
  {
    wOp *in;
    sFORALL(op->Inputs,in)
    {
      if(_i)
        line.Add(L", ");
      line.Add(in->Class->Name);
      sString<32> pos;
      pos.PrintF(L"@x%d",((wStackOp *)in)->PosX);
      line.Add(pos);
    }
  }
  sPrintF(L"%s\n",line);
}

/****************************************************************************/

void sMain()
{
  sPrintF(L"core_connect: phase 2 gate\n");

  // 1. Bring up the document. Its constructor calls RegisterWZ4Classes above,
  //    which registers the basic module, and then builds the conversion and
  //    extraction tables from what was registered.

  Doc = new wDocument;

  sPrintF(L"\n  registered %d types, %d classes\n\n",
    Doc->Types.GetCount(),Doc->Classes.GetCount());
  Check(Doc->Types.GetCount()>0,L"types registered");
  Check(Doc->Classes.GetCount()>0,L"operator classes registered");
  Check(Doc->FindType(L"AnyType")!=0,L"AnyType resolves by name");

  // 2. A stack page, and four operators placed so that geometry alone implies
  //    the graph. Grid units; y grows downward and data flows downward.
  //
  //        x: 0    3    6    9
  //     y=0    [Nop A][Nop B]          two 3-wide sources
  //     y=1    [   Group    ]          6 wide, so it consumes both
  //     y=2
  //     y=3    [ Orphan ]              one row gap: connected to nothing

  wPage *page = new wPage;
  page->Name = L"gate";
  page->IsTree = 0;
  Doc->Pages.AddTail(page);
  Doc->CurrentPage = page;

  wStackOp *a      = AddOp(page,L"Nop",  0,0,3);
  wStackOp *b      = AddOp(page,L"Nop",  3,0,3);
  wStackOp *group  = AddOp(page,L"Group",0,1,6);
  wStackOp *orphan = AddOp(page,L"Nop",  0,3,3);

  if(!a || !b || !group || !orphan)
  {
    sPrintF(L"\ncore_connect: could not place operators\n");
    sSetErrorCode();
    return;
  }

  a->Name = L"A";
  b->Name = L"B";

  // 3. Derive the graph. No connection was ever stated.

  Doc->Connect();

  sPrintF(L"\n  derived input lists:\n");
  PrintInputs(a,L"Nop A   ");
  PrintInputs(b,L"Nop B   ");
  PrintInputs(group,L"Group   ");
  PrintInputs(orphan,L"Nop orphan");
  sPrintF(L"\n");

  // 4. What the geometry should have implied.

  Check(a->Inputs.GetCount()==0,L"a source block has no inputs");
  Check(b->Inputs.GetCount()==0,L"the second source block has no inputs");

  Check(group->Inputs.GetCount()==2,
    L"a 6-wide block under two 3-wide blocks consumes both");

  // Left-to-right by PosX: A is in0 because it sits further left.
  if(group->Inputs.GetCount()==2)
  {
    Check(group->Inputs[0]==a,L"in0 is the leftmost block above (width is semantic)");
    Check(group->Inputs[1]==b,L"in1 is the next one right");
  }

  Check(orphan->Inputs.GetCount()==0,
    L"a one-row gap severs the connection completely");

  // 5. Outputs are the reverse mapping, filled in by the same pass.

  Check(a->Outputs.GetCount()==1 && a->Outputs[0]==group,
    L"the reverse edge is recorded too");

  // 6. Move the orphan up so its bottom edge meets nothing but its top edge
  //    meets Group's bottom. Adjacency is exact, so this must connect.

  orphan->PosY = 2;
  Doc->Connect();
  Check(orphan->Inputs.GetCount()==1 && orphan->Inputs[0]==group,
    L"moving a block to touch an edge connects it, with no other edit");

  // 7. And the type system's own rule, which the connection pass relies on.

  wType *any = Doc->FindType(L"AnyType");
  wType *tex = Doc->FindType(L"Texture2D");
  if(any && tex)
  {
    Check(tex->IsType(any),L"Texture2D is an AnyType");
    Check(!any->IsType(tex),L"AnyType is not a Texture2D");
  }
  else
  {
    Check(0,L"AnyType and Texture2D both resolve");
  }

  sPrintF(L"\ncore_connect: %d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
