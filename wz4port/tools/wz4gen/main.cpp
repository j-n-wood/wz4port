/****************************************************************************/
/***                                                                      ***/
/***   wz4gen — headless Werkkzeug4                                       ***/
/***                                                                      ***/
/****************************************************************************/

// Phase 3, extended by phases 4 and 6:
//
//   list [doc]         registered operators, or the operators in a document
//   describe <class>   (3.4)
//   convert <in> <out> (3.2)
//   identity <doc>     load/save/reload with every class intact
//   render <doc> ...   evaluate one store; -out .png for a bitmap (4.2),
//                      .obj for a mesh (6.2)
//   sweep <doc>        evaluate EVERY store and check every mesh (6.3)
//   diff <a> <b>       why two renders differ, in numbers and as an image
//
// Everything here runs on wz4core/wz4tex/wz4geo, which have no GUI, no graphics
// API and no window system linked in.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz3_bitmap_code.hpp"    // GenBitmap, for render
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "wz4frlib/wz4_mesh.hpp"           // Wz4Mesh, for render
#include "mesh_check.hpp"                  // the 6.3 invariant battery
#include "util/image.hpp"                  // sImage::SavePNG
#include "base/system.hpp"
#include "meta.hpp"
#include "json.hpp"               // wFormatFloat — Altona has no %g
#include "wz4t.hpp"

// Where opsmeta puts its output. Compiled in so the tool works with no
// arguments in a normal build; override with -meta <dir>.
#ifndef WZ4GEN_META_DIR
#define WZ4GEN_META_DIR L"meta"
#endif

/****************************************************************************/

// wDocument's constructor calls this; it is the seam that chooses which
// operator libraries exist.
//
// Order matters: wz3_bitmap's GenBitmap derives from basic's BitmapBase and
// wz4_mesh's Wz4Mesh derives from basic's MeshBase, so basic has to register its
// types first. sREGOPS runs types on pass 0 and operators on pass 1, which is
// what makes cross-module inheritance work.
//
// The mesh modules joined in stage 6.2. wz3_bitmap comes before wz4_mesh because
// two mesh operators take a bitmap input.

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(wz3_bitmap,0);
    sREGOPS(wz4_anim,0);
    sREGOPS(wz4_mesh,0);
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

  // Dispatch on the extension, like `convert` does. `list` used to call
  // Doc->Load unconditionally, which is the binary .wz4 reader, so it could not
  // read the text format this project invented — noticed when stage 5.3's gate
  // asked for exactly that comparison.
  const sChar *ext = sFindFileExtension(filename);
  const sBool text = ext && sCmpStringI(ext,L"wz4t")==0;

  if(text)
  {
    const sChar *dir = sGetShellParameter(L"meta",0);
    if(!dir)
      dir = WZ4GEN_META_DIR;

    wMetaLibrary meta;
    if(!meta.LoadDirectory(dir))
    {
      sPrintF(L"wz4gen: no metadata in <%s>\n",dir);
      sSetErrorCode();
      return;
    }
    if(!wReadWz4t(filename,meta,wWZ4T_ALLOWUNKNOWN))
    {
      sPrintF(L"wz4gen: could not read <%s>\n",filename);
      sSetErrorCode();
      return;
    }
    Doc->Connect();
  }
  else if(!Doc->Load(filename))
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

  // The derived graph, per operator, in slot order. Nothing here is stored in
  // the document: every entry was computed from block rectangles by
  // wDocument::Connect(). This is what the editor's inspector shows, and the
  // two agree by construction because they are the same call — printing it makes
  // that checkable rather than merely true.
  if(sGetShellSwitch(L"inputs"))
  {
    for(sInt p=0;p<Doc->Pages.GetCount();p++)
    {
      wPage *page = Doc->Pages[p];
      if(page->IsTree)
        continue;
      sPrintF(L"\n  page \"%s\"\n",page->Name);

      for(sInt i=0;i<page->Ops.GetCount();i++)
      {
        wStackOp *op = page->Ops[i];
        sString<512> line;
        line.PrintF(L"    %-24s %-16s at %3d,%-3d %2dx%-2d  <- ",
          op->Name.IsEmpty() ? L"-" : (const sChar *) op->Name,
          op->Class ? op->Class->Name : L"?",
          op->PosX,op->PosY,op->SizeX,op->SizeY);

        if(op->Inputs.GetCount()==0)
        {
          line.Add(L"(nothing)");
        }
        else
        {
          for(sInt k=0;k<op->Inputs.GetCount();k++)
          {
            wStackOp *in = (wStackOp *) op->Inputs[k];
            if(k)
              line.Add(L", ");
            sString<64> one;
            const sChar *nm = L"?";
            if(!in->Name.IsEmpty())
              nm = in->Name;
            else if(in->Class)
              nm = in->Class->Name;
            one.PrintF(L"%s@x%d",nm,in->PosX);
            line.Add(one);
          }
        }

        if(op->Hide)   line.Add(L"  [hidden]");
        if(op->Bypass) line.Add(L"  [bypass]");
        sPrintF(L"%s\n",line);
      }
    }
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
/***   describe — the full parameter description of one operator           ***/
/***                                                                      ***/
/****************************************************************************/

// This is what wClass cannot tell you. It comes entirely from the metadata that
// opsmeta emits, and it is the same description the text-format reader and the
// editor's parameter panel will use.

static const sChar *SpaceName(sInt space)
{
  switch(space)
  {
  case wMS_WORDS:   return L"word";
  case wMS_STRINGS: return L"string";
  case wMS_LINKS:   return L"link";
  default:          return L"-";
  }
}

static void DescribeParam(const wMetaParam &p)
{
  // Storage first: it is the part a reader or an editor actually needs, and the
  // part that is easiest to get wrong — there are three independent offset
  // spaces, so an offset on its own is ambiguous.
  sString<64> where;
  if(p.Space==wMS_NONE)
    where.PrintF(L"%-9s",L"-");
  else
    where.PrintF(L"%s %-2d",SpaceName(p.Space),p.Offset);

  sString<64> kind;
  if(p.Layout==wML_VECTOR)
    kind.PrintF(L"%s.xyzw",p.Kind);
  else if(p.Layout==wML_ARRAY)
    kind.PrintF(L"%s[%d]",p.Kind,p.Count);
  else
    kind.PrintF(L"%s",p.Kind);

  sString<512> line;
  line.PrintF(L"  %-9s %-13s %s",where,kind,p.Symbol);

  if(p.Continues)
    line.Add(L"  (continues — shares the word above)");
  if(p.RebuildOnChange)
    line.Add(L"  (rebuilds the panel)");

  sPrintF(L"%s\n",line);

  // Ranges and defaults, where the kind has them.
  if(p.Kind==L"float" || p.Kind==L"int")
  {
    sChar mn[64],mx[64],st[64];
    wFormatFloat(mn,sCOUNTOF(mn),p.Min);
    wFormatFloat(mx,sCOUNTOF(mx),p.Max);
    wFormatFloat(st,sCOUNTOF(st),p.Step);

    sString<256> r;
    r.PrintF(L"            range %s .. %s  step %s",mn,mx,st);
    if(p.LogStep)
      r.Add(L" (log)");
    sPrintF(L"%s\n",r);

    if(p.DefaultsF.GetCount())
    {
      sString<256> d;
      d.PrintF(L"            default ");
      for(sInt i=0;i<p.DefaultsF.GetCount();i++)
      {
        if(i)
          d.Add(L", ");
        sString<64> one;
        if(p.Kind==L"int")
        {
          one.PrintF(L"%d",p.DefaultsI[i]);
        }
        else
        {
          sChar num[64];
          wFormatFloat(num,sCOUNTOF(num),p.DefaultsF[i]);
          one.PrintF(L"%s",num);
        }
        d.Add(one);
      }
      sPrintF(L"%s\n",d);
    }
  }

  if(p.Kind==L"color")
  {
    sString<256> d;
    d.PrintF(L"            channels \"%s\"",p.Channels);
    if(p.DefaultsI.GetCount())
    {
      d.Add(L"  default ");
      for(sInt i=0;i<p.DefaultsI.GetCount();i++)
      {
        if(i)
          d.Add(L", ");
        sString<32> one;
        one.PrintF(L"#%08x",sU32(p.DefaultsI[i]));
        d.Add(one);
      }
    }
    sPrintF(L"%s\n",d);
  }

  if(p.Kind==L"string" || p.Kind==L"filein" || p.Kind==L"fileout")
  {
    if(!p.DefaultString.IsEmpty())
      sPrintF(L"            default \"%s\"\n",p.DefaultString);
  }

  if(p.Kind==L"char")
    sPrintF(L"            capacity %d character(s), %d word(s)\n",p.Capacity,p.Words);

  // Choice widgets: several controls can share one integer at different
  // shifts, which is the thing about `flags` that surprises people.
  for(sInt i=0;i<p.Widgets.GetCount();i++)
  {
    const wMetaWidget *w = p.Widgets[i];
    sString<512> line2;
    line2.PrintF(L"            widget shift %-2d mask 0x%08x  ",w->Shift,sU32(w->Mask));
    for(sInt k=0;k<w->Choices.GetCount();k++)
    {
      if(k)
        line2.Add(L" | ");
      sString<64> one;
      one.PrintF(L"%s=%d",w->Choices[k].Label,w->Choices[k].Value);
      line2.Add(one);
    }
    sPrintF(L"%s\n",line2);
  }
}

static void Describe(wMetaLibrary &meta,const sChar *what)
{
  const wMetaClass *c = meta.Find(what);
  if(!c)
  {
    sPrintF(L"wz4gen: no metadata for <%s>\n",what);
    sPrintF(L"        try \"OutputType.ClassName\", or wz4gen list\n");
    sSetErrorCode();
    return;
  }

  sPrintF(L"%s.%s\n",c->OutputType,c->Name);
  sPrintF(L"  tab %s, column %d%s\n",c->TabType,c->Column,
    c->HasCode ? L"" : L", no code body");

  if(c->Flags.GetCount())
  {
    sString<256> f;
    f.PrintF(L"  flags");
    for(sInt i=0;i<c->Flags.GetCount();i++)
    {
      f.Add(L" ");
      f.Add(c->Flags[i]);
    }
    sPrintF(L"%s\n",f);
  }

  sPrintF(L"  %d parameter word(s), %d string(s)\n\n",c->ParaWords,c->ParaStrings);

  if(c->Params.GetCount()==0)
  {
    sPrintF(L"  (no parameters)\n");
    return;
  }

  for(sInt i=0;i<c->Params.GetCount();i++)
    DescribeParam(*c->Params[i]);

  // The array block, if there is one. Without this, `describe` silently implies
  // that an operator with array rows has no rows — which is how a Gradient case
  // came to be written with none and rendered black.
  if(c->Array)
  {
    sPrintF(L"\n  array: %d word(s) per row, `element { }` in .wz4t\n",
      c->ArrayWords);
    for(sInt i=0;i<c->Array->Params.GetCount();i++)
      DescribeParam(*c->Array->Params[i]);
  }

  // The word budget and the parameters should agree. They come from different
  // fields of the same emitter, so a mismatch means the metadata is wrong —
  // worth saying out loud rather than leaving a reader to notice.
  sInt used = 0;
  for(sInt i=0;i<c->Params.GetCount();i++)
    if(c->Params[i]->Space==wMS_WORDS)
      used += c->Params[i]->Words;
  if(used!=c->ParaWords)
    sPrintF(L"\n  note: parameters account for %d of %d word(s)"
            L" — the rest is padding\n",used,c->ParaWords);
}

/****************************************************************************/
/***                                                                      ***/
/***   checkmeta — read the whole metadata and check it hangs together     ***/
/***                                                                      ***/
/****************************************************************************/

// opsmeta validates what it emits; this validates it from the CONSUMER side,
// which is a different question. It is also the first thing that reads the
// schema in anger, and a schema is only proven useful once something does.

static void CheckMeta(wMetaLibrary &meta)
{
  sInt problems = 0;
  sInt classes = 0;
  sInt params = 0;
  sInt padded = 0;
  sInt choices = 0;
  sInt unnamed = 0;
  sInt arrays = 0;

  for(sInt ci=0;ci<meta.GetClassCount();ci++)
  {
    const wMetaClass *c = meta.GetClass(ci);
    classes++;

    if(c->Name.IsEmpty())
    {
      sPrintF(L"  FAIL class %d has no name\n",ci);
      problems++;
    }
    if(c->OutputType.IsEmpty())
    {
      sPrintF(L"  FAIL %s has no output type\n",c->Name);
      problems++;
    }

    // Word accounting. Parameters may legitimately leave gaps — "padding"
    // reserves words for future use and produces no parameter at all — so a
    // shortfall is a note, but an OVERRUN means the metadata contradicts
    // itself.
    sInt used = 0;
    sInt maxend = 0;

    for(sInt pi=0;pi<c->Params.GetCount();pi++)
    {
      const wMetaParam *p = c->Params[pi];
      params++;

      // A missing symbol is legal in the DSL: `label "Edit";`,
      // `action "Invert" (1);` and even `fileout "Filename";` give a label and
      // no name (parse.cpp:623-637 makes both optional). So this is not an
      // error — but a parameter that OWNS STORAGE and has no name cannot be
      // addressed by name, which the .wz4t reader will have to handle with a
      // label fallback. Counted, not failed.
      if(p->Symbol.IsEmpty() && p->Space!=wMS_NONE)
      {
        unnamed++;
        if(sGetShellSwitch(L"verbose"))
          sPrintF(L"  note %s.%s: %s at %s %d has a label (\"%s\") but no symbol\n",
            c->OutputType,c->Name,p->Kind,SpaceName(p->Space),p->Offset,p->Label);
      }

      if(p->Space==wMS_WORDS)
      {
        if(!p->Continues)
        {
          used += p->Words;
          maxend = sMax(maxend,p->Offset+p->Words);
        }

        if(p->Offset<0)
        {
          sPrintF(L"  FAIL %s.%s %s: word space but offset %d\n",
            c->OutputType,c->Name,p->Symbol,p->Offset);
          problems++;
        }
      }

      if(p->Space==wMS_STRINGS && p->Offset>=c->ParaStrings)
      {
        sPrintF(L"  FAIL %s.%s %s: string %d of %d\n",
          c->OutputType,c->Name,p->Symbol,p->Offset,c->ParaStrings);
        problems++;
      }

      // A continues parameter must land on a word some earlier parameter owns.
      if(p->Continues)
      {
        const wMetaParam *owner = c->FindParam(p->Symbol);
        if(!owner || owner->Continues)
        {
          sPrintF(L"  FAIL %s.%s %s: continues but owns nothing\n",
            c->OutputType,c->Name,p->Symbol);
          problems++;
        }
        else if(owner->Offset!=p->Offset)
        {
          sPrintF(L"  FAIL %s.%s %s: continues at word %d, owner at %d\n",
            c->OutputType,c->Name,p->Symbol,p->Offset,owner->Offset);
          problems++;
        }
      }

      // Every choice must sit inside its widget's mask once shifted, or the
      // editor would write bits that belong to a neighbour.
      for(sInt wi=0;wi<p->Widgets.GetCount();wi++)
      {
        const wMetaWidget *w = p->Widgets[wi];
        for(sInt k=0;k<w->Choices.GetCount();k++)
        {
          choices++;
          sInt bits = w->Choices[k].Value << w->Shift;
          if((bits & ~w->Mask)!=0)
          {
            sPrintF(L"  FAIL %s.%s %s: choice %s = %d at shift %d escapes mask 0x%08x\n",
              c->OutputType,c->Name,p->Symbol,w->Choices[k].Label,
              w->Choices[k].Value,w->Shift,sU32(w->Mask));
            problems++;
          }
        }
      }
    }

    if(maxend>c->ParaWords)
    {
      sPrintF(L"  FAIL %s.%s: parameters reach word %d but only %d declared\n",
        c->OutputType,c->Name,maxend,c->ParaWords);
      problems++;
    }
    else if(used<c->ParaWords)
    {
      padded++;
    }

    // The table widget's row, in its own word space.
    if(c->Array)
    {
      arrays++;
      sInt rowend = 0;
      for(sInt pi=0;pi<c->Array->Params.GetCount();pi++)
      {
        const wMetaParam *p = c->Array->Params[pi];
        params++;
        if(p->Space==wMS_WORDS && !p->Continues)
          rowend = sMax(rowend,p->Offset+p->Words);
      }
      if(rowend>c->ArrayWords)
      {
        sPrintF(L"  FAIL %s.%s: array row reaches word %d but only %d declared\n",
          c->OutputType,c->Name,rowend,c->ArrayWords);
        problems++;
      }
    }
  }

  sPrintF(L"  %d class(es), %d parameter(s), %d choice value(s)\n",
    classes,params,choices);
  sPrintF(L"  %d class(es) with a table widget\n",arrays);
  sPrintF(L"  %d class(es) with reserved words (padding)\n",padded);
  sPrintF(L"  %d storage-bearing parameter(s) with no symbol"
          L" (need a label fallback; -verbose to list)\n",unnamed);
  sPrintF(L"  %d problem(s)\n",problems);

  if(problems)
    sSetErrorCode();
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
  sPrint(L"  describe      the full parameter description of one operator\n");
  sPrint(L"  checkmeta     read all the metadata and check it hangs together\n");
  sPrint(L"  convert       .wz4 <-> .wz4t, direction from the extensions\n");
  sPrint(L"  render        evaluate one operator; -out .png for a bitmap, .obj for a mesh\n");
  sPrint(L"  sweep         evaluate every store and check every mesh it finds\n");
  sPrint(L"  diff          compare two PNGs: how much, where, and a diff image\n");
  sPrint(L"\n");
  sPrint(L"Switches go after the filename: Altona's shell parser treats the\n");
  sPrint(L"token after a -switch as that switch's first parameter.\n");
}

/****************************************************************************/
/***   render, mesh half — stage 6.2                                      ***/
/****************************************************************************/

// The bitmap reporter above prints "uniform or structured, plus a checksum",
// because that is nearly all an image will tell you without being looked at. A
// mesh tells you much more, and every line here is something a reviewer can
// check against what the operator claims to do:
//
//   counts        the first thing wrong when a generator miscounts a ring
//   arity         triangles vs quads — several operators promise one or other
//   degenerate    a face with a repeated vertex position; upstream has a
//                 predicate for it, and it is the classic symptom of a
//                 tesselation off-by-one
//   bounds        catches a missing or doubled transform, and is exact rather
//                 than approximate for most generators
//   checksum      over every vertex position, for 6.3 to lock
//
// Floats go through wFormatFloat, not Altona's %f, which renders 4.0f as
// "4.00000023" (architecture.md A18).

static void PrintVec(const sChar *label,const sVector31 &v)
{
  sChar x[32],y[32],z[32];
  wFormatFloat(x,sCOUNTOF(x),v.x);
  wFormatFloat(y,sCOUNTOF(y),v.y);
  wFormatFloat(z,sCOUNTOF(z),v.z);
  sPrintF(L"  %s %s %s %s\n",label,x,y,z);
}

static void ReportMesh(Wz4Mesh *mesh,const sChar *out)
{
  const sInt vc = mesh->Vertices.GetCount();
  const sInt fc = mesh->Faces.GetCount();

  sInt tris = 0,quads = 0,other = 0,degenerate = 0;
  for(sInt i=0;i<fc;i++)
  {
    switch(mesh->Faces[i].Count)
    {
    case 3:  tris++;  break;
    case 4:  quads++; break;
    default: other++; break;
    }
    if(mesh->IsDegenerateFace(i))
      degenerate++;
  }

  sPrintF(L"  %d vertices, %d faces (%d tri, %d quad",vc,fc,tris,quads);
  if(other)
    sPrintF(L", %d OTHER",other);
  sPrintF(L"), %d clusters\n",mesh->Clusters.GetCount());

  if(degenerate)
    sPrintF(L"  %d degenerate face(s)\n",degenerate);

  // FNV over the raw position words. Bit-exact and endian-stable enough for the
  // one thing it is for — telling two renders on this machine apart — and 6.3
  // is where it gets locked, after the outputs have been reviewed.
  if(vc>0)
  {
    sVector31 lo = mesh->Vertices[0].Pos, hi = lo;
    sU64 sum = 0;
    for(sInt i=0;i<vc;i++)
    {
      const sVector31 &p = mesh->Vertices[i].Pos;
      lo.x = sMin(lo.x,p.x); hi.x = sMax(hi.x,p.x);
      lo.y = sMin(lo.y,p.y); hi.y = sMax(hi.y,p.y);
      lo.z = sMin(lo.z,p.z); hi.z = sMax(hi.z,p.z);
      sum = sum*1099511628211ULL ^ sU64(*(const sU32 *)&p.x);
      sum = sum*1099511628211ULL ^ sU64(*(const sU32 *)&p.y);
      sum = sum*1099511628211ULL ^ sU64(*(const sU32 *)&p.z);
    }
    PrintVec(L"min",lo);
    PrintVec(L"max",hi);
    sPrintF(L"  checksum %08x%08x\n",sU32(sum>>32),sU32(sum));
  }

  if(!out)
    return;

  // Dispatch on the extension, as `convert` does. OBJ is the only mesh format
  // this build can write — SaveOBJ is upstream's, in wz4_mesh_obj.cpp.
  if(sFindString(out,L".obj")<0)
  {
    sPrintF(L"wz4gen: <%s> is not a .obj — a mesh cannot be written as an image\n",out);
    sSetErrorCode();
    return;
  }

  if(!mesh->SaveOBJ(out))
  {
    sPrintF(L"wz4gen: could not write <%s>\n",out);
    sSetErrorCode();
    return;
  }

  // SaveOBJ returns success without checking that anything landed —
  // sTextFileWriter swallows a failed open — so the file is read back. Same
  // lesson as the PNG runner in 4.2, which passed on a directory that did not
  // exist (architecture.md A39).
  sDInt size = 0;
  sU8 *bytes = sLoadFile(out,size);
  if(!bytes || size<=0)
  {
    sPrintF(L"wz4gen: SaveOBJ reported success but <%s> is missing or empty\n",out);
    sSetErrorCode();
    delete[] bytes;
    return;
  }
  delete[] bytes;

  sPrintF(L"  wrote %s (%d bytes)\n",out,sInt(size));
}

/****************************************************************************/
/***   sweep — evaluate every store in a document, check every mesh       ***/
/****************************************************************************/

// Stage 6.3, Suite B. The hand-written cases in tests/mesh_cases.cpp ask each
// operator the easy question; this asks the questions the original authors
// asked. example.wz4 alone drives 39 of the 45 registered mesh operators with
// parameter values nobody on this port chose, which is the closest thing to a
// reference build available here.
//
// It can only check INVARIANTS, never values — a demo graph's correct output is
// unknown. What it is for is the failure mode the per-operator cases are worst
// at: an assertion or a corruption deep inside an upstream algorithm, reached by
// a parameter combination we would not have thought to write.
//
// It sweeps every mesh OPERATOR, not every store. Measured: example.wz4 has 54
// stores of which only 15 yield a mesh, while the document contains 39 distinct
// mesh operator CLASSES. Sweeping stores would have covered a third of what is
// there and reported a number that looked respectable — the interesting
// operators are mid-graph, not at the end of it.
//
// Operators that do not evaluate are counted, not failed: these documents are
// full of scenes, materials and sequencer nodes from modules this build does not
// have, and a mesh operator downstream of one of those cannot run either.

static void SweepDocument(const sChar *file,sBool verbose)
{
  wType *meshtype = Doc->FindType(L"Wz4Mesh");
  if(!meshtype)
  {
    sPrint(L"wz4gen: no Wz4Mesh type registered\n");
    sSetErrorCode();
    return;
  }

  Doc->Connect();

  sInt meshes = 0,failed = 0,violations = 0,empty = 0,badops = 0;

  // Which classes actually got exercised. Coverage is the point of this sweep,
  // so it has to be reported rather than assumed from the class histogram —
  // an operator present in the document but always downstream of an
  // unregistered module is present and never run.
  sArray<wClass *> covered;

  wPage *page;
  sFORALL(Doc->Pages,page)
  {
    wOp *op;
    sFORALL(page->Ops,op)
    {
      if(!op->Class || op->Class->OutputType!=meshtype)
        continue;

      badops++;
      wObject *obj = Doc->CalcOp(op);
      if(!obj)
      {
        failed++;
        continue;
      }

      if(!obj->IsType(meshtype))
      {
        // A Wz4Mesh-typed operator that produced something else would be a
        // genuine defect, not an expected skip.
        sPrintF(L"    VIOLATION  %s.%s produced a %s\n",
          op->Class->OutputType->Symbol,op->Class->Name,
          obj->Type ? obj->Type->Symbol : L"?");
        violations++;
        obj->Release();
        continue;
      }

      meshes++;
      if(!sFindPtr(covered,op->Class))
        covered.AddTail(op->Class);

      const wMeshFacts f = wMeshMeasure((Wz4Mesh *)obj);

      // An empty mesh is legal — Text3D and Path3D produce one by design in
      // this build (patch 12) — but it is worth separating from a real one,
      // because "everything passed" over a document of empty meshes would say
      // nothing at all.
      if(f.Faces==0 && f.Verts==0)
      {
        empty++;
        if(verbose)
          sPrintF(L"  %-24s empty     (%s)\n",op->Name,op->Class->Name);
      }
      else
      {
        // Violations always print; the facts only with -v. A sweep over
        // hundreds of operators is unreadable otherwise, and what a CI run
        // needs is the exceptions.
        if(verbose)
          violations += wMeshReport(op->Class->Name,f);
        else if(f.Violations())
          violations += wMeshReport(op->Class->Name,f);
      }

      obj->Release();
    }
  }

  sPrintF(L"\n%d mesh operator(s): %d evaluated (%d empty), %d could not run\n",
    badops,meshes,empty,failed);
  sPrintF(L"%d distinct operator class(es) exercised, %d violation(s)\n",
    covered.GetCount(),violations);

  if(sGetShellSwitch(L"classes"))
  {
    wClass *cl;
    sFORALL(covered,cl)
      sPrintF(L"    %s\n",cl->Name);
  }

  if(violations)
    sSetErrorCode();

  // A sweep that found no meshes has not proved anything, and would pass
  // silently for the rest of time if a registration were lost. Same lesson as
  // the zero-operator load check in phase 3.
  if(meshes==0)
  {
    sPrintF(L"wz4gen: <%s> yielded no meshes — nothing was actually checked\n",file);
    sSetErrorCode();
  }
}

/****************************************************************************/
/***   diff — why two renders differ, in numbers and as an image          ***/
/****************************************************************************/

// A byte comparison says "these files differ" and nothing else, which is not
// enough to act on: one pixel off by one is a rounding difference to note, and
// half the image off by fifty is a broken operator. This reports the shape of
// the difference and writes an amplified image of it.

static sBool LoadImage(const sChar *name,sImage &img)
{
  sDInt size = 0;
  sU8 *bytes = sLoadFile(name,size);
  if(!bytes)
  {
    sPrintF(L"wz4gen: could not read <%s>\n",name);
    return 0;
  }
  // LoadPNG hands straight to stb_image, which sniffs the format itself.
  sBool ok = img.LoadPNG(bytes,sInt(size));
  delete[] bytes;
  if(!ok)
    sPrintF(L"wz4gen: <%s> is not an image stb_image can read\n",name);
  return ok;
}

static void DiffImages(const sChar *namea,const sChar *nameb,const sChar *out)
{
  sImage a,b;
  if(!LoadImage(namea,a) || !LoadImage(nameb,b))
  {
    sSetErrorCode();
    return;
  }

  if(a.SizeX!=b.SizeX || a.SizeY!=b.SizeY)
  {
    sPrintF(L"  size differs: %d x %d vs %d x %d\n",
      a.SizeX,a.SizeY,b.SizeX,b.SizeY);
    sSetErrorCode();
    return;
  }

  const sInt count = a.SizeX*a.SizeY;
  sU8 *pa = (sU8 *) a.Data;
  sU8 *pb = (sU8 *) b.Data;

  sInt differing = 0;
  sInt worst = 0;
  sInt chan[4] = { 0,0,0,0 };

  for(sInt i=0;i<count;i++)
  {
    sBool any = 0;
    for(sInt c=0;c<4;c++)
    {
      sInt d = sAbs(sInt(pa[i*4+c])-sInt(pb[i*4+c]));
      if(d)
      {
        any = 1;
        if(d>chan[c]) chan[c] = d;
        if(d>worst)   worst = d;
      }
    }
    if(any)
      differing++;
  }

  if(!differing)
  {
    sPrintF(L"  identical: %d x %d, every byte\n",a.SizeX,a.SizeY);
    return;
  }

  sPrintF(L"  %d of %d pixels differ (%d%%), worst channel delta %d of 255\n",
    differing,count,differing*100/count,worst);
  sPrintF(L"  per channel, worst delta: b %d, g %d, r %d, a %d\n",
    chan[0],chan[1],chan[2],chan[3]);

  if(out)
  {
    // Amplified so a one-step difference is actually visible: black where the
    // two agree, brightening towards white with the size of the disagreement.
    // Scaled by the worst delta rather than a fixed factor, so the image is
    // legible whether the difference is 1 or 200 — read the numbers above for
    // the magnitude, and this image for WHERE.
    sImage d;
    d.Init(a.SizeX,a.SizeY);
    sU8 *pd = (sU8 *) d.Data;
    for(sInt i=0;i<count;i++)
    {
      sInt m = 0;
      for(sInt c=0;c<4;c++)
        m = sMax(m,sAbs(sInt(pa[i*4+c])-sInt(pb[i*4+c])));
      sInt v = worst ? sMin(255,m*255/worst) : 0;
      pd[i*4+0] = sU8(v);
      pd[i*4+1] = sU8(v);
      pd[i*4+2] = sU8(v);
      pd[i*4+3] = 255;
    }
    if(!d.SavePNG(out))
      sPrintF(L"wz4gen: could not write <%s>\n",out);
    else
      sPrintF(L"  wrote %s (normalised to the worst delta)\n",out);
  }

  sSetErrorCode();
}

/****************************************************************************/

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
  else if(sCmpString(command,L"describe")==0 || sCmpString(command,L"checkmeta")==0)
  {
    sBool check = sCmpString(command,L"checkmeta")==0;
    const sChar *what = sGetShellParameter(0,1);
    const sChar *dir = sGetShellParameter(L"meta",0);
    if(!dir)
      dir = WZ4GEN_META_DIR;

    if(!check && !what)
    {
      sPrint(L"usage: wz4gen describe <ClassName|OutputType.ClassName> [-meta <dir>]\n");
      sSetErrorCode();
    }
    else
    {
      wMetaLibrary meta;
      if(!meta.LoadDirectory(dir))
      {
        sPrintF(L"wz4gen: %s\n",meta.GetError());
        sPrintF(L"        metadata comes from opsmeta; build it, or pass -meta <dir>\n");
        sSetErrorCode();
      }
      else if(check)
      {
        sPrintF(L"checkmeta: %s\n",dir);
        CheckMeta(meta);
      }
      else
      {
        Describe(meta,what);
      }
    }
  }
  else if(sCmpString(command,L"convert")==0)
  {
    const sChar *in = sGetShellParameter(0,1);
    const sChar *out = sGetShellParameter(0,2);
    const sChar *dir = sGetShellParameter(L"meta",0);
    if(!dir)
      dir = WZ4GEN_META_DIR;

    if(!in || !out)
    {
      sPrint(L"usage: wz4gen convert <in> <out> [-meta <dir>]\n");
      sPrint(L"       direction follows the extensions: .wz4 <-> .wz4t\n");
      sSetErrorCode();
      delete Doc;
      return;
    }

    wMetaLibrary meta;
    if(!meta.LoadDirectory(dir))
    {
      sPrintF(L"wz4gen: %s\n",meta.GetError());
      sSetErrorCode();
      delete Doc;
      return;
    }

    sBool intext = sFindString(in,L".wz4t")>=0;
    sBool outtext = sFindString(out,L".wz4t")>=0;

    // Converting, not validating: an operator from a subsystem this port does
    // not build should survive as a placeholder rather than stop the job.
    sBool ok = intext ? wReadWz4t(in,meta,wWZ4T_ALLOWUNKNOWN) : Doc->Load(in);
    if(!ok)
    {
      sPrintF(L"wz4gen: could not read <%s>\n",in);
      sSetErrorCode();
    }
    else
    {
      Doc->Connect();
      ok = outtext ? wWriteWz4tFile(out,meta) : Doc->Save(out);
      if(!ok)
      {
        sPrintF(L"wz4gen: could not write <%s>\n",out);
        sSetErrorCode();
      }
      else
      {
        sPrintF(L"%s -> %s: %d operator(s)\n",in,out,Doc->AllOps.GetCount());
      }
    }
  }
  else if(sCmpString(command,L"render")==0)
  {
    // Evaluates one store and reports what came out, dispatching on the result
    // TYPE rather than on the requested output format — a GenBitmap can be
    // written as a PNG (phase 4) and a Wz4Mesh as an OBJ (stage 6.2), and asking
    // for the wrong one is an error rather than a silent no-op.
    const sChar *file = sGetShellParameter(0,1);
    const sChar *which = sGetShellParameter(L"op",0);
    const sChar *out = sGetShellParameter(L"out",0);
    const sChar *dir = sGetShellParameter(L"meta",0);
    if(!dir)
      dir = WZ4GEN_META_DIR;

    if(!file || !which)
    {
      sPrint(L"usage: wz4gen render <doc> -op <storename> -out <file>\n");
      sSetErrorCode();
      delete Doc;
      return;
    }

    wMetaLibrary meta;
    sBool ok = meta.LoadDirectory(dir);
    if(ok)
      ok = (sFindString(file,L".wz4t")>=0)
        ? wReadWz4t(file,meta,wWZ4T_ALLOWUNKNOWN)
        : Doc->Load(file);

    if(!ok)
    {
      sPrintF(L"wz4gen: could not load <%s>\n",file);
      sSetErrorCode();
    }
    else
    {
      Doc->Connect();
      wOp *op = Doc->FindStore(which);
      if(!op)
      {
        sPrintF(L"wz4gen: no store called \"%s\" in <%s>\n",which,file);
        sPrintF(L"        wz4gen list %s -stores\n",file);
        sSetErrorCode();
      }
      else
      {
        sPrintF(L"%s: %s.%s\n",which,
          op->Class->OutputType->Symbol,op->Class->Name);

        // Evaluate. This runs the connection builder, the command list and the
        // executive, then the operator's own code body — the first time
        // anything in this port actually executes a generator.
        wObject *obj = Doc->CalcOp(op);
        if(!obj)
        {
          sPrintF(L"wz4gen: evaluation produced nothing\n");
          if(op->CalcErrorString)
            sPrintF(L"        %s\n",op->CalcErrorString);
          sSetErrorCode();
        }
        else
        {
          wType *bmtype = Doc->FindType(L"GenBitmap");
          if(bmtype && obj->IsType(bmtype))
          {
            GenBitmap *bm = (GenBitmap *)obj;

            // "How many pixels are non-zero" turned out to be a poor signal:
            // a pixel is one sU64 of four sU16 channels, so an opaque black
            // gradient is 100% non-zero on alpha alone. What actually
            // distinguishes an operator that worked is whether the image has
            // STRUCTURE, so report whether every pixel is identical.
            //
            // The checksum is reported but deliberately not asserted anywhere
            // yet. Locking bit-exact expectations is stage 4.4's job, after the
            // outputs have been reviewed by eye — locking them earlier would
            // just freeze whatever the code does today, which is precisely the
            // failure mode 06-phase-texture.md warns about.
            // Alpha is tracked and reported separately from the checksum
            // because a transparent PNG looks like a WHITE one in every viewer,
            // and that is indistinguishable by eye from an operator that filled
            // the bitmap with white or did nothing at all. Found the hard way:
            // Color's `invert` mode inverts alpha along with RGB
            // (wz3_bitmap_code.cpp:919 xors every channel with 0x7fff), so an
            // opaque input inverts to fully transparent and reviews as blank.
            //
            // 0x7fff is 1.0 here, not 0x8000: GetColor64 maps 8-bit 0xff to
            // 0x7fff (wz3_bitmap_code.cpp:222).
            sBool uniform = 1;
            sU64 sum = 0;
            sU32 amin = 0xffff, amax = 0;
            for(sInt i=0;i<bm->Size;i++)
            {
              if(bm->Data[i]!=bm->Data[0])
                uniform = 0;
              sum = sum*1099511628211ULL ^ bm->Data[i];
              sU32 a = sU32(bm->Data[i]>>48)&0xffff;
              amin = sMin(amin,a);
              amax = sMax(amax,a);
            }

            // Reported as the raw 16-bit range rather than a category, because
            // the categories are not as clean as they look: `Color mul` drops
            // alpha by one LSB to 0x7ffe through rounding, which is not a
            // meaningful change, while `Color sub` takes it to 0 flat, which
            // is. Printing both endpoints lets the case state its own truth.
            // "Blank" is not amax==0. Mask's `sub` mode leaves alpha at 0x0001
            // out of 0x7fff, which is exactly as invisible as zero but would
            // pass an equality test. The threshold is what survives CopyTo's
            // narrowing to 8 bits: below 0x0100 the PNG's alpha byte rounds to
            // 0 and the image displays as plain white.
            sPrintF(L"  %d x %d, %s, alpha %04x..%04x%s, checksum %08x%08x\n",
              bm->XSize,bm->YSize,
              uniform ? L"uniform" : L"structured",
              amin,amax,
              amax<0x0100 ? L" — renders blank" : L"",
              sU32(sum>>32),sU32(sum));

            if(out)
            {
              // GenBitmap is 16 bits per channel with 0x8000 = 1.0; CopyTo
              // narrows that to 8-bit RGBA and sizes the sImage itself.
              // sImage::SavePNG already handles the channel-order swizzle and
              // already compiles stb_image_write, so there is nothing to add.
              sImage img;
              bm->CopyTo(&img);
              if(!img.SavePNG(out))
              {
                sPrintF(L"wz4gen: could not write <%s>\n",out);
                sSetErrorCode();
              }
              else
              {
                sPrintF(L"  wrote %s\n",out);
              }
            }
          }
          else if(Doc->FindType(L"Wz4Mesh") &&
                  obj->IsType(Doc->FindType(L"Wz4Mesh")))
          {
            ReportMesh((Wz4Mesh *)obj,out);
          }
          else
          {
            sPrintF(L"  produced a %s\n",obj->Type ? obj->Type->Symbol : L"?");
          }

          obj->Release();
        }
      }
    }
  }
  else if(sCmpString(command,L"sweep")==0)
  {
    const sChar *file = sGetShellParameter(0,1);
    const sChar *dir = sGetShellParameter(L"meta",0);
    if(!dir)
      dir = WZ4GEN_META_DIR;

    if(!file)
    {
      sPrint(L"usage: wz4gen sweep <document>\n");
      sSetErrorCode();
      delete Doc;
      return;
    }

    wMetaLibrary meta;
    sBool ok = (sFindString(file,L".wz4t")>=0)
      ? (meta.LoadDirectory(dir) && wReadWz4t(file,meta,wWZ4T_ALLOWUNKNOWN))
      : Doc->Load(file);

    if(!ok)
    {
      sPrintF(L"wz4gen: could not load <%s>\n",file);
      sSetErrorCode();
    }
    else
    {
      sPrintF(L"%s: %d page(s), %d store(s)\n\n",
        file,Doc->Pages.GetCount(),Doc->Stores.GetCount());
      SweepDocument(file,sGetShellSwitch(L"v"));
    }
  }
  else if(sCmpString(command,L"diff")==0)
  {
    const sChar *a = sGetShellParameter(0,1);
    const sChar *b = sGetShellParameter(0,2);
    const sChar *out = sGetShellParameter(L"out",0);
    if(!a || !b)
    {
      sPrint(L"usage: wz4gen diff <a.png> <b.png> [-out <diff.png>]\n");
      sSetErrorCode();
    }
    else
    {
      DiffImages(a,b,out);
    }
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
