/****************************************************************************/
/***                                                                      ***/
/***   Document-level undo — wz4port editor, stage 5.7                    ***/
/***                                                                      ***/
/****************************************************************************/

#include "undo.hpp"
#include "base/serialize.hpp"
#include "base/system.hpp"

/****************************************************************************/

// How many states to keep. A snapshot is the whole page, so this is the memory
// bound: a 37-operator page is a few KB, and even the largest document in the
// corpus (7,090 operators across all pages) would be a few MB per state. 64 is
// far more history than a texture-editing session needs and cannot surprise
// anyone on memory.
static const sInt UNDO_MAX = 64;

/****************************************************************************/

wUndo::wUndo()
{
  Pos = 0;
}

wUndo::~wUndo()
{
  Clear();
}

void wUndo::Clear()
{
  for(sInt i=0;i<States.GetCount();i++)
    delete States[i];
  States.Clear();
  Pos = 0;
}

sDInt wUndo::Bytes() const
{
  sDInt n = 0;
  for(sInt i=0;i<States.GetCount();i++)
    n += States[i]->Data.GetCount();
  return n;
}

/****************************************************************************/

// The serialise-to-memory idiom is upstream's own, from sSetClipboardObject
// (base/windows.hpp:92): a growable memory file, an sWriter over it, then map the
// bytes back out. Copied rather than called, because the clipboard version hands
// the bytes to the window system and is compiled out on anything but Windows and
// Linux.
sBool wUndo::Capture(wPage *page,wState *into)
{
  if(!page || !into)
    return 0;

  sFile *file = sCreateGrowMemFile();
  if(!file)
    return 0;

  sWriter stream;
  stream.Begin(file);
  page->Serialize(stream);
  const sBool ok = stream.End() && stream.IsOk();

  if(ok)
  {
    const sDInt size = file->GetSize();
    sU8 *data = file->Map(0,size);
    if(data && size>0)
    {
      into->Data.Resize(sInt(size));
      sCopyMem(&into->Data[0],data,size);
    }
    else
    {
      delete file;
      return 0;
    }
  }

  delete file;
  return ok;
}

sBool wUndo::Restore(wPage *page,const wState *from)
{
  if(!page || !from || from->Data.GetCount()==0)
    return 0;

  // The arrays must be EMPTY before reading. wPage::Serialize_ reads its ops
  // through sReader::ArrayNew, which asserts `a.GetCount()==0` and then allocates
  // every element itself (serialize.hpp:196) — it exists to fill a freshly
  // constructed object, which is how the clipboard uses it.
  //
  // Restoring into a live page therefore has to clear first. Found by the
  // assertion firing on the very first undo, which is the right way round: an
  // sVERIFY is a better messenger than a page half-read over another one.
  //
  // Clearing drops the old operators; Altona's collector reclaims them.
  page->Ops.Clear();
  page->Tree.Clear();

  sFile *file = sCreateMemFile((const void *)&from->Data[0],
    from->Data.GetCount(),0);
  if(!file)
    return 0;

  sReader stream;
  stream.Begin(file);
  page->Serialize(stream);
  const sBool ok = stream.End() && stream.IsOk();
  delete file;

  return ok;
}

/****************************************************************************/

void wUndo::Reset(wPage *page)
{
  Clear();
  if(!page)
    return;

  wState *s = new wState;
  s->What = L"";
  if(Capture(page,s))
  {
    States.AddTail(s);
    Pos = 0;
  }
  else
  {
    delete s;
  }
}

// Push records the page as it is NOW. So the sequence is: make the edit, then
// push. That is the opposite of the more common "snapshot before mutating", and
// it is deliberate: with states rather than deltas, the stack is simply a list of
// document versions and the index says which one is live. Undo walks back one,
// redo walks forward one, and there is no asymmetry between them to get wrong.
//
// It does mean Reset must be called once after loading, to lay down the version
// that the first undo returns to.
void wUndo::Push(wPage *page,const sChar *what)
{
  if(!page)
    return;

  if(States.GetCount()==0)
  {
    Reset(page);
    return;
  }

  // Anything ahead of the current position is a future that no longer happened.
  while(States.GetCount()>Pos+1)
  {
    delete States[States.GetCount()-1];
    States.RemTail();
  }

  wState *s = new wState;
  s->What = what ? what : L"edit";
  if(!Capture(page,s))
  {
    delete s;
    return;
  }

  // Label the transition on the state we are leaving as well, so an Undo menu
  // can say what it will undo rather than what it will return to.
  States[Pos]->What = s->What;

  States.AddTail(s);
  Pos = States.GetCount()-1;

  // Drop the oldest history rather than the newest.
  while(States.GetCount()>UNDO_MAX)
  {
    delete States[0];
    States.RemAtOrder(0);
    Pos--;
  }
}

sBool wUndo::Undo(wPage *page)
{
  if(!CanUndo())
    return 0;
  if(!Restore(page,States[Pos-1]))
    return 0;
  Pos--;
  return 1;
}

sBool wUndo::Redo(wPage *page)
{
  if(!CanRedo())
    return 0;
  if(!Restore(page,States[Pos+1]))
    return 0;
  Pos++;
  return 1;
}

const sChar *wUndo::UndoLabel() const
{
  if(!CanUndo())
    return 0;
  return States[Pos-1]->What;
}

const sChar *wUndo::RedoLabel() const
{
  if(!CanRedo())
    return 0;
  return States[Pos]->What;
}

/****************************************************************************/
