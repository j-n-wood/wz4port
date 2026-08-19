/****************************************************************************/
/***                                                                      ***/
/***   (C) 2005 Dierk Ohlerich, all rights reserved                       ***/
/***                                                                      ***/
/****************************************************************************/

// Moved verbatim from gui/manager.cpp, to go with the declarations already in
// gui/theme.hpp.
//
// sGuiTheme is part of the wz4 document format: wEditOptions holds one by value
// and serialises it. So the document model needs both the layout (theme.hpp,
// extracted in wz4port/patches/04) and these definitions, without linking the
// gui manager — which pulls in the whole widget toolkit and a window system.

#include "gui/theme.hpp"
#include "base/serialize.hpp"

/****************************************************************************/

const sGuiTheme sGuiThemeDefault =
{
  0xe4e4e4, // back
  0xffffff, // doc
  0xd4d4cc, // button
  0x000000, // text
  0x404040, // draw
  0xb0b0b8, // select
  0xf2f2f2, // high
  0xc0c0c0, // low
  0xfafafa, // high2
  0x808080, // low2
  L"Arial", // prop
  L"Courier New", // fixed
};

const sGuiTheme sGuiThemeDarker =
{
  0xc0c0c0, // back
  0xd0d0d0, // doc
  0xb0b0b0, // button
  0x000000, // text
  0x000000, // draw
  0xff8080, // select
  0xe0e0e0, // high
  0x606060, // low
  0xffffff, // high2
  0x000000, // low2
  L"Arial", // prop
  L"Courier New", // fixed
};

template <class streamer> void sGuiTheme::Serialize_(streamer &s)
{
  sInt version=s.Header(sSerId::sGuiTheme,1);
  sVERIFY(version>0);

  s | BackColor | DocColor | ButtonColor | TextColor | DrawColor;
  s | SelectColor | HighColor | LowColor | HighColor2 | LowColor2;
  s | PropFont | FixedFont;

  s.Footer();
}

void sGuiTheme::Serialize(sReader &s) { Serialize_(s); }
void sGuiTheme::Serialize(sWriter &s) { Serialize_(s); }

void sGuiTheme::Tint(sU32 add,sU32 sub)
{
  sU32 addh = sScaleColorFast(add,0x80);
  sU32 subh = sScaleColorFast(sub,0x80);

  BackColor   = sAddColor(BackColor  ,add);
  ButtonColor = sAddColor(ButtonColor,add);
  SelectColor = sAddColor(SelectColor,add);
  HighColor   = sAddColor(HighColor  ,addh);
  LowColor    = sAddColor(LowColor   ,addh);
  HighColor2  = sAddColor(HighColor2 ,addh);
  LowColor2   = sAddColor(LowColor2  ,addh);

  BackColor   = sSubColor(BackColor  ,sub);
  ButtonColor = sSubColor(ButtonColor,sub);
  SelectColor = sSubColor(SelectColor,sub);
  HighColor   = sSubColor(HighColor  ,subh);
  LowColor    = sSubColor(LowColor   ,subh);
  HighColor2  = sSubColor(HighColor2 ,subh);
  LowColor2   = sSubColor(LowColor2  ,subh);
}

/****************************************************************************/
