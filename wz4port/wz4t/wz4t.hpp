/****************************************************************************/
/***                                                                      ***/
/***   The .wz4t text graph format                                        ***/
/***                                                                      ***/
/****************************************************************************/

// Grammar and rationale: docs/02-target-model.md §4.
//
// The one thing to understand before reading the parser: CONNECTIONS ARE NEVER
// WRITTEN DOWN. A .wz4t file states only where each operator sits on the grid,
// and wDocument::Connect() derives the graph from that geometry. The format
// exercises the rule rather than working around it.

#ifndef FILE_WZ4PORT_WZ4T_HPP
#define FILE_WZ4PORT_WZ4T_HPP

#include "base/types2.hpp"
#include "meta.hpp"

/****************************************************************************/

enum wWz4tReadFlags
{
  // Accept an operator whose class is not registered, substituting the
  // UnknownOp placeholder and remembering the original name — exactly what
  // Altona's .wz4 reader does (doc.cpp:1854, wz4port/patches/07).
  //
  // OFF by default, and that default is the important one. In a hand-written
  // test case an unknown class is a typo, and accepting it would let the case
  // pass while testing nothing. It is switched ON only when converting a
  // document, where the unknown classes are real operators from subsystems this
  // port does not build.
  wWZ4T_ALLOWUNKNOWN = 0x0001,
};

// Reads `filename` into the global Doc, which must already exist and have its
// operator modules registered. Parameter names and value kinds are validated
// against `meta`; a name that is not a parameter of that class is an error, not
// a silently ignored line.
//
// Returns 0 on any error. Diagnostics go to stdout with file and line, because
// this is a format humans write by hand.
//
// Does NOT call Doc->Connect(); the caller decides when, so it can inspect the
// pre-connection state if it wants to.

sBool wReadWz4t(const sChar *filename,const wMetaLibrary &meta,sInt flags=0);

// Same, from memory. `sourcename` only appears in messages.
sBool wReadWz4tText(const sChar *text,const sChar *sourcename,
  const wMetaLibrary &meta,sInt flags=0);

/****************************************************************************/

// Writes the global Doc as canonical .wz4t: explicit `at` and `size` on every
// operator, no sugar, pages in document order and operators sorted by PosY then
// PosX so the output is diff-stable.
//
// Only parameters that differ from their default are written. That is safe
// because the reader runs the operator's SetDefaults before applying settings,
// and because SetDefaults and the metadata defaults are emitted from the same
// parse tree by wz4ops and opsmeta respectively — they agree by construction.
// It is also what keeps a per-operator test case short enough to read.

sBool wWriteWz4t(sTextBuffer &out,const wMetaLibrary &meta);
sBool wWriteWz4tFile(const sChar *filename,const wMetaLibrary &meta);

/****************************************************************************/

#endif // FILE_WZ4PORT_WZ4T_HPP
