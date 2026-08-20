/****************************************************************************/
/***                                                                      ***/
/***   Operator metadata, at runtime                                      ***/
/***                                                                      ***/
/****************************************************************************/

#include "meta.hpp"
#include "json.hpp"
#include "base/system.hpp"

/****************************************************************************/

wMetaParam::wMetaParam()
{
  Space = wMS_NONE;
  Offset = -1;
  Words = 0;
  Layout = wML_SCALAR;
  Count = 1;
  Continues = 0;
  RebuildOnChange = 0;
  Min = 0;
  Max = 0;
  Step = 0;
  RStep = 0;
  LogStep = 0;
  Capacity = 0;
  Lines = 1;
}

wMetaParam::~wMetaParam()
{
  for(sInt i=0;i<Widgets.GetCount();i++)
    delete Widgets[i];
}

wMetaArray::wMetaArray()
{
  HideThreshold = 0;
  Grouped = 0;
  Numbered = 0;
  DefaultMode = 0;
}

wMetaArray::~wMetaArray()
{
  for(sInt i=0;i<Params.GetCount();i++)
    delete Params[i];
}

wMetaClass::wMetaClass()
{
  Column = 0;
  ParaWords = 0;
  ParaStrings = 0;
  ArrayWords = 0;
  HasCode = 0;
  Array = 0;
}

wMetaClass::~wMetaClass()
{
  for(sInt i=0;i<Params.GetCount();i++)
    delete Params[i];
  delete Array;
}

const wMetaParam *wMetaClass::FindParam(const sChar *symbol) const
{
  for(sInt i=0;i<Params.GetCount();i++)
    if(Params[i]->Symbol==symbol)
      return Params[i];
  return 0;
}

/****************************************************************************/

void wGatherWidgets(const sArray<wMetaParam *> &params,const wMetaParam *owner,
  sArray<const wMetaWidget *> &out)
{
  for(sInt i=0;i<params.GetCount();i++)
  {
    const wMetaParam *p = params[i];
    if(p->Symbol!=owner->Symbol)
      continue;
    if(p->Space!=wMS_WORDS || p->Offset!=owner->Offset)
      continue;
    for(sInt k=0;k<p->Widgets.GetCount();k++)
      out.AddTail(p->Widgets[k]);
  }
}

/****************************************************************************/

static sInt SpaceFromName(const sChar *name)
{
  if(sCmpString(name,L"words")==0)   return wMS_WORDS;
  if(sCmpString(name,L"strings")==0) return wMS_STRINGS;
  if(sCmpString(name,L"links")==0)   return wMS_LINKS;
  return wMS_NONE;
}

static sInt LayoutFromName(const sChar *name)
{
  if(sCmpString(name,L"vector")==0) return wML_VECTOR;
  if(sCmpString(name,L"array")==0)  return wML_ARRAY;
  return wML_SCALAR;
}

// "0x0123abcd" — opsmeta writes masks and colours as hex strings rather than
// numbers, so that they read as what they are.
static sU32 ParseHexString(const sChar *s)
{
  sU32 v = 0;
  if(s[0]=='0' && (s[1]=='x' || s[1]=='X'))
    s += 2;
  for(;*s;s++)
  {
    sInt d = -1;
    if(*s>='0' && *s<='9') d = *s-'0';
    if(*s>='a' && *s<='f') d = *s-'a'+10;
    if(*s>='A' && *s<='F') d = *s-'A'+10;
    if(d<0)
      break;
    v = v*16 + sU32(d);
  }
  return v;
}

static void ReadParam(const wJsonValue *j,wMetaParam &p)
{
  p.Kind = j->GetString(L"kind");
  p.Symbol = j->GetString(L"symbol");
  p.Label = j->GetString(L"label");

  p.Space = SpaceFromName(j->GetString(L"space"));
  p.Offset = j->GetInt(L"offset",-1);
  p.Words = j->GetInt(L"words",0);
  p.Layout = LayoutFromName(j->GetString(L"layout"));
  p.Count = j->GetInt(L"count",1);

  p.Continues = j->GetBool(L"continues");
  p.RebuildOnChange = j->GetBool(L"rebuildOnChange");

  p.Min = j->GetFloat(L"min");
  p.Max = j->GetFloat(L"max");
  p.Step = j->GetFloat(L"step");
  p.RStep = j->GetFloat(L"rstep");
  p.LogStep = j->GetBool(L"logStep");

  p.Channels = j->GetString(L"channels");
  p.Options = j->GetString(L"options");
  p.Capacity = j->GetInt(L"capacity",0);
  p.Lines = j->GetInt(L"lines",1);
  p.DefaultString = j->GetString(L"default");

  // Defaults are numbers for most kinds and hex strings for colours, so both
  // shapes are accepted and both lists are filled where it makes sense.
  // Indexed loops throughout: sFORALL deduces its element type from a
  // non-const array, and everything reachable from a `const wJsonValue *` is
  // const. Not worth fighting the macro over.

  const wJsonValue *defs = j->GetArray(L"defaults");
  if(defs)
  {
    for(sInt i=0;i<defs->Items.GetCount();i++)
    {
      const wJsonValue *d = defs->Items[i];
      if(d->Type==wJSON_NUMBER)
      {
        p.DefaultsF.AddTail(d->AsFloat());
        p.DefaultsI.AddTail(d->AsInt());
      }
      else if(d->Type==wJSON_STRING)
      {
        sU32 v = ParseHexString(d->String);
        p.DefaultsI.AddTail(sInt(v));
        p.DefaultsF.AddTail(sF32(v));
      }
    }
  }

  const wJsonValue *widgets = j->GetArray(L"widgets");
  if(widgets)
  {
    for(sInt i=0;i<widgets->Items.GetCount();i++)
    {
      const wJsonValue *w = widgets->Items[i];
      wMetaWidget *mw = new wMetaWidget;
      p.Widgets.AddTail(mw);
      mw->Shift = w->GetInt(L"shift",0);
      mw->Mask = sInt(ParseHexString(w->GetString(L"mask",L"0")));

      const wJsonValue *choices = w->GetArray(L"choices");
      if(choices)
      {
        for(sInt k=0;k<choices->Items.GetCount();k++)
        {
          const wJsonValue *c = choices->Items[k];
          wMetaChoice *mc = mw->Choices.AddMany(1);
          mc->Label = c->GetString(L"label");
          mc->Value = c->GetInt(L"value",0);
        }
      }
    }
  }
}

/****************************************************************************/

wMetaLibrary::wMetaLibrary()
{
  ErrorMsg = L"";
}

wMetaLibrary::~wMetaLibrary()
{
  wMetaClass *c;
  sFORALL(Classes,c)
    delete c;
}

sBool wMetaLibrary::LoadModule(const sChar *filename)
{
  wJsonDoc doc;
  wJsonValue *root = doc.Load(filename);
  if(!root)
  {
    ErrorMsg.PrintF(L"%s: %s",filename,doc.GetError());
    return 0;
  }

  // Refuse a schema we do not know rather than guessing at it — that is what
  // the version field is for (docs/02-target-model.md §6.1).
  sInt version = root->GetInt(L"schemaVersion",0);
  if(version!=1)
  {
    ErrorMsg.PrintF(L"%s: schemaVersion %d, expected 1",filename,version);
    return 0;
  }

  const wJsonValue *classes = root->GetArray(L"classes");
  if(!classes)
  {
    ErrorMsg.PrintF(L"%s: no \"classes\" array",filename);
    return 0;
  }

  for(sInt i=0;i<classes->Items.GetCount();i++)
  {
    const wJsonValue *cj = classes->Items[i];

    wMetaClass *c = new wMetaClass;
    c->Name = cj->GetString(L"name");
    c->OutputType = cj->GetString(L"outputType");
    c->TabType = cj->GetString(L"tabType");
    c->Column = cj->GetInt(L"column",0);
    c->ParaWords = cj->GetInt(L"paraWords",0);
    c->ParaStrings = cj->GetInt(L"paraStrings",0);
    c->ArrayWords = cj->GetInt(L"arrayWords",0);
    c->HasCode = cj->GetBool(L"hasCode");

    const wJsonValue *flags = cj->GetArray(L"flags");
    if(flags)
    {
      for(sInt k=0;k<flags->Items.GetCount();k++)
        c->Flags.AddTail(flags->Items[k]->String);
    }

    const wJsonValue *params = cj->GetArray(L"parameters");
    if(params)
    {
      for(sInt k=0;k<params->Items.GetCount();k++)
      {
        wMetaParam *p = new wMetaParam;
        c->Params.AddTail(p);
        ReadParam(params->Items[k],*p);
      }
    }

    // The table widget, if the operator has one. Its parameters live in their
    // own word space (wOp::ArrayData), so offsets restart from zero here.
    const wJsonValue *arr = cj->Member(L"array");
    if(arr && arr->Type==wJSON_OBJECT)
    {
      wMetaArray *a = new wMetaArray;
      c->Array = a;
      a->HideThreshold = arr->GetInt(L"hideThreshold",0);
      a->Grouped = arr->GetBool(L"grouped");
      a->Numbered = arr->GetBool(L"numbered");
      a->DefaultMode = arr->GetInt(L"defaultMode",0);

      const wJsonValue *ap = arr->GetArray(L"parameters");
      if(ap)
      {
        for(sInt k=0;k<ap->Items.GetCount();k++)
        {
          wMetaParam *p = new wMetaParam;
          a->Params.AddTail(p);
          ReadParam(ap->Items[k],*p);
        }
      }
    }

    Classes.AddTail(c);
  }

  return 1;
}

sBool wMetaLibrary::LoadDirectory(const sChar *dir)
{
  sArray<sDirEntry> entries;
  if(!sLoadDir(entries,dir))
  {
    ErrorMsg.PrintF(L"could not list <%s>",dir);
    return 0;
  }

  sBool any = 0;
  sBool ok = 1;

  sDirEntry *e;
  sFORALL(entries,e)
  {
    sString<1024> path;
    path.PrintF(L"%s/%s",dir,e->Name);

    if(e->Flags & sDEF_DIR)
    {
      // One level down. opsmeta writes meta/<subdir>/<module>.json, mirroring
      // the source layout, so a caller should be able to name meta/ itself.
      sArray<sDirEntry> sub;
      if(sLoadDir(sub,path))
      {
        sDirEntry *s;
        sFORALL(sub,s)
        {
          if(s->Flags & sDEF_DIR)
            continue;
          if(sFindString(s->Name,L".json")<0)
            continue;
          sString<1024> subpath;
          subpath.PrintF(L"%s/%s",path,s->Name);
          any = 1;
          if(!LoadModule(subpath))
            ok = 0;
        }
      }
    }
    else if(sFindString(e->Name,L".json")>=0)
    {
      any = 1;
      if(!LoadModule(path))
        ok = 0;
    }
  }

  if(!any)
  {
    ErrorMsg.PrintF(L"no .json files under <%s>",dir);
    return 0;
  }

  return ok;
}

const wMetaClass *wMetaLibrary::Find(const sChar *name) const
{
  // Accept "OutputType.ClassName" here too, so callers do not have to split.
  sInt dot = sFindString(name,L".");
  if(dot>=0)
  {
    sString<128> type;
    type.Init(name,dot);
    return Find(type,name+dot+1);
  }

  for(sInt i=0;i<Classes.GetCount();i++)
    if(Classes[i]->Name==name)
      return Classes[i];
  return 0;
}

const wMetaClass *wMetaLibrary::Find(const sChar *outputtype,const sChar *name) const
{
  for(sInt i=0;i<Classes.GetCount();i++)
    if(Classes[i]->Name==name && Classes[i]->OutputType==outputtype)
      return Classes[i];
  return 0;
}

/****************************************************************************/
