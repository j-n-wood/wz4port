/****************************************************************************/
/***                                                                      ***/
/***   The operator palette — wz4port editor, stage 5.4                   ***/
/***                                                                      ***/
/****************************************************************************/
//
// Built from the LIVE CLASS REGISTRY (Doc->Types, Doc->Classes) rather than from
// the JSON metadata, which is a deliberate departure from
// docs/07-phase-texture-gui.md.
//
// The reason is that a palette's job is to offer what can be inserted, and only
// a registered class can be. Driving it from the registry makes an unofferable
// entry impossible by construction; driving it from metadata would allow the
// palette and the runtime to disagree, which is exactly the class of bug the
// metadata was introduced to avoid elsewhere. The registry also carries what the
// layout needs — wClass::Column, wClass::Shortcut, wClass::TabType and
// wType::ColumnHeaders — so nothing is lost.
//
// The plan's actual point, "no per-operator UI code", holds either way: there is
// none here.

#ifndef FILE_WZ4PORT_EDITOR_PALETTE_HPP
#define FILE_WZ4PORT_EDITOR_PALETTE_HPP

#include "wz4lib/doc_core.hpp"
#include "imgui_wz4.hpp"

class wPalette
{
public:
  wPalette();

  // Draws the palette. Returns the class the user asked to insert, or null.
  // Insertion itself is the caller's, because it needs the page and the cursor.
  wClass *Draw();

  // True if any registered, insertable class matches the current filter — used
  // to say "nothing matches" rather than showing an empty pane.
  sBool AnyVisible() const;

private:
  char Filter[64];
  sInt CurrentTab;                  // index into Doc->Types

  sBool Matches(wClass *cl) const;
  sBool Insertable(wClass *cl) const;
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_PALETTE_HPP
