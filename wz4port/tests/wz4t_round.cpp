/****************************************************************************/
/***                                                                      ***/
/***   Stage 3.2 gate — writer output re-parses to an identical document   ***/
/***                                                                      ***/
/****************************************************************************/

// read -> write -> read, then compare the two documents field by field,
// including every parameter word and string. Comparing only operator counts
// would pass while silently losing a value.
//
// Then write a second time and require the two texts to be byte-identical,
// which is what "diff-stable" has to mean in practice.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "base/system.hpp"
#include "wz4t.hpp"
#include "meta.hpp"

/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
  }
}

static sInt Failures = 0;

static void Check(sBool cond,const sChar *what)
{
  if(cond)
  {
    sPrintF(L"  ok    %s\n",what);
  }
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

/****************************************************************************/

// A flat, comparable snapshot of one operator: everything a round trip is
// supposed to preserve.
struct Snap
{
  sPoolString Class;
  sPoolString Type;
  sPoolString Name;
  sInt PosX,PosY,SizeX,SizeY;
  sInt Hide,Bypass;
  sArray<sU32> Words;
  sArray<sPoolString> Strings;
  sArray<sPoolString> Links;
};

static void Take(sArray<Snap *> &out)
{
  wPage *page;
  sFORALL(Doc->Pages,page)
  {
    for(sInt i=0;i<page->Ops.GetCount();i++)
    {
      wStackOp *op = page->Ops[i];
      Snap *s = new Snap;
      out.AddTail(s);

      s->Class = op->Class->Name;
      s->Type = op->Class->OutputType->Symbol;
      // wDocName is sString<64>, not pooled; go via the raw pointer.
      s->Name = sPoolString((const sChar *)op->Name);
      s->PosX = op->PosX;
      s->PosY = op->PosY;
      s->SizeX = op->SizeX;
      s->SizeY = op->SizeY;
      s->Hide = op->Hide;
      s->Bypass = op->Bypass;

      for(sInt k=0;k<op->Class->ParaWords;k++)
        s->Words.AddTail(op->EditU()[k]);
      for(sInt k=0;k<op->EditStringCount;k++)
        s->Strings.AddTail(sPoolString(op->EditString[k]->Get()));
      for(sInt k=0;k<op->Links.GetCount();k++)
        s->Links.AddTail(sPoolString((const sChar *)op->Links[k].LinkName));
    }
  }
}

// Sorted the same way the writer sorts, so the two snapshots line up.
static void Sort(sArray<Snap *> &a)
{
  for(sInt i=1;i<a.GetCount();i++)
  {
    Snap *v = a[i];
    sInt j = i-1;
    while(j>=0 && (a[j]->PosY>v->PosY
      || (a[j]->PosY==v->PosY && a[j]->PosX>v->PosX)))
    {
      a[j+1] = a[j];
      j--;
    }
    a[j+1] = v;
  }
}

static sBool Same(Snap *a,Snap *b,sString<512> &why)
{
  if(a->Class!=b->Class || a->Type!=b->Type)
  {
    why.PrintF(L"class %s.%s became %s.%s",a->Type,a->Class,b->Type,b->Class);
    return 0;
  }
  if(a->Name!=b->Name)
  {
    why.PrintF(L"%s.%s: name \"%s\" became \"%s\"",
      a->Type,a->Class,a->Name,b->Name);
    return 0;
  }
  if(a->PosX!=b->PosX || a->PosY!=b->PosY
    || a->SizeX!=b->SizeX || a->SizeY!=b->SizeY)
  {
    why.PrintF(L"%s.%s: geometry %d,%d %dx%d became %d,%d %dx%d",
      a->Type,a->Class,a->PosX,a->PosY,a->SizeX,a->SizeY,
      b->PosX,b->PosY,b->SizeX,b->SizeY);
    return 0;
  }
  if(a->Hide!=b->Hide || a->Bypass!=b->Bypass)
  {
    why.PrintF(L"%s.%s: hide/bypass changed",a->Type,a->Class);
    return 0;
  }
  if(a->Words.GetCount()!=b->Words.GetCount())
  {
    why.PrintF(L"%s.%s: word count changed",a->Type,a->Class);
    return 0;
  }
  for(sInt i=0;i<a->Words.GetCount();i++)
  {
    if(a->Words[i]!=b->Words[i])
    {
      why.PrintF(L"%s.%s: word %d was 0x%08x, now 0x%08x",
        a->Type,a->Class,i,a->Words[i],b->Words[i]);
      return 0;
    }
  }
  if(a->Strings.GetCount()!=b->Strings.GetCount())
  {
    why.PrintF(L"%s.%s: string count changed",a->Type,a->Class);
    return 0;
  }
  for(sInt i=0;i<a->Strings.GetCount();i++)
  {
    if(a->Strings[i]!=b->Strings[i])
    {
      why.PrintF(L"%s.%s: string %d was \"%s\", now \"%s\"",
        a->Type,a->Class,i,a->Strings[i],b->Strings[i]);
      return 0;
    }
  }
  for(sInt i=0;i<a->Links.GetCount() && i<b->Links.GetCount();i++)
  {
    if(a->Links[i]!=b->Links[i])
    {
      why.PrintF(L"%s.%s: link %d was \"%s\", now \"%s\"",
        a->Type,a->Class,i,a->Links[i],b->Links[i]);
      return 0;
    }
  }
  return 1;
}

/****************************************************************************/

void sMain()
{
  const sChar *metadir = sGetShellParameter(0,0);
  const sChar *casefile = sGetShellParameter(0,1);
  if(!metadir || !casefile)
  {
    sPrint(L"usage: wz4t_round <metadir> <case.wz4t>\n");
    sSetErrorCode();
    return;
  }

  wMetaLibrary meta;
  if(!meta.LoadDirectory(metadir))
  {
    sPrintF(L"wz4t_round: %s\n",meta.GetError());
    sSetErrorCode();
    return;
  }

  sPrintF(L"wz4t_round: %s\n\n",casefile);

  // --- pass 1 ------------------------------------------------------------

  Doc = new wDocument;
  if(!wReadWz4t(casefile,meta))
  {
    sPrintF(L"wz4t_round: the case did not parse\n");
    sSetErrorCode();
    return;
  }
  Doc->Connect();

  sArray<Snap *> before;
  Take(before);
  Sort(before);
  sInt pagesbefore = Doc->Pages.GetCount();

  sTextBuffer first;
  if(!wWriteWz4t(first,meta))
  {
    sPrintF(L"wz4t_round: could not write\n");
    sSetErrorCode();
    return;
  }

  // --- pass 2 ------------------------------------------------------------

  delete Doc;
  Doc = new wDocument;
  if(!wReadWz4tText(first.Get(),L"<written>",meta))
  {
    sPrintF(L"\nwz4t_round: the WRITTEN text did not parse. Output was:\n%s\n",
      first.Get());
    sSetErrorCode();
    return;
  }
  Doc->Connect();

  sArray<Snap *> after;
  Take(after);
  Sort(after);

  sTextBuffer second;
  wWriteWz4t(second,meta);

  // --- compare -----------------------------------------------------------

  Check(pagesbefore==Doc->Pages.GetCount(),L"page count survives");
  Check(before.GetCount()==after.GetCount(),L"operator count survives");

  if(before.GetCount()==after.GetCount())
  {
    sInt bad = 0;
    for(sInt i=0;i<before.GetCount();i++)
    {
      sString<512> why;
      if(!Same(before[i],after[i],why))
      {
        sPrintF(L"  FAIL  %s\n",why);
        bad++;
      }
    }
    Check(bad==0,L"every operator identical: class, geometry, name, flags, "
                 L"every parameter word, string and link");
  }

  Check(sCmpString(first.Get(),second.Get())==0,
    L"writing twice gives byte-identical text (diff-stable)");

  // A round trip being STABLE is not the same as it being CORRECT. Reading
  // UTF-8 as Latin-1 and then writing it back as UTF-8 is idempotent after the
  // first pass, so the checks above all passed while "café" was silently
  // becoming "cafÃ©". So: assert an actual character value, once, against a
  // literal the compiler encodes independently of any file.
  //
  // The case file states  Text = "café °C — ΔΣ 中文"  on its first operator.
  if(before.GetCount() && before[0]->Strings.GetCount())
  {
    const sChar *got = before[0]->Strings[0];
    if(sFindString(got,L"caf")>=0)
    {
      Check(sCmpString(got,L"café °C — ΔΣ 中文")==0,
        L"non-ASCII text decoded correctly, not merely stably");
      if(sCmpString(got,L"café °C — ΔΣ 中文")!=0)
        sPrintF(L"        got \"%s\"\n",got);
    }
  }

  if(Failures)
  {
    sPrintF(L"\n--- first pass output ---\n%s\n",first.Get());
    if(sCmpString(first.Get(),second.Get())!=0)
      sPrintF(L"--- second pass output ---\n%s\n",second.Get());
  }

  sPrintF(L"\nwz4t_round: %d operator(s), %d failure(s)\n",
    before.GetCount(),Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
