/****************************************************************************/
/***                                                                      ***/
/***   opsmeta — emit operator metadata as JSON from a .ops file          ***/
/***                                                                      ***/
/****************************************************************************/

// Reads a .ops file with wz4ops' own parser (tools/wz4ops/parse.cpp and
// doc.cpp, linked directly — they have no dependency on the C++ emitter) and
// writes the metadata the new editor needs to build parameter panels with no
// per-operator code.
//
// The C++ emitter is NOT linked, and this tool never writes C++. That split is
// deliberate: anything that determines memory layout stays on wz4ops' single
// code path. See docs/architecture.md A11 and wz4port/patches/05.

#ifndef FILE_WZ4PORT_OPSMETA_HPP
#define FILE_WZ4PORT_OPSMETA_HPP

#include "base/types2.hpp"

/****************************************************************************/

// Bump when the emitted shape changes in a way a reader must notice. Readers
// are expected to reject a version they do not know rather than guess.

#define OPSMETA_SCHEMA_VERSION 1

// Serialises the parse tree in the global wz4ops `Doc` to JSON. Returns 0 and
// leaves a message via sPrintF if the document cannot be represented — an
// unresolvable parameter symbol in a conditional, for instance.

sBool wEmitMetaJson(class sTextBuffer &out,const sChar *module);

/****************************************************************************/

#endif // FILE_WZ4PORT_OPSMETA_HPP
