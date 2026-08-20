/****************************************************************************/
/***                                                                      ***/
/***   Writing .wz4t                                                      ***/
/***                                                                      ***/
/****************************************************************************/

#include "wz4t.hpp"
#include "json.hpp"               // wFormatFloat — Altona has no %g
#include "wz4lib/doc_core.hpp"
#include "base/system.hpp"

/****************************************************************************/

// Is this safe to write bare, or does it need quoting? Choice labels in the
// corpus include "-", "16 Samples" and "San Andreas Gap", so this matters.
static sBool IsBareName(const sChar *s)
{
  if(!s || !*s)
    return 0;
  if(!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') || *s=='_'))
    return 0;
  for(;*s;s++)
  {
    sBool ok = (*s>='a' && *s<='z') || (*s>='A' && *s<='Z')
            || (*s>='0' && *s<='9') || *s=='_';
    if(!ok)
      return 0;
  }
  return 1;
}

static void PrintQuoted(sTextBuffer &out,const sChar *s)
{
  out.PrintChar('"');
  for(;*s;s++)
  {
    switch(*s)
    {
    case '"':  out.Print(L"\\\""); break;
    case '\\': out.Print(L"\\\\"); break;
    case '\n': out.Print(L"\\n");  break;
    case '\r': out.Print(L"\\r");  break;
    case '\t': out.Print(L"\\t");  break;
    default:   out.PrintChar(*s);  break;
    }
  }
  out.PrintChar('"');
}

static void PrintFloat(sTextBuffer &out,sF32 v)
{
  sChar buf[64];
  wFormatFloat(buf,sCOUNTOF(buf),v);
  out.Print(buf);
}

/****************************************************************************/

// A flags parameter's integer can be shared: `continue flags` declares more
// widgets on a word an earlier parameter owns. To decode the word into labels
// we need EVERY widget that touches it, not just the owner's — otherwise the
// continued bits would be silently dropped on write.
static void GatherWidgets(const wMetaClass *mc,const wMetaParam *owner,
  sArray<const wMetaWidget *> &out)
{
  for(sInt i=0;i<mc->Params.GetCount();i++)
  {
    const wMetaParam *p = mc->Params[i];
    if(p->Symbol!=owner->Symbol)
      continue;
    if(p->Space!=wMS_WORDS || p->Offset!=owner->Offset)
      continue;
    for(sInt k=0;k<p->Widgets.GetCount();k++)
      out.AddTail(p->Widgets[k]);
  }
}

// True if `label` names exactly one choice across all these widgets. An
// ambiguous label cannot be written, because the reader resolves the first
// match and would pick the wrong widget. "-" as a blank entry is the usual
// case; see docs/architecture.md A16.
static sBool LabelIsUnique(sArray<const wMetaWidget *> &widgets,
  const sChar *label)
{
  sInt seen = 0;
  for(sInt i=0;i<widgets.GetCount();i++)
    for(sInt k=0;k<widgets[i]->Choices.GetCount();k++)
      if(widgets[i]->Choices[k].Label==label)
        seen++;
  return seen==1;
}

// Renders a packed integer as choice labels, or fails so the caller can fall
// back to writing the number.
static sBool PrintChoices(sTextBuffer &out,const wMetaClass *mc,
  const wMetaParam *p,sInt value)
{
  sArray<const wMetaWidget *> widgets;
  GatherWidgets(mc,p,widgets);
  if(widgets.GetCount()==0)
    return 0;

  sTextBuffer tb;
  sInt written = 0;
  sInt covered = 0;

  for(sInt i=0;i<widgets.GetCount();i++)
  {
    const wMetaWidget *w = widgets[i];
    sInt v = (value & w->Mask) >> w->Shift;
    covered |= w->Mask;

    const wMetaChoice *match = 0;
    for(sInt k=0;k<w->Choices.GetCount();k++)
      if(w->Choices[k].Value==v)
        match = &w->Choices[k];

    if(!match)
      return 0;                   // a value with no name: write the number

    // A zero-valued blank entry contributes nothing, so leave it out rather
    // than emitting "-|sin".
    if(v==0)
      continue;

    if(!LabelIsUnique(widgets,match->Label))
      return 0;
    if(!IsBareName(match->Label))
      return 0;                   // quoting works, but the number reads better

    if(written)
      tb.Print(L"|");
    tb.Print(match->Label);
    written++;
  }

  // Any bit set outside every mask would be lost.
  if((value & ~covered)!=0)
    return 0;

  if(!written)
    tb.Print(L"0");               // all widgets at their blank entry

  out.Print(tb.Get());
  return 1;
}

/****************************************************************************/

// Does the operator's current value for this parameter differ from the default
// the metadata records? Only differences are written.
static sBool IsDefault(wOp *op,const wMetaParam *p)
{
  if(p->Space==wMS_STRINGS)
  {
    if(p->Offset>=op->EditStringCount)
      return 1;
    const sChar *cur = op->EditString[p->Offset]->Get();
    // A `random` default is a fresh string every time, so it is never "the
    // default" and must always be written.
    return sCmpString(cur,p->DefaultString)==0;
  }

  if(p->Space!=wMS_WORDS)
    return 1;

  sInt slots = 1;
  if(p->Layout==wML_VECTOR || p->Layout==wML_ARRAY)
    slots = p->Count;

  if(p->Kind==L"char")
  {
    const sChar *cur = (const sChar *)(op->EditU()+p->Offset);
    return sCmpString(cur,p->DefaultString)==0;
  }

  for(sInt i=0;i<slots;i++)
  {
    if(p->Kind==L"float")
    {
      sF32 want = i<p->DefaultsF.GetCount() ? p->DefaultsF[i] : 0;
      if(op->EditF()[p->Offset+i]!=want)
        return 0;
    }
    else
    {
      sInt want = i<p->DefaultsI.GetCount() ? p->DefaultsI[i] : 0;
      if(op->EditS()[p->Offset+i]!=want)
        return 0;
    }
  }
  return 1;
}

static void PrintValue(sTextBuffer &out,wOp *op,const wMetaClass *mc,
  const wMetaParam *p)
{
  if(p->Space==wMS_STRINGS)
  {
    PrintQuoted(out,op->EditString[p->Offset]->Get());
    return;
  }

  if(p->Kind==L"char")
  {
    PrintQuoted(out,(const sChar *)(op->EditU()+p->Offset));
    return;
  }

  if(p->Kind==L"color")
  {
    sString<32> hex;
    hex.PrintF(L"#%08x",sU32(op->EditU()[p->Offset]));
    out.Print(hex);
    return;
  }

  if(p->Widgets.GetCount()>0)
  {
    if(PrintChoices(out,mc,p,op->EditS()[p->Offset]))
      return;
    // Fall through to the raw integer.
    sString<32> num;
    num.PrintF(L"%d",op->EditS()[p->Offset]);
    out.Print(num);
    return;
  }

  sInt slots = 1;
  if(p->Layout==wML_VECTOR || p->Layout==wML_ARRAY)
    slots = p->Count;

  for(sInt i=0;i<slots;i++)
  {
    if(i)
      out.Print(L", ");
    if(p->Kind==L"float")
    {
      PrintFloat(out,op->EditF()[p->Offset+i]);
    }
    else
    {
      sString<32> num;
      num.PrintF(L"%d",op->EditS()[p->Offset+i]);
      out.Print(num);
    }
  }
}

/****************************************************************************/

static void WriteOp(sTextBuffer &out,wStackOp *op,const wMetaLibrary &meta,
  sInt &unwritable)
{
  // An operator whose class we could not register was substituted for
  // UnknownOp on read, and its parameters were discarded then (patches/07).
  // Write what identity we still have and say so, rather than pretending.
  sBool foreign = !op->ForeignClass.IsEmpty();

  const sChar *type = foreign ? (const sChar *)op->ForeignType
                              : (const sChar *)op->Class->OutputType->Symbol;
  const sChar *name = foreign ? (const sChar *)op->ForeignClass
                              : (const sChar *)op->Class->Name;

  sString<512> head;
  head.PrintF(L"op %s.%s at %d,%d size %dx%d",
    type,name,op->PosX,op->PosY,op->SizeX,op->SizeY);
  out.Print(head);

  const wMetaClass *mc = foreign ? 0 : meta.Find(type,name);

  // Collect the body first, so an operator with nothing to say stays on one
  // line instead of carrying an empty brace block.
  sTextBuffer body;

  if(!op->Name.IsEmpty())
  {
    body.Print(L"  name = ");
    if(IsBareName(op->Name))
      body.Print(op->Name);
    else
      PrintQuoted(body,op->Name);
    body.Print(L"\n");
  }
  if(op->Hide)
    body.Print(L"  hide\n");
  if(op->Bypass)
    body.Print(L"  bypass\n");

  if(foreign)
  {
    unwritable++;
    body.Print(L"  // parameters not preserved: this build cannot load "
               L"this class\n");
  }
  else if(!mc)
  {
    unwritable++;
    body.Print(L"  // parameters not written: no metadata for this class\n");
  }
  else
  {
    for(sInt i=0;i<mc->Params.GetCount();i++)
    {
      const wMetaParam *p = mc->Params[i];

      if(p->Space==wMS_NONE)      // label, group, action, strobe
        continue;
      if(p->Continues)            // its bits belong to the owner's word
        continue;
      if(p->Space==wMS_LINKS)
      {
        if(p->Offset<op->Links.GetCount()
          && !op->Links[p->Offset].LinkName.IsEmpty())
        {
          body.PrintF(L"  %s = ",p->Symbol);
          PrintQuoted(body,op->Links[p->Offset].LinkName);
          body.Print(L"\n");
        }
        continue;
      }
      if(p->Symbol.IsEmpty())
      {
        // One case in the whole corpus: TextObject.TextExport's
        // `fileout "Filename";`. It cannot be named, so it cannot be written.
        if(!IsDefault(op,p))
          unwritable++;
        continue;
      }
      if(IsDefault(op,p))
        continue;

      body.PrintF(L"  %s = ",p->Symbol);
      PrintValue(body,op,mc,p);
      body.Print(L"\n");
    }
  }

  if(body.GetCount()>0)
  {
    out.Print(L" {\n");
    out.Print(body.Get());
    out.Print(L"}\n");
  }
  else
  {
    out.Print(L"\n");
  }

  // One blank line between operators. Constant, so it costs nothing in a diff.
  out.Print(L"\n");
}

/****************************************************************************/

sBool wWriteWz4t(sTextBuffer &out,const wMetaLibrary &meta)
{
  sVERIFY(Doc);

  out.Print(L"wz4t 1\n\n");

  sInt unwritable = 0;
  sInt trees = 0;

  wPage *page;
  sFORALL(Doc->Pages,page)
  {
    // Tree pages use indentation rather than geometry (01-existing-model.md
    // §2.3). The format describes stack pages, so say so rather than emitting
    // something that would not read back.
    if(page->IsTree)
    {
      trees++;
      continue;
    }

    out.Print(L"page ");
    PrintQuoted(out,page->Name);
    out.Print(L"\n\n");

    // Deterministic order: PosY then PosX. Diff-stability is the point — a
    // converted document has to diff cleanly against the next conversion.
    sArray<wStackOp *> sorted;
    sorted = page->Ops;
    for(sInt i=1;i<sorted.GetCount();i++)
    {
      wStackOp *v = sorted[i];
      sInt j = i-1;
      while(j>=0 && (sorted[j]->PosY>v->PosY
        || (sorted[j]->PosY==v->PosY && sorted[j]->PosX>v->PosX)))
      {
        sorted[j+1] = sorted[j];
        j--;
      }
      sorted[j+1] = v;
    }

    for(sInt i=0;i<sorted.GetCount();i++)
      WriteOp(out,sorted[i],meta,unwritable);
  }

  if(trees)
    sPrintF(L"wz4t: skipped %d tree page(s); .wz4t describes stack pages\n",trees);
  if(unwritable)
    sPrintF(L"wz4t: %d operator(s) could not have their parameters written\n",
      unwritable);

  return 1;
}

sBool wWriteWz4tFile(const sChar *filename,const wMetaLibrary &meta)
{
  sTextBuffer out;
  if(!wWriteWz4t(out,meta))
    return 0;

  // UTF-8, not sSaveTextAnsi.
  //
  // A .wz4t file carries arbitrary document text — operator names, comment
  // bodies, file paths — and sSaveTextAnsi truncates every sChar to one byte,
  // so anything outside Latin-1 is silently corrupted and a round trip loses
  // it. Converting example.wz4 with Ansi also produced a file `grep` reported
  // as binary, which defeats the format's whole point.
  //
  // sLoadText recognises the UTF-8 BOM and decodes it (system.cpp:1080), so
  // this reads back losslessly. Compare opsmeta, which escapes non-ASCII as
  // \uXXXX instead and can stay Ansi because its output is ASCII by
  // construction; here escaping would make the file less readable, not more.
  if(!sSaveTextUTF8(filename,out.Get()))
  {
    sPrintF(L"wz4t: could not write <%s>\n",filename);
    return 0;
  }
  return 1;
}

/****************************************************************************/
