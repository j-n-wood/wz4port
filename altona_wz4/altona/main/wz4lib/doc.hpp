/*+**************************************************************************/
/***                                                                      ***/
/***   Copyright (C) by Dierk Ohlerich                                    ***/
/***   all rights reserverd                                               ***/
/***                                                                      ***/
/***   To license this software, please contact the copyright holder.     ***/
/***                                                                      ***/
/**************************************************************************+*/

// This header was split in two so that the document model can be used without
// the gui library and without the generated shader library:
//
//   doc_core.hpp  the document, page, operator, class and type model, the
//                 command list and the executive. No gui, no shaders.
//   doc_gui.hpp   wPaintInfo, wHandle, wGridFrameHelper, wCustomEditor.
//
// Including doc.hpp gives you both, exactly as before.

#ifndef FILE_WERKKZEUG4_DOC_HPP
#define FILE_WERKKZEUG4_DOC_HPP

#ifndef __GNUC__
#pragma once
#endif

#include "wz4lib/doc_core.hpp"
#include "wz4lib/doc_gui.hpp"

/****************************************************************************/

#endif // FILE_WERKKZEUG4_DOC_HPP

