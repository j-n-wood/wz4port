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
#include "util/image.hpp"
#include "base/system.hpp"

#include "meta.hpp"
#include "wz4t.hpp"

// Never <imgui.h> directly: Altona macro-defines `new`, which mangles ImGui's
// placement-new declaration. See the header for the whole story.
#include "imgui_wz4.hpp"
#include "canvas.hpp"

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

    sInt ops = 0;
    for(sInt p=0;p<Doc->Pages.GetCount();p++)
      ops += Doc->Pages[p]->Ops.GetCount();
    Status.PrintF(L"%d page(s), %d operator(s)",Doc->Pages.GetCount(),ops);
    return 1;
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
    ImGui::TextDisabled("No document loaded.");
    ImGui::TextDisabled("File > Open, or pass a .wz4t on the command line.");
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

static void DrawInspector()
{
  if(!Ed->Selected)
  {
    ImGui::TextDisabled("Select an operator.");
    return;
  }

  wOp *op = Ed->Selected;
  if(!op->Class)
  {
    ImGui::TextDisabled("Operator has no class.");
    return;
  }

  ImGui::Text("%s.%s",wUtf8(op->Class->OutputType->Symbol),
                      wUtf8(op->Class->Name));

  const wMetaClass *mc = Ed->Meta.Find(op->Class->OutputType->Symbol,
                                       op->Class->Name);
  if(!mc)
  {
    ImGui::TextDisabled("No metadata for this class.");
    return;
  }

  ImGui::Separator();
  ImGui::Text("%d parameter word(s), %d string(s)",mc->ParaWords,mc->ParaStrings);
  if(mc->Array)
    ImGui::Text("array: %d word(s) per row, %d row(s)",
      mc->ArrayWords,op->GetArrayCount());

  ImGui::Separator();
  if(ImGui::BeginTable("params",3,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg))
  {
    ImGui::TableSetupColumn("Parameter");
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("Offset",ImGuiTableColumnFlags_WidthFixed,56.0f);
    ImGui::TableHeadersRow();

    for(sInt i=0;i<mc->Params.GetCount();i++)
    {
      const wMetaParam *p = mc->Params[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(wUtf8(p->Symbol.IsEmpty() ? p->Label : p->Symbol));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(wUtf8(p->Kind));
      ImGui::TableNextColumn();
      ImGui::Text("%d",p->Offset);
    }
    ImGui::EndTable();
  }
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
      if(ImGui::MenuItem("Quit","Ctrl+Q"))
        quit = 1;
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

  const float canvasw = vp->Size.x*0.70f;
  const ImGuiWindowFlags paneflags = ImGuiWindowFlags_NoMove
                                    |ImGuiWindowFlags_NoResize
                                    |ImGuiWindowFlags_NoCollapse;

  // The canvas is the editor. It gets the space, and the panels sit beside it —
  // in this model the geometry IS the graph, so the canvas is not a view of the
  // document, it is the document.
  ImGui::SetNextWindowPos(ImVec2(0,menuh));
  ImGui::SetNextWindowSize(ImVec2(canvasw,vp->Size.y-menuh));
  ImGui::Begin("Canvas",0,paneflags);
  {
    wPage *page = 0;
    if(Doc && Doc->Pages.GetCount())
    {
      if(Ed->CurrentPage>=Doc->Pages.GetCount())
        Ed->CurrentPage = 0;
      page = Doc->Pages[Ed->CurrentPage];
    }

    if(page)
    {
      // Reconnect on any structural change. Moving a block one cell can make or
      // break an input, so this is not cosmetic — Connect() is what turns the
      // new geometry back into a graph.
      if(Ed->Canvas.Draw(page))
      {
        Doc->Connect();
        Ed->Status.PrintF(L"reconnected: %d operator(s)",page->Ops.GetCount());
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

  ImGui::SetNextWindowPos(ImVec2(canvasw,menuh));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x-canvasw,vp->Size.y-menuh));
  ImGui::Begin("Inspector",0,paneflags);
  DrawInspector();
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
  }

  if(ImGui::IsKeyPressed(ImGuiKey_Home,false))
    Ed->Canvas.FitPending = 1;

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
  sPrint(L"usage: wz4ed [<document.wz4t>] [-meta <dir>]\n");
  sPrint(L"             [-frames <n>] [-shot <file.png>] [-select <name>]\n\n");
  sPrint(L"  -frames  render n frames and exit, instead of running\n");
  sPrint(L"  -shot    write the last frame as a PNG. Implies -frames 2\n");
  sPrint(L"  -select  select an operator by store name at startup\n");
  sPrint(L"\n");
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

  sBool quit = 0;
  sBool failed = 0;
  sInt drawn = 0;

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
