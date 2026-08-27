/****************************************************************************/
/***                                                                      ***/
/***   wz4ed — the texture editor. Stage 5.1: the application shell        ***/
/***                                                                      ***/
/****************************************************************************/
//
// Dear ImGui + GLFW + OpenGL 3.3 core, per docs/07-phase-texture-gui.md. The
// window, the panes, the menu bar, and a panel that lists a loaded document's
// operators. Canvas, parameter panel and preview are stages 5.2 to 5.6.
//
// The editor uses wz4lib as a HEADLESS LIBRARY and brings its own window. None
// of Altona's GUI is compiled — that is the architecture the whole port has been
// built around, and this is the stage that cashes it in: phases 2 and 3 made the
// document model, the metadata and the text format usable without a GUI, so the
// only thing left to write here is the front end.
//
// Entry point is sMain, like every other tool in this port, so Altona's memory
// system and shell-parameter parsing are initialised before anything runs.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz3_bitmap_code.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "util/image.hpp"
#include "base/system.hpp"

#include "meta.hpp"
#include "wz4t.hpp"

// Never <imgui.h> directly: Altona macro-defines `new`, which mangles ImGui's
// placement-new declaration. See the header for the whole story.
#include "imgui_wz4.hpp"
#include "canvas.hpp"
#include "palette.hpp"
#include "docedit.hpp"
#include "params.hpp"
#include "preview.hpp"
#include "meshview.hpp"
#include "gltf_write.hpp"         // phase 8.4: File -> Export glTF
#include "gl_wz4.hpp"
#include "undo.hpp"

#include <GLFW/glfw3.h>
#include <stdio.h>                  // fflush, for the exit path at the bottom
#include <stdlib.h>                 // _Exit

#ifndef WZ4ED_META_DIR
#define WZ4ED_META_DIR L"meta"
#endif

/****************************************************************************/

// wDocument's constructor calls this. Same two-pass registration and same order
// as wz4gen: wz3_bitmap's GenBitmap derives from basic's BitmapBase.
void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(wz3_bitmap,0);
    // The mesh modules joined in stage 6.4. wz3_bitmap comes first because two
    // mesh operators take a bitmap input.
    sREGOPS(wz4_anim,0);
    sREGOPS(wz4_mesh,0);
    sREGOPS(animate,0);             // ours — phase 7's AnimateBones
    sREGOPS(material,0);            // ours — phase 9's Wz4Mtrl types
  }
}

/****************************************************************************/
/***   bridging Altona's strings to ImGui's                               ***/
/****************************************************************************/

// sChar is 2 bytes and ImGui wants UTF-8. Altona already has the converter, so
// this is a buffer and a call rather than anything clever. Deliberately not
// hidden behind an operator: every crossing of this boundary should be visible,
// because getting it wrong is how "café" became "cafÃ©" in phase 3.

struct wUtf8
{
  char Buffer[1024];
  wUtf8(const sChar *s)
  {
    Buffer[0] = 0;
    if(s)
      sCopyStringToUTF8(Buffer,s,sCOUNTOF(Buffer));
  }
  operator const char *() const { return Buffer; }
};

// And the other direction, for text arriving from GLFW.
struct wWide
{
  sChar Buffer[512];
  wWide(const char *s)
  {
    Buffer[0] = 0;
    if(s)
      sCopyStringFromUTF8(Buffer,s,sCOUNTOF(Buffer));
  }
  operator const sChar *() const { return Buffer; }
};

/****************************************************************************/
/***   editor state                                                       ***/
/****************************************************************************/

struct wEditor
{
  wMetaLibrary Meta;
  sString<1024> DocPath;            // empty until something is loaded
  sString<256> Status;
  sBool MetaOk;

  sInt CurrentPage;

  // Selection lives on wOp::Select, because that is what wPage::CheckDest and
  // CheckMove read — the editor must not keep a parallel set that could drift
  // from the rule the document format enforces. `Selected` is only the
  // single-selection convenience the inspector needs.
  wOp *Selected;

  wCanvas Canvas;
  wPalette Palette;
  wPreview Preview;
  wMeshView MeshView;

  // Bumped by every edit. The preview re-evaluates when it changes, which is
  // what keeps a parameter drag responsive without re-rendering the graph on
  // every frame of the drag.
  sInt Revision;

  wUndo Undo;

  // An edit happened this frame and has not been snapshotted yet. The snapshot
  // is deferred until no ImGui item is active, which is what COALESCES a gesture
  // into one undo entry: a parameter drag reports a change on every frame it
  // moves, and thirty entries for one drag would make undo useless.
  sBool Dirty;
  sString<64> DirtyWhat;

  void MarkDirty(const sChar *what)
  {
    Dirty = 1;
    DirtyWhat = what;
    Revision++;
  }

  // `bool`, not sBool: ImGui takes bool* for its toggles and sBool is an int.
  bool ShowDemo;
  bool ShowList;

  wEditor()
  {
    MetaOk = 0;
    CurrentPage = 0;
    Selected = 0;
    ShowDemo = false;
    ShowList = false;
    Revision = 0;
    Dirty = 0;
    DirtyWhat = L"";
    Status = L"no document";
  }

  sBool LoadMeta(const sChar *dir)
  {
    MetaOk = Meta.LoadDirectory(dir);
    if(!MetaOk)
      Status.PrintF(L"metadata not found in %s",dir);
    return MetaOk;
  }

  sBool LoadDoc(const sChar *path)
  {
    if(!MetaOk)
    {
      Status = L"cannot load a document without metadata";
      return 0;
    }

    // A fresh document rather than merging into the current one. Doc is the
    // global wz4lib document; deleting it drops every operator with it, so the
    // selection has to go first.
    Selected = 0;
    CurrentPage = 0;
    delete Doc;
    Doc = new wDocument;

    if(!wReadWz4t(path,Meta,wWZ4T_ALLOWUNKNOWN))
    {
      Status.PrintF(L"could not read %s",path);
      DocPath = L"";
      return 0;
    }

    Doc->Connect();
    DocPath = path;
    Canvas.FitPending = 1;          // frame the whole graph, not a corner of it

    // The baseline the first undo returns to. Without it there is nothing
    // behind the first edit and undo would be a no-op exactly once.
    Undo.Reset(Doc->Pages.GetCount() ? Doc->Pages[0] : 0);
    Dirty = 0;
    Revision++;

    sInt ops = 0;
    for(sInt p=0;p<Doc->Pages.GetCount();p++)
      ops += Doc->Pages[p]->Ops.GetCount();
    Status.PrintF(L"%d page(s), %d operator(s)",Doc->Pages.GetCount(),ops);
    return 1;
  }

  // Insertion, following the original (gui.cpp:5165-5197 via
  // docs/01-existing-model.md §2.5):
  //
  //   place a 3 x 1 block at the cursor IF CheckDest allows
  //   apply the class's defaults
  //   reconnect
  //   select it, and ADVANCE THE CURSOR DOWN ONE ROW
  //
  // That last step is what makes the palette usable: pressing the same key
  // repeatedly builds a stack top to bottom, because each new block lands
  // touching the one before and so becomes its consumer. If the cells are
  // occupied the insert is refused and nothing moves — the same all-or-nothing
  // rule as a drag.
  //
  // Width is hardcoded to 3, as upstream. It is not sized to the input count;
  // widening a consumer is a manual act, because width is semantic.
  sBool Insert(wPage *page,wClass *cl)
  {
    if(!page || !cl)
      return 0;

    wStackOp *op = wInsertOp(page,cl,Canvas.CursorX,Canvas.CursorY);
    if(!op)
    {
      Status.PrintF(L"no room at %d,%d",Canvas.CursorX,Canvas.CursorY);
      return 0;
    }

    Selected = op;
    Canvas.CursorY += op->SizeY;    // so repeated insertion builds a stack
    Status.PrintF(L"inserted %s",cl->Name);

    sString<64> what;
    what.PrintF(L"insert %s",cl->Name);
    MarkDirty(what);
    return 1;
  }

  // Delete is not in stage 5.4's brief, but an editor that can only add is not
  // usable enough to test the palette with — you would have to restart to undo a
  // mistake. §2.5 lists it as one of the basic operations, so it is here.
  sBool DeleteSelection(wPage *page)
  {
    const sInt n = wDeleteSelection(page);
    if(n)
    {
      Selected = 0;
      Status.PrintF(L"deleted %d operator(s)",n);
      MarkDirty(n==1 ? L"delete" : L"delete several");
    }
    return n!=0;
  }

  // Exports the selected operator as glTF. Phase 8.4.
  //
  // Evaluates through Doc->CalcOp and does NOT release the result, matching
  // wMeshView and wPreview: the document's cache and this pointer are not
  // independent references, and releasing one broke the next frame in 6.4
  // (architecture.md A58).
  //
  // Shared by the menu item and the -export switch, so the gate exercises the
  // same code the menu does rather than a parallel path that could drift.
  sBool ExportGltf(const sChar *path)
  {
    if(!Selected)
    {
      Status = L"select an operator first";
      return 0;
    }

    wType *meshtype = Doc ? Doc->FindType(L"Wz4Mesh") : 0;
    wObject *obj = Doc->CalcOp(Selected);
    if(!obj || !meshtype || !obj->IsType(meshtype))
    {
      Status.PrintF(L"%s is not a mesh — glTF export is for Wz4Mesh",
        Selected->Class ? Selected->Class->Name : L"the selection");
      return 0;
    }

    wGltfStats st;
    if(!wWriteGltfFile(path,(Wz4Mesh *)obj,&st))
    {
      Status.PrintF(L"could not write %s",path);
      return 0;
    }

    // A writer's success return is not evidence that anything landed (A39), and
    // the editor has no console the user is watching, so the byte count goes in
    // the status line where it can be read.
    sDInt size = 0;
    sU8 *bytes = sLoadFile(path,size);
    if(!bytes || size<=0)
    {
      delete[] bytes;
      Status.PrintF(L"the writer reported success but %s is missing or empty",path);
      return 0;
    }
    delete[] bytes;

    Status.PrintF(L"exported %d vertices, %d triangles to %s (%d bytes)",
      st.Verts,st.Tris,path,sInt(size));
    sPrintF(L"wz4ed: %s\n",(const sChar *)Status);
    return 1;
  }

  // Where the menu item writes, having no file dialog: beside the document,
  // named after the operator. A name is what the user typed, so it is what they
  // will look for; an unnamed operator falls back to its class.
  void DefaultExportPath(const sStringDesc &out)
  {
    sString<1024> path(DocPath);
    sInt cut = -1;
    for(sInt i=0;path[i];i++)
      if(path[i]=='/' || path[i]=='\\')
        cut = i;
    path[cut+1] = 0;

    const sChar *leaf = L"mesh";
    if(Selected)
    {
      if(!Selected->Name.IsEmpty())
        leaf = Selected->Name;
      else if(Selected->Class)
        leaf = Selected->Class->Name;
    }
    path.Add(leaf);
    path.Add(L".glb");
    sCopyString(out,path);
  }

  // Restoring replaces every wStackOp on the page, so every operator pointer the
  // editor holds has to go. Selected is the only one; the canvas keeps deltas
  // rather than pointers, and bumping Revision makes the preview re-evaluate
  // instead of comparing against an address that may have been reused.
  void AfterUndoRedo(wPage *page)
  {
    Selected = 0;
    if(page)
    {
      for(sInt i=0;i<page->Ops.GetCount();i++)
        page->Ops[i]->Select = 0;
    }
    Doc->Connect();
    Dirty = 0;
    Revision++;
  }

  // Selects by store name. Exists for -select, which is what lets the
  // screenshot self-test exercise the inspector: without it the only panel a
  // non-interactive run can show is the list, and the metadata half of the
  // editor would go unverified.
  sBool SelectByName(const sChar *name)
  {
    for(sInt p=0;p<Doc->Pages.GetCount();p++)
    {
      wPage *page = Doc->Pages[p];
      for(sInt i=0;i<page->Ops.GetCount();i++)
      {
        if(sCmpString(page->Ops[i]->Name,name)==0)
        {
          CurrentPage = p;
          for(sInt k=0;k<page->Ops.GetCount();k++)
            page->Ops[k]->Select = 0;
          page->Ops[i]->Select = 1;
          Selected = page->Ops[i];
          return 1;
        }
      }
    }
    sPrintF(L"wz4ed: no operator named <%s>\n",name);
    return 0;
  }
};

static wEditor *Ed = 0;

/****************************************************************************/
/***   panels                                                             ***/
/****************************************************************************/

// The operator list, which is stage 5.1's gate: a loaded document's operators,
// with the geometry that defines their connections, because in this model the
// geometry IS the graph. Sorted the way Connect() reads them — down the page,
// then left to right — so the reading order matches the evaluation order.

static void DrawOperatorList()
{
  if(!Doc || Doc->Pages.GetCount()==0)
  {
    // There is no File > Open — the editor has no file dialog. Say what the
    // user can actually do, not what a more complete editor would offer.
    ImGui::TextDisabled("No document loaded.");
    ImGui::TextDisabled("Pass a .wz4t on the command line: wz4ed <doc> -meta <dir>");
    return;
  }

  if(Ed->CurrentPage>=Doc->Pages.GetCount())
    Ed->CurrentPage = 0;

  // Page selector.
  if(ImGui::BeginCombo("Page",wUtf8(Doc->Pages[Ed->CurrentPage]->Name)))
  {
    for(sInt i=0;i<Doc->Pages.GetCount();i++)
    {
      sBool sel = (i==Ed->CurrentPage);
      if(ImGui::Selectable(wUtf8(Doc->Pages[i]->Name),sel))
      {
        Ed->CurrentPage = i;
        Ed->Selected = 0;
      }
    }
    ImGui::EndCombo();
  }

  wPage *page = Doc->Pages[Ed->CurrentPage];
  ImGui::Separator();

  const ImGuiTableFlags flags = ImGuiTableFlags_Borders
                              | ImGuiTableFlags_RowBg
                              | ImGuiTableFlags_SizingStretchProp
                              | ImGuiTableFlags_ScrollY;

  if(ImGui::BeginTable("ops",5,flags))
  {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Class");
    ImGui::TableSetupColumn("Pos",ImGuiTableColumnFlags_WidthFixed,64.0f);
    ImGui::TableSetupColumn("Size",ImGuiTableColumnFlags_WidthFixed,64.0f);
    ImGui::TableSetupColumn("In",ImGuiTableColumnFlags_WidthFixed,32.0f);
    ImGui::TableSetupScrollFreeze(0,1);
    ImGui::TableHeadersRow();

    for(sInt i=0;i<page->Ops.GetCount();i++)
    {
      wStackOp *op = page->Ops[i];
      ImGui::TableNextRow();
      ImGui::PushID(i);

      ImGui::TableNextColumn();
      // An operator with no store name is the common case; show the class in
      // grey rather than an empty cell, so rows are never blank.
      const sChar *name = op->Name.IsEmpty() ? 0 : (const sChar *) op->Name;
      sBool selected = (Ed->Selected==op);
      if(ImGui::Selectable(name ? wUtf8(name) : "(unnamed)",selected,
           ImGuiSelectableFlags_SpanAllColumns))
        Ed->Selected = op;

      ImGui::TableNextColumn();
      if(op->Class)
        ImGui::TextUnformatted(wUtf8(op->Class->Name));
      else
        ImGui::TextDisabled("?");

      ImGui::TableNextColumn();
      ImGui::Text("%d,%d",op->PosX,op->PosY);

      ImGui::TableNextColumn();
      ImGui::Text("%dx%d",op->SizeX,op->SizeY);

      ImGui::TableNextColumn();
      ImGui::Text("%d",op->Inputs.GetCount());

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

// What the metadata knows about the selected operator. Not the parameter panel —
// that is 5.5, and it edits. This is the same information `wz4gen describe`
// prints, shown here to prove the metadata is reaching the editor, which is what
// every later stage depends on.

static sBool DrawInspector()
{
  if(!Ed->Selected)
  {
    ImGui::TextDisabled("Select an operator.");
    return 0;
  }

  wOp *op = Ed->Selected;
  if(!op->Class)
  {
    ImGui::TextDisabled("Operator has no class.");
    return 0;
  }

  sBool changed = 0;

  ImGui::Text("%s.%s",wUtf8(op->Class->OutputType->Symbol),
                      wUtf8(op->Class->Name));

  // Hide and Bypass. These are graph edits, not display options: Hide drops the
  // block from its consumers' input lists and Bypass splices it out, passing its
  // own in0 through. So both reconnect, and the derived inputs below change
  // under them — which is the point of showing them together.
  {
    bool hide = ((wStackOp *) op)->Hide!=0;
    if(ImGui::Checkbox("Hide (H)",&hide))
    {
      ((wStackOp *) op)->Hide = hide ? 1 : 0;
      changed = 1;
    }
    ImGui::SameLine();
    bool bypass = op->Bypass!=0;
    if(ImGui::Checkbox("Bypass (B)",&bypass))
    {
      op->Bypass = bypass ? 1 : 0;
      changed = 1;
    }
  }

  // The derived input list, in slot order. This is the editor showing what the
  // connection rule produced for this operator — geometry in, arguments out —
  // and it is how a person checks that a drag did what it looked like it did.
  // Nothing here is stored in the document: every entry was computed from
  // rectangles by wDocument::Connect().
  ImGui::Separator();
  if(op->Inputs.GetCount()==0)
  {
    ImGui::TextDisabled("no inputs derived");
  }
  else
  {
    ImGui::Text("%d input(s), left to right:",op->Inputs.GetCount());
    for(sInt i=0;i<op->Inputs.GetCount();i++)
    {
      wStackOp *in = (wStackOp *) op->Inputs[i];

      // Spelled out rather than nested ternaries: sPoolString and
      // const sChar * convert to each other, so a ternary mixing them is
      // ambiguous and clang refuses it.
      const sChar *nm = L"?";
      if(!in->Name.IsEmpty())
        nm = in->Name;
      else if(in->Class)
        nm = in->Class->Name;

      ImGui::BulletText("in%d  %s  @x%d",i,wUtf8(nm),in->PosX);
    }
  }

  if(!op->NoError())
  {
    const sChar *why = op->CalcErrorString ? op->CalcErrorString
                                           : op->ConnectErrorString;
    ImGui::TextColored(ImVec4(1.0f,0.45f,0.45f,1.0f),"error: %s",
      why ? wUtf8(why) : "connection refused");
  }

  const wMetaClass *mc = Ed->Meta.Find(op->Class->OutputType->Symbol,
                                       op->Class->Name);
  if(!mc)
  {
    ImGui::TextDisabled("No metadata for this class.");
    return changed;
  }

  ImGui::Separator();
  ImGui::Text("%d parameter word(s), %d string(s)",mc->ParaWords,mc->ParaStrings);
  if(mc->Array)
    ImGui::Text("array: %d word(s) per row, %d row(s)",
      mc->ArrayWords,op->GetArrayCount());

  ImGui::Separator();

  // The generated parameter panel. Everything below this line is a function of
  // the metadata; there is no per-operator UI code anywhere in the editor.
  const sInt pc = wDrawParams(op,mc);
  if(pc & wPC_VALUE)
  {
    // ChangeMsg: drop caches downstream and mark the document dirty. Doc->Change
    // walks the outputs, which is what makes an edit to a Perlin invalidate the
    // Blur that reads it rather than only itself.
    Doc->Change(op);
    Ed->MarkDirty(L"parameter");
    Ed->Status.PrintF(L"changed %s",
      op->Class ? (const sChar *) op->Class->Name : L"operator");
  }
  if(pc & wPC_CONNECT)
  {
    changed = 1;                    // ConnectMsg: the caller reconnects
    Ed->MarkDirty(L"rename or link");
  }

  return changed;
}

/****************************************************************************/
/***   frame                                                              ***/
/****************************************************************************/

// Panes are laid out by the application rather than docked. ImGui's docking is
// still branch-only and this port pins releases — see third_party/VENDORED.md.
// A texture editor has a known pane arrangement anyway, so an explicit layout is
// no loss and one less moving part.

static void DrawFrame(GLFWwindow *window,sBool &quit)
{
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const float menuh = ImGui::GetFrameHeight();

  // Declared before the menu bar because the Edit menu sets them, and applied
  // right at the end of the frame — see the note down there.
  sBool undo = 0;
  sBool redo = 0;

  if(ImGui::BeginMainMenuBar())
  {
    if(ImGui::BeginMenu("File"))
    {
      if(ImGui::MenuItem("Reload","Ctrl+R",false,!Ed->DocPath.IsEmpty()))
      {
        sString<1024> path(Ed->DocPath);
        Ed->LoadDoc(path);
      }
      ImGui::Separator();

      // Enabled only for a mesh operator, because glTF export is for Wz4Mesh and
      // a menu item that can only fail is worse than one that is greyed out.
      // There is no file dialog — ImGui has none and one is not in scope — so it
      // writes beside the document, named after the operator, and says where in
      // the status line.
      {
        wType *meshtype = Doc ? Doc->FindType(L"Wz4Mesh") : 0;
        const sBool exportable = Ed->Selected && Ed->Selected->Class && meshtype
          && Ed->Selected->Class->OutputType==meshtype && !Ed->DocPath.IsEmpty();
        if(ImGui::MenuItem("Export glTF",0,false,exportable!=0))
        {
          sString<1024> path;
          Ed->DefaultExportPath(path);
          Ed->ExportGltf(path);
        }
      }

      ImGui::Separator();
      if(ImGui::MenuItem("Quit","Ctrl+Q"))
        quit = 1;
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit"))
    {
      // The labels say what will be undone, not what will be returned to, which
      // is the difference between "Undo insert Perlin" and "Undo".
      sString<96> ul,rl;
      const sChar *u = Ed->Undo.UndoLabel();
      const sChar *r = Ed->Undo.RedoLabel();
      ul.PrintF(L"Undo %s",u ? u : L"");
      rl.PrintF(L"Redo %s",r ? r : L"");

      if(ImGui::MenuItem(wUtf8(ul),"Ctrl+Z",false,Ed->Undo.CanUndo()!=0))
        undo = 1;
      if(ImGui::MenuItem(wUtf8(rl),"Ctrl+Shift+Z",false,Ed->Undo.CanRedo()!=0))
        redo = 1;
      ImGui::Separator();
      ImGui::TextDisabled("%d/%d states, %d bytes",
        Ed->Undo.Position()+1,Ed->Undo.Depth(),sInt(Ed->Undo.Bytes()));
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View"))
    {
      ImGui::MenuItem("Operator list",0,&Ed->ShowList);
      sBool guides = Ed->Canvas.ShowGuides;
      bool g = guides!=0;
      if(ImGui::MenuItem("Connection guides",0,&g))
        Ed->Canvas.ShowGuides = g ? 1 : 0;
      ImGui::Separator();
      if(ImGui::MenuItem("Fit page","Home"))
        Ed->Canvas.FitPending = 1;
      if(ImGui::MenuItem("Reset view (1:1)"))
        Ed->Canvas.ResetView();
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Help"))
    {
      // The ImGui demo, kept in-tree at the pinned version. It is the reference
      // for every widget the parameter panel will need in 5.5.
      ImGui::MenuItem("ImGui widget reference",0,&Ed->ShowDemo);
      ImGui::EndMenu();
    }

    // Right-aligned status: what is loaded, and whether metadata was found.
    // Metadata being absent is the one failure that makes the editor useless,
    // so it is stated permanently rather than in a transient dialog.
    ImGui::SameLine(vp->Size.x-320.0f);
    if(!Ed->MetaOk)
      ImGui::TextColored(ImVec4(1.0f,0.4f,0.4f,1.0f),"%s",wUtf8(Ed->Status));
    else
      ImGui::TextDisabled("%s",wUtf8(Ed->Status));
    ImGui::EndMainMenuBar();
  }

  const float palw = vp->Size.x*0.16f;
  const float canvasw = vp->Size.x*0.54f;
  const ImGuiWindowFlags paneflags = ImGuiWindowFlags_NoMove
                                    |ImGuiWindowFlags_NoResize
                                    |ImGuiWindowFlags_NoCollapse;

  // Any edit that changes the graph — a drag, a Hide, a Bypass — sets this, and
  // the frame ends with one Connect(). Collected rather than reconnecting at the
  // point of the edit so that two edits in one frame cost one rebuild.
  sBool reconnect = 0;

  wPage *page = 0;
  if(Doc && Doc->Pages.GetCount())
  {
    if(Ed->CurrentPage>=Doc->Pages.GetCount())
      Ed->CurrentPage = 0;
    page = Doc->Pages[Ed->CurrentPage];
  }

  // Palette on the left, canvas in the middle, inspector on the right: what you
  // can add, what you have, what it is set to.
  ImGui::SetNextWindowPos(ImVec2(0,menuh));
  ImGui::SetNextWindowSize(ImVec2(palw,vp->Size.y-menuh));
  ImGui::Begin("Palette",0,paneflags);
  {
    wClass *pick = Ed->Palette.Draw();
    if(pick && page)
    {
      if(Ed->Insert(page,pick))
        reconnect = 1;
    }
  }
  ImGui::End();

  // The middle column splits: canvas above, preview below. That is the original's
  // arrangement and the right one — you edit the graph and watch the result,
  // and the two want to be visible at once.
  const float canvash = (vp->Size.y-menuh)*0.58f;

  // The canvas is the editor. In this model the geometry IS the graph, so the
  // canvas is not a view of the document, it is the document.
  ImGui::SetNextWindowPos(ImVec2(palw,menuh));
  ImGui::SetNextWindowSize(ImVec2(canvasw,canvash));
  ImGui::Begin("Canvas",0,paneflags);
  {

    if(page)
    {
      // Moving a block one cell can make or break an input, so a drag is a
      // structural change and not a cosmetic one.
      if(Ed->Canvas.Draw(page))
      {
        reconnect = 1;
        Ed->MarkDirty(L"move or resize");
      }
      Ed->Selected = Ed->Canvas.SingleSelection(page);
    }
    else
    {
      ImGui::TextDisabled("No document loaded.");
      ImGui::TextDisabled("Pass a .wz4t on the command line.");
    }
  }
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2(palw,menuh+canvash));
  ImGui::SetNextWindowSize(ImVec2(canvasw,vp->Size.y-menuh-canvash));
  ImGui::Begin("Preview",0,paneflags);
  // Routed on the selected operator's OUTPUT TYPE, which is what the original
  // editor's `gui = base2d` / `base3d` per-type setting encodes
  // (01-existing-model.md §7). Two panes, one slot: a mesh operator gets the 3D
  // viewer and everything else keeps the bitmap preview. The two Draw signatures
  // are identical on purpose, which is what makes this a two-line decision.
  {
    wType *meshtype = Doc ? Doc->FindType(L"Wz4Mesh") : 0;
    const sBool ismesh = Ed->Selected && Ed->Selected->Class
      && meshtype && Ed->Selected->Class->OutputType==meshtype;
    if(ismesh)
      Ed->MeshView.DrawPane(Ed->Selected,Ed->Revision);
    else
      Ed->Preview.Draw(Ed->Selected,Ed->Revision);
  }
  ImGui::End();

  ImGui::SetNextWindowPos(ImVec2(palw+canvasw,menuh));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x-palw-canvasw,vp->Size.y-menuh));
  ImGui::Begin("Inspector",0,paneflags);
  if(DrawInspector())
    reconnect = 1;
  ImGui::End();

  // The list is now a secondary view rather than the main one. It is kept
  // because it shows the derived input count per operator, which is the quickest
  // way to check that a canvas edit changed the graph the way it looked like it
  // did.
  if(Ed->ShowList)
  {
    ImGui::SetNextWindowSize(ImVec2(560,420),ImGuiCond_FirstUseEver);
    ImGui::Begin("Operator list",&Ed->ShowList);
    DrawOperatorList();
    ImGui::End();
  }

  if(Ed->ShowDemo)
    ImGui::ShowDemoWindow(&Ed->ShowDemo);

  if(ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl))
  {
    if(ImGui::IsKeyPressed(ImGuiKey_Q,false))
      quit = 1;
    if(ImGui::IsKeyPressed(ImGuiKey_R,false) && !Ed->DocPath.IsEmpty())
    {
      sString<1024> path(Ed->DocPath);
      Ed->LoadDoc(path);
    }
    if(ImGui::IsKeyPressed(ImGuiKey_Z,false))
    {
      if(ImGui::GetIO().KeyShift)
        redo = 1;
      else
        undo = 1;
    }
    if(ImGui::IsKeyPressed(ImGuiKey_Y,false))
      redo = 1;                     // the other common binding
  }

  if(ImGui::IsKeyPressed(ImGuiKey_Home,false))
    Ed->Canvas.FitPending = 1;

  // H and B on the whole selection, which is how the original works — these are
  // per-block graph edits and are usually applied to several blocks at once.
  // Guarded on no text field having focus, or typing an H in a name would toggle
  // Hide on everything selected.
  if(!ImGui::GetIO().WantTextInput && page)
  {
    const sBool h = ImGui::IsKeyPressed(ImGuiKey_H,false);
    const sBool b = ImGui::IsKeyPressed(ImGuiKey_B,false);
    if(h || b)
    {
      for(sInt i=0;i<page->Ops.GetCount();i++)
      {
        wStackOp *op = page->Ops[i];
        if(!op->Select) continue;
        if(h) op->Hide = op->Hide ? 0 : 1;
        if(b) op->Bypass = op->Bypass ? 0 : 1;
        reconnect = 1;
        Ed->MarkDirty(h ? L"hide" : L"bypass");
      }
    }

    if(ImGui::IsKeyPressed(ImGuiKey_Delete,false) ||
       ImGui::IsKeyPressed(ImGuiKey_Backspace,false))
    {
      if(Ed->DeleteSelection(page))
        reconnect = 1;
    }

    // Class shortcuts, as the original has them: one key inserts one operator
    // at the cursor. Only unmodified presses, so Ctrl+R stays a reload.
    //
    // H and B are checked first and are not available as class shortcuts here.
    // Upstream resolves that collision through a data-driven binding file
    // (werkkzeug4.wire.txt) which this port does not read; the palette's click
    // path reaches every operator regardless, so nothing is unreachable.
    if(!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt &&
       !ImGui::GetIO().KeySuper && !h && !b)
    {
      for(sInt i=0;i<Doc->Classes.GetCount() && !reconnect;i++)
      {
        wClass *cl = Doc->Classes[i];
        if(!cl->Shortcut || (cl->Flags & wCF_HIDE))
          continue;
        // Letters only; the registry stores an ASCII code.
        const sInt c = cl->Shortcut;
        if(c<'a' || c>'z')
          continue;
        const ImGuiKey key = ImGuiKey(ImGuiKey_A + (c-'a'));
        if(ImGui::IsKeyPressed(key,false) && !ImGui::GetIO().KeyShift)
        {
          if(Ed->Insert(page,cl))
            reconnect = 1;
        }
      }
    }
  }

  // One rebuild per frame, however many edits produced it.
  if(reconnect && Doc)
  {
    Doc->Connect();
    Ed->Revision++;                 // a structural change invalidates the preview
  }

  // Undo and redo are applied after everything else has had its say, so a frame
  // that both edits and undoes cannot interleave the two.
  if(undo && page && Ed->Undo.Undo(page))
  {
    Ed->Status.PrintF(L"undo (%d/%d)",Ed->Undo.Position()+1,Ed->Undo.Depth());
    Ed->AfterUndoRedo(page);
  }
  else if(redo && page && Ed->Undo.Redo(page))
  {
    Ed->Status.PrintF(L"redo (%d/%d)",Ed->Undo.Position()+1,Ed->Undo.Depth());
    Ed->AfterUndoRedo(page);
  }
  else if(Ed->Dirty && page)
  {
    // Deferred until nothing is being manipulated, which is what turns a whole
    // drag gesture into ONE undo entry. ImGui reports a DragFloat as active for
    // every frame the mouse is down, and the canvas holds its InvisibleButton
    // active for the length of a block drag, so both coalesce for free.
    if(!ImGui::IsAnyItemActive())
    {
      Ed->Undo.Push(page,Ed->DirtyWhat);
      Ed->Dirty = 0;
    }
  }

  (void)window;
}

/****************************************************************************/
/***   screenshot                                                         ***/
/****************************************************************************/

// Reads the framebuffer back and writes a PNG. This exists so that a GUI can be
// reviewed and regression-tested the same way the texture suite is: `-frames N
// -shot out.png` renders N frames and exits, which proves the GL context, the
// font atlas and the whole ImGui pipeline work, and leaves an image to look at.
//
// Without it "the window opens" would be a claim nobody could check.

static sBool SaveScreenshot(GLFWwindow *window,const sChar *path)
{
  sInt w = 0,h = 0;
  glfwGetFramebufferSize(window,&w,&h);
  if(w<=0 || h<=0)
  {
    sPrint(L"wz4ed: framebuffer has no size; nothing to save\n");
    sSetErrorCode();
    return 0;
  }

  sU8 *pixels = new sU8[sDInt(w)*h*4];
  glPixelStorei(GL_PACK_ALIGNMENT,1);
  glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);

  // GL's origin is bottom-left and sImage's is top-left, so the rows go back in
  // reverse. sImage is BGRA internally, which is why the channels are swapped
  // here rather than left alone.
  sImage img;
  img.Init(w,h);
  sU32 *dst = img.Data;
  for(sInt y=0;y<h;y++)
  {
    const sU8 *src = pixels + sDInt(h-1-y)*w*4;
    for(sInt x=0;x<w;x++)
    {
      const sU8 r = src[x*4+0], g = src[x*4+1], b = src[x*4+2], a = src[x*4+3];
      dst[y*w+x] = (sU32(a)<<24)|(sU32(r)<<16)|(sU32(g)<<8)|sU32(b);
    }
  }
  delete[] pixels;

  if(!img.SavePNG(path))
  {
    sPrintF(L"wz4ed: could not write <%s>\n",path);
    sSetErrorCode();
    return 0;
  }

  sPrintF(L"wz4ed: wrote %s (%d x %d)\n",path,w,h);
  return 1;
}

/****************************************************************************/
/***   main                                                               ***/
/****************************************************************************/

static void GlfwError(int code,const char *text)
{
  sPrintF(L"glfw: error %d: %s\n",code,(const sChar *) wWide(text));
}

static void Usage()
{
  sPrint(L"usage: wz4ed [<document.wz4t>] [-meta <dir>] [switches]\n\n");
  sPrint(L"  -meta     where the operator metadata is (default \"meta\")\n");
  sPrint(L"  -select   select an operator by store name at startup\n");
  sPrint(L"  -export   write the selected operator as glTF and exit\n");
  sPrint(L"            (.glb or .gltf by extension; needs -select)\n");
  sPrint(L"  -shot     write the last frame as a PNG. Implies -frames 2\n");
  sPrint(L"  -frames   render n frames and exit, instead of running\n");
  sPrint(L"  -time     animation time as a percentage, 0..100\n");
  sPrint(L"  -wire     start with wireframe on\n");
  sPrint(L"  -bbox     start with the bounding box on\n");
  sPrint(L"  -nobones  start with the skeleton overlay OFF (it is on by default)\n");
  sPrint(L"  -guides   start with connection guides on (View toggle otherwise)\n");
  sPrint(L"\n");
  sPrint(L"-shot, -frames and -export run non-interactively: the window is\n");
  sPrint(L"created hidden so the run does not steal keyboard focus.\n\n");
  sPrint(L"See docs/editor.md for the panes, the shortcuts and glTF export.\n\n");
  sPrint(L"Switches go after the filename: Altona's shell parser treats the\n");
  sPrint(L"token after a -switch as that switch's first parameter.\n");
}

void sMain()
{
  if(sGetShellSwitch(L"help") || sGetShellSwitch(L"h"))
  {
    Usage();
    return;
  }

  const sChar *doc = sGetShellParameter(0,0);
  const sChar *metadir = sGetShellParameter(L"meta",0);
  const sChar *shot = sGetShellParameter(L"shot",0);
  // sGetShellParameterInt, the (option,index,default) family — NOT sGetShellInt,
  // which is the (short,long,default) family and would silently take the
  // variable as the default and return the answer we then threw away. That
  // mistake showed up as the editor ignoring -frames and running interactively
  // forever, with no diagnostic at all.
  sInt frames = sGetShellParameterInt(L"frames",0,0);
  if(shot && frames<2)
    frames = 2;                     // one frame to lay out, one to look at

  if(!metadir)
    metadir = WZ4ED_META_DIR;

  // A run that only wants a screenshot is not an interactive run, and it must not
  // behave like one. GLFW's macOS backend calls
  //
  //     [NSApp activateIgnoringOtherApps:YES]        (cocoa_window.m:1266)
  //
  // whenever a window is shown, which STEALS FOCUS from whatever the user is
  // doing — and `ctest` runs three of these in a row, so a full suite interrupts
  // typing three times and drops keystrokes into other applications.
  //
  // It is also the most likely explanation for wz4ed_shell's intermittent
  // failures: both were the first GUI test after a clean build, which is exactly
  // when activation contention is worst.
  //
  // So a non-interactive run creates the window HIDDEN and suppresses the menu
  // bar. Nothing is shown, so nothing is activated.
  //
  // -export counts, and forgetting it would have reintroduced exactly the defect
  // this whole block exists to fix: an export run draws nothing and exits, so a
  // window that appears and grabs the keyboard on the way past is pure damage.
  const sBool headlessrun = (shot!=0) || (frames>0)
                         || (sGetShellParameter(L"export",0)!=0);

  if(headlessrun)
  {
    // An INIT hint, not a window hint: the menu bar and dock icon are created
    // inside glfwInit, so this has to precede it.
    glfwInitHint(GLFW_COCOA_MENUBAR,GLFW_FALSE);
  }

  glfwSetErrorCallback(GlfwError);
  if(!glfwInit())
  {
    sPrint(L"wz4ed: GLFW would not initialise. A window server is required;\n");
    sPrint(L"       this needs a graphical session, not just a terminal.\n");
    sSetErrorCode();
    return;
  }

  // GL 3.3 core, forward-compatible. macOS gives 4.1 at most and only in a
  // core profile, and 3.3 is available everywhere on Linux — so one context
  // version covers both targets, which is the whole reason for choosing GL over
  // Metal here.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE);
#if defined(__APPLE__)
  glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER,GLFW_TRUE);
#endif

  if(headlessrun)
  {
    // Never shown, so never activated. FOCUS_ON_SHOW is belt-and-braces for the
    // case where something later decides to show it anyway.
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED,GLFW_FALSE);
  }

  GLFWwindow *window = glfwCreateWindow(1280,800,"wz4ed",0,0);
  if(!window)
  {
    sPrint(L"wz4ed: could not create a window or a GL 3.3 core context\n");
    glfwTerminate();
    sSetErrorCode();
    return;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // No imgui.ini. The layout is the application's, not the user's, until there
  // is something worth persisting; writing a file into the working directory as
  // a side effect of starting up is exactly the litter that stage 4.5 had to
  // clean out of the texture suite.
  io.IniFilename = 0;

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window,true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  // After ImGui_ImplOpenGL3_Init, which is what initialises the vendored GL
  // loader the mesh viewer borrows. This adds only the eleven framebuffer entry
  // points that loader is stripped of — see editor/gl_wz4.hpp. A failure here is
  // not fatal: the 3D preview goes away and the texture editor carries on.
  wGlLoadExtras();

  // wz4lib comes up here, after the window: RegisterWZ4Classes runs from
  // wDocument's constructor and the editor wants its failures on screen.
  Doc = new wDocument;
  Ed = new wEditor;
  Ed->LoadMeta(metadir);
  if(doc)
    Ed->LoadDoc(doc);

  const sChar *select = sGetShellParameter(L"select",0);
  if(select && Doc->Pages.GetCount())
    Ed->SelectByName(select);

  // For screenshots: the guides are a View toggle, and a non-interactive run
  // cannot reach a menu.
  if(sGetShellSwitch(L"guides"))
    Ed->Canvas.ShowGuides = 1;

  // Likewise the mesh viewer's toggles. These exist so the 6.4 gate can assert
  // that wireframe and the bounding box actually RENDER, rather than that a
  // checkbox exists — a control whose effect is never exercised is not tested by
  // a screenshot of the control.
  if(sGetShellSwitch(L"wire"))
    Ed->MeshView.Wireframe = true;
  if(sGetShellSwitch(L"bbox"))
    Ed->MeshView.ShowBBox = true;

  // -nobones rather than -bones: the skeleton overlay is ON by default, so the
  // 7.5 gate needs a way to turn it OFF, and it renders the two and requires them
  // to differ. A -bones switch would have been a no-op and the gate would have
  // compared a pose against itself — passing only by accident.
  if(sGetShellSwitch(L"nobones"))
    Ed->MeshView.ShowBones = false;

  // And the timeline, so the 7.4 gate can assert a POSE rather than merely that
  // a scrubber exists. Given as a percentage because Altona's shell parser has
  // an integer parameter getter and no float one — 0..100 maps to t = 0..1.
  {
    const sInt pct = sGetShellParameterInt(L"time",0,-1);
    if(pct>=0)
      Ed->MeshView.Time = sClamp(sF32(pct)/100.0f,0.0f,1.0f);
  }

  sBool quit = 0;
  sBool failed = 0;
  sInt drawn = 0;

  // -export <path> runs the File menu's export on the selected operator and
  // exits. It drives ExportGltf, the SAME function the menu item calls, for the
  // reason -wire, -time and -nobones all exist: a screenshot of a menu item is
  // not evidence that the menu item works, and a gate that reimplemented the
  // export would be testing itself.
  //
  // Sets quit rather than returning, so the frame loop is skipped and the one
  // teardown below runs — duplicating it here is how a shutdown order gets
  // subtly wrong, and this file already carries two A47 scars from that.
  {
    const sChar *exportpath = sGetShellParameter(L"export",0);
    if(exportpath)
    {
      if(!Ed->ExportGltf(exportpath))
      {
        sPrintF(L"wz4ed: %s\n",(const sChar *)Ed->Status);
        failed = 1;
      }
      quit = 1;
    }
  }

  while(!quit && !glfwWindowShouldClose(window))
  {
    glfwPollEvents();

    // Rendering while minimised is wasted work and gives glReadPixels nothing.
    if(glfwGetWindowAttrib(window,GLFW_ICONIFIED))
    {
      glfwWaitEvents();
      continue;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    DrawFrame(window,quit);

    ImGui::Render();
    int fbw,fbh;
    glfwGetFramebufferSize(window,&fbw,&fbh);
    glViewport(0,0,fbw,fbh);
    glClearColor(0.09f,0.10f,0.12f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    drawn++;
    if(frames>0 && drawn>=frames)
    {
      // What the preview ended up showing, so a non-interactive run can be
      // checked rather than only looked at. Whichever pane was routed to — the
      // status line has to describe the pane the user is actually looking at,
      // or the screenshot runner asserts against the wrong one.
      sString<128> pv;
      wType *meshtype = Doc ? Doc->FindType(L"Wz4Mesh") : 0;
      const sBool ismesh = Ed->Selected && Ed->Selected->Class
        && meshtype && Ed->Selected->Class->OutputType==meshtype;
      if(ismesh)
        Ed->MeshView.Describe(pv);
      else
        Ed->Preview.Describe(pv);
      sPrintF(L"wz4ed: %s\n",pv);

      // Before the swap, so what is read back is the frame just drawn.
      if(shot && !SaveScreenshot(window,shot))
        failed = 1;
      quit = 1;
    }

    glfwSwapBuffers(window);
  }

  delete Ed;
  Ed = 0;
  delete Doc;
  Doc = 0;

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();

  // Everything above is torn down in order and cleanly. What follows is not
  // about this program's own memory.
  //
  // Altona replaces the GLOBAL operator new and delete (base/types.hpp:1713),
  // which interposes for every dylib in the process, and its memory handlers are
  // unregistered as it shuts down after sMain returns. Static destructors — in
  // the GUI frameworks the editor now links, which no earlier tool in this port
  // did — run after that and free through the interposed delete, at which point
  // sFreeMem_ finds no handler that owns the pointer and calls sFatal
  // (base/types.cpp:4776). The result was four "FATAL ERROR: pointer ... seems
  // not to belong to any sMemoryHandler" lines printed on every clean exit.
  //
  // Nothing is corrupted: Altona detects the foreign pointer and refuses it. But
  // a tool must not print FATAL ERROR when it succeeded, and the ordering is not
  // ours to fix — it is a property of interposing a process-global allocator
  // whose lifetime is shorter than the process.
  //
  // So the editor ends here rather than unwinding through it. The cost is that
  // this one binary gets no leak report from Altona at exit; every other tool in
  // the port still does, which is where that check earns its keep anyway. stdout
  // is flushed first, because _Exit does not.
  //
  // See docs/architecture.md A46.
  fflush(0);
  _Exit(failed ? 1 : 0);
}

/****************************************************************************/
