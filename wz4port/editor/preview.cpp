/****************************************************************************/
/***                                                                      ***/
/***   The 2D preview — wz4port editor, stage 5.6                         ***/
/***                                                                      ***/
/****************************************************************************/

#include "preview.hpp"
#include "wz4frlib/wz3_bitmap_code.hpp"
#include "util/image.hpp"

#include <GLFW/glfw3.h>

/****************************************************************************/

wPreview::wPreview()
{
  Texture = 0;
  TexX = TexY = 0;
  ShownOp = 0;
  ShownRevision = -1;
  ShownMode = -1;
  Failed = 0;
  Zoom = 8;                         // 1:1
  PanX = PanY = 0.0f;
  Tile = false;
  AlphaMode = AM_RGB;               // as the original — see the header
  Info = L"";
}

wPreview::~wPreview()
{
  Release();
}

void wPreview::Release()
{
  if(Texture)
  {
    GLuint t = Texture;
    glDeleteTextures(1,&t);
    Texture = 0;
  }
  TexX = TexY = 0;
}

void wPreview::ResetView()
{
  Zoom = 8;
  PanX = PanY = 0.0f;
}

void wPreview::Describe(sString<128> &out) const
{
  if(Texture)
    out.PrintF(L"preview: %d x %d uploaded",TexX,TexY);
  else if(!Info.IsEmpty())
    out.PrintF(L"preview: %s",Info);
  else
    out = L"preview: nothing shown";
}

/****************************************************************************/

sBool wPreview::Upload(wOp *op)
{
  Release();
  Failed = 1;
  Info = L"";

  if(!op || !Doc)
    return 0;

  // CalcOp uses the document's own cache, so this is not a full re-evaluation of
  // the graph — it stops at the first still-valid cached result upstream. That is
  // the caching model §6.3 describes, and it is why editing the last operator in
  // a long chain is cheap.
  wObject *obj = Doc->CalcOp(op);
  if(!obj)
  {
    Info = L"evaluation produced nothing";
    return 0;
  }

  // Dispatch on the RESULT's type, not on the operator — §7. A GenBitmap is the
  // only thing this pane can show; a mesh or a texture would need the 3D path
  // that is out of scope for this phase.
  if(!op->Class || !op->Class->OutputType ||
     sCmpString(op->Class->OutputType->Symbol,L"GenBitmap")!=0)
  {
    Info.PrintF(L"no 2D preview for %s",
      op->Class && op->Class->OutputType ? op->Class->OutputType->Symbol : L"?");
    return 0;
  }

  GenBitmap *bm = (GenBitmap *) obj;
  if(bm->XSize<=0 || bm->YSize<=0 || !bm->Data)
  {
    Info = L"empty bitmap";
    return 0;
  }

  // 16-bit fixed point down to 8-bit RGBA, which CopyTo does — the same
  // narrowing `wz4gen render` uses, so what the preview shows is what a PNG
  // export would contain.
  sImage img;
  bm->CopyTo(&img);

  const sInt n = img.SizeX*img.SizeY;
  sU8 *rgba = new sU8[sDInt(n)*4];

  // sImage words are 0xAARRGGBB. Swizzled explicitly rather than uploaded as
  // GL_BGRA: the byte order of a sU32 depends on endianness, and an explicit
  // loop cannot be wrong on a big-endian host the way a format constant can.
  for(sInt i=0;i<n;i++)
  {
    const sU32 c = img.Data[i];
    const sU8 a = sU8((c>>24)&255);

    if(AlphaMode==AM_ALPHA)
    {
      // The alpha channel as greyscale. Baked into the upload rather than done
      // with a tint, because no tint can isolate a channel.
      rgba[i*4+0] = a;
      rgba[i*4+1] = a;
      rgba[i*4+2] = a;
      rgba[i*4+3] = 255;
    }
    else
    {
      rgba[i*4+0] = sU8((c>>16)&255);
      rgba[i*4+1] = sU8((c>> 8)&255);
      rgba[i*4+2] = sU8((c    )&255);
      // RGB mode forces opaque, which is what the original's preview did.
      rgba[i*4+3] = (AlphaMode==AM_RGB) ? 255 : a;
    }
  }

  GLuint tex = 0;
  glGenTextures(1,&tex);
  glBindTexture(GL_TEXTURE_2D,tex);

  // NEAREST, deliberately. This is a texture editor: at 4:1 the user is looking
  // at individual texels, and bilinear filtering would blur exactly the detail
  // they zoomed in to inspect.
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,img.SizeX,img.SizeY,0,
    GL_RGBA,GL_UNSIGNED_BYTE,rgba);

  delete[] rgba;

  Texture = tex;
  TexX = img.SizeX;
  TexY = img.SizeY;
  Failed = 0;
  return 1;
}

/****************************************************************************/

void wPreview::Draw(wOp *op,sInt revision)
{
  // Re-evaluate only when something actually changed. The alpha mode counts,
  // because it is baked into the upload.
  if(op!=ShownOp || revision!=ShownRevision || AlphaMode!=ShownMode)
  {
    ShownOp = op;
    ShownRevision = revision;
    ShownMode = AlphaMode;
    Upload(op);
  }

  // --- toolbar -------------------------------------------------------------

  if(ImGui::Button("1:1"))
  {
    ResetView();
  }
  ImGui::SameLine();
  if(ImGui::Button("-") && Zoom>0)
    Zoom--;
  ImGui::SameLine();
  if(ImGui::Button("+") && Zoom<15)
    Zoom++;

  ImGui::SameLine();
  ImGui::Checkbox("tile",&Tile);

  // Three named modes rather than two independent toggles: "alpha off" and
  // "show alpha as grey" are not orthogonal, and presenting them as checkboxes
  // made it unclear what the combination meant.
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  const char *names[3] = { "rgb","rgba","alpha" };
  for(sInt i=0;i<3;i++)
  {
    ImGui::SameLine();
    if(ImGui::RadioButton(names[i],AlphaMode==i))
      AlphaMode = i;
  }
  if(ImGui::IsItemHovered())
    ImGui::SetTooltip("rgb ignores alpha, as the original's preview did.\n"
                      "rgba composites over the checkerboard.\n"
                      "alpha shows the channel as greyscale.");

  // The W x H readout the original prints, plus the zoom as a ratio rather than
  // as the raw index — 8 means nothing to a reader, 1:1 does.
  ImGui::SameLine();
  if(Texture)
  {
    if(Zoom>=8)
      ImGui::Text("  %d x %d   %d:1",TexX,TexY,1<<(Zoom-8));
    else
      ImGui::Text("  %d x %d   1:%d",TexX,TexY,1<<(8-Zoom));
  }
  else
  {
    ImGui::TextDisabled("  %s",Info.IsEmpty() ? L"nothing selected" : (const sChar *)Info);
  }

  // --- image ---------------------------------------------------------------

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 avail = ImGui::GetContentRegionAvail();
  if(avail.x<16.0f) avail.x = 16.0f;
  if(avail.y<16.0f) avail.y = 16.0f;

  ImGui::InvisibleButton("preview",avail,
    ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(origin,ImVec2(origin.x+avail.x,origin.y+avail.y),true);

  // A checkerboard ground, so a transparent texture reads as transparent rather
  // than as whatever colour happens to be behind it. Same reasoning as the alpha
  // toggle: not being able to see transparency is how a blank render passes for
  // a white one.
  {
    const float cs = 8.0f;
    const sInt nx = sInt(avail.x/cs)+1;
    const sInt ny = sInt(avail.y/cs)+1;
    dl->AddRectFilled(origin,ImVec2(origin.x+avail.x,origin.y+avail.y),
      IM_COL32(40,42,48,255));
    for(sInt y=0;y<ny;y++)
    {
      for(sInt x=0;x<nx;x++)
      {
        if(((x^y)&1)==0) continue;
        const float px = origin.x + x*cs;
        const float py = origin.y + y*cs;
        dl->AddRectFilled(ImVec2(px,py),
          ImVec2(sMin(px+cs,origin.x+avail.x),sMin(py+cs,origin.y+avail.y)),
          IM_COL32(52,55,62,255));
      }
    }
  }

  ImGuiIO &io = ImGui::GetIO();
  const sBool hovered = ImGui::IsItemHovered();

  if(hovered && io.MouseWheel!=0.0f)
    Zoom = sClamp<sInt>(Zoom + (io.MouseWheel>0 ? 1 : -1),0,15);

  if(hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
  {
    PanX += io.MouseDelta.x;
    PanY += io.MouseDelta.y;
  }

  if(Texture)
  {
    // scale = 2^(zoom-8), upstream's exact mapping.
    float scale;
    if(Zoom>=8)
      scale = float(1<<(Zoom-8));
    else
      scale = 1.0f/float(1<<(8-Zoom));

    const float w = TexX*scale;
    const float h = TexY*scale;

    // Centred, then panned — so a fresh preview is where you expect it and the
    // pan is a deliberate act.
    const float x0 = origin.x + (avail.x-w)*0.5f + PanX;
    const float y0 = origin.y + (avail.y-h)*0.5f + PanY;

    const ImTextureRef ref((ImTextureID)(sDInt)Texture);
    const sInt reps = Tile ? 1 : 0;    // 3x3 means one repeat either side

    for(sInt ty=-reps;ty<=reps;ty++)
    {
      for(sInt tx=-reps;tx<=reps;tx++)
      {
        const float px = x0 + tx*w;
        const float py = y0 + ty*h;
        // The eight surrounding tiles are dimmed, so the original is still
        // identifiable — the point of 3x3 is to judge how the texture WRAPS, and
        // that is easier when you can see where the seam is.
        const ImU32 tint = (tx||ty) ? IM_COL32(150,150,150,255)
                                    : IM_COL32(255,255,255,255);
        dl->AddImage(ref,ImVec2(px,py),ImVec2(px+w,py+h),
          ImVec2(0,0),ImVec2(1,1),tint);
      }
    }

    // A one-pixel frame around the untiled image, so its extent is visible when
    // the content runs to the edge of the bitmap.
    dl->AddRect(ImVec2(x0,y0),ImVec2(x0+w,y0+h),IM_COL32(255,255,255,60));
  }
  else if(Failed && !Info.IsEmpty())
  {
    char utf8[256];
    utf8[0] = 0;
    sCopyStringToUTF8(utf8,Info,sCOUNTOF(utf8));
    dl->AddText(ImVec2(origin.x+8.0f,origin.y+8.0f),
      IM_COL32(200,140,140,255),utf8);
  }

  dl->PopClipRect();
}

/****************************************************************************/
