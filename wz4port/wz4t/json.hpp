/****************************************************************************/
/***                                                                      ***/
/***   A small JSON reader                                                ***/
/***                                                                      ***/
/****************************************************************************/

// Reads the operator metadata that tools/opsmeta emits. Kept general rather
// than shaped to that one schema, because the ImGui editor will have to read
// the same files at runtime and this is the piece it would otherwise duplicate.
//
// Deliberately not built on sScanner: JSON's lexical rules differ from
// Altona's in ways (bare `-`, `e` exponents, `\u` escapes, no comments) that
// cost more to configure around than to write directly.
//
// Values are owned by the wJsonDoc that parsed them and die with it.

#ifndef FILE_WZ4PORT_JSON_HPP
#define FILE_WZ4PORT_JSON_HPP

#include "base/types2.hpp"

/****************************************************************************/

enum wJsonType
{
  wJSON_NULL = 0,
  wJSON_BOOL,
  wJSON_NUMBER,
  wJSON_STRING,
  wJSON_ARRAY,
  wJSON_OBJECT,
};

class wJsonValue;

struct wJsonMember
{
  sPoolString Key;
  wJsonValue *Value;
};

class wJsonValue
{
public:
  wJsonValue();

  sInt Type;                      // wJSON_???
  sBool Bool;
  sF64 Number;                    // integers land here too
  sPoolString String;
  sArray<wJsonValue *> Items;     // wJSON_ARRAY
  sArray<wJsonMember> Members;    // wJSON_OBJECT

  // Object access. All of these tolerate a missing key and a wrong type by
  // returning the default, because metadata fields are optional by design —
  // opsmeta omits what does not apply to a parameter kind.

  const wJsonValue *Member(const sChar *key) const;
  sBool Has(const sChar *key) const           { return Member(key)!=0; }

  sInt GetInt(const sChar *key,sInt def=0) const;
  sF32 GetFloat(const sChar *key,sF32 def=0) const;
  sBool GetBool(const sChar *key,sBool def=0) const;
  sPoolString GetString(const sChar *key,const sChar *def=L"") const;
  const wJsonValue *GetArray(const sChar *key) const;   // 0 if absent

  // Direct access, for array elements.
  sInt AsInt() const                          { return sInt(Number); }
  sF32 AsFloat() const                        { return sF32(Number); }
};

/****************************************************************************/

// Formats a float at the shortest precision that round-trips, via libc.
//
// Altona's sFormatStringBuffer has no %g at all — an unknown format character
// falls through to PrintInt (base/types.cpp:2997), so "%g" silently prints the
// integer part and 0.125 comes out as "0". Its %f is also not correctly rounded
// past a few digits; see docs/architecture.md A18, which is why opsmeta's
// writer routes around it too.
//
// Anything in this port that shows a number to a human should use this.

void wFormatFloat(sChar *buffer,sInt size,sF32 value);

/****************************************************************************/

class wJsonDoc
{
  sArray<wJsonValue *> All;       // everything allocated, for teardown

  const sChar *Scan;
  const sChar *Start;
  sInt Line;
  sString<256> ErrorMsg;
  sBool Failed;

  void Error(const sChar *what);
  void Space();
  wJsonValue *New(sInt type);
  wJsonValue *Value();
  sBool String(sPoolString &out);

public:
  wJsonDoc();
  ~wJsonDoc();

  // Parses in place; `text` must outlive the call but not the document.
  // Returns 0 on failure, with Error() describing where.
  wJsonValue *Parse(const sChar *text);

  wJsonValue *Root;
  const sChar *GetError() const                { return ErrorMsg; }

  // Convenience: read a whole file and parse it.
  wJsonValue *Load(const sChar *filename);

private:
  sChar *FileText;                // owned when Load() was used
};

/****************************************************************************/

#endif // FILE_WZ4PORT_JSON_HPP
