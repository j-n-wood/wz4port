/****************************************************************************/
/***                                                                      ***/
/***   The parameter panel — wz4port editor, stage 5.5                    ***/
/***                                                                      ***/
/****************************************************************************/

#include "params.hpp"

/****************************************************************************/

struct wPUtf8
{
  char Buffer[512];
  wPUtf8(const sChar *s)
  {
    Buffer[0] = 0;
    if(s)
      sCopyStringToUTF8(Buffer,s,sCOUNTOF(Buffer));
  }
  operator const char *() const { return Buffer; }
};

// The label a row shows: the author's label if there is one, else the symbol.
// Animatable parameters are marked upstream by prefixing the label with "* ",
// which is the entire affordance (§5.2) — kept verbatim rather than reinterpreted,
// because there is no animation in this build to attach anything else to.
static const sChar *RowLabel(const wMetaParam *p)
{
  if(!p->Label.IsEmpty())
    return p->Label;
  return p->Symbol;
}

/****************************************************************************/
/***   scalars                                                            ***/
/****************************************************************************/

// One float control. Step drives the drag speed and Min/Max clamp it, both
// straight out of the .ops declaration, so a parameter cannot be dragged
// somewhere its author said it should not go.
//
// Double-click resets to the declared default. The original binds Home to that
// as well; double-click is the discoverable one and costs nothing.
static sBool DragFloatCell(const char *id,sF32 *v,const wMetaParam *p,sInt slot)
{
  sBool changed = 0;

  // Altona's step is "how much one notch moves", ImGui's speed is "how much one
  // pixel moves". A notch per pixel is far too fast for a range like -64..64,
  // so the speed is the step scaled by the range — which makes a full drag
  // across a 200px panel cover the range about twice, whatever the range is.
  const sF32 span = p->Max - p->Min;
  sF32 speed = p->Step>0.0f ? p->Step : 0.01f;
  if(span>0.0f)
    speed = sMax(speed,span/400.0f);

  const char *fmt = "%.4g";
  ImGuiSliderFlags flags = ImGuiSliderFlags_None;
  if(p->LogStep)
    flags |= ImGuiSliderFlags_Logarithmic;

  if(ImGui::DragFloat(id,v,speed,p->Min,p->Max,fmt,flags))
    changed = 1;

  if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    if(slot<p->DefaultsF.GetCount())
      *v = p->DefaultsF[slot];
    else if(p->DefaultsF.GetCount()==1)
      *v = p->DefaultsF[0];
    else
      *v = 0.0f;
    changed = 1;
  }

  if(ImGui::IsItemHovered())
    ImGui::SetTooltip("%.6g  (%.4g .. %.4g, step %.4g)",*v,p->Min,p->Max,p->Step);

  return changed;
}

static sBool DragIntCell(const char *id,sS32 *v,const wMetaParam *p,sInt slot)
{
  sBool changed = 0;

  const sF32 span = p->Max - p->Min;
  sF32 speed = 1.0f;
  if(span>800.0f)
    speed = span/400.0f;

  sInt vv = *v;
  if(ImGui::DragInt(id,&vv,speed,sInt(p->Min),sInt(p->Max)))
  {
    *v = vv;
    changed = 1;
  }

  if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    if(slot<p->DefaultsI.GetCount())
      *v = p->DefaultsI[slot];
    else if(p->DefaultsI.GetCount()==1)
      *v = p->DefaultsI[0];
    else
      *v = 0;
    changed = 1;
  }

  return changed;
}

/****************************************************************************/
/***   flags: several controls sharing one integer                        ***/
/****************************************************************************/

// A `flags` parameter is ONE integer split across several independent controls by
// shift and mask (§5.2). Two choices render as a checkbox and more as a dropdown,
// which is what the original does and also what reads best.
//
// Writing back has to preserve the bits belonging to the other controls, which is
// the whole reason this is not just a combo bound to a word. Getting it wrong
// silently corrupts a neighbouring control — the same hazard the .wz4t reader hit
// in phase 4 (architecture.md A36).
static sBool DrawFlagsWidget(const char *id,sU32 *word,const wMetaWidget *w,
  const sChar *fallback)
{
  if(w->Choices.GetCount()==0)
    return 0;

  const sU32 mask = sU32(w->Mask);
  const sInt cur = sInt((*word & mask) >> w->Shift);

  // Two choices whose values are 0 and 1: a checkbox.
  if(w->Choices.GetCount()==2 &&
     w->Choices[0].Value==0 && w->Choices[1].Value==1)
  {
    // The caption is the ON choice's own label, which is the only name this
    // control has. Perlin's Mode is the case that showed why it matters: it is
    // one word carrying two independent toggles whose choices are "-|abs" and
    // "-|sin", so labelling them from the PARAMETER gave two identical unnamed
    // checkboxes under a "Mode" heading and no way to tell which did what.
    //
    // Falls back to the parameter's label when the on-choice has no name of its
    // own, which is better than an empty caption.
    const sChar *cap = w->Choices[1].Label;
    if(!cap || !cap[0] || sCmpString(cap,L"-")==0)
      cap = fallback;

    // A constant id suffix is enough: DrawParam has already pushed the parameter
    // and the row onto ImGui's id stack, and multi-widget callers push the widget
    // index, so the caption text does not have to carry uniqueness.
    sString<128> capbuf;
    capbuf.PrintF(L"%s##fw",cap ? cap : L"");

    bool on = cur!=0;
    if(ImGui::Checkbox(wPUtf8(capbuf),&on))
    {
      *word = (*word & ~mask) | ((on ? 1u : 0u) << w->Shift);
      return 1;
    }
    return 0;
  }

  // Otherwise a dropdown over the declared choices. The label shown is the one
  // whose value matches; an unmatched value is shown as a number rather than
  // silently snapped to a legal one, because snapping would hide a real bug.
  const sChar *label = 0;
  for(sInt i=0;i<w->Choices.GetCount();i++)
    if(w->Choices[i].Value==cur)
      label = w->Choices[i].Label;

  sString<64> shown;
  if(label)
    shown = label;
  else
    shown.PrintF(L"<%d>",cur);

  sBool changed = 0;
  if(ImGui::BeginCombo(id,wPUtf8(shown)))
  {
    for(sInt i=0;i<w->Choices.GetCount();i++)
    {
      const sBool sel = w->Choices[i].Value==cur;
      if(ImGui::Selectable(wPUtf8(w->Choices[i].Label),sel!=0))
      {
        *word = (*word & ~mask) | ((sU32(w->Choices[i].Value) << w->Shift) & mask);
        changed = 1;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

/****************************************************************************/
/***   one parameter                                                      ***/
/****************************************************************************/

// `words` is the word space to edit: the operator's own, or one array row's.
// `plist` is the matching parameter list, because an array row has its own.
static sInt DrawParam(wOp *op,const wMetaParam *p,
  const sArray<wMetaParam *> &plist,sU32 *words,sInt rowid)
{
  sInt change = 0;

  ImGui::PushID(p);
  ImGui::PushID(rowid);

  const sChar *lbl = RowLabel(p);
  sString<128> idbuf;
  idbuf.PrintF(L"%s##%s",lbl,p->Symbol);
  const wPUtf8 id(idbuf);

  if(p->Kind==L"group")
  {
    ImGui::SeparatorText(wPUtf8(lbl));
  }
  else if(p->Kind==L"label")
  {
    ImGui::TextDisabled("%s",wPUtf8(lbl));
  }
  else if(p->Kind==L"float")
  {
    sF32 *f = (sF32 *)(words + p->Offset);
    const sInt n = (p->Layout==wML_SCALAR) ? 1 : sMax(1,p->Count);

    if(n==1)
    {
      if(DragFloatCell(id,f,p,0))
        change |= wPC_VALUE;
    }
    else
    {
      // Components on one row, sharing the label. Tied dragging under Ctrl is
      // the original's behaviour and is NOT implemented — see the note in
      // docs/07-phase-texture-gui.md; each component drags on its own.
      ImGui::TextUnformatted(wPUtf8(lbl));
      const float w = (ImGui::GetContentRegionAvail().x - 4.0f*(n-1))/n;
      for(sInt i=0;i<n;i++)
      {
        if(i) ImGui::SameLine(0.0f,4.0f);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(w);
        if(DragFloatCell("##c",f+i,p,i))
          change |= wPC_VALUE;
        ImGui::PopID();
      }
    }
  }
  else if(p->Kind==L"int")
  {
    sS32 *v = (sS32 *)(words + p->Offset);
    const sInt n = (p->Layout==wML_SCALAR) ? 1 : sMax(1,p->Count);

    if(n==1)
    {
      if(DragIntCell(id,v,p,0))
        change |= wPC_VALUE;
    }
    else
    {
      ImGui::TextUnformatted(wPUtf8(lbl));
      const float w = (ImGui::GetContentRegionAvail().x - 4.0f*(n-1))/n;
      for(sInt i=0;i<n;i++)
      {
        if(i) ImGui::SameLine(0.0f,4.0f);
        ImGui::PushID(i);
        ImGui::SetNextItemWidth(w);
        if(DragIntCell("##c",v+i,p,i))
          change |= wPC_VALUE;
        ImGui::PopID();
      }
    }
  }
  else if(p->Kind==L"color")
  {
    // Stored as one word, 0xAARRGGBB. ImGui wants normalised floats, and the
    // round trip has to be exact for a value the user did not touch — hence
    // integer construction on the way back rather than a scaled float.
    sU32 *c = words + p->Offset;
    const sBool alpha = p->Channels.IsEmpty() || sFindFirstChar(p->Channels,'a')>=0;

    float col[4];
    col[0] = ((*c>>16)&255)/255.0f;
    col[1] = ((*c>> 8)&255)/255.0f;
    col[2] = ((*c    )&255)/255.0f;
    col[3] = ((*c>>24)&255)/255.0f;

    ImGuiColorEditFlags cf = ImGuiColorEditFlags_Float
                            |ImGuiColorEditFlags_AlphaPreviewHalf;
    sBool hit = 0;
    if(alpha)
      hit = ImGui::ColorEdit4(id,col,cf);
    else
      hit = ImGui::ColorEdit3(id,col,cf);

    if(hit)
    {
      const sU32 r = sU32(sClamp(col[0],0.0f,1.0f)*255.0f+0.5f);
      const sU32 g = sU32(sClamp(col[1],0.0f,1.0f)*255.0f+0.5f);
      const sU32 b = sU32(sClamp(col[2],0.0f,1.0f)*255.0f+0.5f);
      const sU32 a = alpha ? sU32(sClamp(col[3],0.0f,1.0f)*255.0f+0.5f)
                           : ((*c>>24)&255);
      *c = (a<<24)|(r<<16)|(g<<8)|b;
      change |= wPC_VALUE;
    }
  }
  else if(p->Kind==L"flags" || p->Kind==L"radio")
  {
    // Every control declared on this word, including those contributed by
    // `continue flags` parameters — which is what wGatherWidgets is for. A
    // `continue` parameter declares no storage of its own; it adds a widget to a
    // word already declared, so drawing per-parameter would draw it twice or
    // not at all.
    if(!p->Continues)
    {
      sArray<const wMetaWidget *> widgets;
      wGatherWidgets(plist,p,widgets);

      sU32 *word = words + p->Offset;
      if(widgets.GetCount()==1)
      {
        if(DrawFlagsWidget(id,word,widgets[0],lbl))
          change |= wPC_VALUE;
      }
      else
      {
        // Several controls in one word go on ONE row, not stacked. Stacked, two
        // identical 14-entry dropdowns for Flat's Size gave no clue which was x
        // and which was y; side by side they read as a pair, which is how
        // upstream lays out components too.
        ImGui::TextUnformatted(wPUtf8(lbl));
        const sInt n = widgets.GetCount();
        const float w = (ImGui::GetContentRegionAvail().x - 6.0f*(n-1))/n;
        for(sInt i=0;i<n;i++)
        {
          if(i) ImGui::SameLine(0.0f,6.0f);
          ImGui::PushID(i);
          ImGui::SetNextItemWidth(w);
          // "##w", not "w": a combo uses this string as its visible label, so a
          // bare id showed a stray "w" between Flat's two Size dropdowns. The
          // checkbox path overrides the caption anyway.
          if(DrawFlagsWidget("##w",word,widgets[i],lbl))
            change |= wPC_VALUE;
          ImGui::PopID();
        }
      }
    }
  }
  else if(p->Kind==L"char")
  {
    // A fixed-size string living inside the word block, not in EditString.
    // Capacity is in characters; the words are (n+1)/2 because sChar is 2 bytes.
    sChar *s = (sChar *)(words + p->Offset);
    const sInt cap = sMax(1,p->Capacity);

    char utf8[512];
    utf8[0] = 0;
    sCopyStringToUTF8(utf8,s,sMin(sInt(sCOUNTOF(utf8)),cap*4));
    if(ImGui::InputText(id,utf8,sizeof(utf8)))
    {
      sString<256> back;
      sCopyStringFromUTF8(back,utf8,sMin(sInt(back.Size()),cap));
      sCopyString(s,back,cap);
      change |= wPC_VALUE;
    }
  }
  else if(p->Kind==L"string" || p->Kind==L"filein" || p->Kind==L"fileout")
  {
    if(p->Offset<op->EditStringCount && op->EditString[p->Offset])
    {
      sTextBuffer *tb = op->EditString[p->Offset];
      char utf8[1024];
      utf8[0] = 0;
      sCopyStringToUTF8(utf8,tb->Get(),sCOUNTOF(utf8));

      // Multi-line where the author asked for it (`lines N`), which is how the
      // Text operator's body is edited.
      sBool hit = 0;
      if(p->Lines>1)
      {
        const float h = ImGui::GetTextLineHeight()*sMin(p->Lines,8)+8.0f;
        ImGui::TextUnformatted(wPUtf8(lbl));
        hit = ImGui::InputTextMultiline("##s",utf8,sizeof(utf8),ImVec2(-1.0f,h));
      }
      else
      {
        hit = ImGui::InputText(id,utf8,sizeof(utf8));
      }

      if(hit)
      {
        sString<1024> back;
        sCopyStringFromUTF8(back,utf8,back.Size());
        tb->Clear();
        tb->Print(back);
        change |= wPC_VALUE;
      }

      // A file parameter is a path, and the browse button the original has needs
      // a file dialog this build does not have yet (5.1's note). The path is
      // editable as text, which is enough to use.
      if(p->Kind==L"filein" || p->Kind==L"fileout")
      {
        ImGui::SameLine();
        ImGui::TextDisabled(p->Kind==L"filein" ? "(in)" : "(out)");
      }
    }
  }
  else if(p->Kind==L"link")
  {
    // The link's name. Changing it changes the graph, so it reconnects — this is
    // the ConnectMsg level of the change contract, and the only parameter kind
    // in the corpus that reaches it.
    if(p->Offset<op->Links.GetCount())
    {
      wOpInputInfo &li = op->Links[p->Offset];
      char utf8[256];
      utf8[0] = 0;
      sCopyStringToUTF8(utf8,li.LinkName,sCOUNTOF(utf8));
      if(ImGui::InputText(id,utf8,sizeof(utf8)))
      {
        sString<256> back;
        sCopyStringFromUTF8(back,utf8,back.Size());
        li.LinkName = back;
        change |= wPC_VALUE|wPC_CONNECT;
      }
      ImGui::SameLine();
      ImGui::TextDisabled(li.Link ? "linked" : "unresolved");
    }
  }
  else if(p->Kind==L"action")
  {
    // A push button calling the class's own actions handler. The id is carried in
    // the metadata's default-int slot, which is where opsmeta puts it.
    const sInt code = p->DefaultsI.GetCount() ? p->DefaultsI[0] : 0;
    if(ImGui::Button(wPUtf8(lbl)))
    {
      if(op->Class && op->Class->Actions)
      {
        op->Class->Actions(op,code,0);
        change |= wPC_VALUE;
      }
    }
  }
  else if(p->Kind==L"strobe")
  {
    // One-shot trigger. wOp::Strobe is cleared by the runtime after a successful
    // execution, so setting it is the whole interaction.
    if(ImGui::Button(wPUtf8(lbl)))
    {
      op->Strobe = 1;
      change |= wPC_VALUE;
    }
  }
  else
  {
    // Everything the corpus does not use: bitmask, custom, tie, padding. Named
    // rather than skipped, so an operator with one is visibly incomplete instead
    // of quietly missing a control.
    ImGui::TextDisabled("%s: no editor for kind \"%s\"",
      wPUtf8(lbl),wPUtf8(p->Kind));
  }

  ImGui::PopID();
  ImGui::PopID();
  return change;
}

/****************************************************************************/
/***   arrays                                                             ***/
/****************************************************************************/

static sInt DrawArray(wOp *op,const wMetaClass *mc)
{
  sInt change = 0;
  if(!mc->Array)
    return 0;

  ImGui::SeparatorText("rows");

  const sInt rows = op->GetArrayCount();
  ImGui::Text("%d row(s), %d word(s) each",rows,mc->ArrayWords);

  if(ImGui::Button("add row"))
  {
    // AddArray runs the generated SetDefaultsArray, which sets each field's
    // default and then LINEARLY INTERPOLATES every float field between the
    // neighbouring rows (§5.4). That is why inserting a gradient key lands
    // halfway between its neighbours instead of at a default, and it is why the
    // .wz4t writer emits every field of every row (architecture.md A38).
    op->AddArray(-1);
    change |= wPC_VALUE;
  }

  for(sInt r=0;r<rows;r++)
  {
    ImGui::PushID(r);
    sString<32> head;
    head.PrintF(L"row %d",r);

    if(ImGui::TreeNodeEx(wPUtf8(head),ImGuiTreeNodeFlags_DefaultOpen))
    {
      sU32 *words = (sU32 *) op->GetArray<void>(r);
      if(words)
      {
        for(sInt i=0;i<mc->Array->Params.GetCount();i++)
          change |= DrawParam(op,mc->Array->Params[i],mc->Array->Params,words,r);
      }

      if(ImGui::SmallButton("insert above"))
      {
        op->AddArray(r);
        change |= wPC_VALUE;
        ImGui::TreePop();
        ImGui::PopID();
        break;                      // the row indices just moved under us
      }
      ImGui::SameLine();
      if(ImGui::SmallButton("remove"))
      {
        op->RemArray(r);
        change |= wPC_VALUE;
        ImGui::TreePop();
        ImGui::PopID();
        break;
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  return change;
}

/****************************************************************************/

sInt wDrawParams(wOp *op,const wMetaClass *mc)
{
  if(!op || !mc)
    return 0;

  sInt change = 0;

  // The store name, which is also what a Load operator resolves against — so
  // changing it is a graph change, not a value change.
  {
    char utf8[256];
    utf8[0] = 0;
    sCopyStringToUTF8(utf8,op->Name,sCOUNTOF(utf8));
    if(ImGui::InputText("name",utf8,sizeof(utf8)))
    {
      sString<256> back;
      sCopyStringFromUTF8(back,utf8,back.Size());
      op->Name = back;
      change |= wPC_VALUE|wPC_CONNECT;
    }
  }

  ImGui::Separator();

  sU32 *words = op->EditU();
  if(!words && mc->ParaWords>0)
  {
    ImGui::TextDisabled("operator has no parameter storage");
    return change;
  }

  for(sInt i=0;i<mc->Params.GetCount();i++)
    change |= DrawParam(op,mc->Params[i],mc->Params,words,-1);

  if(mc->Array)
    change |= DrawArray(op,mc);

  return change;
}

/****************************************************************************/
