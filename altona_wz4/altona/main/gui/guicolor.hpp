/*+**************************************************************************/
/***                                                                      ***/
/***   This file is distributed under a BSD license.                      ***/
/***   See LICENSE.txt for details.                                       ***/
/***                                                                      ***/
/**************************************************************************+*/

/****************************************************************************/
/***                                                                      ***/
/***   (C) 2005 Dierk Ohlerich, all rights reserved                       ***/
/***                                                                      ***/
/****************************************************************************/

// wz4port: extracted verbatim from gui/window.hpp, which still includes this
// header so there is exactly one definition of the enum.
//
// The reason for the extraction is GenBitmap::Text (wz4frlib/wz3_bitmap_code.cpp
// :2505). It names sGC_BLACK and sGC_MAX to drive Altona's 2D software drawing
// layer, and that layer lives in base/windows.hpp — no GUI needed. But the
// colour indices it takes were declared in gui/window.hpp, alongside sWindow,
// so a headless build could not name them without pulling the whole widget
// toolkit in.
//
// Same shape as gui/theme.hpp and gui/palette.hpp from phase 2: a GUI-free
// declaration that headless code legitimately needs, moved to its own header
// rather than duplicated. Duplicating it would be worse than a patch here —
// phase 5 puts a GUI on the texture library, so both definitions would land in
// one translation unit and collide.
//
// See wz4port/patches/09-guicolor-header.md.

#ifndef HEADER_ALTONA_GUI_GUICOLOR
#define HEADER_ALTONA_GUI_GUICOLOR

#ifndef __GNUC__
#pragma once
#endif

/****************************************************************************/

enum sGuiColor
{
  sGC_BACK = 1,                   // standard background color
  sGC_DOC,                        // document background color (brighter)
  sGC_BUTTON,                     // button background color (darker)
  sGC_TEXT,                       // text color
  sGC_DRAW,                       // color for drawing, usually black
  sGC_SELECT,                     // selected text
  sGC_HIGH,                       // high edge, outer
  sGC_LOW,                        // low edge, outer
  sGC_HIGH2,                      // hight edge, inner
  sGC_LOW2,                       // low edge, inner

  sGC_RED,                        // the color, with contrast to sGC_TEXT and sGC_DRAW
  sGC_YELLOW,                     // the color, with contrast to sGC_TEXT and sGC_DRAW
  sGC_GREEN,                      // the color, with contrast to sGC_TEXT and sGC_DRAW
  sGC_BLUE,                       // the color, with contrast to sGC_TEXT and sGC_DRAW
  sGC_BLACK,                      // 0x000000
  sGC_WHITE,                      // 0xffffff
  sGC_DARKGRAY,                   // 0x404040
  sGC_GRAY,                       // 0x808080
  sGC_LTGRAY,                     // 0xc0c0c0
  sGC_PINK,                       // better contrast than red to sGC_TEXT

  sGC_MAX,
};

/****************************************************************************/

#endif  // HEADER_ALTONA_GUI_GUICOLOR
