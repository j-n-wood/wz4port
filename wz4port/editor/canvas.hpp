/****************************************************************************/
/***                                                                      ***/
/***   The stacking canvas — wz4port editor, stage 5.2                    ***/
/***                                                                      ***/
/****************************************************************************/
//
// Werkkzeug's canvas is a grid of blocks with no wires: an operator's inputs are
// the blocks touching it from above, ordered left to right. So the canvas is not
// a view of the graph, it IS the graph, and dragging a block one cell is a
// structural edit.
//
// Drawn with ImGui DrawList calls over a 24 x 16 px cell, per
// docs/07-phase-texture-gui.md. A node-editor library was rejected in that plan:
// their data model is links-first, and there are no links here to give them.
//
// Collision is NOT reimplemented. wPage::CheckDest and wPage::CheckMove are
// compiled into the headless library and are called directly, so the editor
// cannot drift from the rule the document format enforces. They read wOp::Select,
// which is therefore the selection state — the editor keeps no parallel set.

#ifndef FILE_WZ4PORT_EDITOR_CANVAS_HPP
#define FILE_WZ4PORT_EDITOR_CANVAS_HPP

#include "wz4lib/doc_core.hpp"

// Through the wrapper, never <imgui.h> directly: Altona macro-defines `new`.
#include "imgui_wz4.hpp"

class wCanvas
{
public:
  wCanvas();

  // Draws the page and handles interaction. Returns 1 if the document was
  // structurally changed, so the caller can reconnect and invalidate.
  sBool Draw(wPage *page);

  // Selection, as the rest of the editor sees it. Null when the selection is
  // empty or has more than one member.
  wOp *SingleSelection(wPage *page) const;

  void ResetView();

  // Frames every block on the page. Called when a document loads, because
  // landing at 1:1 on the top-left corner of a 192 x 128 cell page shows a
  // fraction of a wide graph and gives no hint that the rest exists.
  void FitPage(wPage *page,ImVec2 size);

  // Grid cursor: where an inserted operator lands. Stage 5.4 uses it; it is
  // drawn and moved here because clicking the canvas is what sets it.
  sInt CursorX;
  sInt CursorY;

  sBool ShowGuides;                 // draw derived connections (off by default)

  // Set to have the next Draw() fit the page. Deferred rather than done
  // immediately because fitting needs the pane size, which is only known inside
  // Draw — and a document can be loaded from the menu, before any frame.
  sBool FitPending;

private:
  float Zoom;
  float PanX;                       // pixels, top-left of the view in page space
  float PanY;

  // Drag state. Mode mirrors the original's single DragMove(dd,mode) handler.
  enum wDragMode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE, DRAG_RUBBER, DRAG_PAN };
  wDragMode Drag;
  float DragStartX,DragStartY;      // screen pixels
  sInt DragDX,DragDY;               // current delta, in cells
  sBool DragValid;                  // does CheckMove allow the current delta?

  void DrawGrid(ImDrawList *dl,ImVec2 origin,ImVec2 size);
  void DrawBlock(ImDrawList *dl,ImVec2 origin,wStackOp *op,sBool comment);
  sBool HandleInput(wPage *page,ImVec2 origin,ImVec2 size);

  ImVec2 CellToScreen(ImVec2 origin,float cx,float cy) const;
  void ScreenToCell(ImVec2 origin,ImVec2 p,sInt &cx,sInt &cy) const;
  wStackOp *OpAt(wPage *page,sInt cx,sInt cy) const;
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_CANVAS_HPP
