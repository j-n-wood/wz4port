/****************************************************************************/
/***                                                                      ***/
/***   Operator metadata, at runtime                                      ***/
/***                                                                      ***/
/****************************************************************************/

// wClass knows how many parameter words and strings an operator needs, but
// nothing about the parameters themselves — no names, no kinds, no offsets.
// That description only ever existed inside the generated MakeGui, which the
// headless build omits (wz4port/patches/05).
//
// So anything that has to turn "Size = 256,256" into words at an offset needs
// the metadata that tools/opsmeta emits. It is not a convenience for the text
// format; it is the only description of a parameter that survives headless.
//
// Schema contract: docs/02-target-model.md §6, schemaVersion 1.

#ifndef FILE_WZ4PORT_META_HPP
#define FILE_WZ4PORT_META_HPP

#include "base/types2.hpp"

/****************************************************************************/

// Which of the three independent offset spaces a parameter lives in. See
// docs/architecture.md A20 — word 0, string 0 and link 0 all exist in the same
// operator, so an offset alone is ambiguous.

enum wMetaSpace
{
  wMS_NONE = 0,                   // label, group: no storage
  wMS_WORDS,                      // the Para struct / wOp::EditData
  wMS_STRINGS,                    // wOp::EditString[n]
  wMS_LINKS,                      // wOp::Links[n]
};

enum wMetaLayout
{
  wML_SCALAR = 0,
  wML_VECTOR,                     // float2/30/31/4, addressed .x .y .z .w
  wML_ARRAY,                      // float X[n] / int X[n]
};

struct wMetaChoice
{
  sPoolString Label;
  sInt Value;
};

// One control of a flags/radio/strobe parameter. Several of these can share a
// single integer, at different shifts.
//
// NOTE on why wMetaWidget and wMetaParam are always heap-allocated below:
// Altona's sArray::AddMany hands back RAW MEMORY without running constructors,
// and AddManyInit is no better — it assigns Type() into that raw memory, which
// for an element owning storage means operator= reading uninitialised pointers.
// Any element type with an sArray inside it therefore has to be held by
// pointer. Getting this wrong is a segfault, not a warning.
struct wMetaWidget
{
  sInt Shift;
  sInt Mask;
  sArray<wMetaChoice> Choices;    // wMetaChoice owns nothing, so this is fine

  wMetaWidget()                   { Shift = 0; Mask = 0; }
};

struct wMetaParam
{
  sPoolString Kind;               // "float", "flags", "color", ...
  sPoolString Symbol;
  sPoolString Label;

  sInt Space;                     // wMS_???
  sInt Offset;
  sInt Words;                     // actual word consumption; char[n] is (n+1)/2
  sInt Layout;                    // wML_???
  sInt Count;

  sBool Continues;                // another widget on an already-declared word
  sBool RebuildOnChange;

  sF32 Min,Max,Step,RStep;
  sBool LogStep;

  sArray<sF32> DefaultsF;
  sArray<sInt> DefaultsI;
  sPoolString DefaultString;

  sPoolString Channels;           // color: "rgb", "rgba"
  sPoolString Options;            // the raw choice string, for presentation
  sArray<wMetaWidget *> Widgets;  // by pointer — see the note above

  sInt Capacity;                  // char[n]
  sInt Lines;                     // string, custom

  wMetaParam();
  ~wMetaParam();
};

// The table widget: an operator can carry a repeating row of parameters in
// their own word space, addressed through wOp::ArrayData rather than EditData.
struct wMetaArray
{
  sInt HideThreshold;             // auto-collapse past this many rows
  sBool Grouped;
  sBool Numbered;
  sInt DefaultMode;               // 0 auto, 1 all, 2 hide, 3 group
  sArray<wMetaParam *> Params;    // one row's worth

  wMetaArray();
  ~wMetaArray();
};

struct wMetaClass
{
  sPoolString Name;
  sPoolString OutputType;
  sPoolString TabType;
  sInt Column;
  sInt ParaWords;
  sInt ParaStrings;
  sInt ArrayWords;                // words per array row, 0 if no array
  sBool HasCode;
  sArray<sPoolString> Flags;
  sArray<wMetaParam *> Params;    // by pointer — see the note above
  wMetaArray *Array;              // 0 if this operator has no table

  wMetaClass();
  ~wMetaClass();

  const wMetaParam *FindParam(const sChar *symbol) const;
};

/****************************************************************************/

class wMetaLibrary
{
  sArray<wMetaClass *> Classes;
  sString<256> ErrorMsg;

  sBool LoadModule(const sChar *filename);

public:
  wMetaLibrary();
  ~wMetaLibrary();

  // Loads every *.json directly inside `dir`, and inside its immediate
  // subdirectories, so that build/meta (which opsmeta fills as
  // meta/<subdir>/<module>.json) can be pointed at as a whole.
  sBool LoadDirectory(const sChar *dir);

  sInt GetClassCount() const                  { return Classes.GetCount(); }
  const wMetaClass *GetClass(sInt i) const    { return Classes[i]; }

  // By class name alone, or qualified as "OutputType.ClassName" — which is how
  // .wz4 identifies operators and therefore how .wz4t does too.
  const wMetaClass *Find(const sChar *name) const;
  const wMetaClass *Find(const sChar *outputtype,const sChar *name) const;

  const sChar *GetError() const               { return ErrorMsg; }
};

/****************************************************************************/

#endif // FILE_WZ4PORT_META_HPP
