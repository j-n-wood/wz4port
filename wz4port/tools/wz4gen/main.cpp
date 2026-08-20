/****************************************************************************/
/***                                                                      ***/
/***   wz4gen — headless Werkkzeug4                                       ***/
/***                                                                      ***/
/****************************************************************************/

// Phase 3. Commands arrive stage by stage:
//
//   list [doc]        registered operators, or the operators in a document
//   describe <class>  (3.4)
//   convert <in> <out> (3.2)
//   render <doc> ...  (stubbed here, completed in phase 4)
//
// Everything here runs on wz4core, which has no GUI, no graphics API and no
// window system linked in.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "base/system.hpp"

/****************************************************************************/

// wDocument's constructor calls this; it is the seam that chooses which
// operator libraries exist. Only `basic` for now — the texture and geometry
// modules arrive in phases 4 and 6.

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)           // two passes: types first, then operators
  {
    sREGOPS(basic,0);
  }
}

/****************************************************************************/
/***                                                                      ***/
/***   list                                                               ***/
/***                                                                      ***/
/****************************************************************************/

static void ListRegistered()
{
  sPrintF(L"%d types, %d operator classes registered\n\n",
    Doc->Types.GetCount(),Doc->Classes.GetCount());

  wType *type;
  sFORALL(Doc->Types,type)
  {
    sInt count = 0;
    wClass *cl;
    sFORALL(Doc->Classes,cl)
      if(cl->OutputType==type)
        count++;

    sPrintF(L"%-16s %-24s %2d operator(s)%s\n",
      type->Symbol,type->Label,count,
      type->Parent ? L"" : L"   [root]");

    sFORALL(Doc->Classes,cl)
    {
      if(cl->OutputType!=type)
        continue;
      sString<256> line;
      line.PrintF(L"    %-24s",cl->Name);
      if(cl->Inputs.GetCount()==0)
      {
        line.Add(L" ()");
      }
      else
      {
        line.Add(L" (");
        wClassInputInfo *in;
        sFORALL(cl->Inputs,in)
        {
          if(_i)
            line.Add(L",");
          if(in->Flags & wCIF_OPTIONAL)
            line.Add(L"?");
          if(in->Flags & wCIF_WEAK)
            line.Add(L"~");
          line.Add(in->Type ? in->Type->Symbol : L"?");
        }
        line.Add(L")");
      }
      sPrintF(L"%s\n",line);
    }
    sPrintF(L"\n");
  }
}

// The interesting part of loading a real document: how much of it did we
// actually understand? Anything whose class was not registered comes back as
// UnknownOp, which is what keeps the rest of the document intact.

static void ListDocument(const sChar *filename)
{
  sPrintF(L"loading %s\n",filename);

  if(!Doc->Load(filename))
  {
    sPrintF(L"wz4gen: could not load <%s>\n",filename);
    sSetErrorCode();
    return;
  }

  // An empty load is a silent failure mode: sLoadObject can report success on
  // a file it did not understand. Treat it as an error so a regression cannot
  // hide behind a zero.
  if(Doc->AllOps.GetCount()==0)
  {
    sPrintF(L"wz4gen: <%s> loaded but contains no operators\n",filename);
    sSetErrorCode();
    return;
  }

  // Two different counts, and they do not agree — deliberately reported as
  // separate numbers. AllOps is what survives in the live document.
  // UnknownOps counts deserialisation events (doc.cpp:1859), which includes
  // default-operator instances that never reach AllOps.

  sPrintF(L"\n  %d page(s), %d operator(s) live\n",
    Doc->Pages.GetCount(),Doc->AllOps.GetCount());
  sPrintF(L"  %d unknown class(es) encountered while reading\n",Doc->UnknownOps);

  if(sGetShellSwitch(L"pages"))
  {
    wPage *page;
    sFORALL(Doc->Pages,page)
      sPrintF(L"    %-40s %-6s %4d op(s)\n",
        page->Name,page->IsTree ? L"tree" : L"stack",
        page->IsTree ? page->Tree.GetCount() : page->Ops.GetCount());
  }

  // Which classes appear, and how often. Sorted by count so the shape of the
  // document is visible at a glance.

  struct Tally { wClass *Class; sInt Count; };
  sArray<Tally> tally;

  wOp *op;
  sFORALL(Doc->AllOps,op)
  {
    Tally *t = 0;
    Tally *scan;
    sFORALL(tally,scan)
      if(scan->Class==op->Class)
        t = scan;
    if(t)
    {
      t->Count++;
    }
    else
    {
      t = tally.AddMany(1);
      t->Class = op->Class;
      t->Count = 1;
    }
  }

  sSortDown(tally,&Tally::Count);

  sPrintF(L"\n  classes used:\n");
  Tally *t;
  sFORALL(tally,t)
  {
    sPrintF(L"    %5d  %-24s %s\n",t->Count,
      t->Class ? t->Class->Name : L"(none)",
      t->Class && t->Class->OutputType ? t->Class->OutputType->Symbol : L"");
  }

  // Store operators are the addressable entry points — the names a CLI or a
  // test case can refer to.

  // wDocument::Load ends by calling Connect(), so the geometry-derived graph
  // has already been type-checked by the time we get here. Counting the
  // operators that came back with an error is the closest thing to a
  // correctness signal available without a reference build: a document whose
  // connections all resolve was almost certainly deserialised correctly.

  // Most errors on a real document are fallout from the classes we have not
  // registered yet, and they arrive by two different routes:
  //
  //   1. UnknownOp is declared with ZERO inputs (basic_ops.ops:239), so an
  //      operator it replaced still sits in geometry that feeds it: "too many
  //      inputs".
  //   2. UnknownOp's output type is AnyType, which does not satisfy a typed
  //      input, so a real consumer above it reports "input has wrong type" —
  //      every MakeTexture(BitmapBase) fed by an unregistered generator, for
  //      instance.
  //
  // Both are attributable to the placeholder, so both are classified by CAUSE
  // rather than by message: an operator is placeholder fallout if it, or any of
  // its inputs, is an UnknownOp. What is left over is the number worth reading.

  sInt connecterrors = 0;
  sInt placeholder = 0;
  sInt residual = 0;

  sFORALL(Doc->AllOps,op)
  {
    if(op->ConnectError || op->ConnectErrorString)
      connecterrors++;
    if(!op->CalcErrorString)
      continue;

    sBool unregistered = op->Class && sCmpString(op->Class->Name,L"UnknownOp")==0;
    wOp *in;
    sFORALL(op->Inputs,in)
      if(in->Class && sCmpString(in->Class->Name,L"UnknownOp")==0)
        unregistered = 1;

    if(unregistered)
      placeholder++;
    else
      residual++;
  }

  sPrintF(L"\n  %d connection error(s)\n",connecterrors);
  sPrintF(L"  %d error(s) from unregistered classes (expected until phase 4)\n",placeholder);
  sPrintF(L"  %d unexplained error(s)%s\n",residual,
    residual ? L"   <- worth looking at" : L"");

  if(sGetShellSwitch(L"errors"))
  {
    sFORALL(Doc->AllOps,op)
      if(op->ConnectErrorString)
        sPrintF(L"    connect  %-20s %s\n",
          op->Class ? op->Class->Name : L"?",op->ConnectErrorString);
    sFORALL(Doc->AllOps,op)
      if(op->CalcErrorString)
        sPrintF(L"    calc     %-20s %s\n",
          op->Class ? op->Class->Name : L"?",op->CalcErrorString);
  }

  // What the unregistered operators actually were. wOp::ForeignClass retains
  // the name the file gave, so this says exactly which modules would have to be
  // registered — which is the useful input to planning phase 4 and 6.

  struct Foreign { wDocName Class; wDocName Type; sInt Count; };
  sArray<Foreign> foreign;

  sFORALL(Doc->AllOps,op)
  {
    if(op->ForeignClass.IsEmpty())
      continue;
    Foreign *f = 0;
    Foreign *scan;
    sFORALL(foreign,scan)
      if(scan->Class==op->ForeignClass && scan->Type==op->ForeignType)
        f = scan;
    if(f)
    {
      f->Count++;
    }
    else
    {
      f = foreign.AddMany(1);
      f->Class = op->ForeignClass;
      f->Type = op->ForeignType;
      f->Count = 1;
    }
  }

  sSortDown(foreign,&Foreign::Count);

  sPrintF(L"\n  %d distinct unregistered class(es)",foreign.GetCount());
  if(sGetShellSwitch(L"unknown"))
  {
    sPrintF(L":\n");
    Foreign *f;
    sFORALL(foreign,f)
      sPrintF(L"    %5d  %-28s %s\n",f->Count,f->Class,f->Type);
  }
  else
  {
    sPrintF(L" (-unknown to list them)\n");
  }

  sPrintF(L"\n  %d store(s)",Doc->Stores.GetCount());
  if(sGetShellSwitch(L"stores"))
  {
    sPrintF(L":\n");
    sFORALL(Doc->Stores,op)
      sPrintF(L"    %-32s %s\n",op->Name,
        op->Class && op->Class->OutputType ? op->Class->OutputType->Symbol : L"");
  }
  else
  {
    sPrintF(L" (-stores to list them)\n");
  }
}

/****************************************************************************/
/***                                                                      ***/
/***   identity — does a document survive a load/save by this build?       ***/
/***                                                                      ***/
/****************************************************************************/

// Loads, saves, reloads, and checks that every operator still claims the same
// class. This is the test for wOp::ForeignClass: without it, a build that does
// not know every module silently rewrites unregistered operators as UnknownOp
// and the document is permanently damaged.
//
// It does NOT claim parameter fidelity for unregistered operators — the reader
// skipped their parameter words, and this port deliberately does not carry
// those through. See wz4port/patches/07.

struct Ident { wDocName Class; wDocName Type; sInt Count; };

static void TallyIdents(sArray<Ident> &out)
{
  wOp *op;
  sFORALL(Doc->AllOps,op)
  {
    // What this operator claims to be: its retained original name if it was
    // substituted, otherwise its real class.
    wDocName cls = op->ForeignClass.IsEmpty()
      ? (op->Class ? wDocName(op->Class->Name) : wDocName(L"?"))
      : op->ForeignClass;
    wDocName typ = op->ForeignType.IsEmpty()
      ? (op->Class && op->Class->OutputType ? wDocName(op->Class->OutputType->Symbol) : wDocName(L"?"))
      : op->ForeignType;

    Ident *f = 0;
    Ident *scan;
    sFORALL(out,scan)
      if(scan->Class==cls && scan->Type==typ)
        f = scan;
    if(f)
    {
      f->Count++;
    }
    else
    {
      f = out.AddMany(1);
      f->Class = cls;
      f->Type = typ;
      f->Count = 1;
    }
  }
  sSortDown(out,&Ident::Count);
}

static void CheckIdentity(const sChar *filename,const sChar *tempfile)
{
  sPrintF(L"identity: %s\n",filename);

  if(!Doc->Load(filename) || Doc->AllOps.GetCount()==0)
  {
    sPrintF(L"wz4gen: could not load <%s>\n",filename);
    sSetErrorCode();
    return;
  }

  sArray<Ident> before;
  TallyIdents(before);
  sInt opsbefore = Doc->AllOps.GetCount();
  sInt pagesbefore = Doc->Pages.GetCount();

  if(!Doc->Save(tempfile))
  {
    sPrintF(L"wz4gen: could not save <%s>\n",tempfile);
    sSetErrorCode();
    return;
  }

  // Fresh document. wDocument's constructor points the global Doc at itself.
  delete Doc;
  Doc = new wDocument;

  if(!Doc->Load(tempfile) || Doc->AllOps.GetCount()==0)
  {
    sPrintF(L"wz4gen: could not reload <%s>\n",tempfile);
    sSetErrorCode();
    return;
  }

  sArray<Ident> after;
  TallyIdents(after);

  sPrintF(L"  %d -> %d operator(s), %d -> %d page(s)\n",
    opsbefore,Doc->AllOps.GetCount(),pagesbefore,Doc->Pages.GetCount());
  sPrintF(L"  %d -> %d distinct class identit(ies)\n",
    before.GetCount(),after.GetCount());

  sInt bad = 0;

  if(opsbefore!=Doc->AllOps.GetCount())
  {
    sPrintF(L"  FAIL operator count changed\n");
    bad++;
  }
  if(pagesbefore!=Doc->Pages.GetCount())
  {
    sPrintF(L"  FAIL page count changed\n");
    bad++;
  }

  Ident *b;
  sFORALL(before,b)
  {
    Ident *a = 0;
    Ident *scan;
    sFORALL(after,scan)
      if(scan->Class==b->Class && scan->Type==b->Type)
        a = scan;
    if(!a)
    {
      sPrintF(L"  FAIL %s.%s (%d) vanished\n",b->Type,b->Class,b->Count);
      bad++;
    }
    else if(a->Count!=b->Count)
    {
      sPrintF(L"  FAIL %s.%s %d -> %d\n",b->Type,b->Class,b->Count,a->Count);
      bad++;
    }
  }
  sFORALL(after,b)
  {
    Ident *scan;
    sBool found = 0;
    sFORALL(before,scan)
      if(scan->Class==b->Class && scan->Type==b->Type)
        found = 1;
    if(!found)
    {
      sPrintF(L"  FAIL %s.%s (%d) appeared\n",b->Type,b->Class,b->Count);
      bad++;
    }
  }

  if(bad)
  {
    sPrintF(L"  %d problem(s)\n",bad);
    sSetErrorCode();
  }
  else
  {
    sPrintF(L"  ok: every operator kept its class through a save/reload\n");
  }
}

/****************************************************************************/
/***                                                                      ***/
/***   main                                                               ***/
/***                                                                      ***/
/****************************************************************************/

static void Usage()
{
  sPrint(L"wz4gen — headless Werkkzeug4\n");
  sPrint(L"\n");
  sPrint(L"usage: wz4gen list [document.wz4] [-pages] [-stores] [-unknown] [-errors]\n");
  sPrint(L"       wz4gen identity <document.wz4> <scratch.wz4>\n");
  sPrint(L"\n");
  sPrint(L"  list          registered operators, by output type\n");
  sPrint(L"  list <doc>    the operators in a document, with a class tally\n");
  sPrint(L"    -pages      also list every page\n");
  sPrint(L"    -stores     also list every store name\n");
  sPrint(L"    -unknown    also list the unregistered classes, by name\n");
  sPrint(L"    -errors     also list connection and calc errors\n");
  sPrint(L"  identity      load, save, reload, and check every operator kept\n");
  sPrint(L"                its class — including ones this build cannot load\n");
  sPrint(L"\n");
  sPrint(L"Switches go after the filename: Altona's shell parser treats the\n");
  sPrint(L"token after a -switch as that switch's first parameter.\n");
  sPrint(L"\n");
  sPrint(L"describe, convert and render arrive later in phase 3.\n");
}

void sMain()
{
  const sChar *command = sGetShellParameter(0,0);
  if(!command)
  {
    Usage();
    sSetErrorCode();
    return;
  }

  Doc = new wDocument;

  if(sCmpString(command,L"list")==0)
  {
    const sChar *file = sGetShellParameter(0,1);
    if(file)
      ListDocument(file);
    else
      ListRegistered();
  }
  else if(sCmpString(command,L"identity")==0)
  {
    const sChar *file = sGetShellParameter(0,1);
    const sChar *temp = sGetShellParameter(0,2);
    if(!file || !temp)
    {
      sPrint(L"usage: wz4gen identity <document.wz4> <scratch.wz4>\n");
      sSetErrorCode();
    }
    else
    {
      CheckIdentity(file,temp);
    }
  }
  else
  {
    sPrintF(L"wz4gen: unknown command <%s>\n\n",command);
    Usage();
    sSetErrorCode();
  }

  delete Doc;
}

/****************************************************************************/
