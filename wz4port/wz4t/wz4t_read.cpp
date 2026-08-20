/****************************************************************************/
/***                                                                      ***/
/***   Reading .wz4t                                                      ***/
/***                                                                      ***/
/****************************************************************************/

#include "wz4t.hpp"
#include "json.hpp"               // wFormatFloat
#include "wz4lib/doc_core.hpp"
#include "util/scanner.hpp"
#include "base/system.hpp"

/****************************************************************************/

enum
{
  TOK_HASH = sTOK_USER,           // '#' starts a colour literal
};

// Grammar decision, recorded because docs/02-target-model.md §4.2 is ambiguous
// about it: comments are `//` and `/* */`, NOT `#`.
//
// The example in §4.2 uses `#` for a trailing comment on one line and for a
// colour literal (`#ff8040c0`) two lines later. Both cannot be true.
// sScanner offers `#` comments as sSF_NUMBERCOMMENT, so the choice is real —
// and the colour syntax is in §4.2's normative bullet list while comments are
// not mentioned at all. So `#` belongs to colours and comments are C-style.

/****************************************************************************/

// A parsed right-hand side. Kept deliberately loose: which of these fields
// matters depends on the parameter's kind, and the metadata decides that, not
// the syntax.
struct wValue
{
  sBool IsString;
  sBool IsFloat;
  sF32 F;
  sInt I;
  sPoolString S;

  // The token's source text, for every kind. Choice labels in this corpus are
  // often numeric — GenBitmap.Size offers "1".."8192" — so `Size = 64, 64`
  // arrives as integers while meaning the LABEL "64", whose control value is 6.
  // Without the text there is no way to tell that from a raw 64.
  sPoolString Text;

  wValue() { IsString = 0; IsFloat = 0; F = 0; I = 0; }
};

/****************************************************************************/

class wWz4tReader
{
  sScanner Scan;
  const wMetaLibrary *Meta;
  sInt Flags;                     // wWZ4T_???

  wPage *Page;                    // current page, created on demand
  sBool TookDefault;              // see AddPage
  sInt Errors;
  sInt Substituted;               // ops accepted as UnknownOp placeholders

  void Fail(const sChar *msg);

  wPage *AddPage(sPoolString name);
  wPage *NeedPage();
  sBool Coord(sInt &x,sInt &y);
  sBool Size(sInt &w,sInt &h);
  sBool Colour(sU32 &out);
  sBool Value(wValue &out);

  sBool Op(sInt *stackx,sInt *stacky,sInt *rowx,sInt rowy);
  sBool Settings(wStackOp *op,const wMetaClass *mc);
  sBool Apply(wStackOp *op,const wMetaClass *mc,const wMetaParam *p,
    sArray<wValue> &values);
  sBool Block(const sChar *what);

public:
  wWz4tReader(const wMetaLibrary &meta,sInt flags);
  sBool Run(const sChar *text,const sChar *sourcename);
};

/****************************************************************************/

wWz4tReader::wWz4tReader(const wMetaLibrary &meta,sInt flags)
{
  Meta = &meta;
  Flags = flags;
  Page = 0;
  TookDefault = 0;
  Errors = 0;
  Substituted = 0;
}

void wWz4tReader::Fail(const sChar *msg)
{
  Scan.Error(L"%s",msg);
  Errors++;
}

// A freshly constructed wDocument ALREADY OWNS one empty page: the constructor
// calls DefaultDoc() (doc.cpp:2514, :2609), which appends one and connects.
//
// So the first page in a file takes that one over instead of appending. Without
// this, reading a two-page file yields three pages, and a .wz4t round trip would
// gain a stray empty page on every pass.
wPage *wWz4tReader::AddPage(sPoolString name)
{
  if(!TookDefault
    && Doc->Pages.GetCount()==1
    && Doc->Pages[0]->Ops.GetCount()==0
    && Doc->Pages[0]->Tree.GetCount()==0)
  {
    TookDefault = 1;
    Page = Doc->Pages[0];
  }
  else
  {
    Page = new wPage;
    Doc->Pages.AddTail(Page);
  }

  Page->Name = name;
  Page->IsTree = 0;
  if(!Doc->CurrentPage)
    Doc->CurrentPage = Page;
  return Page;
}

// A file with no explicit `page` still gets one, so a two-line case works.
wPage *wWz4tReader::NeedPage()
{
  if(!Page)
    AddPage(L"main");
  return Page;
}

/****************************************************************************/

sBool wWz4tReader::Coord(sInt &x,sInt &y)
{
  x = Scan.ScanInt();
  Scan.Match(',');
  y = Scan.ScanInt();
  return !Scan.Errors;
}

// "3x1". The tokeniser splits that into INT(3) and NAME("x1") — verified, not
// assumed — so the height arrives glued to an 'x'. "3 x 1" works too.
sBool wWz4tReader::Size(sInt &w,sInt &h)
{
  w = Scan.ScanInt();

  if(Scan.Token==sTOK_NAME)
  {
    sPoolString name;
    Scan.ScanName(name);
    const sChar *s = name;
    if(*s!='x' && *s!='X')
    {
      Fail(L"expected <width>x<height>");
      return 0;
    }
    s++;
    if(*s==0)                     // "3 x 1"
    {
      h = Scan.ScanInt();
    }
    else                          // "3x1"
    {
      sInt v = 0;
      if(!sScanInt(s,v))
      {
        Fail(L"expected <width>x<height>");
        return 0;
      }
      h = v;
    }
  }
  else
  {
    Fail(L"expected <width>x<height>");
    return 0;
  }

  return !Scan.Errors;
}

// #aarrggbb. A hex run does not survive as one token: "08ff0000" lexes as
// INT("08") + NAME("ff0000") and "1e500000" as a single FLOAT. All three token
// kinds expose their exact source text, so the run is reassembled from that.
// Measured against sScanner rather than assumed.
sBool wWz4tReader::Colour(sU32 &out)
{
  sString<64> hex;
  hex = L"";

  while(sGetStringLen(hex)<8)
  {
    if(Scan.Token==sTOK_NAME)
    {
      hex.Add(Scan.Name);
      Scan.Scan();
    }
    else if(Scan.Token==sTOK_INT || Scan.Token==sTOK_FLOAT)
    {
      hex.Add(Scan.ValueString);
      Scan.Scan();
    }
    else
    {
      break;
    }
  }

  if(sGetStringLen(hex)!=8)
  {
    Fail(L"a colour is '#' and exactly 8 hex digits, as #aarrggbb");
    return 0;
  }

  sU32 v = 0;
  for(sInt i=0;i<8;i++)
  {
    sChar c = hex[i];
    sInt d = -1;
    if(c>='0' && c<='9') d = c-'0';
    if(c>='a' && c<='f') d = c-'a'+10;
    if(c>='A' && c<='F') d = c-'A'+10;
    if(d<0)
    {
      Fail(L"a colour is '#' and exactly 8 hex digits, as #aarrggbb");
      return 0;
    }
    v = v*16 + sU32(d);
  }

  out = v;
  return 1;
}

sBool wWz4tReader::Value(wValue &out)
{
  if(Scan.IfToken(TOK_HASH))
  {
    sU32 c = 0;
    if(!Colour(c))
      return 0;
    out.I = sInt(c);
    out.F = sF32(c);
    return 1;
  }

  sBool negative = Scan.IfToken('-');

  if(Scan.Token==sTOK_INT)
  {
    if(!negative)
      out.Text = sPoolString(Scan.ValueString);
    out.I = Scan.ScanInt();
    out.F = sF32(out.I);
    if(negative)
    {
      out.I = -out.I;
      out.F = -out.F;
    }
    return 1;
  }

  if(Scan.Token==sTOK_FLOAT)
  {
    out.IsFloat = 1;
    if(!negative)
      out.Text = sPoolString(Scan.ValueString);
    // Not Scan.ScanFloat(): it is not correctly rounded and loses a ULP, which
    // showed up as a document round trip changing 0x3f9e9828 to 0x3f9e9827.
    // ValueString is the token's exact source text. See wParseFloat.
    out.F = wParseFloat(Scan.ValueString);
    Scan.Scan();
    if(negative)
      out.F = -out.F;
    out.I = sInt(out.F);
    return 1;
  }

  if(negative)
  {
    Fail(L"expected a number after '-'");
    return 0;
  }

  if(Scan.Token==sTOK_STRING)
  {
    out.IsString = 1;
    Scan.ScanString(out.S);
    out.Text = out.S;
    return 1;
  }

  if(Scan.Token==sTOK_NAME)
  {
    out.IsString = 1;
    Scan.ScanName(out.S);
    out.Text = out.S;
    return 1;
  }

  Fail(L"expected a value");
  return 0;
}

/****************************************************************************/

// Writes one parsed right-hand side into the operator's storage. Everything it
// needs to know — which space, which offset, how many words — comes from the
// metadata, because wClass does not carry it (docs/architecture.md A28).
sBool wWz4tReader::Apply(wStackOp *op,const wMetaClass *mc,const wMetaParam *p,
  sArray<wValue> &values)
{
  // How many values may this parameter take?
  sInt slots = 1;
  if(p->Layout==wML_VECTOR || p->Layout==wML_ARRAY)
    slots = p->Count;

  if(p->Kind==L"string" || p->Kind==L"filein" || p->Kind==L"fileout"
    || p->Kind==L"char" || p->Kind==L"link")
    slots = 1;

  // A choice parameter is one word, but it can hold several controls at
  // different shifts — so it accepts one value per control. See the widget
  // branch below.
  if(p->Widgets.GetCount()>0)
  {
    sArray<const wMetaWidget *> widgets;
    wGatherWidgets(mc,p,widgets);
    slots = sMax(1,widgets.GetCount());
  }

  if(values.GetCount()>slots)
  {
    sString<256> msg;
    msg.PrintF(L"%s takes %d value(s), got %d",p->Symbol,slots,values.GetCount());
    Fail(msg);
    return 0;
  }

  // Strings
  if(p->Kind==L"string" || p->Kind==L"filein" || p->Kind==L"fileout")
  {
    if(p->Offset<0 || p->Offset>=op->EditStringCount)
    {
      Fail(L"string parameter is outside the operator's string storage");
      return 0;
    }
    op->EditString[p->Offset]->Clear();
    op->EditString[p->Offset]->Print(values[0].S);
    return 1;
  }

  // Link names. The link is resolved later by Connect(), by name.
  if(p->Kind==L"link")
  {
    if(p->Offset<0 || p->Offset>=op->Links.GetCount())
    {
      Fail(L"link parameter is outside the operator's link storage");
      return 0;
    }
    op->Links[p->Offset].LinkName = values[0].S;
    return 1;
  }

  if(p->Space!=wMS_WORDS)
  {
    sString<256> msg;
    msg.PrintF(L"%s has no storage and cannot be assigned",p->Symbol);
    Fail(msg);
    return 0;
  }

  if(p->Offset<0 || p->Offset+p->Words>mc->ParaWords)
  {
    Fail(L"parameter offset is outside the operator's word storage");
    return 0;
  }

  // char[n] is an sString<n> living inline in the parameter words.
  if(p->Kind==L"char")
  {
    sChar *dest = (sChar *)(op->EditU()+p->Offset);
    sInt max = p->Capacity>0 ? p->Capacity : 1;
    sInt i = 0;
    const sChar *src = values[0].S;
    while(i<max-1 && src[i])
    {
      dest[i] = src[i];
      i++;
    }
    dest[i] = 0;
    return 1;
  }

  // flags / radio / strobe. One word, but often SEVERAL controls packed into it
  // at different shifts — and `continue flags` can add more from a separate
  // declaration, so the full set comes from wGatherWidgets.
  //
  // Two forms, and the distinction is what makes `Size = 256, 256` work:
  //
  //   several values  positional, one per widget, each resolved WITHIN its own
  //                   widget. That is the form docs/02 §4.2 uses, and it is
  //                   unambiguous even when two widgets share a label — which
  //                   Size does, twice over, with "1".."8192" in both.
  //   one value       searched across every widget, or taken as a raw integer.
  //                   Convenient for a single-control parameter.
  if(p->Widgets.GetCount()>0)
  {
    sArray<const wMetaWidget *> widgets;
    wGatherWidgets(mc,p,widgets);

    sInt acc = 0;

    if(values.GetCount()>1)
    {
      if(values.GetCount()>widgets.GetCount())
      {
        sString<512> msg;
        msg.PrintF(L"%s has %d control(s), got %d value(s)",
          p->Symbol,widgets.GetCount(),values.GetCount());
        Fail(msg);
        return 0;
      }

      for(sInt vi=0;vi<values.GetCount();vi++)
      {
        const wMetaWidget *w = widgets[vi];

        // A label match wins over a raw number. `Size = 64, 64` therefore means
        // the choice labelled "64" (control value 6), which is what an author
        // writing it intends. Where a label happens to equal its own value —
        // "2" = 2 in Blur's Passes — the two readings agree anyway.
        sInt v = 0;
        sBool found = 0;
        for(sInt k=0;k<w->Choices.GetCount() && !found;k++)
        {
          if(w->Choices[k].Label==values[vi].Text)
          {
            v = w->Choices[k].Value;
            found = 1;
          }
        }

        if(!found)
        {
          if(values[vi].IsString)
          {
            sString<512> msg;
            msg.PrintF(L"%s control %d has no choice called \"%s\"; options are \"%s\"",
              p->Symbol,vi,values[vi].S,p->Options);
            Fail(msg);
            return 0;
          }
          v = values[vi].I;       // a plain control value
        }

        acc |= (v << w->Shift) & w->Mask;
      }
    }
    else
    {
      sBool found = 0;
      for(sInt wi=0;wi<widgets.GetCount() && !found;wi++)
      {
        const wMetaWidget *w = widgets[wi];
        for(sInt k=0;k<w->Choices.GetCount() && !found;k++)
        {
          if(w->Choices[k].Label==values[0].Text)
          {
            acc |= (w->Choices[k].Value << w->Shift) & w->Mask;
            found = 1;
          }
        }
      }

      if(!found)
      {
        if(values[0].IsString)
        {
          sString<512> msg;
          msg.PrintF(L"%s has no choice called \"%s\"; options are \"%s\"",
            p->Symbol,values[0].S,p->Options);
          Fail(msg);
          return 0;
        }
        acc = values[0].I;        // the whole word, as written
      }
    }

    op->EditS()[p->Offset] = acc;
    return 1;
  }

  // Plain numbers. A single value fills every slot, which is how the DSL's own
  // defaults behave (`float31 Scale = 1` means 1,1,1).
  sBool isfloat = (p->Kind==L"float");
  for(sInt i=0;i<slots;i++)
  {
    const wValue &v = values[values.GetCount()==1 ? 0 : i];
    if(i>=values.GetCount() && values.GetCount()!=1)
      break;

    if(isfloat)
      op->EditF()[p->Offset+i] = v.F;
    else
      op->EditS()[p->Offset+i] = v.I;
  }

  return 1;
}

/****************************************************************************/

sBool wWz4tReader::Settings(wStackOp *op,const wMetaClass *mc)
{
  Scan.Match('{');

  while(!Scan.Errors && Scan.Token!='}' && Scan.Token!=sTOK_END)
  {
    // Bare flags first: they have no '=' and would otherwise look like a
    // parameter name.
    if(Scan.IfName(L"hide"))
    {
      op->Hide = 1;
      continue;
    }
    if(Scan.IfName(L"bypass"))
    {
      op->Bypass = 1;
      continue;
    }

    sPoolString key;
    if(!Scan.ScanName(key))
      break;
    Scan.Match('=');

    // `name = ` is the store name, not a parameter.
    if(key==L"name")
    {
      sPoolString n;
      if(Scan.Token==sTOK_STRING)
        Scan.ScanString(n);
      else
        Scan.ScanName(n);
      op->Name = n;
      continue;
    }

    // A placeholder has no metadata, so nothing can be assigned to it. Its
    // parameters were already lost when the document was first read; the file
    // says so in a comment. Consume the value and move on.
    if(!mc)
    {
      wValue discard;
      while(!Scan.Errors && Value(discard) && (Scan.IfToken(',') || Scan.IfToken('|')))
        ;
      continue;
    }

    const wMetaParam *p = mc->FindParam(key);
    if(!p)
    {
      // A misspelled parameter must not be silently ignored: in a hand-written
      // test case that would mean the case quietly tests the default.
      sString<512> msg;
      msg.PrintF(L"%s.%s has no parameter called \"%s\"",
        mc->OutputType,mc->Name,key);
      Fail(msg);

      // Skip the value so one typo does not cascade.
      wValue discard;
      while(!Scan.Errors && Value(discard) && Scan.IfToken(','))
        ;
      continue;
    }

    sArray<wValue> values;
    for(;;)
    {
      wValue v;
      if(!Value(v))
        break;
      values.AddTail(v);
      if(!Scan.IfToken(',') && !Scan.IfToken('|'))
        break;
    }

    if(!Scan.Errors && values.GetCount())
      Apply(op,mc,p,values);
  }

  Scan.Match('}');
  return !Scan.Errors;
}

/****************************************************************************/

// One `op`. In a stack or row block the position comes from the block's
// running cursor instead of from `at`.
sBool wWz4tReader::Op(sInt *stackx,sInt *stacky,sInt *rowx,sInt rowy)
{
  sPoolString type,name;
  if(!Scan.ScanName(type))
    return 0;
  Scan.Match('.');
  if(!Scan.ScanName(name))
    return 0;

  const wMetaClass *mc = Meta->Find(type,name);
  wClass *cl = Doc->FindClass(name,type);
  sBool substitute = 0;

  if(!cl || !mc)
  {
    if(!(Flags & wWZ4T_ALLOWUNKNOWN))
    {
      sString<512> msg;
      if(!cl)
        msg.PrintF(L"no registered operator %s.%s",type,name);
      else
        msg.PrintF(L"no metadata for %s.%s — is build/meta up to date?",type,name);
      Fail(msg);
      return 0;
    }

    // Converting a document rather than reading a case: keep the operator as a
    // placeholder so its geometry still shapes the graph, and remember what it
    // was so a later write puts the name back.
    cl = Doc->FindClass(L"UnknownOp",L"AnyType");
    if(!cl)
    {
      Fail(L"UnknownOp is not registered, so an unknown class cannot be kept");
      return 0;
    }
    mc = 0;
    substitute = 1;
    Substituted++;
  }

  sInt x = 0,y = 0,w = 3,h = 1;
  sBool placed = 0;

  if(stackx)
  {
    x = *stackx;
    y = *stacky;
    placed = 1;
  }
  else if(rowx)
  {
    x = *rowx;
    y = rowy;
    placed = 1;
  }

  if(Scan.IfName(L"at"))
  {
    if(!Coord(x,y))
      return 0;
    placed = 1;
  }
  if(Scan.IfName(L"size"))
  {
    if(!Size(w,h))
      return 0;
  }

  if(!placed)
  {
    Fail(L"an op needs 'at X,Y', or to sit inside a stack or row block");
    return 0;
  }

  wStackOp *op = new wStackOp;
  op->Init(cl);
  op->PosX = x;
  op->PosY = y;
  op->SizeX = w;
  op->SizeY = h;
  if(substitute)
  {
    op->ForeignClass = name;
    op->ForeignType = type;
  }

  // Defaults first, so a file only has to state what it changes. This is the
  // same SetDefaults the editor runs when you place an operator.
  if(cl->SetDefaults)
    cl->SetDefaults(op);

  NeedPage()->Ops.AddTail(op);

  if(Scan.Token=='{')
  {
    if(!Settings(op,mc))
      return 0;
  }

  // Advance the block cursor. A stack grows downward by the operator's height,
  // which is exactly what makes the next one connect to this one.
  if(stackx)
    *stacky = y + h;
  if(rowx)
    *rowx = x + w;

  return !Scan.Errors;
}

sBool wWz4tReader::Block(const sChar *what)
{
  sBool isstack = sCmpString(what,L"stack")==0;

  sInt x = 0,y = 0;
  if(!Scan.IfName(L"at"))
  {
    Fail(L"a stack or row block needs 'at X,Y'");
    return 0;
  }
  if(!Coord(x,y))
    return 0;

  Scan.Match('{');
  while(!Scan.Errors && Scan.Token!='}' && Scan.Token!=sTOK_END)
  {
    if(!Scan.IfName(L"op"))
    {
      Fail(L"only 'op' is allowed inside a stack or row block");
      return 0;
    }
    if(isstack)
    {
      if(!Op(&x,&y,0,0))
        return 0;
    }
    else
    {
      if(!Op(0,0,&x,y))
        return 0;
    }
  }
  Scan.Match('}');

  return !Scan.Errors;
}

/****************************************************************************/

sBool wWz4tReader::Run(const sChar *text,const sChar *sourcename)
{
  Scan.Init();
  Scan.DefaultTokens();
  // C-style comments, not '#': see the note at the top of this file.
  Scan.Flags = sSF_CPPCOMMENT|sSF_ESCAPECODES;
  Scan.AddToken(L"#",TOK_HASH);
  Scan.Start(text);
  Scan.Stream->Filename = sourcename;

  if(!Scan.IfName(L"wz4t"))
  {
    Fail(L"a .wz4t file starts with 'wz4t <version>'");
    return 0;
  }
  sInt version = Scan.ScanInt();
  if(version!=1)
  {
    sString<128> msg;
    msg.PrintF(L"unknown .wz4t version %d, this build reads 1",version);
    Fail(msg);
    return 0;
  }

  while(!Scan.Errors && Scan.Token!=sTOK_END)
  {
    if(Scan.IfName(L"page"))
    {
      sPoolString n;
      Scan.ScanString(n);
      AddPage(n);
    }
    else if(Scan.IfName(L"op"))
    {
      if(!Op(0,0,0,0))
        break;
    }
    else if(Scan.IfName(L"stack"))
    {
      if(!Block(L"stack"))
        break;
    }
    else if(Scan.IfName(L"row"))
    {
      if(!Block(L"row"))
        break;
    }
    else
    {
      Fail(L"expected 'page', 'op', 'stack' or 'row'");
      break;
    }
  }

  if(Substituted)
    sPrintF(L"wz4t: %d operator(s) kept as UnknownOp placeholders\n",Substituted);

  return !Scan.Errors && Errors==0;
}

/****************************************************************************/

sBool wReadWz4tText(const sChar *text,const sChar *sourcename,
  const wMetaLibrary &meta,sInt flags)
{
  sVERIFY(Doc);
  wWz4tReader reader(meta,flags);
  return reader.Run(text,sourcename);
}

// .wz4t is UTF-8, always, whether or not the file carries a BOM.
//
// sLoadText only decodes UTF-8 when it finds a BOM (system.cpp:1080) and
// otherwise takes each byte as one character. That is wrong here, and wrong in
// the way that hides: a hand-written file — which is the entire point of this
// format, and which no ordinary editor gives a BOM — would read as Latin-1, so
// "café" became "cafÃ©". Because the writer then re-encodes those characters as
// UTF-8, the corruption is IDEMPOTENT, and a read/write/read round-trip test
// passes while quietly mangling the text. Found exactly that way.

sBool wReadWz4t(const sChar *filename,const wMetaLibrary &meta,sInt flags)
{
  sDInt size = 0;
  sU8 *bytes = sLoadFile(filename,size);
  if(!bytes)
  {
    sPrintF(L"wz4t: could not read <%s>\n",filename);
    return 0;
  }

  sU8 *utf8 = bytes;
  sDInt len = size;
  if(len>=3 && utf8[0]==0xef && utf8[1]==0xbb && utf8[2]==0xbf)
  {
    utf8 += 3;                    // skip a BOM if one is there
    len -= 3;
  }

  // sCopyStringFromUTF8 wants a zero-terminated byte string.
  sChar8 *zero = new sChar8[len+1];
  for(sDInt i=0;i<len;i++)
    zero[i] = sChar8(utf8[i]);
  zero[len] = 0;

  // One sChar per byte is always enough: UTF-8 never shortens.
  sChar *text = new sChar[len+1];
  sCopyStringFromUTF8(text,zero,sInt(len+1));

  sBool ok = wReadWz4tText(text,filename,meta,flags);

  delete[] text;
  delete[] zero;
  delete[] bytes;
  return ok;
}

/****************************************************************************/
