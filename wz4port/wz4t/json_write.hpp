/****************************************************************************/
/***                                                                      ***/
/***   A very small JSON writer                                           ***/
/***                                                                      ***/
/****************************************************************************/

// Deliberately minimal, and deliberately deterministic: fixed two-space
// indent, keys in the order the caller emits them, no timestamps and no
// absolute paths. The output is a review artefact, so it has to diff cleanly
// when a .ops file changes by one line.
//
// Moved here from tools/opsmeta/ in phase 8, unchanged. It now has two callers —
// opsmeta's metadata and geo/gltf_write.cpp — and it belongs beside json.hpp,
// the reader, because together they are the reason this port needs no JSON
// dependency: we write with one and the tests read back with the other, and the
// two share no code. That is the same relationship that makes tests/mesh_obj.cpp
// a real test of SaveOBJ rather than a self-consistent one.
//
// `opsmeta` compiles this file by path rather than linking wz4t, so the move
// cost it one include line. The proof that it changed nothing is that the
// generated metadata is byte-identical: see docs/10-phase-gltf.md 8.1.
//
// All output is pure ASCII — characters outside 0x20..0x7e are escaped as
// \uXXXX. That keeps the file readable regardless of what encoding the .ops
// source used, which matters in this tree (see wz4port/patches/03).

#ifndef FILE_WZ4PORT_JSON_WRITE_HPP
#define FILE_WZ4PORT_JSON_WRITE_HPP

#include "base/types2.hpp"

/****************************************************************************/

class wJsonWriter
{
  sTextBuffer *Out;

  // One entry per open object/array, holding "nothing emitted here yet".
  //
  // This started as a fixed array of 16 and crashed on the second .ops file it
  // was pointed at: condition trees nest as deeply as the source nests its
  // if() blocks, and each expression node costs two levels. There is no
  // defensible constant, so there isn't one.
  sArray<sInt> Empty;

  void Indent();
  void Comma();                   // separator before the next member
  void Key(const sChar *key);     // "key": , or nothing for array elements
  void Escaped(const sChar *s);

public:
  wJsonWriter(sTextBuffer &out);

  void BeginObject(const sChar *key=0);
  void EndObject();
  void BeginArray(const sChar *key=0);
  void EndArray();

  void Str(const sChar *key,const sChar *value);
  void Int(const sChar *key,sInt value);
  void Float(const sChar *key,sF32 value);
  void Bool(const sChar *key,sBool value);
  void HexStr(const sChar *key,sU32 value);   // emitted as "0x0123abcd"

  // array elements
  void Str(const sChar *value)   { Str((const sChar *)0,value); }
  void Int(sInt value)           { Int((const sChar *)0,value); }
  void Float(sF32 value)         { Float((const sChar *)0,value); }

  void Finish();                  // trailing newline
};

/****************************************************************************/

#endif // FILE_WZ4PORT_JSON_WRITE_HPP
