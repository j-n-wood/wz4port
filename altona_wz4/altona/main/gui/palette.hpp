/****************************************************************************/
/***                                                                      ***/
/***   (C) 2005 Dierk Ohlerich, all rights reserved                       ***/
/***                                                                      ***/
/****************************************************************************/

// The colour picker's 32 palette swatches, extracted from gui/color.cpp.
//
// They are part of the wz4 document format — wDocOptions::Serialize_ reads and
// writes them at version 12 and up — so the document model has to reach them
// without linking the gui. sColorPickerWindow::PaletteColors is now a reference
// to this array, so every existing use of it still works.

#ifndef FILE_GUI_PALETTE_HPP
#define FILE_GUI_PALETTE_HPP

#include "base/types.hpp"

/****************************************************************************/

extern sF32 sGuiPaletteColors[32][4];

/****************************************************************************/

#endif // FILE_GUI_PALETTE_HPP
