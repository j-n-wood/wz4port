/****************************************************************************/
/***                                                                      ***/
/***   Document-level undo — wz4port editor, stage 5.7                    ***/
/***                                                                      ***/
/****************************************************************************/
//
// The original has per-operator, single-level, parameter-panel-only undo, and no
// undo at all for insert, delete, move, resize or paste. The reference calls that
// "a genuine gap, not a subtlety to preserve"
// (docs/01-existing-model.md §5.5). This is the one place the port sets out to be
// better rather than faithful.
//
// SNAPSHOTS, NOT COMMANDS. docs/07-phase-texture-gui.md proposed a
// command-pattern stack with an inverse per operation. That was rejected on
// contact with the code, because upstream already has a whole-page serialiser:
// wPage::Serialize covers the ops array, and through wOp::Serialize each
// operator's parameter words, strings, link names and array rows — it exists for
// the clipboard, so it is code that already works and is already exercised.
//
// An inverse per command is a correctness surface that grows with every editing
// feature and is wrong in exactly the cases nobody tests. A snapshot cannot be
// wrong about what it captured. The cost is memory, which is bounded here and
// trivial at the sizes this editor deals with.
//
// No ImGui in this file, deliberately: it compiles into the headless test.

#ifndef FILE_WZ4PORT_EDITOR_UNDO_HPP
#define FILE_WZ4PORT_EDITOR_UNDO_HPP

#include "wz4lib/doc_core.hpp"

class wUndo
{
public:
  wUndo();
  ~wUndo();

  // Discards everything and takes the baseline snapshot. Call after loading.
  void Reset(wPage *page);

  // Records the page as it is NOW, as a new state. Called after a change has
  // been made, not before — see the note on ordering in the .cpp.
  void Push(wPage *page,const sChar *what);

  sBool CanUndo() const             { return Pos>0; }
  sBool CanRedo() const             { return Pos+1<States.GetCount(); }

  // Both restore into the EXISTING page object, so pointers to the page stay
  // valid. Pointers to the OPERATORS do not: every wStackOp is replaced. The
  // caller must drop any operator pointer it holds — selection included.
  sBool Undo(wPage *page);
  sBool Redo(wPage *page);

  const sChar *UndoLabel() const;
  const sChar *RedoLabel() const;

  sInt Depth() const                { return States.GetCount(); }
  sInt Position() const             { return Pos; }
  sDInt Bytes() const;

private:
  struct wState
  {
    sArray<sU8> Data;
    sString<64> What;               // what the edit AFTER this state was
  };

  sArray<wState *> States;
  sInt Pos;                         // index of the state currently in the page

  void Clear();
  sBool Capture(wPage *page,wState *into);
  sBool Restore(wPage *page,const wState *from);
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_UNDO_HPP
