/****************************************************************************/
/***                                                                      ***/
/***   sFont2D and the 2D software surface, on FreeType — wz4port          ***/
/***                                                                      ***/
/****************************************************************************/
//
// GenBitmap::Text (wz3_bitmap_code.cpp:2505) is the only texture operator that
// needs an OS font. It drives Altona's 2D software-drawing layer:
//
//   sRender2DBegin(xs,ys) / sRender2DEnd()   an offscreen surface
//   sRect2D(x0,y0,x1,y1,colid)               fill
//   sSetColor2D(colid,rgb)                   palette
//   sRender2DGet(data)                       read back, one sU32 per pixel
//   sFont2D(name,size,flags,width)           and GetHeight / GetWidth /
//                                            SetColor / Print
//
// Altona declares all of that in base/windows.hpp but implements it only in
// base/windows.cpp (GDI) and base/windows_xlib.cpp (X11), neither of which this
// build compiles — see the exclusion list in wz4port/CMakeLists.txt. So the
// symbols are simply absent, and this file supplies them. Nothing in Altona is
// patched: sFont2D holds its state behind an opaque `sFont2DPrivate *prv`, which
// is exactly the seam needed to implement the class from outside its own
// translation unit.
//
// SCOPE. Only what GenBitmap::Text uses, plus the cheap metric queries. The
// rest of sFont2D's declared interface — PrintMarked, PrintBasic, the sRect
// overload of Print, sGetLetterDimensions, GetCharCountFromWidth, AddResource —
// is for the GUI text layer and is left undefined on purpose. Referencing one
// gives a clear undefined-symbol error pointing here, which is better than a
// silent stub that draws nothing.
//
// Output is NOT pixel-identical to GDI or X11, and cannot be: glyph
// rasterisation, hinting and metrics all differ. That is expected, and it is why
// the Text test case asserts structure rather than being golden-locked. See
// docs/06-phase-texture.md stage 4.5.

#include "base/types.hpp"
#include "base/windows.hpp"
#include "gui/guicolor.hpp"

#include "font_outline.hpp"       // stage 6.6c: glyph outlines for Text3D

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

/****************************************************************************/
/***   the offscreen surface                                              ***/
/****************************************************************************/

// Altona's model is a single implicit render target between Begin and End,
// which is why these are file statics rather than an object.

static sU32 *Surface2D = 0;
static sInt Surface2DX = 0;
static sInt Surface2DY = 0;

// sGC_MAX is the end of the standard GUI palette; GenBitmap::Text defines two
// entries past it for its own foreground and background, so leave headroom.
static sU32 Palette2D[sGC_MAX+16];
static sBool PaletteInit = 0;

static void InitPalette()
{
  if(PaletteInit) return;
  PaletteInit = 1;
  for(sInt i=0;i<sGC_MAX+16;i++)
    Palette2D[i] = 0x000000;
  Palette2D[sGC_BLACK]    = 0x000000;
  Palette2D[sGC_WHITE]    = 0xffffff;
  Palette2D[sGC_DARKGRAY] = 0x404040;
  Palette2D[sGC_GRAY]     = 0x808080;
  Palette2D[sGC_LTGRAY]   = 0xc0c0c0;
}

void sSetColor2D(sInt colid,sU32 color)
{
  InitPalette();
  if(colid>=0 && colid<sGC_MAX+16)
    Palette2D[colid] = color;
}

void sRender2DBegin(sInt xs,sInt ys)
{
  InitPalette();
  sVERIFY(!Surface2D);              // Altona's layer does not nest
  sVERIFY(xs>0 && ys>0);
  Surface2DX = xs;
  Surface2DY = ys;
  Surface2D = new sU32[xs*ys];
  for(sInt i=0;i<xs*ys;i++)
    Surface2D[i] = 0;
}

void sRender2DEnd()
{
  delete[] Surface2D;
  Surface2D = 0;
  Surface2DX = 0;
  Surface2DY = 0;
}

void sRender2DGet(sU32 *data)
{
  sVERIFY(Surface2D);
  for(sInt i=0;i<Surface2DX*Surface2DY;i++)
    data[i] = Surface2D[i];
}

void sRect2D(sInt x0,sInt y0,sInt x1,sInt y1,sInt colid)
{
  if(!Surface2D) return;
  InitPalette();
  sU32 c = (colid>=0 && colid<sGC_MAX+16) ? Palette2D[colid] : 0;

  x0 = sClamp(x0,0,Surface2DX);
  x1 = sClamp(x1,0,Surface2DX);
  y0 = sClamp(y0,0,Surface2DY);
  y1 = sClamp(y1,0,Surface2DY);

  for(sInt y=y0;y<y1;y++)
    for(sInt x=x0;x<x1;x++)
      Surface2D[y*Surface2DX+x] = c;
}

void sRect2D(const sRect &r,sInt colid)
{
  sRect2D(r.x0,r.y0,r.x1,r.y1,colid);
}

/****************************************************************************/
/***   finding a font file                                                ***/
/****************************************************************************/

// The operator's Font parameter is a FAMILY NAME ("arial" by default), not a
// path, so it has to be resolved. An absolute path is accepted too, which is the
// only way to get a reproducible result — see the note about goldens above.

static const sChar *FontDirs[] =
{
#if defined(__APPLE__)
  L"/System/Library/Fonts",
  L"/System/Library/Fonts/Supplemental",
  L"/Library/Fonts",
#else
  L"/usr/share/fonts/truetype",
  L"/usr/share/fonts/TTF",
  L"/usr/share/fonts",
  L"/usr/local/share/fonts",
#endif
  0
};

static const sChar *FontExts[] = { L".ttf", L".ttc", L".otf", 0 };

// Families the .wz4 documents in this tree actually ask for, mapped to files
// that exist on a stock machine. Werkkzeug4 was a Windows tool, so every
// document names Windows fonts; without this, every Text operator in the corpus
// would silently render nothing.
struct FontAlias { const sChar *Want; const sChar *Try; };
static const FontAlias FontAliases[] =
{
#if defined(__APPLE__)
  { L"arial",           L"Arial"            },
  { L"arial",           L"ArialHB"          },
  { L"arial",           L"Helvetica"        },
  { L"helvetica",       L"Helvetica"        },
  { L"times",           L"Times New Roman"  },
  { L"times new roman", L"Times New Roman"  },
  { L"courier",         L"Courier New"      },
  { L"courier new",     L"Courier New"      },
  { L"verdana",         L"Verdana"          },
  { L"tahoma",          L"Tahoma"           },
  { L"impact",          L"Impact"           },
  { L"georgia",         L"Georgia"          },
#else
  { L"arial",           L"DejaVuSans"       },
  { L"arial",           L"LiberationSans-Regular" },
  { L"helvetica",       L"DejaVuSans"       },
  { L"times",           L"DejaVuSerif"      },
  { L"times new roman", L"LiberationSerif-Regular" },
  { L"courier",         L"DejaVuSansMono"   },
  { L"courier new",     L"LiberationMono-Regular" },
#endif
  { 0, 0 }
};

static sBool TryFontFile(const sChar *path,sString<1024> &out)
{
  if(!sCheckFile(path))
    return 0;
  out = path;
  return 1;
}

// Looks for <stem> with each known extension in each known directory.
static sBool FindFontStem(const sChar *stem,sString<1024> &out)
{
  for(sInt d=0;FontDirs[d];d++)
  {
    for(sInt e=0;FontExts[e];e++)
    {
      sString<1024> path;
      path.PrintF(L"%s/%s%s",FontDirs[d],stem,FontExts[e]);
      if(TryFontFile(path,out))
        return 1;
    }
  }
  return 0;
}

static sBool ResolveFont(const sChar *name,sString<1024> &out)
{
  if(!name || !name[0])
    name = L"arial";

  // An explicit path wins, and is the reproducible option.
  if(name[0]=='/' && TryFontFile(name,out))
    return 1;

  // The name as given, then the alias table.
  if(FindFontStem(name,out))
    return 1;

  sString<256> lower(name);
  for(sInt i=0;lower[i];i++)
    if(lower[i]>='A' && lower[i]<='Z')
      lower[i] = lower[i]-'A'+'a';

  for(sInt i=0;FontAliases[i].Want;i++)
    if(sCmpString(lower,FontAliases[i].Want)==0)
      if(FindFontStem(FontAliases[i].Try,out))
        return 1;

  // Anything at all, so that a graph with a Text operator still shows text.
  for(sInt i=0;FontAliases[i].Want;i++)
    if(FindFontStem(FontAliases[i].Try,out))
      return 1;

  return 0;
}

/****************************************************************************/
/***   sFont2D                                                            ***/
/****************************************************************************/

static FT_Library FreeType = 0;
static sBool FreeTypeFailed = 0;

// Initialised on first use and never torn down: Altona offers no shutdown hook
// at this layer, and one FT_Library for the life of the process is the same
// thing every FreeType application does.
static FT_Library GetFreeType()
{
  if(!FreeType && !FreeTypeFailed)
  {
    if(FT_Init_FreeType(&FreeType)!=0)
    {
      FreeType = 0;
      FreeTypeFailed = 1;
      sPrint(L"font: FreeType would not initialise; Text will render nothing\n");
    }
  }
  return FreeType;
}

struct sFont2DPrivate
{
  FT_Face Face;
  sU8 *FileData;                    // FT_New_Memory_Face does not copy this
  sInt Height;                      // line height in pixels
  sInt Baseline;                    // ascender, pixels below the top of a line
  sInt TextColor;
  sInt BackColor;
  sBool Bold;
};

sFont2D::sFont2D(const sChar *name,sInt size,sInt flags,sInt width)
{
  prv = new sFont2DPrivate;
  prv->Face = 0;
  prv->FileData = 0;
  prv->Height = size>0 ? size : 1;
  prv->Baseline = prv->Height;
  prv->TextColor = sGC_WHITE;
  prv->BackColor = sGC_BLACK;
  prv->Bold = 0;
  Init(name,size,flags,width);
}

sFont2D::~sFont2D()
{
  Exit();
  delete prv;
  prv = 0;
}

void sFont2D::Init(const sChar *name,sInt size,sInt flags,sInt width)
{
  Exit();

  FT_Library ft = GetFreeType();
  if(!ft) return;
  if(size<1) size = 1;

  sString<1024> path;
  if(!ResolveFont(name,path))
  {
    sPrintF(L"font: no font file found for <%s>; Text will render nothing\n",name);
    return;
  }

  // Read the file ourselves rather than using FT_New_Face, because the path is
  // sChar (2-byte) and FreeType wants char*. sLoadFile already handles that
  // conversion consistently with the rest of the port.
  sDInt bytes = 0;
  sU8 *data = sLoadFile(path,bytes);
  if(!data)
  {
    sPrintF(L"font: could not read <%s>\n",path);
    return;
  }

  FT_Face face = 0;
  if(FT_New_Memory_Face(ft,data,FT_Long(bytes),0,&face)!=0)
  {
    sPrintF(L"font: FreeType could not open <%s>\n",path);
    delete[] data;
    return;
  }

  // width 0 means "whatever the aspect ratio says"; GenBitmap::Text passes
  // XSize*Stretch, and Stretch defaults to 0.
  if(FT_Set_Pixel_Sizes(face,width>0 ? FT_UInt(width) : 0,FT_UInt(size))!=0)
  {
    // A bitmap-only face will refuse an arbitrary size. Take the nearest strike
    // rather than giving up entirely.
    if(face->num_fixed_sizes>0)
      FT_Select_Size(face,0);
  }

  if(flags & sF2C_ITALICS)
  {
    // A 12° shear, which is what a synthetic oblique normally is.
    FT_Matrix m;
    m.xx = 0x10000; m.xy = 0x03800;
    m.yx = 0;       m.yy = 0x10000;
    FT_Set_Transform(face,&m,0);
  }

  prv->Face = face;
  prv->FileData = data;
  prv->Bold = (flags & sF2C_BOLD) ? 1 : 0;
  prv->Height = face->size->metrics.height >> 6;
  prv->Baseline = face->size->metrics.ascender >> 6;
  if(prv->Height<1)
    prv->Height = size;
}

void sFont2D::Exit()
{
  if(!prv) return;
  if(prv->Face)
  {
    FT_Done_Face(prv->Face);
    prv->Face = 0;
  }
  if(prv->FileData)
  {
    delete[] prv->FileData;
    prv->FileData = 0;
  }
}

void sFont2D::SetColor(sInt text,sInt back)
{
  prv->TextColor = text;
  prv->BackColor = back;
}

sInt sFont2D::GetHeight()      { return prv->Height; }
sInt sFont2D::GetBaseline()    { return prv->Baseline; }
sInt sFont2D::GetCharHeight()  { return prv->Height; }

// One glyph's contribution to the pen position. Kept in one place so GetWidth
// and Print can never disagree — GenBitmap::Text centres with GetWidth and then
// draws with Print, so a mismatch would show up as text drifting off centre.
static sInt LoadGlyph(sFont2DPrivate *prv,sChar c,sBool render)
{
  FT_Face face = prv->Face;
  FT_Int32 mode = render ? FT_LOAD_RENDER : FT_LOAD_DEFAULT;
  if(FT_Load_Char(face,FT_ULong(c),mode)!=0)
    return 0;
  if(prv->Bold && face->glyph->format==FT_GLYPH_FORMAT_OUTLINE)
    FT_Outline_Embolden(&face->glyph->outline,1<<5);
  return sInt(face->glyph->advance.x >> 6);
}

sInt sFont2D::GetWidth(const sChar *text,sInt len)
{
  if(!prv->Face || !text) return 0;
  if(len<0) len = sGetStringLen(text);

  sInt w = 0;
  for(sInt i=0;i<len;i++)
    w += LoadGlyph(prv,text[i],0);
  return w;
}

sInt sFont2D::GetAdvance(const sChar *text,sInt len)
{
  return GetWidth(text,len);
}

sBool sFont2D::LetterExists(sChar letter)
{
  if(!prv->Face) return 0;
  return FT_Get_Char_Index(prv->Face,FT_ULong(letter))!=0;
}

void sFont2D::Print(sInt flags,sInt x,sInt y,const sChar *text,sInt len)
{
  if(!prv->Face || !Surface2D || !text) return;
  if(len<0) len = sGetStringLen(text);

  InitPalette();
  const sU32 fg = Palette2D[sClamp(prv->TextColor,0,sGC_MAX+15)];
  const sInt fr = (fg>>16)&255, fgc = (fg>>8)&255, fb = fg&255;

  // y is the TOP of the line, matching GDI's TA_TOP default, which is what
  // GenBitmap::Text assumes when it steps y by the line height.
  const sInt baseline = y + prv->Baseline;
  FT_Face face = prv->Face;
  sInt pen = x;

  for(sInt i=0;i<len;i++)
  {
    sInt advance = LoadGlyph(prv,text[i],1);
    FT_GlyphSlot g = face->glyph;

    if(g->format==FT_GLYPH_FORMAT_OUTLINE)
      FT_Render_Glyph(g,FT_RENDER_MODE_NORMAL);

    const FT_Bitmap &bm = g->bitmap;
    if(bm.buffer && bm.width>0 && bm.rows>0)
    {
      const sInt ox = pen + g->bitmap_left;
      const sInt oy = baseline - g->bitmap_top;

      for(sInt r=0;r<sInt(bm.rows);r++)
      {
        const sInt py = oy + r;
        if(py<0 || py>=Surface2DY) continue;
        const sU8 *src = bm.buffer + r*bm.pitch;
        sU32 *dst = Surface2D + py*Surface2DX;

        for(sInt c=0;c<sInt(bm.width);c++)
        {
          const sInt px = ox + c;
          if(px<0 || px>=Surface2DX) continue;

          sInt cov = src[c];
          if(bm.pixel_mode==FT_PIXEL_MODE_MONO)
            cov = (src[c>>3] & (0x80>>(c&7))) ? 255 : 0;
          if(!cov) continue;

          // Blend the text colour over what is already there, rather than
          // overwriting, so overlapping glyphs and antialiased edges compose
          // correctly. GenBitmap::Text reads only the low byte of the result.
          const sU32 d = dst[px];
          const sInt dr = (d>>16)&255, dg = (d>>8)&255, db = d&255;
          const sInt nr = dr + (fr  - dr)*cov/255;
          const sInt ng = dg + (fgc - dg)*cov/255;
          const sInt nb = db + (fb  - db)*cov/255;
          dst[px] = (sU32(nr)<<16)|(sU32(ng)<<8)|sU32(nb);
        }
      }
    }

    pen += advance;
  }
}

/****************************************************************************/
/***   Glyph outlines, for Text3D — stage 6.6c                            ***/
/****************************************************************************/
//
// Here rather than in a file of its own because this one already owns the
// FreeType handle and ResolveFont — the alias table, the macOS font directories
// and the fallback that keeps a graph working when the named font is absent. See
// compat/font_outline.hpp for the coordinate convention.

sBool wLoadGlyphOutline(const sChar *font,sF32 height,sBool bold,sBool italic,
  sInt ch,wGlyphOutline &out)
{
  out.Clear();

  FT_Library ft = GetFreeType();
  if(!ft)
    return 0;

  sString<1024> path;
  if(!ResolveFont(font,path))
  {
    sPrintF(L"Text3D: no font file found for <%s>\n",font);
    return 0;
  }

  sDInt bytes = 0;
  sU8 *data = sLoadFile(path,bytes);
  if(!data)
  {
    sPrintF(L"Text3D: could not read <%s>\n",(const sChar *)path);
    return 0;
  }

  FT_Face face = 0;
  if(FT_New_Memory_Face(ft,data,FT_Long(bytes),0,&face)!=0)
  {
    sPrintF(L"Text3D: FreeType could not open <%s>\n",(const sChar *)path);
    delete[] data;
    return 0;
  }

  // The same scale the Windows path uses: GDI is asked for height*128 and
  // divides by 128, so the two produce the same units.
  const sInt px = sMax(8,sInt(height*128.0f));
  FT_Set_Pixel_Sizes(face,0,FT_UInt(px));

  if(italic)
  {
    FT_Matrix m;
    m.xx = 0x10000; m.xy = 0x03800;     // a 12 degree shear, as in sFont2D above
    m.yx = 0;       m.yy = 0x10000;
    FT_Set_Transform(face,&m,0);
  }

  // FT_LOAD_NO_BITMAP because a bitmap strike has no outline to walk, and
  // NO_HINTING because hinting distorts an outline to fit a pixel grid — which
  // is exactly wrong when the destination is geometry rather than pixels.
  const FT_UInt index = FT_Get_Char_Index(face,FT_ULong(ch));
  if(FT_Load_Glyph(face,index,FT_LOAD_NO_BITMAP|FT_LOAD_NO_HINTING)!=0)
  {
    FT_Done_Face(face);
    delete[] data;
    return 0;
  }

  if(bold)
    FT_Outline_Embolden(&face->glyph->outline,px*64/24);

  const sF32 scale = 1.0f/(64.0f*128.0f);
  out.AdvanceX = face->glyph->advance.x * scale;

  FT_BBox box;
  FT_Outline_Get_CBox(&face->glyph->outline,&box);
  out.BlackBoxX = (box.xMax-box.xMin) * scale;

  const FT_Outline &ol = face->glyph->outline;
  sInt first = 0;
  for(sInt c=0;c<ol.n_contours;c++)
  {
    const sInt last = ol.contours[c];
    for(sInt i=first;i<=last;i++)
    {
      wGlyphPoint *p = out.Points.AddMany(1);
      p->X = ol.points[i].x * scale;
      p->Y = ol.points[i].y * scale;

      // FT_CURVE_TAG is the low 2 bits: 1 = on-curve, 0 = conic control,
      // 2 = cubic control. TrueType uses conic and CFF uses cubic, so a build
      // that handled only one would work for half the fonts on the machine —
      // the same trap tess2d's winding test guards against.
      const sInt tag = FT_CURVE_TAG(ol.tags[i]);
      p->Kind = (tag==FT_CURVE_TAG_ON)    ? wGP_ON
              : (tag==FT_CURVE_TAG_CUBIC) ? wGP_CUBIC
                                          : wGP_CONIC;
    }
    out.ContourEnd.AddTail(out.Points.GetCount()-1);
    first = last+1;
  }

  FT_Done_Face(face);
  delete[] data;
  return 1;
}

/****************************************************************************/
