/****************************************************************************/
/***                                                                      ***/
/***   The 2D preview — wz4port editor, stage 5.6                         ***/
/***                                                                      ***/
/****************************************************************************/
//
// Evaluates the selected operator and shows the bitmap. This is the stage that
// turns a graph editor into a texture editor: everything before it manipulated a
// document, and nothing showed what the document produced.
//
// Zoom is an integer 0..15 where **8 is 1:1**, and the scale is 2^(zoom-8). That
// is upstream's exact mapping, from `Rect.x1 = Rect.x0 + ((xs<<zoom)>>8)` in
// wPaintInfo::CalcRect (doc.cpp:584) — the `wPaintInfo::Zoom2D` field is
// documented only as "8 = normal size", and the shift is where the rest of the
// meaning lives. Nothing else in the code dump reads it, so this is the whole
// specification.

#ifndef FILE_WZ4PORT_EDITOR_PREVIEW_HPP
#define FILE_WZ4PORT_EDITOR_PREVIEW_HPP

#include "wz4lib/doc_core.hpp"
#include "imgui_wz4.hpp"

class wPreview
{
public:
  wPreview();
  ~wPreview();

  // `revision` is bumped by the caller whenever the document changes. The
  // preview re-evaluates when it, or the operator, differs from last time —
  // which is what makes a parameter edit show up without re-evaluating every
  // frame.
  void Draw(wOp *op,sInt revision);

  void ResetView();

  // One line describing what the pane is currently showing. Printed once by a
  // non-interactive run so that "the preview worked" is an asserted claim rather
  // than only a visible one — the same habit as architecture.md A39.
  void Describe(sString<128> &out) const;

private:
  sU32 Texture;                     // GL name, 0 if none
  sInt TexX,TexY;

  wOp *ShownOp;
  sInt ShownRevision;
  sBool Failed;
  sString<128> Info;

  sInt Zoom;                        // 0..15, 8 = 1:1
  float PanX,PanY;
  bool Tile;

  // How the alpha channel is presented. RGB is the DEFAULT and matches the
  // original: wPaintInfo::PaintTex2D draws through a plain sSimpleMaterial with
  // no blend flags (doc.cpp:129), so Werkkzeug4's preview ignored alpha unless
  // you switched to its alpha view.
  //
  // That default matters for more than nostalgia. Several arithmetic Merge and
  // Color modes destroy alpha as a side effect — `sub` subtracts it to zero —
  // and compositing those honestly makes a perfectly good RGB result look like
  // an empty pane. Ignoring alpha by default shows the work; RGBA is one click
  // away when the question is "what is the alpha doing".
  enum wAlphaMode { AM_RGB, AM_RGBA, AM_ALPHA };
  sInt AlphaMode;
  sInt ShownMode;

  void Release();
  sBool Upload(wOp *op);
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_PREVIEW_HPP
