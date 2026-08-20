/****************************************************************************/
/***                                                                      ***/
/***   The operator palette — wz4port editor, stage 5.4                   ***/
/***                                                                      ***/
/****************************************************************************/

#include "palette.hpp"

/****************************************************************************/

// sChar is 2 bytes and ImGui wants UTF-8. Same converter as main.cpp, kept
// local rather than shared through a header: it is four lines, and every
// crossing of this boundary should be visible at the point it happens.
struct wPalUtf8
{
  char Buffer[256];
  wPalUtf8(const sChar *s)
  {
    Buffer[0] = 0;
    if(s)
      sCopyStringToUTF8(Buffer,s,sCOUNTOF(Buffer));
  }
  operator const char *() const { return Buffer; }
};

static sBool ContainsNoCase(const char *hay,const char *needle)
{
  if(!needle || !needle[0])
    return 1;
  for(sInt i=0;hay[i];i++)
  {
    sInt k = 0;
    while(needle[k])
    {
      char a = hay[i+k], b = needle[k];
      if(a>='A' && a<='Z') a = char(a-'A'+'a');
      if(b>='A' && b<='Z') b = char(b-'A'+'a');
      if(a!=b) break;
      k++;
    }
    if(!needle[k])
      return 1;
  }
  return 0;
}

/****************************************************************************/

wPalette::wPalette()
{
  Filter[0] = 0;
  CurrentTab = 0;
}

// wCF_HIDE is documented in doc_core.hpp as "hide in op palette", so it is the
// registry telling us directly. Conversions and extractions are excluded for the
// same reason the original excludes them: the editor inserts them automatically
// when a type mismatch needs bridging, and offering them by hand invites graphs
// that cannot be reasoned about.
sBool wPalette::Insertable(wClass *cl) const
{
  if(!cl || !cl->OutputType)
    return 0;
  if(cl->Flags & wCF_HIDE)
    return 0;
  if(cl->Flags & wCF_CONVERSION)
    return 0;
  if(!cl->Extract.IsEmpty())
    return 0;
  return 1;
}

sBool wPalette::Matches(wClass *cl) const
{
  if(!Filter[0])
    return 1;
  if(ContainsNoCase(wPalUtf8(cl->Name),Filter))
    return 1;
  if(!cl->Label.IsEmpty() && ContainsNoCase(wPalUtf8(cl->Label),Filter))
    return 1;
  return 0;
}

sBool wPalette::AnyVisible() const
{
  if(!Doc)
    return 0;
  for(sInt i=0;i<Doc->Classes.GetCount();i++)
  {
    wClass *cl = Doc->Classes[i];
    if(Insertable(cl) && Matches(cl))
      return 1;
  }
  return 0;
}

/****************************************************************************/

wClass *wPalette::Draw()
{
  if(!Doc)
  {
    ImGui::TextDisabled("No document.");
    return 0;
  }

  wClass *chosen = 0;

  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##filter","filter",Filter,sizeof(Filter));

  // A filter that matches nothing looks identical to a broken palette, so say
  // which it is.
  if(Filter[0] && !AnyVisible())
  {
    ImGui::TextDisabled("nothing matches \"%s\"",Filter);
    return 0;
  }

  if(!ImGui::BeginTabBar("types",ImGuiTabBarFlags_FittingPolicyScroll))
    return 0;

  // Tab order. wType::Order is documented in doc_core.hpp as "sorting order, set
  // to 1..9 to assign type keyboard shortcuts 1..9", which would be the right
  // answer — except that **no type in either registered module sets it**, so it
  // is 0 everywhere and sorting by it alone is a no-op. Checked, after sorting by
  // it changed nothing.
  //
  // In registration order the bar opened on AnyType's seventeen structural
  // operators — Call, Dummy, EndLoop, InjectGlobals — and scrolled GenBitmap's
  // thirty-four off the end, which is exactly backwards for a texture editor. So
  // Order is honoured where it is set, and the tie-break is the number of
  // insertable classes, descending: the type you are most likely to want is the
  // one with the most operators in it.
  //
  // That tie-break is a judgement, not upstream behaviour. It is here because the
  // alternative was to leave the useful tab hidden.
  sArray<wType *> order;
  for(sInt t=0;t<Doc->Types.GetCount();t++)
    order.AddTail(Doc->Types[t]);

  for(sInt i=0;i<order.GetCount()-1;i++)
  {
    for(sInt j=i+1;j<order.GetCount();j++)
    {
      const sInt ao = order[i]->Order ? order[i]->Order : 1000;
      const sInt bo = order[j]->Order ? order[j]->Order : 1000;

      sBool swap = 0;
      if(ao!=bo)
      {
        swap = ao>bo;
      }
      else
      {
        sInt an = 0,bn = 0;
        for(sInt k=0;k<Doc->Classes.GetCount();k++)
        {
          wClass *cl = Doc->Classes[k];
          if(!Insertable(cl)) continue;
          if(cl->TabType==order[i]) an++;
          if(cl->TabType==order[j]) bn++;
        }
        swap = bn>an;
      }
      if(swap)
        order.Swap(i,j);
    }
  }

  // One tab per type that has something insertable in it. Grouping is by
  // wClass::TabType, not OutputType: they are usually the same, but where they
  // differ the registry is expressing where the author wanted the operator to
  // appear, and that is the whole purpose of the field.
  for(sInt t=0;t<order.GetCount();t++)
  {
    wType *type = order[t];

    sInt count = 0;
    for(sInt i=0;i<Doc->Classes.GetCount();i++)
    {
      wClass *cl = Doc->Classes[i];
      if(Insertable(cl) && cl->TabType==type && Matches(cl))
        count++;
    }
    if(count==0)
      continue;

    sString<128> label;
    label.PrintF(L"%s (%d)",
      type->Label && type->Label[0] ? type->Label : type->Symbol,count);

    if(ImGui::BeginTabItem(wPalUtf8(label)))
    {
      // Columns 0..31, each under the type's own header for that column. This is
      // how the original groups a tab — generator / filter / merge / and so on —
      // and the headers live on the type rather than being hardcoded, which is
      // why they can be printed rather than invented.
      for(sInt col=0;col<32;col++)
      {
        sInt shown = 0;
        for(sInt i=0;i<Doc->Classes.GetCount();i++)
        {
          wClass *cl = Doc->Classes[i];
          if(!Insertable(cl) || cl->TabType!=type || cl->Column!=col)
            continue;
          if(!Matches(cl))
            continue;

          if(!shown)
          {
            const sChar *header = type->ColumnHeaders[col];
            if(header && header[0])
              ImGui::SeparatorText(wPalUtf8(header));
            else
              ImGui::SeparatorText("");
            shown = 1;
          }

          // Label if the author gave one, else the internal name.
          const sChar *nm = cl->Name;
          if(!cl->Label.IsEmpty())
            nm = cl->Label;

          ImGui::PushID(cl);
          if(ImGui::Selectable(wPalUtf8(nm)))
            chosen = cl;

          // The shortcut, right-aligned, so the keyboard path is discoverable
          // from the palette rather than only from a manual.
          if(cl->Shortcut)
          {
            char key[8];
            key[0] = char(cl->Shortcut);
            key[1] = 0;
            const float w = ImGui::CalcTextSize(key).x;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - w);
            ImGui::TextDisabled("%s",key);
          }

          if(ImGui::IsItemHovered())
          {
            ImGui::SetTooltip("%s.%s\n%d input(s), %d parameter word(s)",
              wPalUtf8(cl->OutputType->Symbol),wPalUtf8(cl->Name),
              cl->Inputs.GetCount(),cl->ParaWords);
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTabItem();
    }
  }

  ImGui::EndTabBar();
  return chosen;
}

/****************************************************************************/
