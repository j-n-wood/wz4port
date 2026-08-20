/****************************************************************************/
/***                                                                      ***/
/***   Document edits, with no UI attached — wz4port editor                ***/
/***                                                                      ***/
/****************************************************************************/

#include "docedit.hpp"

/****************************************************************************/

wStackOp *wInsertOp(wPage *page,wClass *cl,sInt x,sInt y)
{
  if(!page || !cl)
    return 0;

  const sInt w = 3, h = 1;

  // Width is hardcoded to 3, as upstream (gui.cpp:5170, where the multi-input
  // heuristic is commented out). It is deliberately NOT sized to the input
  // count: width is semantic in this model, so choosing it is the user's act.
  //
  // CheckDest with move=0, because nothing is selected-and-moving here — a new
  // block must fit in genuinely empty cells.
  if(!page->CheckDest(0,x,y,w,h,0))
    return 0;

  wStackOp *op = new wStackOp;
  op->Init(cl);
  op->PosX = x;
  op->PosY = y;
  op->SizeX = w;
  op->SizeY = h;

  // The class's own defaults, the same call the .wz4t reader makes before
  // applying a file's overrides. Without it the parameter block is zeroed, which
  // for most operators means a 1 x 1 bitmap and a black result.
  if(cl->SetDefaults)
    cl->SetDefaults(op);

  page->Ops.AddTail(op);

  for(sInt i=0;i<page->Ops.GetCount();i++)
    page->Ops[i]->Select = 0;
  op->Select = 1;

  return op;
}

/****************************************************************************/

sInt wDeleteSelection(wPage *page)
{
  if(!page)
    return 0;

  sInt n = 0;
  // Backwards, because removal shifts the tail of the array.
  for(sInt i=page->Ops.GetCount()-1;i>=0;i--)
  {
    wStackOp *op = page->Ops[i];
    if(!op->Select)
      continue;
    page->Rem(op);                  // the object itself is reclaimed by the GC
    n++;
  }
  return n;
}

/****************************************************************************/
