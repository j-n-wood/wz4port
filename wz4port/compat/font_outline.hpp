/****************************************************************************/
/***                                                                      ***/
/***   Glyph outlines from FreeType, for Text3D — wz4port                  ***/
/***                                                                      ***/
/****************************************************************************/
//
// Stage 6.6c. The second of the two genuinely platform-specific things in
// wz4_mesh.cpp's 2D extrusion region: Windows fetches glyph outlines with
// GetGlyphOutlineW and walks TTPOLYGONHEADER records, and this is the same job
// done through FT_Outline.
//
// Implemented in compat/font_freetype.cpp rather than a file of its own, because
// that file already owns the FreeType library handle and the font-name
// resolution — the alias table, the macOS font directories, the fallback that
// keeps a graph with a Text operator working when the named font is absent. A
// second file would either duplicate all of it or need it exported.
//
// COORDINATES match what the Windows path produces, so the two agree. GDI is
// asked for a font of `height*128` and divides positions by 128; this asks
// FreeType for the same pixel size and divides the 26.6 fixed-point outline by
// 64*128. The result is in the operator's own units, where `height` means what
// the parameter says.

#ifndef FILE_WZ4PORT_COMPAT_FONT_OUTLINE_HPP
#define FILE_WZ4PORT_COMPAT_FONT_OUTLINE_HPP

#include "base/types.hpp"
#include "base/types2.hpp"
#include "base/math.hpp"

/****************************************************************************/

// How the point is reached FROM the previous one. This is FreeType's tag model,
// kept rather than pre-flattened, because the caller already has the Bezier
// subdivision it wants — the same recursive-to-a-tolerance code the Windows path
// uses, so both platforms flatten identically.
enum wGlyphPointKind
{
  wGP_ON = 0,           // on the curve: a line from the previous on-curve point
  wGP_CONIC,            // quadratic control point (TrueType)
  wGP_CUBIC,            // cubic control point (CFF/PostScript)
};

struct wGlyphPoint
{
  sF32 X,Y;
  sInt Kind;
};

struct wGlyphOutline
{
  sArray<wGlyphPoint> Points;
  sArray<sInt> ContourEnd;    // index of the LAST point of each contour

  sF32 AdvanceX;              // pen movement, the equivalent of gmCellIncX
  sF32 BlackBoxX;             // inked width, the equivalent of gmBlackBoxX

  wGlyphOutline() { AdvanceX = 0; BlackBoxX = 0; }
  void Clear() { Points.Clear(); ContourEnd.Clear(); AdvanceX = 0; BlackBoxX = 0; }
};

// Returns 0 if the font could not be resolved or the glyph could not be loaded.
// A glyph with no contours — a space — returns 1 with an empty outline and a
// non-zero AdvanceX, which is the distinction the caller needs.
sBool wLoadGlyphOutline(const sChar *font,sF32 height,sBool bold,sBool italic,
  sInt ch,wGlyphOutline &out);

/****************************************************************************/

#endif  // FILE_WZ4PORT_COMPAT_FONT_OUTLINE_HPP
