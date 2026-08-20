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

sBool wReadWz4t(const sChar *filename,const wMetaLibrary &meta);

// Same, from memory. `sourcename` only appears in messages.
sBool wReadWz4tText(const sChar *text,const sChar *sourcename,
  const wMetaLibrary &meta);

/****************************************************************************/

#endif // FILE_WZ4PORT_WZ4T_HPP
