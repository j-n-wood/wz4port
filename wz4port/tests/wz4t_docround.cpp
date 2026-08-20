/****************************************************************************/
/***                                                                      ***/
/***   Phase 3 gate — .wz4 -> .wz4t -> .wz4 over a real document           ***/
/***                                                                      ***/
/****************************************************************************/

// The gate from docs/05-phase-text-format.md: the round trip preserves every
// operator, its geometry, its parameters and its store name; and re-deriving
// connections from the result reproduces the original input lists exactly.
//
// Scope, as the plan states it: "limited to the subgraphs whose classes we have
// registered". PARAMETERS are only compared for operators this build can load.
// For the rest, Altona discarded the parameter words when it first read the
// .wz4 (UnknownOp declares no storage — wz4port/patches/07), so there is
// nothing left to preserve and claiming otherwise would be false. Their
// IDENTITY and GEOMETRY are compared, and so are the connections they take part
// in, which is what makes the graph comparison meaningful.

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
    sPrintF(L"  ok    %s\n",what);
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

/****************************************************************************/

struct Snap
{
  sPoolString Page;
  sPoolString Class;              // as the operator claims to be
  sPoolString Type;
  sPoolString Name;
  sInt PosX,PosY,SizeX,SizeY;
  sInt Hide,Bypass;
  sBool Registered;               // false for an UnknownOp placeholder
  sArray<sU32> Words;
  sArray<sPoolString> Strings;
  sArray<sInt> InputKeys;         // each input identified by its position
};

// Position is a stable identity within a page: the connection rule is built on
// it, and no two operators can overlap.
static sInt KeyOf(wOp *op)
{
  wStackOp *s = (wStackOp *)op;
  return s->PosY*4096 + s->PosX;
}

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

      sBool foreign = !op->ForeignClass.IsEmpty();
      s->Page = sPoolString((const sChar *)page->Name);
      s->Registered = !foreign;
      s->Class = foreign ? op->ForeignClass : sPoolString(op->Class->Name);
      s->Type = foreign ? op->ForeignType
                        : sPoolString(op->Class->OutputType->Symbol);
      s->Name = sPoolString((const sChar *)op->Name);
      s->PosX = op->PosX;
      s->PosY = op->PosY;
      s->SizeX = op->SizeX;
      s->SizeY = op->SizeY;
      s->Hide = op->Hide;
      s->Bypass = op->Bypass;

      if(!foreign)
      {
        for(sInt k=0;k<op->Class->ParaWords;k++)
          s->Words.AddTail(op->EditU()[k]);
        for(sInt k=0;k<op->EditStringCount;k++)
          s->Strings.AddTail(sPoolString(op->EditString[k]->Get()));
      }

      for(sInt k=0;k<op->Inputs.GetCount();k++)
        s->InputKeys.AddTail(KeyOf(op->Inputs[k]));
    }
  }
}

static void Sort(sArray<Snap *> &a)
{
  for(sInt i=1;i<a.GetCount();i++)
  {
    Snap *v = a[i];
    sInt j = i-1;
    while(j>=0 && (a[j]->Page>v->Page
      || (a[j]->Page==v->Page && a[j]->PosY>v->PosY)
      || (a[j]->Page==v->Page && a[j]->PosY==v->PosY && a[j]->PosX>v->PosX)))
    {
      a[j+1] = a[j];
      j--;
    }
    a[j+1] = v;
  }
}

/****************************************************************************/

void sMain()
{
  const sChar *metadir = sGetShellParameter(0,0);
  const sChar *docfile = sGetShellParameter(0,1);
  const sChar *textfile = sGetShellParameter(0,2);
  const sChar *backfile = sGetShellParameter(0,3);

  if(!metadir || !docfile || !textfile || !backfile)
  {
    sPrint(L"usage: wz4t_docround <metadir> <in.wz4> <scratch.wz4t> <scratch.wz4>\n");
    sSetErrorCode();
    return;
  }

  wMetaLibrary meta;
  if(!meta.LoadDirectory(metadir))
  {
    sPrintF(L"wz4t_docround: %s\n",meta.GetError());
    sSetErrorCode();
    return;
  }

  sPrintF(L"wz4t_docround: %s\n",docfile);

  // --- .wz4 in -----------------------------------------------------------

  Doc = new wDocument;
  if(!Doc->Load(docfile) || Doc->AllOps.GetCount()==0)
  {
    sPrintF(L"wz4t_docround: could not load <%s>\n",docfile);
    sSetErrorCode();
    return;
  }

  sArray<Snap *> before;
  Take(before);
  Sort(before);

  sInt registered = 0;
  for(sInt i=0;i<before.GetCount();i++)
    if(before[i]->Registered)
      registered++;

  sPrintF(L"  %d operator(s), %d of them registered\n",
    before.GetCount(),registered);

  // --- out to .wz4t, back, out to .wz4, back -----------------------------

  if(!wWriteWz4tFile(textfile,meta))
  {
    sPrintF(L"wz4t_docround: could not write <%s>\n",textfile);
    sSetErrorCode();
    return;
  }

  delete Doc;
  Doc = new wDocument;
  if(!wReadWz4t(textfile,meta,wWZ4T_ALLOWUNKNOWN))
  {
    sPrintF(L"wz4t_docround: could not read back <%s>\n",textfile);
    sSetErrorCode();
    return;
  }
  Doc->Connect();

  if(!Doc->Save(backfile))
  {
    sPrintF(L"wz4t_docround: could not write <%s>\n",backfile);
    sSetErrorCode();
    return;
  }

  delete Doc;
  Doc = new wDocument;
  if(!Doc->Load(backfile))
  {
    sPrintF(L"wz4t_docround: could not load <%s>\n",backfile);
    sSetErrorCode();
    return;
  }

  sArray<Snap *> after;
  Take(after);
  Sort(after);

  // --- compare -----------------------------------------------------------

  Check(before.GetCount()==after.GetCount(),L"operator count survives");

  if(before.GetCount()!=after.GetCount())
  {
    sPrintF(L"        %d -> %d\n",before.GetCount(),after.GetCount());
  }
  else
  {
    sInt badid = 0,badgeo = 0,badname = 0,badpara = 0,badgraph = 0;

    for(sInt i=0;i<before.GetCount();i++)
    {
      Snap *a = before[i];
      Snap *b = after[i];

      if(a->Class!=b->Class || a->Type!=b->Type)
      {
        if(badid<3)
          sPrintF(L"        %s.%s became %s.%s\n",a->Type,a->Class,b->Type,b->Class);
        badid++;
        continue;                 // the rest of the comparison is meaningless
      }
      if(a->PosX!=b->PosX || a->PosY!=b->PosY
        || a->SizeX!=b->SizeX || a->SizeY!=b->SizeY || a->Page!=b->Page)
      {
        badgeo++;
        continue;
      }
      if(a->Name!=b->Name || a->Hide!=b->Hide || a->Bypass!=b->Bypass)
      {
        if(badname<3)
          sPrintF(L"        %s.%s: name \"%s\" -> \"%s\"\n",
            a->Type,a->Class,a->Name,b->Name);
        badname++;
      }

      // Parameters, for the registered subgraph only — see the note at the top.
      if(a->Registered && b->Registered)
      {
        sBool same = a->Words.GetCount()==b->Words.GetCount()
                  && a->Strings.GetCount()==b->Strings.GetCount();
        for(sInt k=0;same && k<a->Words.GetCount();k++)
          if(a->Words[k]!=b->Words[k])
            same = 0;
        for(sInt k=0;same && k<a->Strings.GetCount();k++)
          if(a->Strings[k]!=b->Strings[k])
            same = 0;
        if(!same)
        {
          if(badpara<3)
          {
            sPrintF(L"        %s.%s at %d,%d:\n",a->Type,a->Class,a->PosX,a->PosY);
            for(sInt k=0;k<a->Words.GetCount() && k<b->Words.GetCount();k++)
              if(a->Words[k]!=b->Words[k])
                sPrintF(L"          word %d: 0x%08x -> 0x%08x\n",
                  k,a->Words[k],b->Words[k]);
            for(sInt k=0;k<a->Strings.GetCount() && k<b->Strings.GetCount();k++)
              if(a->Strings[k]!=b->Strings[k])
                sPrintF(L"          string %d: \"%s\" -> \"%s\"\n",
                  k,a->Strings[k],b->Strings[k]);
          }
          badpara++;
        }
      }

      // The graph, re-derived from geometry on both sides.
      sBool graph = a->InputKeys.GetCount()==b->InputKeys.GetCount();
      for(sInt k=0;graph && k<a->InputKeys.GetCount();k++)
        if(a->InputKeys[k]!=b->InputKeys[k])
          graph = 0;
      if(!graph)
      {
        if(badgraph<3)
          sPrintF(L"        %s.%s at %d,%d: input list changed (%d -> %d)\n",
            a->Type,a->Class,a->PosX,a->PosY,
            a->InputKeys.GetCount(),b->InputKeys.GetCount());
        badgraph++;
      }
    }

    Check(badid==0,L"every operator kept its class, registered or not");
    Check(badgeo==0,L"every operator kept its page and geometry");
    Check(badname==0,L"every store name, hide and bypass survived");
    Check(badpara==0,L"every parameter of every registered operator survived");
    Check(badgraph==0,L"re-derived connections reproduce the original input lists");
  }

  sPrintF(L"\nwz4t_docround: %d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
