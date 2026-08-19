/****************************************************************************/
/***                                                                      ***/
/***   (C) 2005 Dierk Ohlerich, all rights reserved                       ***/
/***                                                                      ***/
/****************************************************************************/

// Extracted verbatim from gui/manager.hpp so that the theme record, which is
// plain serializable data, can be used by code that does not link, or even
// include, the gui. manager.hpp includes this header, so its own consumers
// are unaffected.

#ifndef FILE_GUI_THEME_HPP
#define FILE_GUI_THEME_HPP

#include "base/types2.hpp"

/****************************************************************************/
/***                                                                      ***/
/***   Theme support                                                      ***/
/***                                                                      ***/
/****************************************************************************/

struct sGuiTheme
{
  sU32 BackColor;
  sU32 DocColor;
  sU32 ButtonColor;
  sU32 TextColor;
  sU32 DrawColor;
  sU32 SelectColor;
  sU32 HighColor;
  sU32 LowColor;
  sU32 HighColor2;
  sU32 LowColor2;

  sString<64> PropFont;
  sString<64> FixedFont;

  template <class streamer> void Serialize_(streamer &stream);
  void Serialize(sWriter &s);
  void Serialize(sReader &s);

  void Tint(sU32 add,sU32 sub);
};

extern const sGuiTheme sGuiThemeDefault;
extern const sGuiTheme sGuiThemeDarker;

/****************************************************************************/

#endif // FILE_GUI_THEME_HPP
