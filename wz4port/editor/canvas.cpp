/****************************************************************************/
/***                                                                      ***/
/***   The stacking canvas — wz4port editor, stage 5.2                    ***/
/***                                                                      ***/
/****************************************************************************/

#include "canvas.hpp"
#include "wz4lib/basic_ops.hpp"

/****************************************************************************/

// The original's cell, from docs/07-phase-texture-gui.md. Blocks are 3 x 1 by
// default, so a default block is 72 x 16 — wide and flat, which is what makes a
// stack of them readable as a pipeline.
static const float CELLX = 24.0f;
static const float CELLY = 16.0f;

/****************************************************************************/

wCanvas::wCanvas()
{
  CursorX = 0;
  CursorY = 0;
  ShowGuides = 0;
  FitPending = 0;
  Zoom = 1.0f;
  PanX = -8.0f;                     // a little margin, so 0,0 is not flush
  PanY = -8.0f;
  Drag = DRAG_NONE;
  DragStartX = DragStartY = 0;
  DragDX = DragDY = 0;
  DragValid = 1;
}

void wCanvas::ResetView()
{
  Zoom = 1.0f;
  PanX = -8.0f;
  PanY = -8.0f;
}

void wCanvas::FitPage(wPage *page,ImVec2 size)
{
  FitPending = 0;
  if(!page || page->Ops.GetCount()==0)
  {
    ResetView();
    return;
  }

  sInt x0 = wPAGEXS,y0 = wPAGEYS,x1 = 0,y1 = 0;
  for(sInt i=0;i<page->Ops.GetCount();i++)
  {
    wStackOp *op = page->Ops[i];
    x0 = sMin(x0,op->PosX);
    y0 = sMin(y0,op->PosY);
    x1 = sMax(x1,op->PosX+op->SizeX);
    y1 = sMax(y1,op->PosY+op->SizeY);
  }

  const float w = (x1-x0)*CELLX;
  const float h = (y1-y0)*CELLY;
  if(w<=0.0f || h<=0.0f)
  {
    ResetView();
    return;
  }

  // Fit with a margin, and never magnify past 1:1 — a two-block document
  // blown up to fill the pane looks broken rather than helpful.
  const float margin = 24.0f;
  const float zx = (size.x-margin*2.0f)/w;
  const float zy = (size.y-margin*2.0f)/h;
  Zoom = sClamp(sMin(zx,zy),0.3f,1.0f);

  // Centre what is left over, so a narrow graph is not pinned to one corner.
  PanX = x0*CELLX - (size.x/Zoom - w)*0.5f;
  PanY = y0*CELLY - (size.y/Zoom - h)*0.5f;
}

/****************************************************************************/

ImVec2 wCanvas::CellToScreen(ImVec2 origin,float cx,float cy) const
{
  return ImVec2(origin.x + (cx*CELLX - PanX)*Zoom,
                origin.y + (cy*CELLY - PanY)*Zoom);
}

void wCanvas::ScreenToCell(ImVec2 origin,ImVec2 p,sInt &cx,sInt &cy) const
{
  const float px = (p.x - origin.x)/Zoom + PanX;
  const float py = (p.y - origin.y)/Zoom + PanY;
  // sFloor, not a cast: a negative coordinate must round DOWN, and (sInt)(-0.5)
  // is 0, which would make the cell left of the origin read as cell 0.
  cx = sInt(sFFloor(px/CELLX));
  cy = sInt(sFFloor(py/CELLY));
}

wStackOp *wCanvas::OpAt(wPage *page,sInt cx,sInt cy) const
{
  // Comments are click-through, so they are skipped here even though they are
  // drawn. Searched last-to-first so that the topmost of any overlap wins, which
  // matches the paint order.
  for(sInt i=page->Ops.GetCount()-1;i>=0;i--)
  {
    wStackOp *op = page->Ops[i];
    if(op->Class && (op->Class->Flags & wCF_COMMENT))
      continue;
    if(cx>=op->PosX && cx<op->PosX+op->SizeX &&
       cy>=op->PosY && cy<op->PosY+op->SizeY)
      return op;
  }
  return 0;
}

wOp *wCanvas::SingleSelection(wPage *page) const
{
  wOp *found = 0;
  for(sInt i=0;i<page->Ops.GetCount();i++)
  {
    if(page->Ops[i]->Select)
    {
      if(found)
        return 0;                   // more than one
      found = page->Ops[i];
    }
  }
  return found;
}

/****************************************************************************/

void wCanvas::DrawGrid(ImDrawList *dl,ImVec2 origin,ImVec2 size)
{
  const ImU32 line = IM_COL32(255,255,255,10);
  const ImU32 line8 = IM_COL32(255,255,255,20);

  // Every cell, with every eighth emphasised. Without the emphasis a 192 x 128
  // page of 24 x 16 cells is an undifferentiated haze at this density.
  const float stepx = CELLX*Zoom;
  const float stepy = CELLY*Zoom;
  if(stepx<3.0f || stepy<3.0f)
    return;                         // too dense to be anything but noise

  const sInt firstx = sInt(sFFloor(PanX/CELLX));
  const sInt firsty = sInt(sFFloor(PanY/CELLY));

  for(sInt cx=firstx;;cx++)
  {
    const float x = CellToScreen(origin,float(cx),0).x;
    if(x>origin.x+size.x) break;
    if(x>=origin.x)
      dl->AddLine(ImVec2(x,origin.y),ImVec2(x,origin.y+size.y),
        (cx%8)==0 ? line8 : line);
  }
  for(sInt cy=firsty;;cy++)
  {
    const float y = CellToScreen(origin,0,float(cy)).y;
    if(y>origin.y+size.y) break;
    if(y>=origin.y)
      dl->AddLine(ImVec2(origin.x,y),ImVec2(origin.x+size.x,y),
        (cy%8)==0 ? line8 : line);
  }
}

/****************************************************************************/

// Block appearance follows docs/01-existing-model.md §2.6. What is implemented
// here is everything the headless document model can actually tell us:
//
//   fill          the output type's colour, so the canvas is colour-coded by
//                 data type — wType::Color
//   selected      inverted bevel
//   error         red, from CalcErrorString / ConnectErrorString / ConnectError
//   unreachable   greyed, from ConnectedToRoot
//   slow-skipped  greyed, from SlowSkipFlag
//   Hide          a red X
//   Bypass        a vertical red bar
//   cached        one status dot, from wOp::Cache
//   comments      painted last, expanded, tinted, click-through
//
// Deliberately absent: the "shown in a viewport" and "open in the parameter
// panel" dots, because there is no viewport until 5.6 and no parameter panel
// until 5.5. Adding them now would mean inventing state to display.

void wCanvas::DrawBlock(ImDrawList *dl,ImVec2 origin,wStackOp *op,sBool comment)
{
  const sBool sel = op->Select!=0;

  float expand = 0.0f;
  if(comment)
    expand = 4.0f*Zoom;             // comments are drawn 4px larger, as upstream

  ImVec2 a = CellToScreen(origin,float(op->PosX),float(op->PosY));
  ImVec2 b = CellToScreen(origin,float(op->PosX+op->SizeX),
                                 float(op->PosY+op->SizeY));
  a.x -= expand; a.y -= expand;
  b.x += expand; b.y += expand;

  // While dragging, selected blocks are drawn at their projected destination as
  // a frame, and the block itself stays put — that is the original's behaviour
  // and it is what makes an illegal move obvious before you commit to it.
  sU32 typecol = 0xff808080;
  if(op->Class && op->Class->OutputType)
    typecol = op->Class->OutputType->Color;

  sInt r = (typecol>>16)&255, g = (typecol>>8)&255, bl = typecol&255;

  if(!op->ConnectedToRoot || op->SlowSkipFlag)
  {
    // Greyed: pull towards mid-grey rather than reducing alpha, so it reads as
    // "inactive" against the dark canvas instead of "translucent".
    r = (r+128)/3; g = (g+128)/3; bl = (bl+128)/3;
  }

  ImU32 fill = IM_COL32(r,g,bl,comment ? 90 : 255);
  if(!op->NoError())
    fill = IM_COL32(190,40,40,comment ? 90 : 255);

  dl->AddRectFilled(a,b,fill);

  // Bevel. Highlight top-left and shadow bottom-right normally; swapped when
  // selected, which is the original's "inverted bevel" and reads as pressed-in.
  const ImU32 hi = IM_COL32(255,255,255,sel ? 40 : 110);
  const ImU32 lo = IM_COL32(0,0,0,sel ? 140 : 70);
  const ImU32 top = sel ? lo : hi;
  const ImU32 bot = sel ? hi : lo;
  dl->AddLine(ImVec2(a.x,a.y),ImVec2(b.x-1,a.y),top);
  dl->AddLine(ImVec2(a.x,a.y),ImVec2(a.x,b.y-1),top);
  dl->AddLine(ImVec2(a.x,b.y-1),ImVec2(b.x-1,b.y-1),bot);
  dl->AddLine(ImVec2(b.x-1,a.y),ImVec2(b.x-1,b.y-1),bot);

  if(sel)
    dl->AddRect(a,b,IM_COL32(255,220,120,220));

  // Label: the store name if there is one, else the class. Clipped to the block
  // rather than allowed to overflow, because an overflowing label in a grid this
  // dense makes neighbours unreadable.
  const sChar *label = 0;
  if(!op->Name.IsEmpty())
    label = op->Name;
  else if(op->Class)
    label = op->Class->Name;

  if(label && (b.x-a.x)>12.0f)
  {
    char utf8[256];
    utf8[0] = 0;
    sCopyStringToUTF8(utf8,label,sCOUNTOF(utf8));

    // Black or white, whichever has contrast against this type's colour.
    const sInt lum = (r*30 + g*59 + bl*11)/100;
    const ImU32 text = lum>140 ? IM_COL32(0,0,0,230) : IM_COL32(255,255,255,230);

    dl->PushClipRect(a,b,true);
    dl->AddText(ImVec2(a.x+3.0f,a.y+1.0f),text,utf8);
    dl->PopClipRect();
  }

  if(comment)
    return;                         // the rest is meaningless for a comment

  // Hide: a red X across the block.
  if(op->Hide)
  {
    const ImU32 x = IM_COL32(255,60,60,220);
    dl->AddLine(ImVec2(a.x+2,a.y+2),ImVec2(b.x-3,b.y-3),x,1.5f);
    dl->AddLine(ImVec2(b.x-3,a.y+2),ImVec2(a.x+2,b.y-3),x,1.5f);
  }

  // Bypass: a vertical red bar.
  if(op->Bypass)
  {
    const float mid = (a.x+b.x)*0.5f;
    dl->AddLine(ImVec2(mid,a.y+2),ImVec2(mid,b.y-3),
      IM_COL32(255,60,60,220),2.0f);
  }

  // Status dot: has a cache. Bottom-right so it never collides with the label.
  if(op->Cache && (b.x-a.x)>16.0f)
    dl->AddCircleFilled(ImVec2(b.x-4.0f,b.y-4.0f),2.0f,
      IM_COL32(120,255,140,220));
}

/****************************************************************************/

sBool wCanvas::HandleInput(wPage *page,ImVec2 origin,ImVec2 size)
{
  ImGuiIO &io = ImGui::GetIO();
  sBool changed = 0;

  const sBool hovered = ImGui::IsItemHovered();
  const sBool active = ImGui::IsItemActive();

  // Zoom on the wheel, about the cursor, so the thing under the pointer stays
  // under the pointer. Clamped: below about 0.3 the labels are unreadable and
  // above 3 a 3 x 1 block fills the pane.
  if(hovered && io.MouseWheel!=0.0f)
  {
    const float old = Zoom;
    Zoom = sClamp(Zoom * (io.MouseWheel>0 ? 1.15f : 1.0f/1.15f),0.3f,3.0f);
    if(Zoom!=old)
    {
      const float mx = io.MousePos.x - origin.x;
      const float my = io.MousePos.y - origin.y;
      PanX += mx/old - mx/Zoom;
      PanY += my/old - my/Zoom;
    }
  }

  sInt cx,cy;
  ScreenToCell(origin,io.MousePos,cx,cy);

  // Middle-drag pans. The original scrolls with scrollbars; a middle-drag is the
  // modern equivalent and costs nothing.
  if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    Drag = DRAG_PAN;
  if(Drag==DRAG_PAN)
  {
    if(ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
      PanX -= io.MouseDelta.x/Zoom;
      PanY -= io.MouseDelta.y/Zoom;
    }
    else
    {
      Drag = DRAG_NONE;
    }
    return 0;
  }

  // --- press ---------------------------------------------------------------
  if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
  {
    wStackOp *hit = OpAt(page,cx,cy);

    // The cursor follows the click, as upstream: it is where the next inserted
    // operator lands.
    CursorX = cx;
    CursorY = cy;

    if(hit)
    {
      if(io.KeyShift)
      {
        hit->Select = 1;                      // add
      }
      else if(io.KeyCtrl)
      {
        hit->Select = hit->Select ? 0 : 1;    // toggle
      }
      else if(!hit->Select)
      {
        for(sInt i=0;i<page->Ops.GetCount();i++)
          page->Ops[i]->Select = 0;
        hit->Select = 1;
      }

      if(hit->Select)
      {
        Drag = DRAG_MOVE;
        DragStartX = io.MousePos.x;
        DragStartY = io.MousePos.y;
        DragDX = DragDY = 0;
        DragValid = 1;
      }
    }
    else
    {
      if(!io.KeyShift && !io.KeyCtrl)
        for(sInt i=0;i<page->Ops.GetCount();i++)
          page->Ops[i]->Select = 0;
      Drag = DRAG_RUBBER;
      DragStartX = io.MousePos.x;
      DragStartY = io.MousePos.y;
    }
  }

  // Resize is Shift+RMB anywhere on the block, not an edge handle — upstream
  // does it that way and edge handles would be unusable at 16px cell height.
  if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && io.KeyShift)
  {
    wStackOp *hit = OpAt(page,cx,cy);
    if(hit)
    {
      if(!hit->Select)
      {
        for(sInt i=0;i<page->Ops.GetCount();i++)
          page->Ops[i]->Select = 0;
        hit->Select = 1;
      }
      Drag = DRAG_RESIZE;
      DragStartX = io.MousePos.x;
      DragStartY = io.MousePos.y;
      DragDX = DragDY = 0;
      DragValid = 1;
    }
  }

  // --- drag ----------------------------------------------------------------
  if(Drag==DRAG_MOVE || Drag==DRAG_RESIZE)
  {
    // Pixel deltas rounded to cells, as upstream.
    const float dx = (io.MousePos.x - DragStartX)/Zoom;
    const float dy = (io.MousePos.y - DragStartY)/Zoom;
    // Altona has no round-to-nearest for floats, so floor(x+0.5) it is — and
    // that is the form we want anyway, because it rounds negative deltas the
    // same direction as positive ones.
    DragDX = sInt(sFFloor(dx/CELLX + 0.5f));
    DragDY = sInt(sFFloor(dy/CELLY + 0.5f));

    // Vertical resize needs wCF_VERTICALRESIZE on every selected block.
    if(Drag==DRAG_RESIZE)
    {
      for(sInt i=0;i<page->Ops.GetCount();i++)
      {
        wStackOp *op = page->Ops[i];
        if(op->Select && op->Class && !(op->Class->Flags & wCF_VERTICALRESIZE))
        {
          DragDY = 0;
          break;
        }
      }
    }

    // The document's own rule decides whether this is legal — CheckMove, not a
    // reimplementation of it.
    if(Drag==DRAG_MOVE)
      DragValid = page->CheckMove(DragDX,DragDY,0,0,1);
    else
      DragValid = page->CheckMove(0,0,DragDX,DragDY,1);

    const sBool released = (Drag==DRAG_MOVE)
      ? ImGui::IsMouseReleased(ImGuiMouseButton_Left)
      : ImGui::IsMouseReleased(ImGuiMouseButton_Right);

    if(released)
    {
      if(DragValid && (DragDX || DragDY))
      {
        for(sInt i=0;i<page->Ops.GetCount();i++)
        {
          wStackOp *op = page->Ops[i];
          if(!op->Select) continue;
          if(Drag==DRAG_MOVE)
          {
            op->PosX += DragDX;
            op->PosY += DragDY;
          }
          else
          {
            op->SizeX = sMax(1,op->SizeX+DragDX);
            op->SizeY = sMax(1,op->SizeY+DragDY);
          }
        }
        changed = 1;                // geometry changed, so the GRAPH changed
      }
      Drag = DRAG_NONE;
      DragDX = DragDY = 0;
    }
  }

  // --- rubber band ---------------------------------------------------------
  if(Drag==DRAG_RUBBER)
  {
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
      sInt ax,ay,bx,by;
      ScreenToCell(origin,ImVec2(DragStartX,DragStartY),ax,ay);
      ScreenToCell(origin,io.MousePos,bx,by);
      if(ax>bx) sSwap(ax,bx);
      if(ay>by) sSwap(ay,by);

      for(sInt i=0;i<page->Ops.GetCount();i++)
      {
        wStackOp *op = page->Ops[i];
        if(op->Class && (op->Class->Flags & wCF_COMMENT))
          continue;
        // Touched, not enclosed: a 3-wide block in a dense stack is hard to
        // fully enclose without catching its neighbours.
        if(op->PosX<=bx && op->PosX+op->SizeX-1>=ax &&
           op->PosY<=by && op->PosY+op->SizeY-1>=ay)
          op->Select = 1;
      }
      Drag = DRAG_NONE;
    }
  }

  (void)active;
  (void)size;
  return changed;
}

/****************************************************************************/

sBool wCanvas::Draw(wPage *page)
{
  if(!page)
  {
    ImGui::TextDisabled("No page.");
    return 0;
  }

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  if(size.x<32.0f) size.x = 32.0f;
  if(size.y<32.0f) size.y = 32.0f;

  // An InvisibleButton over the whole area, so the canvas owns the mouse rather
  // than fighting the window for it.
  if(FitPending)
    FitPage(page,size);

  ImGui::InvisibleButton("canvas",size,
    ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|
    ImGuiButtonFlags_MouseButtonMiddle);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  dl->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),
    IM_COL32(24,26,32,255));

  DrawGrid(dl,origin,size);

  const sBool changed = HandleInput(page,origin,size);

  // Non-comment blocks first, then comments, because comments are painted last
  // and tinted over the top — §2.6.
  for(sInt pass=0;pass<2;pass++)
  {
    for(sInt i=0;i<page->Ops.GetCount();i++)
    {
      wStackOp *op = page->Ops[i];
      const sBool comment = op->Class && (op->Class->Flags & wCF_COMMENT);
      if((pass==0) == (comment!=0))
        continue;
      DrawBlock(dl,origin,op,comment);
    }
  }

  // Derived connections, as guides. The original draws none, and once you try to
  // draw them you find out why: this is a CONTACT model, so a connection has no
  // length. The two blocks share an edge, and a line from one centre to the
  // other has both endpoints on that same edge — invisible, hidden under the
  // borders. Drawing wires here is a category error.
  //
  // What is worth drawing is the CONTACT PATCH: the span of the shared edge
  // where the two blocks actually overlap, which is precisely the thing that
  // makes the connection exist and precisely the thing a sideways drag destroys.
  // Highlighting it answers "why is this connected, and how much room is there
  // before it stops being connected", which no wire could.
  if(ShowGuides)
  {
    for(sInt i=0;i<page->Ops.GetCount();i++)
    {
      wStackOp *op = page->Ops[i];
      for(sInt k=0;k<op->Inputs.GetCount();k++)
      {
        wStackOp *in = (wStackOp *) op->Inputs[k];
        if(!in) continue;

        const sInt x0 = sMax(op->PosX,in->PosX);
        const sInt x1 = sMin(op->PosX+op->SizeX,in->PosX+in->SizeX);
        if(x1<=x0) continue;        // bypass substitution can leave no overlap

        const float y = float(op->PosY);
        const ImVec2 a2 = CellToScreen(origin,float(x0),y);
        const ImVec2 b2 = CellToScreen(origin,float(x1),y);
        dl->AddLine(ImVec2(a2.x+1.0f,a2.y),ImVec2(b2.x-1.0f,b2.y),
          IM_COL32(120,220,255,200),sMax(2.0f,2.0f*Zoom));
      }
    }
  }

  // The projected destination of a drag, framed. Red when CheckMove refuses it.
  if(Drag==DRAG_MOVE || Drag==DRAG_RESIZE)
  {
    const ImU32 col = DragValid ? IM_COL32(255,220,120,230)
                                : IM_COL32(255,70,70,230);
    for(sInt i=0;i<page->Ops.GetCount();i++)
    {
      wStackOp *op = page->Ops[i];
      if(!op->Select) continue;

      sInt x = op->PosX, y = op->PosY, w = op->SizeX, h = op->SizeY;
      if(Drag==DRAG_MOVE) { x += DragDX; y += DragDY; }
      else { w = sMax(1,w+DragDX); h = sMax(1,h+DragDY); }

      dl->AddRect(CellToScreen(origin,float(x),float(y)),
                  CellToScreen(origin,float(x+w),float(y+h)),col,0.0f,0,2.0f);
    }
  }

  // Rubber band.
  if(Drag==DRAG_RUBBER)
  {
    const ImVec2 p = ImGui::GetIO().MousePos;
    dl->AddRectFilled(ImVec2(DragStartX,DragStartY),p,IM_COL32(255,220,120,30));
    dl->AddRect(ImVec2(DragStartX,DragStartY),p,IM_COL32(255,220,120,160));
  }

  // The insert cursor: an empty 3 x 1 frame, as upstream.
  dl->AddRect(CellToScreen(origin,float(CursorX),float(CursorY)),
              CellToScreen(origin,float(CursorX+3),float(CursorY+1)),
              IM_COL32(120,200,255,180));

  dl->PopClipRect();
  return changed;
}

/****************************************************************************/
