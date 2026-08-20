/****************************************************************************/
/***                                                                      ***/
/***   Document edits, with no UI attached — wz4port editor                ***/
/***                                                                      ***/
/****************************************************************************/
//
// Insert and delete, factored out of the editor so that a test can drive the
// SAME code the palette drives. The alternative was to copy the twenty lines
// into the test, which would have tested a copy — and a copy is exactly the
// thing that drifts.
//
// Same reasoning as the canvas calling wPage::CheckMove rather than
// reimplementing it: one implementation, exercised from both sides.
//
// No ImGui here, deliberately: this compiles into the headless test.
//
// Named docedit rather than the obvious edit_ops, because .gitignore excludes
// `*_ops.cpp` and `*_ops.hpp` — those are the files wz4ops and opsmeta generate,
// in several directories. `edit_ops.cpp` matched, so `git add -A` silently
// dropped it: the build kept working locally off the untracked file on disk,
// and the commit would have been broken for everyone else. Renaming is better
// than a negation rule, because a filename that trips a broad ignore pattern is
// a trap for the next person too.

#ifndef FILE_WZ4PORT_EDITOR_DOCEDIT_HPP
#define FILE_WZ4PORT_EDITOR_DOCEDIT_HPP

#include "wz4lib/doc_core.hpp"

// Places a class at a grid position, following the original (gui.cpp:5165):
// 3 x 1 if CheckDest allows, class defaults applied, selected, nothing displaced
// if it does not fit. Returns the new operator, or null if there was no room.
//
// Does NOT reconnect or advance a cursor — the caller owns both, because the
// editor batches one Connect() per frame and the cursor is a view concept.
wStackOp *wInsertOp(wPage *page,wClass *cl,sInt x,sInt y);

// Removes every selected operator from the page. Returns how many went.
sInt wDeleteSelection(wPage *page);

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_DOCEDIT_HPP
