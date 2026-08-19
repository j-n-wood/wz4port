/****************************************************************************/
/***                                                                      ***/
/***   (C) 2005 Dierk Ohlerich, all rights reserved                       ***/
/***                                                                      ***/
/****************************************************************************/

// Extracted verbatim from gui/listwindow.hpp so that pure data structures
// which happen to live in the gui library can be used by code that does not
// link, or even include, the gui. listwindow.hpp includes this header, so
// its own consumers are unaffected.

#ifndef FILE_GUI_TREEINFO_HPP
#define FILE_GUI_TREEINFO_HPP

#include "base/types.hpp"

/****************************************************************************/

#define sLW_MAXTREENEST   128     // max nesting of trees

template <class Type>             // use ptr type!
struct sListWindowTreeInfo
{
  sInt Level;                     // level of indention, starting with 0
  sInt Flags;                     // 1: show children 0: hide children
  Type Parent;                    // link to parent, if not root (there may be multiple roots)
  Type FirstChild;                // link to first children
  Type NextSibling;               // link to next sibling
  Type *TempPtr;                  // for building single linked list

  sListWindowTreeInfo() { Level=0; Flags=0; Parent=0; FirstChild=0; NextSibling=0; TempPtr=0; }
  void Need() { Parent->Need(); FirstChild->Need(); NextSibling->Need(); }
};
enum sListWindowTreeInfoFlags
{
  sLWTI_CLOSED = 0x0001,
  sLWTI_HIDDEN = 0x0002,
};

/****************************************************************************/

#endif // FILE_GUI_TREEINFO_HPP
