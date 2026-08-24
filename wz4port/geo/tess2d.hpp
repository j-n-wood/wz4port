/****************************************************************************/
/***                                                                      ***/
/***   tess2d — polygon tessellation for Text3D and Path3D, stage 6.6      ***/
/***                                                                      ***/
/****************************************************************************/
//
// Turns a set of closed 2D contours into triangles. This is the piece
// Werkkzeug4 got from glu32, which exists on Windows and nowhere this port
// runs.
//
// EAR CLIPPING WITH HOLE BRIDGING, and the limitation is the point of this
// paragraph. GLU is a sweep-line tessellator: it copes with self-intersecting
// contours, coincident edges and arbitrary winding rules. This does not. What it
// handles is a set of simple closed contours, properly nested, which is exactly
// what a glyph outline is — TrueType and CFF outlines are non-self-intersecting
// by construction, with holes wound opposite to their enclosing contour.
//
// So for Text3D the restriction costs nothing. For Path3D, a hand-written path
// that crosses itself will tessellate wrongly or not at all, where GLU would
// have coped. That is a real behavioural difference from the original tool, and
// it is written down here rather than discovered later.
//
// Vendoring libtess2 — a maintained fork of the same SGI tessellator glu32 uses
// — would remove the restriction entirely, and is the right answer if a case
// ever needs it. It was not done here because it means adding a downloaded
// dependency, which is not a decision to take quietly in the middle of a stage.
//
// WINDING. Contour orientation decides solid from hole, by the even-odd nesting
// depth rather than by the sign of the area: a contour inside an odd number of
// others is a hole. That matches how glyph outlines are built and, unlike a
// signed-area test, does not care whether a font winds its outers clockwise
// (TrueType) or counter-clockwise (CFF/PostScript).

#ifndef FILE_WZ4PORT_GEO_TESS2D_HPP
#define FILE_WZ4PORT_GEO_TESS2D_HPP

#include "base/types.hpp"
#include "base/types2.hpp"     // sArray
#include "base/math.hpp"

/****************************************************************************/

// A point, plus whatever the caller wants to carry through. Text3D and Path3D
// both need the tessellator to hand back the ORIGINAL vertex identity, because
// the mesh vertices were already created (with their normals) before
// tessellation — so the output is indices into the caller's own array, and this
// is how they get there.
struct wTessVertex
{
  sF32 X,Y;
  sInt Tag;                 // opaque; echoed back in the output indices
};

class wTess2D
{
public:
  wTess2D();
  ~wTess2D();

  // One polygon at a time. Contours between Begin and End belong together, so
  // their nesting decides which are holes.
  void Begin();
  void BeginContour();
  void AddVertex(sF32 x,sF32 y,sInt tag);
  void EndContour();

  // Tessellates and appends triangles to Indices as tag triples. Returns the
  // number of TRIANGLES added, or -1 if the input could not be tessellated —
  // which the callers must report rather than silently emit nothing.
  sInt End(sArray<sInt> &indices);

  // Why the last End() failed, for the caller's error message. Empty on success.
  const sChar *GetError() const { return Error; }

private:
  struct Contour
  {
    sInt First,Count;       // into Points
    sF32 Area;              // signed; sign is orientation
    sF32 MinX,MinY,MaxX,MaxY;
    sInt Depth;             // how many contours contain this one
    sBool Hole;
  };

  sArray<wTessVertex> Points;
  sArray<Contour> Contours;
  sInt ContourStart;
  sBool InContour;
  const sChar *Error;

  // Ear-clips one ring of indices-into-Points. Appends tag triples.
  sInt ClipRing(sArray<sInt> &ring,sArray<sInt> &indices);
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_GEO_TESS2D_HPP
