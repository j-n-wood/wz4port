/****************************************************************************/
/***                                                                      ***/
/***   The parameter panel — wz4port editor, stage 5.5                    ***/
/***                                                                      ***/
/****************************************************************************/
//
// Generated entirely from the phase-2 metadata. There is no per-operator UI code
// anywhere in this file, and that is the whole point: 34 texture operators today
// and 47 mesh operators later cost nothing extra, because the panel is a function
// of the schema rather than of the operator.
//
// This is the payoff for opsmeta existing. wClass knows how many parameter words
// an operator has and nothing about what they mean; that description lived only
// inside the generated MakeGui, which the headless build omits. The metadata is
// the only surviving description, and this is what it was for.

#ifndef FILE_WZ4PORT_EDITOR_PARAMS_HPP
#define FILE_WZ4PORT_EDITOR_PARAMS_HPP

#include "wz4lib/doc_core.hpp"
#include "meta.hpp"
#include "imgui_wz4.hpp"

// What an edit requires of the caller. These mirror the original's four-level
// change contract (docs/01-existing-model.md §5.1): a value change drops caches
// and dirties the document, a link or name change has to reconnect first, and
// some changes are both.
enum wParamChange
{
  wPC_NONE      = 0,
  wPC_VALUE     = 1,                // ChangeMsg: drop caches, mark dirty
  wPC_CONNECT   = 2,                // ConnectMsg: Connect() before the change
};

// Draws the whole panel for one operator. Returns a mask of wParamChange.
sInt wDrawParams(wOp *op,const wMetaClass *mc);

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_PARAMS_HPP
