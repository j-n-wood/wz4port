/****************************************************************************/
/***                                                                      ***/
/***   Stage 2.1 gate — the document model is free of the GUI             ***/
/***                                                                      ***/
/****************************************************************************/

// This translation unit includes wz4lib/doc_core.hpp and nothing else, and is
// compiled with wz4port/tests/gui_poison.h force-included ahead of it.
//
// gui_poison.h poisons a handful of identifiers that only the real Altona GUI
// and shader headers declare, so if doc_core.hpp ever reacquires a dependency
// on gui/ or on the generated util/shaders.hpp, this file stops compiling with
// a message naming the offender. That is the whole point of the split and it
// is easy to lose by accident.
//
// Compile-only: it is an OBJECT library, because doc.cpp is not built until
// stage 2.4. Everything below is checked by the compiler, not at runtime.

#include "wz4lib/doc_core.hpp"

/****************************************************************************/

// Every GUI-facing member of wClass is a pointer to an incomplete type. If a
// forward declaration were not enough, none of these would compile. This was
// the main risk flagged for the phase.

typedef void (*wMakeGuiFunc)(wGridFrameHelper &,wOp *);
typedef void (*wHandlesFunc)(wPaintInfo &,wOp *);
typedef void (*wSpecialDragFunc)(const sWindowDrag &,sDInt,wOp *,const sViewport &,wPaintInfo &);
typedef wCustomEditor *(*wCustomEdFunc)(wOp *);

static wMakeGuiFunc     *const MakeGuiSlot     = &((wClass *)0)->MakeGui;
static wHandlesFunc     *const HandlesSlot     = &((wClass *)0)->Handles;
static wSpecialDragFunc *const SpecialDragSlot = &((wClass *)0)->SpecialDrag;
static wCustomEdFunc    *const CustomEdSlot    = &((wClass *)0)->CustomEd;

// wDocument::Show and wType's virtuals take wPaintInfo by reference, which is
// likewise only ever an incomplete type here.

typedef void (wDocument::*wShowMethod)(wObject *,wPaintInfo &);
static const wShowMethod ShowMethod = &wDocument::Show;

// The two records that forced gui/theme.hpp and gui/treeinfo.hpp out of the
// GUI headers are held by value, so their full layout has to be available.

static_assert(sizeof(sGuiTheme) == sizeof(((wEditOptions *)0)->CustomTheme),
  "wEditOptions::CustomTheme needs the complete sGuiTheme");
static_assert(sizeof(sListWindowTreeInfo<wPage *>) == sizeof(((wPage *)0)->TreeInfo),
  "wPage::TreeInfo needs the complete sListWindowTreeInfo");
static_assert(sizeof(sListWindowTreeInfo<wTreeOp *>) == sizeof(((wTreeOp *)0)->TreeInfo),
  "wTreeOp::TreeInfo needs the complete sListWindowTreeInfo");
static_assert(sLW_MAXTREENEST == 128,"sLW_MAXTREENEST must survive the extraction");

// The parts of the model the headless runtime is built on must be complete
// types, not just names.

static_assert(sizeof(wOp) > 0,"wOp incomplete");
static_assert(sizeof(wClass) > 0,"wClass incomplete");
static_assert(sizeof(wType) > 0,"wType incomplete");
static_assert(sizeof(wPage) > 0,"wPage incomplete");
static_assert(sizeof(wStackOp) > 0,"wStackOp incomplete");
static_assert(sizeof(wTreeOp) > 0,"wTreeOp incomplete");
static_assert(sizeof(wDocument) > 0,"wDocument incomplete");
static_assert(sizeof(wCommand) > 0,"wCommand incomplete");
static_assert(sizeof(wExecutive) > 0,"wExecutive incomplete");
static_assert(sizeof(wObject) > 0,"wObject incomplete");
static_assert(sizeof(wDocOptions) > 0,"wDocOptions incomplete");
static_assert(sizeof(wEditOptions) > 0,"wEditOptions incomplete");
static_assert(sizeof(wHitInfo) > 0,"wHitInfo incomplete");
static_assert(sizeof(wHandleSelectTag) > 0,"wHandleSelectTag incomplete");

// sREGOPS is what every generated *_ops.cpp registers itself through, so it
// has to expand in a GUI-free translation unit too.

void wz4portRegisterProbe(sInt i,sBool s)
{
  sREGOPS(basic,s);
}

/****************************************************************************/
