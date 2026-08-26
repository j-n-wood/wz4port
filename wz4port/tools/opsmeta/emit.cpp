/****************************************************************************/
/***                                                                      ***/
/***   opsmeta — turn wz4ops' parse tree into metadata JSON               ***/
/***                                                                      ***/
/****************************************************************************/

#include "doc.hpp"                // tools/wz4ops — the parse tree
#include "base/system.hpp"
#include "opsmeta.hpp"
#include "json_write.hpp"

/****************************************************************************/
/***                                                                      ***/
/***   Name tables                                                        ***/
/***                                                                      ***/
/****************************************************************************/

// Parameter kinds. Index is DocTypes, which starts at 1.

static const sChar *ParaKindName(sInt type)
{
  switch(type)
  {
  case TYPE_LABEL:      return L"label";
  case TYPE_INT:        return L"int";
  case TYPE_FLOAT:      return L"float";
  case TYPE_COLOR:      return L"color";
  case TYPE_FLAGS:      return L"flags";
  case TYPE_RADIO:      return L"radio";
  case TYPE_STRING:     return L"string";
  case TYPE_FILEIN:     return L"filein";
  case TYPE_FILEOUT:    return L"fileout";
  case TYPE_LINK:       return L"link";
  case TYPE_GROUP:      return L"group";
  case TYPE_STROBE:     return L"strobe";
  case TYPE_ACTION:     return L"action";
  case TYPE_CUSTOM:     return L"custom";
  case TYPE_BITMASK:    return L"bitmask";
  case TYPE_CHARARRAY:  return L"char";
  default:              return L"unknown";
  }
}

// Which of the three independent offset spaces a parameter lives in. This is
// the trap the schema sketch in docs/02-target-model.md §6 did not show: word
// offset 0, string offset 0 and link offset 0 all exist in the same operator,
// because parse.cpp advances three separate counters.

static const sChar *ParaSpaceName(const Parameter *para)
{
  switch(para->Type)
  {
  case TYPE_STRING:
  case TYPE_FILEIN:
  case TYPE_FILEOUT:
    return L"strings";

  case TYPE_LINK:
    return L"links";

  default:
    // label, group, strobe and action carry no storage at all, which the
    // parser signals by leaving CType empty.
    return para->CType.IsEmpty() ? L"none" : L"words";
  }
}

// How many 32-bit words the parameter occupies. Two special cases:
//
//   char[n]           parse.cpp:684 advances by (n+1)/2, not by Count.
//   continue flags    declares another widget on a variable already declared,
//                     so it allocates nothing at all.

static sInt ParaWordCount(const Parameter *para)
{
  if(para->ContinueFlag)
    return 0;
  if(sCmpString(ParaSpaceName(para),L"words")!=0)
    return 0;
  if(para->Type==TYPE_CHARARRAY)
    return (sInt(para->Max)+1)/2;
  return para->Count;
}

// Count means two different things in the parse tree. XYZW types are one
// struct field addressed .x .y .z .w; float X[n] / int X[n] are C arrays.

static const sChar *ParaLayoutName(const Parameter *para)
{
  if(para->XYZW)
    return L"vector";
  if(para->Count>1)
    return L"array";
  return L"scalar";
}

// Class flags, in the bit order _Choice assigns them from the option string at
// parse.cpp:380-382. Bit 0 is set by a '*' on the last input rather than by a
// keyword. Bits 4 and 5 have DSL names but no wCF_ constant in doc_core.hpp —
// they are accepted by the parser and ignored by the runtime.

static const sChar *ClassFlagNames[] =
{
  L"varargs",                     // 0x00000001, from '*'
  0,                              // 0x00000002, unnamed in the DSL
  L"load",                        // 0x00000004
  L"store",                       // 0x00000008
  L"delete_import",               // 0x00000010, no wCF_ constant
  L"delete_array_import",         // 0x00000020, no wCF_ constant
  L"hide",                        // 0x00000040
  L"conversion",                  // 0x00000080
  L"logging",                     // 0x00000100
  L"slow",                        // 0x00000200
  L"blockhandles",                // 0x00000400
  L"passinput",                   // 0x00000800
  L"passoutput",                  // 0x00001000
  L"curve",                       // 0x00002000
  L"clip",                        // 0x00004000
  L"obsolete",                    // 0x00008000
  L"verticalresize",              // 0x00010000
  L"comment",                     // 0x00020000
  L"call",                        // 0x00040000
  L"input",                       // 0x00080000
  L"loop",                        // 0x00100000
  L"endloop",                     // 0x00200000
  L"shellswitch",                 // 0x00400000
  L"typefrominput",               // 0x00800000
  L"blockchange",                 // 0x01000000
};

static const sChar *TypeFlagNames[]   = { L"notab", L"render3d", L"uncache" };
static const sChar *TypeGuiNames[]    = { L"base2d", L"base3d", L"mode" };

// Parameter modifier flags, ParaFlag in tools/wz4ops/doc.hpp.

static void EmitParaFlags(wJsonWriter &j,sInt flags)
{
  j.BeginArray(L"modifiers");
  if(flags & PF_Anim)       j.Str(L"anim");
  if(flags & PF_LineNumber) j.Str(L"linenumber");
  if(flags & PF_Narrow)     j.Str(L"narrow");
  if(flags & PF_OverLabel)  j.Str(L"overlabel");
  if(flags & PF_OverBox)    j.Str(L"overbox");
  if(flags & PF_Static)     j.Str(L"static");
  j.EndArray();
}

static void EmitBitNames(wJsonWriter &j,const sChar *key,sInt value,
  const sChar **names,sInt count)
{
  j.BeginArray(key);
  for(sInt i=0;i<count;i++)
    if((value & (1<<i)) && names[i])
      j.Str(names[i]);
  j.EndArray();
}

static const sChar *LinkMethodName(sInt method)
{
  switch(method)
  {
  case IE_INPUT:  return L"input";
  case IE_LINK:   return L"link";
  case IE_BOTH:   return L"both";
  case IE_CHOOSE: return L"choose";
  case IE_ANIM:   return L"anim";
  default:        return L"input";
  }
}

/****************************************************************************/
/***                                                                      ***/
/***   Choice widgets                                                     ***/
/***                                                                      ***/
/****************************************************************************/

// flags/radio/strobe carry an option string that packs SEVERAL independent
// widgets into ONE integer:
//
//     "a|b:*4c|d"   ->  widget 0: shift 0, choices a=0,b=1
//                       widget 1: shift 4, choices c=0,d=1
//
// ':' separates widgets, '*N' sets that widget's bit shift, '|' separates
// choices and advances the value by one, and a leading integer sets the next
// choice's value outright.
//
// That last rule has a consequence worth knowing: a leading space is LOAD
// BEARING. The corpus writes " 1| 2| 4| 8" rather than "1|2|4|8" because
// without the space the digit is consumed as an explicit value, leaving an
// empty label. " 1D| 2D| 3D" works for the same reason.
//
// This walk mirrors sFindFlag (base/types.cpp:1780) exactly, including its mask
// derivation — the smallest all-ones value covering the largest choice, shifted
// left. ValidateChoices below checks that claim against sFindFlag itself rather
// than taking my word for it.

struct wChoiceEntry
{
  sInt Widget;                    // index of the widget this choice belongs to
  sInt Shift;
  sInt Mask;
  sInt Value;
  sPoolString Label;
};

static void DecomposeChoices(const sChar *choices,sArray<wChoiceEntry> &out)
{
  const sChar *s = choices;
  sInt widget = 0;

  while(*s)
  {
    sInt shift = 0;
    sInt value = 0;
    sInt max = 0;
    sInt first = out.GetCount();

    if(*s=='*')
    {
      s++;
      sScanInt(s,shift);
    }

    for(;;)
    {
      while(*s=='|')
      {
        s++;
        value++;
      }
      if(*s==':' || *s==0)
        break;

      if(sIsDigit(*s))
        sScanInt(s,value);
      if(*s==' ')
        s++;

      sInt len = 0;
      while(s[len]!='|' && s[len]!=':' && s[len]!=0) len++;

      wChoiceEntry *e = out.AddMany(1);
      e->Widget = widget;
      e->Shift = shift;
      e->Mask = 0;                // filled in once max is known
      e->Value = value;
      e->Label.Init(s,len);

      s += len;
      max = sMax(max,value);
    }

    sInt mask = 1;
    while(mask < max)
      mask = mask*2+1;
    mask <<= shift;
    for(sInt i=first;i<out.GetCount();i++)
      out[i].Mask = mask;

    widget++;
    if(*s==':')
      s++;
  }
}

static void EmitChoiceWidgets(wJsonWriter &j,const sChar *choices)
{
  sArray<wChoiceEntry> entries;
  DecomposeChoices(choices,entries);

  j.BeginArray(L"widgets");

  sInt i = 0;
  while(i<entries.GetCount())
  {
    sInt widget = entries[i].Widget;

    j.BeginObject();
    j.Int(L"shift",entries[i].Shift);
    j.HexStr(L"mask",sU32(entries[i].Mask));
    j.BeginArray(L"choices");
    while(i<entries.GetCount() && entries[i].Widget==widget)
    {
      j.BeginObject();
      j.Str(L"label",entries[i].Label);
      j.Int(L"value",entries[i].Value);
      j.EndObject();
      i++;
    }
    j.EndArray();
    j.EndObject();
  }

  j.EndArray();
}

/****************************************************************************/
/***                                                                      ***/
/***   Conditions                                                         ***/
/***                                                                      ***/
/****************************************************************************/

// The expression tree is already fully lowered by the time we see it:
// parse.cpp desugars Flags.choicename into (Symbol & mask) == value while
// parsing, and ANDs enclosing if() conditions into the inner one. So there is
// no nesting to represent and no choice names to resolve — just five node
// kinds.

static const sChar *ExprOpName(sInt op)
{
  switch(op)
  {
  case EOP_GT:     return L">";
  case EOP_LT:     return L"<";
  case EOP_GE:     return L">=";
  case EOP_LE:     return L"<=";
  case EOP_EQ:     return L"==";
  case EOP_NE:     return L"!=";
  case EOP_AND:    return L"&&";
  case EOP_OR:     return L"||";
  case EOP_BITAND: return L"&";
  case EOP_BITOR:  return L"|";
  case EOP_NOT:    return L"!";
  case EOP_BITNOT: return L"~";
  default:         return 0;
  }
}

// Resolves a parameter symbol within one operator, skipping "continue"
// declarations so it always lands on the one that owns the storage.
//
// Used for two things: conditionals naming another parameter, and giving a
// "continue flags" widget the offset of the variable it shares.

static const Parameter *FindParaBySymbol(Op *op,sPoolString symbol)
{
  Parameter *para;
  sFORALL(op->Parameters,para)
    if(para->Symbol==symbol && !para->ContinueFlag)
      return para;
  sFORALL(op->ArrayParam,para)
    if(para->Symbol==symbol && !para->ContinueFlag)
      return para;
  return 0;
}

static sBool EmitExpr(wJsonWriter &j,ExprNode *node,Op *op,const sChar *what)
{
  j.BeginObject();

  sBool ok = 1;
  switch(node->Op)
  {
  case EOP_INT:
    j.Str(L"node",L"int");
    j.Int(L"value",node->Value);
    break;

  case EOP_INPUT:
    // Tests wOp::ConnectionMask, i.e. "is input N connected".
    j.Str(L"node",L"input");
    j.Int(L"index",node->Value);
    break;

  case EOP_SYMBOL:
    {
      j.Str(L"node",L"symbol");
      j.Str(L"symbol",node->Symbol);
      const Parameter *ref = FindParaBySymbol(op,node->Symbol);
      if(!ref)
      {
        sPrintF(L"opsmeta: %s in op <%s> names unknown parameter <%s>\n",
          what,op->Name,node->Symbol);
        ok = 0;
        j.Int(L"offset",-1);
      }
      else
      {
        j.Int(L"offset",ref->Offset);
        j.Str(L"space",ParaSpaceName(ref));
        j.Str(L"kind",ParaKindName(ref->Type));
      }
    }
    break;

  default:
    {
      const sChar *name = ExprOpName(node->Op);
      if(!name)
      {
        sPrintF(L"opsmeta: %s in op <%s> uses unknown operator %d\n",
          what,op->Name,node->Op);
        ok = 0;
        j.Str(L"node",L"unknown");
        break;
      }
      j.Str(L"node",node->Right ? L"binary" : L"unary");
      j.Str(L"op",name);
      j.BeginArray(L"args");
      if(node->Left)
        ok &= EmitExpr(j,node->Left,op,what);
      if(node->Right)
        ok &= EmitExpr(j,node->Right,op,what);
      j.EndArray();
    }
    break;
  }

  j.EndObject();
  return ok;
}

/****************************************************************************/
/***                                                                      ***/
/***   Parameters                                                         ***/
/***                                                                      ***/
/****************************************************************************/

static sBool EmitParameter(wJsonWriter &j,Parameter *para,Op *op)
{
  sBool ok = 1;

  j.BeginObject();
  j.Str(L"kind",ParaKindName(para->Type));
  j.Str(L"symbol",para->Symbol);
  j.Str(L"label",para->Label);

  const sChar *space = ParaSpaceName(para);
  j.Str(L"space",space);

  // A "continue flags" widget carries Offset == -1, because the parser skips
  // allocation for it (parse.cpp:678-679). It still edits a real word — the one
  // belonging to the earlier declaration of the same symbol — so resolve it
  // here. Emitting -1 would leave the editor with a widget and nowhere to put
  // the value.
  sInt offset = para->Offset;
  if(para->ContinueFlag)
  {
    const Parameter *owner = FindParaBySymbol(op,para->Symbol);
    if(!owner)
    {
      sPrintF(L"opsmeta: continue in op <%s> names unknown parameter <%s>\n",
        op->Name,para->Symbol);
      ok = 0;
    }
    else
    {
      offset = owner->Offset;
    }
  }
  j.Int(L"offset",offset);
  j.Int(L"words",ParaWordCount(para));
  j.Str(L"layout",ParaLayoutName(para));
  j.Int(L"count",para->Count);

  if(!para->CType.IsEmpty())
    j.Str(L"ctype",para->CType);

  // "continue flags" declares another widget on a variable already declared,
  // so it has no storage of its own. The editor must not allocate for it.
  j.Bool(L"continues",para->ContinueFlag);

  // layout means "rebuild the panel on change", not "relayout the window":
  // it is what makes conditional parameters take effect. See
  // 01-existing-model.md §5.1.
  j.Bool(L"rebuildOnChange",para->LayoutFlag!=0);

  EmitParaFlags(j,para->Flags);

  switch(para->Type)
  {
  case TYPE_FLOAT:
    j.Float(L"min",para->Min);
    j.Float(L"max",para->Max);
    // A negative Step is how the parser records "logstep" (parse.cpp:733).
    if(para->Step<0)
    {
      j.Float(L"step",-para->Step);
      j.Bool(L"logStep",sTRUE);
    }
    else
    {
      j.Float(L"step",para->Step);
      j.Bool(L"logStep",sFALSE);
    }
    j.Float(L"rstep",para->RStep);
    j.BeginArray(L"defaults");
    for(sInt i=0;i<para->Count;i++)
      j.Float(para->DefaultF[i]);
    j.EndArray();
    break;

  case TYPE_INT:
    j.Float(L"min",para->Min);
    j.Float(L"max",para->Max);
    j.Float(L"step",para->Step);
    j.Float(L"rstep",para->RStep);
    if(!para->Format.IsEmpty())
      j.Str(L"format",para->Format);
    j.BeginArray(L"defaults");
    for(sInt i=0;i<para->Count;i++)
      j.Int(para->DefaultS[i]);
    j.EndArray();
    break;

  case TYPE_BITMASK:
    // Max holds the number of bit columns, clamped 1..4 (parse.cpp:767).
    j.Int(L"bits",sInt(para->Max));
    j.BeginArray(L"defaults");
    for(sInt i=0;i<para->Count;i++)
      j.Int(para->DefaultS[i]);
    j.EndArray();
    break;

  case TYPE_COLOR:
    // Options is the channel string: "rgb", "rgba", "hsv"...
    j.Str(L"channels",para->Options);
    // Packed ARGB, so hex rather than a decimal nobody can read.
    j.BeginArray(L"defaults");
    for(sInt i=0;i<para->Count;i++)
      j.HexStr((const sChar *)0,para->DefaultU[i]);
    j.EndArray();
    break;

  case TYPE_FLAGS:
  case TYPE_RADIO:
  case TYPE_STROBE:
    j.Str(L"options",para->Options);
    EmitChoiceWidgets(j,para->Options);
    j.BeginArray(L"defaults");
    for(sInt i=0;i<para->Count;i++)
      j.Int(sInt(para->DefaultU[i]));
    j.EndArray();
    break;

  case TYPE_CHARARRAY:
    // Max is the declared character capacity (parse.cpp:565).
    j.Int(L"capacity",sInt(para->Max));
    j.Str(L"default",para->DefaultString);
    break;

  case TYPE_STRING:
    j.Int(L"lines",para->GridLines);
    j.Str(L"default",para->DefaultString);
    j.Bool(L"defaultRandom",para->DefaultStringRandom);
    break;

  case TYPE_FILEIN:
  case TYPE_FILEOUT:
    j.Str(L"default",para->DefaultString);
    break;

  case TYPE_LINK:
    j.Str(L"method",LinkMethodName(para->LinkMethod));
    break;

  case TYPE_ACTION:
    j.Int(L"actionId",para->DefaultS[0]);
    break;

  case TYPE_CUSTOM:
    j.Str(L"controlClass",para->CustomName);
    j.Int(L"lines",para->GridLines);
    break;

  default:
    break;
  }

  // "filter" marks the array column that drives auto-collapse; -1 is "not a
  // filter" and is the parser's default.
  if(para->FilterMode>=0)
    j.Int(L"filterMode",para->FilterMode);

  if(para->Condition)
  {
    j.BeginObject(L"condition");
    j.Str(L"node",L"root");
    j.BeginArray(L"args");
    ok &= EmitExpr(j,para->Condition,op,L"condition");
    j.EndArray();
    j.EndObject();
  }

  j.EndObject();
  return ok;
}

/****************************************************************************/
/***                                                                      ***/
/***   Classes and types                                                  ***/
/***                                                                      ***/
/****************************************************************************/

static sBool EmitClass(wJsonWriter &j,Op *op)
{
  sBool ok = 1;

  j.BeginObject();
  j.Str(L"name",op->Name);
  j.Str(L"label",op->Label);
  j.Str(L"outputType",op->OutputType);
  j.Str(L"tabType",op->TabType.IsEmpty() ? op->OutputType : op->TabType);
  if(!op->OutputClass.IsEmpty())
    j.Str(L"outputClass",op->OutputClass);

  // Column is always the effective value: parse.cpp:334-341 infers it from the
  // signature before an explicit "column = N;" can override it, so the editor
  // needs no inference of its own.
  j.Int(L"column",op->Column);
  if(op->Shortcut)
  {
    sString<8> sc;
    sc.PrintF(L"%c",op->Shortcut);
    j.Str(L"shortcut",sc);
  }
  if(op->GridColumns)
    j.Int(L"gridColumns",op->GridColumns);
  if(!op->Extract.IsEmpty())
    j.Str(L"extract",op->Extract);

  j.HexStr(L"flagBits",sU32(op->Flags));
  EmitBitNames(j,L"flags",op->Flags,ClassFlagNames,sCOUNTOF(ClassFlagNames));

  j.Bool(L"hasCode",op->Code!=0);

  // Storage budget. paraWords and paraStrings are what wOp allocates.
  j.Int(L"paraWords",op->MaxOffset);
  j.Int(L"paraStrings",op->MaxStrings);
  j.Int(L"arrayWords",op->MaxArrayOffset);
  j.HexStr(L"fileInMask",op->FileInMask);
  j.HexStr(L"fileOutMask",op->FileOutMask);
  if(!op->FileInFilter.IsEmpty())
    j.Str(L"fileInFilter",op->FileInFilter);

  // Inputs
  j.BeginArray(L"inputs");
  Input *in;
  sFORALL(op->Inputs,in)
  {
    j.BeginObject();
    j.Str(L"type",in->Type);
    j.Bool(L"optional",(in->InputFlags & IF_OPTIONAL)!=0);
    j.Bool(L"weak",(in->InputFlags & IF_WEAK)!=0);
    // Varargs is a property of the operator's last input, not of each input.
    j.Bool(L"varargs",(op->Flags & 1) && _i==op->Inputs.GetCount()-1);
    j.Str(L"method",LinkMethodName(in->Method));
    if(!in->GuiSymbol.IsEmpty())
      j.Str(L"linkSymbol",in->GuiSymbol);
    if(!in->DefaultOpName.IsEmpty())
    {
      j.Str(L"defaultOpType",in->DefaultOpType);
      j.Str(L"defaultOpName",in->DefaultOpName);
    }
    j.EndObject();
  }
  j.EndArray();

  // Actions declared by "action" parameters, mirrored into wClass::ActionIds.
  j.BeginArray(L"actionIds");
  ActionInfo *ai;
  sFORALL(op->ActionInfos,ai)
  {
    j.BeginObject();
    j.Str(L"name",ai->Name);
    j.Int(L"id",ai->Id);
    j.EndObject();
  }
  j.EndArray();

  // Parameters, in declaration order, which is panel order.
  j.BeginArray(L"parameters");
  Parameter *para;
  sFORALL(op->Parameters,para)
    ok &= EmitParameter(j,para,op);
  j.EndArray();

  // The parameter array (the table widget), if any.
  if(op->ArrayParam.GetCount()>0)
  {
    j.BeginObject(L"array");
    j.Int(L"hideThreshold",op->HideArray);
    j.Bool(L"grouped",op->GroupArray!=0);
    j.Bool(L"numbered",op->ArrayNumbers!=0);
    j.Int(L"defaultMode",op->DefaultArrayMode);
    j.BeginArray(L"parameters");
    sFORALL(op->ArrayParam,para)
      ok &= EmitParameter(j,para,op);
    j.EndArray();
    j.BeginArray(L"ties");
    Tie *tie;
    sFORALL(op->ArrayTies,tie)
    {
      j.BeginArray();
      sPoolString *name;
      sFORALL(tie->Paras,name)
        j.Str(*name);
      j.EndArray();
    }
    j.EndArray();
    j.EndObject();
  }

  // Ties: separately declared floats that drag together under Ctrl. No .ops
  // file in the tree uses these — see docs/04 stage 2.3.
  j.BeginArray(L"ties");
  Tie *tie;
  sFORALL(op->Ties,tie)
  {
    j.BeginArray();
    sPoolString *name;
    sFORALL(tie->Paras,name)
      j.Str(*name);
    j.EndArray();
  }
  j.EndArray();

  j.EndObject();
  return ok;
}

static void EmitType(wJsonWriter &j,Type *type)
{
  j.BeginObject();
  j.Str(L"symbol",type->Symbol);
  j.Str(L"label",type->Label);
  j.Str(L"parent",type->Parent);
  j.Bool(L"virtual",type->Virtual!=0);
  j.HexStr(L"color",type->Color);
  EmitBitNames(j,L"flags",type->Flags,TypeFlagNames,sCOUNTOF(TypeFlagNames));
  EmitBitNames(j,L"gui",type->GuiSets,TypeGuiNames,sCOUNTOF(TypeGuiNames));

  // Column headers are a sparse array indexed by palette column, so the index
  // has to travel with the text.
  j.BeginArray(L"columnHeaders");
  for(sInt i=0;i<sCOUNTOF(type->ColumnHeaders);i++)
  {
    if(!type->ColumnHeaders[i].IsEmpty())
    {
      j.BeginObject();
      j.Int(L"column",i);
      j.Str(L"label",type->ColumnHeaders[i]);
      j.EndObject();
    }
  }
  j.EndArray();

  j.EndObject();
}

/****************************************************************************/
/***                                                                      ***/
/***   Validation                                                         ***/
/***                                                                      ***/
/****************************************************************************/

// wz4ops detects overlapping parameter offsets, but it does so in
// OutputParaStruct — inside the C++ emitter, which this tool deliberately does
// not link. So the check has to exist here too, or the JSON could describe a
// layout that the generated struct would have rejected.
//
// Overlap matters more for us than for the original: the editor writes straight
// into wOp::EditData at the offset the metadata gives it. An overlap means two
// widgets silently fighting over one word.

static sBool CheckSpace(Op *op,sArray<Parameter *> &paras,const sChar *wanted,
  sInt declared,const sChar *what)
{
  sBool ok = 1;

  // One slot per word (or string, or link). -1 is free, otherwise the index of
  // the parameter that claimed it.
  sArray<sInt> owner;
  if(declared>0)
  {
    sInt *p = owner.AddMany(declared);
    for(sInt i=0;i<declared;i++)
      p[i] = -1;
  }

  Parameter *para;
  sFORALL(paras,para)
  {
    if(para->ContinueFlag)
      continue;
    if(sCmpString(ParaSpaceName(para),wanted)!=0)
      continue;

    sInt slots = (sCmpString(wanted,L"words")==0) ? ParaWordCount(para) : para->Count;
    if(slots<=0)
      continue;

    if(para->Offset<0 || para->Offset+slots>declared)
    {
      sPrintF(L"opsmeta: op <%s> parameter <%s> claims %s %d..%d, but only %d are declared\n",
        op->Name,para->Symbol,what,para->Offset,para->Offset+slots-1,declared);
      ok = 0;
      continue;
    }

    for(sInt i=0;i<slots;i++)
    {
      sInt slot = para->Offset+i;
      if(owner[slot]>=0)
      {
        sPrintF(L"opsmeta: op <%s> parameters <%s> and <%s> overlap at %s %d\n",
          op->Name,paras[owner[slot]]->Symbol,para->Symbol,what,slot);
        ok = 0;
      }
      else
      {
        owner[slot] = _i;
      }
    }
  }

  return ok;
}

// The choice decomposition above is a re-implementation of sFindFlag's parse.
// The editor's flags widgets are only correct if it agrees with the original,
// and there is no reference output to diff against — so check it against
// sFindFlag directly, for every choice of every flags parameter.
//
// A label that appears in more than one widget of the same option string is
// skipped: sFindFlag returns the first match, so there is nothing to compare
// against. That is genuinely ambiguous in the format, not a defect here — the
// common case is "-" used as a blank entry in several widgets, as in
// "-|abs:*1-|sin".

static sBool ValidateChoices(Op *op,Parameter *para,sInt &checked)
{
  sBool ok = 1;

  sArray<wChoiceEntry> entries;
  DecomposeChoices(para->Options,entries);

  wChoiceEntry *e;
  sFORALL(entries,e)
  {
    // Ambiguous label? Skip it.
    sInt seen = 0;
    wChoiceEntry *f;
    sFORALL(entries,f)
      if(f->Label==e->Label)
        seen++;
    if(seen>1)
      continue;

    sInt mask = 0;
    sInt value = 0;
    if(!sFindFlag(e->Label,para->Options,mask,value))
    {
      sPrintF(L"opsmeta: op <%s> parameter <%s>: sFindFlag does not recognise choice <%s>\n",
        op->Name,para->Symbol,e->Label);
      ok = 0;
      continue;
    }

    if(mask!=e->Mask || value!=(e->Value<<e->Shift))
    {
      sPrintF(L"opsmeta: op <%s> parameter <%s> choice <%s>: "
              L"emitted mask 0x%08x value 0x%08x, sFindFlag says mask 0x%08x value 0x%08x\n",
        op->Name,para->Symbol,e->Label,
        sU32(e->Mask),sU32(e->Value<<e->Shift),sU32(mask),sU32(value));
      ok = 0;
      continue;
    }

    checked++;
  }

  return ok;
}

// Counters, so the build log says how much was actually checked rather than
// just staying quiet.

static sInt ChoicesChecked = 0;
static sInt ParametersSeen = 0;

static sBool ValidateOp(Op *op)
{
  sBool ok = 1;

  Parameter *p;
  sFORALL(op->Parameters,p)
    if(p->Type==TYPE_FLAGS || p->Type==TYPE_RADIO || p->Type==TYPE_STROBE)
      ok &= ValidateChoices(op,p,ChoicesChecked);
  sFORALL(op->ArrayParam,p)
    if(p->Type==TYPE_FLAGS || p->Type==TYPE_RADIO || p->Type==TYPE_STROBE)
      ok &= ValidateChoices(op,p,ChoicesChecked);

  ParametersSeen += op->Parameters.GetCount() + op->ArrayParam.GetCount();

  ok &= CheckSpace(op,op->Parameters,L"words",op->MaxOffset,L"word");
  ok &= CheckSpace(op,op->Parameters,L"strings",op->MaxStrings,L"string");
  ok &= CheckSpace(op,op->ArrayParam,L"words",op->MaxArrayOffset,L"array word");

  // Link parameters index wOp::Links, which is sized from the input count.
  Parameter *para;
  sFORALL(op->Parameters,para)
  {
    if(para->Type!=TYPE_LINK)
      continue;
    if(para->Offset<0 || para->Offset>=op->Inputs.GetCount())
    {
      sPrintF(L"opsmeta: op <%s> link <%s> has offset %d but there are %d inputs\n",
        op->Name,para->Symbol,para->Offset,op->Inputs.GetCount());
      ok = 0;
    }
  }

  return ok;
}

/****************************************************************************/
/***                                                                      ***/
/***   Entry point                                                        ***/
/***                                                                      ***/
/****************************************************************************/

sBool wEmitMetaJson(sTextBuffer &out,const sChar *module)
{
  wJsonWriter j(out);
  sBool ok = 1;

  ChoicesChecked = 0;
  ParametersSeen = 0;

  j.BeginObject();
  j.Int(L"schemaVersion",OPSMETA_SCHEMA_VERSION);
  j.Str(L"module",module);
  j.Int(L"priority",Doc->Priority);

  j.BeginArray(L"types");
  Type *type;
  sFORALL(Doc->Types,type)
    EmitType(j,type);
  j.EndArray();

  j.BeginArray(L"classes");
  Op *op;
  sFORALL(Doc->Ops,op)
  {
    ok &= ValidateOp(op);
    ok &= EmitClass(j,op);
  }
  j.EndArray();

  j.EndObject();
  j.Finish();

  sPrintF(L"opsmeta %s: %d types, %d classes, %d parameters, %d choices cross-checked against sFindFlag\n",
    module,Doc->Types.GetCount(),Doc->Ops.GetCount(),ParametersSeen,ChoicesChecked);

  return ok;
}

/****************************************************************************/
