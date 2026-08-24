/****************************************************************************/
/***                                                                      ***/
/***   Stage 6.6a gate — the tessellator, on its own                       ***/
/***                                                                      ***/
/****************************************************************************/
//
// The tessellator is tested BEFORE anything uses it, and alone, because a subtle
// bug in it surfaces as "the text looks slightly off" — which is close to the
// worst failure signal available. A missing ear, a hole bridged to the wrong
// vertex or a reversed winding all produce output that is plausible at a glance.
//
// So the assertions are of two kinds.
//
// DERIVED COUNTS. A simple polygon with n vertices always tessellates to exactly
// n-2 triangles; that is a theorem, not an observation. A square is 2, a concave
// L of 6 vertices is 4. A ring bridged with h holes gains 2 vertices per hole,
// so a square with a square hole is (4 + 4 + 2) - 2 = 8.
//
// AREA. The one check that catches what counts cannot: the total area of the
// output triangles must equal the input's. A dropped ear, a doubled triangle, a
// hole tessellated as solid, or a bridge that folds back all change the area
// while leaving the triangle count either right or plausibly wrong. It is also
// the only assertion here that would survive a change of algorithm.

#include "base/types.hpp"
#include "base/system.hpp"
#include "tess2d.hpp"

/****************************************************************************/

static sInt Failures = 0;

static void Check(sBool cond,const sChar *what)
{
  if(cond)
  {
    sPrintF(L"  ok    %s\n",what);
  }
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

/****************************************************************************/

// The caller's vertex array, exactly as Text3D and Path3D will hold it: the
// tessellator is given tags and hands the same tags back, so this is what the
// output indices mean.
//
// PLAIN ARRAYS, not sArray, and the reason is worth a note. As file-scope
// sArrays their destructors run after Altona has unregistered its memory
// handlers, and sFreeMem_ then prints "FATAL ERROR: pointer ... seems not to
// belong to any sMemoryHandler" — with a ZERO exit code, so the test passed while
// announcing a fatal error. That is A47, and the second time in this stage: 6.3b's
// lock printer hit the same hazard from the other end, where a file-scope
// sTextBuffer allocated in its CONSTRUCTOR before the handlers existed.
//
// A pointer allocated in sMain would fix it. A fixed array is better: no case
// here needs more than a dozen points, so a growable container was never the
// right tool, and this removes the whole hazard class rather than working around
// it once more.
static const sInt MaxPoints = 64;
static sF32 VX[MaxPoints];
static sF32 VY[MaxPoints];
static sInt VCount = 0;

static sInt AddPoint(sF32 x,sF32 y)
{
  sVERIFY(VCount<MaxPoints);
  VX[VCount] = x;
  VY[VCount] = y;
  return VCount++;
}

static void Reset()
{
  VCount = 0;
}

// Total unsigned area of the output triangles. Unsigned on purpose: the point is
// to compare against the input's area, and a triangle emitted with reversed
// winding is still covering the same ground — winding is checked separately, by
// tess2d itself, since it only ever clips counter-clockwise rings.
static sF32 TriangleArea(const sArray<sInt> &idx)
{
  sF32 total = 0;
  for(sInt i=0;i+2<idx.GetCount();i+=3)
  {
    const sF32 ax = VX[idx[i]],   ay = VY[idx[i]];
    const sF32 bx = VX[idx[i+1]], by = VY[idx[i+1]];
    const sF32 cx = VX[idx[i+2]], cy = VY[idx[i+2]];
    total += sFAbs((bx-ax)*(cy-ay) - (by-ay)*(cx-ax))*0.5f;
  }
  return total;
}

// Every triangle must be non-degenerate and every index in range. Cheap, and it
// is what stops a "correct" area from hiding a sliver.
static void CheckTriangles(const sArray<sInt> &idx,const sChar *what)
{
  sInt degenerate = 0,bad = 0;
  for(sInt i=0;i+2<idx.GetCount();i+=3)
  {
    for(sInt k=0;k<3;k++)
      if(idx[i+k]<0 || idx[i+k]>=VCount)
        bad++;
    if(bad)
      continue;
    if(idx[i]==idx[i+1] || idx[i+1]==idx[i+2] || idx[i]==idx[i+2])
      degenerate++;
  }
  Check(bad==0,L"every index is in range");
  if(degenerate)
    sPrintF(L"        %d triangle(s) repeat a vertex\n",degenerate);
  Check(degenerate==0,what);
}

/****************************************************************************/

void sMain()
{
  sPrint(L"tess2d_shapes: stage 6.6a gate\n\n");

  wTess2D tess;
  sArray<sInt> idx;

  // --- a square -------------------------------------------------------------

  sPrint(L"a square is two triangles\n");
  {
    Reset();
    idx.Clear();
    sInt t[4];
    t[0] = AddPoint(0,0);
    t[1] = AddPoint(2,0);
    t[2] = AddPoint(2,2);
    t[3] = AddPoint(0,2);

    tess.Begin();
    tess.BeginContour();
    for(sInt i=0;i<4;i++)
      tess.AddVertex(VX[t[i]],VY[t[i]],t[i]);
    tess.EndContour();
    const sInt n = tess.End(idx);

    Check(n==2,L"4 vertices give 4-2 = 2 triangles");
    Check(idx.GetCount()==6,L"and 6 indices");
    CheckTriangles(idx,L"and neither is degenerate");
    Check(sFAbs(TriangleArea(idx)-4.0f)<1e-4f,
      L"and the area is 4, the square's own");
  }

  // --- the same square wound the other way ----------------------------------

  sPrint(L"\nand so is the same square wound clockwise\n");
  {
    Reset();
    idx.Clear();
    sInt t[4];
    t[0] = AddPoint(0,0);
    t[1] = AddPoint(0,2);
    t[2] = AddPoint(2,2);
    t[3] = AddPoint(2,0);

    tess.Begin();
    tess.BeginContour();
    for(sInt i=0;i<4;i++)
      tess.AddVertex(VX[t[i]],VY[t[i]],t[i]);
    tess.EndContour();
    const sInt n = tess.End(idx);

    // Orientation must not matter. TrueType winds outer contours clockwise and
    // CFF winds them counter-clockwise, so a tessellator that only accepted one
    // would work for half the fonts on the machine.
    Check(n==2,L"orientation does not change the count");
    Check(sFAbs(TriangleArea(idx)-4.0f)<1e-4f,L"nor the area");
    CheckTriangles(idx,L"and still nothing degenerate");
  }

  // --- a concave L ----------------------------------------------------------

  sPrint(L"\na concave L of six vertices is four triangles\n");
  {
    Reset();
    idx.Clear();
    // Area: a 3x3 square less a 2x2 bite = 9 - 4 = 5.
    sInt t[6];
    t[0] = AddPoint(0,0);
    t[1] = AddPoint(3,0);
    t[2] = AddPoint(3,1);
    t[3] = AddPoint(1,1);
    t[4] = AddPoint(1,3);
    t[5] = AddPoint(0,3);

    tess.Begin();
    tess.BeginContour();
    for(sInt i=0;i<6;i++)
      tess.AddVertex(VX[t[i]],VY[t[i]],t[i]);
    tess.EndContour();
    const sInt n = tess.End(idx);

    Check(n==4,L"6 vertices give 6-2 = 4 triangles");
    CheckTriangles(idx,L"none degenerate");
    // 5, not 9: an ear clipper that ignored the reflex vertex would fill the
    // notch and report 9. This is the assertion that catches that.
    Check(sFAbs(TriangleArea(idx)-5.0f)<1e-4f,
      L"and the area is 5 — the notch was NOT filled in");
  }

  // --- a square with a square hole ------------------------------------------

  sPrint(L"\na square with a square hole keeps the hole\n");
  {
    Reset();
    idx.Clear();
    // Outer 4x4 counter-clockwise, inner 2x2 clockwise: area 16 - 4 = 12.
    sInt o[4],h[4];
    o[0] = AddPoint(0,0);
    o[1] = AddPoint(4,0);
    o[2] = AddPoint(4,4);
    o[3] = AddPoint(0,4);
    h[0] = AddPoint(1,1);
    h[1] = AddPoint(1,3);
    h[2] = AddPoint(3,3);
    h[3] = AddPoint(3,1);

    tess.Begin();
    tess.BeginContour();
    for(sInt i=0;i<4;i++)
      tess.AddVertex(VX[o[i]],VY[o[i]],o[i]);
    tess.EndContour();
    tess.BeginContour();
    for(sInt i=0;i<4;i++)
      tess.AddVertex(VX[h[i]],VY[h[i]],h[i]);
    tess.EndContour();
    const sInt n = tess.End(idx);

    sPrintF(L"        %d triangles\n",n);

    // Bridging adds two vertices to the ring, so 4 + 4 + 2 = 10 ring vertices
    // and 10 - 2 = 8 triangles.
    Check(n==8,L"4 outer + 4 hole + 2 bridge vertices give 8 triangles");
    CheckTriangles(idx,L"none degenerate");
    Check(sFAbs(TriangleArea(idx)-12.0f)<1e-4f,
      L"and the area is 16-4 = 12 — the hole is a hole, not filled");
  }

  // --- a solid island inside the hole ---------------------------------------

  sPrint(L"\nand an island inside the hole is solid again\n");
  {
    Reset();
    idx.Clear();
    // Three nested squares: 6x6 solid, 4x4 hole, 2x2 solid. 36 - 16 + 4 = 24.
    // This is the nesting a glyph like a ring-within-a-ring produces, and the
    // case a depth test gets right and a simple inside/outside test does not.
    sInt a[4],b[4],c[4];
    a[0] = AddPoint(0,0); a[1] = AddPoint(6,0); a[2] = AddPoint(6,6); a[3] = AddPoint(0,6);
    b[0] = AddPoint(1,1); b[1] = AddPoint(1,5); b[2] = AddPoint(5,5); b[3] = AddPoint(5,1);
    c[0] = AddPoint(2,2); c[1] = AddPoint(4,2); c[2] = AddPoint(4,4); c[3] = AddPoint(2,4);

    tess.Begin();
    tess.BeginContour();
    for(sInt i=0;i<4;i++) tess.AddVertex(VX[a[i]],VY[a[i]],a[i]);
    tess.EndContour();
    tess.BeginContour();
    for(sInt i=0;i<4;i++) tess.AddVertex(VX[b[i]],VY[b[i]],b[i]);
    tess.EndContour();
    tess.BeginContour();
    for(sInt i=0;i<4;i++) tess.AddVertex(VX[c[i]],VY[c[i]],c[i]);
    tess.EndContour();
    const sInt n = tess.End(idx);

    sPrintF(L"        %d triangles\n",n);
    Check(n>0,L"it tessellated");
    CheckTriangles(idx,L"none degenerate");
    Check(sFAbs(TriangleArea(idx)-24.0f)<1e-4f,
      L"and the area is 36-16+4 = 24 — depth 2 is solid, not a second hole");
  }

  // --- degenerate input is dropped, not fatal -------------------------------

  sPrint(L"\ndegenerate contours are dropped rather than fatal\n");
  {
    Reset();
    idx.Clear();
    const sInt p0 = AddPoint(0,0);
    const sInt p1 = AddPoint(1,0);

    tess.Begin();
    tess.BeginContour();                  // two points: not a polygon
    tess.AddVertex(0,0,p0);
    tess.AddVertex(1,0,p1);
    tess.EndContour();
    const sInt n = tess.End(idx);

    // A space character has no usable contour, and a path can contain one. The
    // caller needs "nothing to draw", not an error.
    Check(n==0,L"a two-point contour yields no triangles and no error");
    Check(idx.GetCount()==0,L"and no indices");
  }

  // --- repeated points, which every glyph outline has -----------------------

  sPrint(L"\nrepeated points do not stall the clip\n");
  {
    Reset();
    idx.Clear();
    sInt t[6];
    t[0] = AddPoint(0,0);
    t[1] = AddPoint(0,0);        // duplicate of the previous
    t[2] = AddPoint(2,0);
    t[3] = AddPoint(2,2);
    t[4] = AddPoint(2,2);        // and another
    t[5] = AddPoint(0,2);

    tess.Begin();
    tess.BeginContour();
    for(sInt i=0;i<6;i++)
      tess.AddVertex(VX[t[i]],VY[t[i]],t[i]);
    tess.EndContour();
    const sInt n = tess.End(idx);

    // Collapsed to the 4 distinct corners, so 2 triangles. A zero-length edge
    // makes every cross product through it zero, which reads as "degenerate ear"
    // and would otherwise stall the clip until the guard tripped.
    Check(n==2,L"consecutive duplicates collapse, giving 2 triangles");
    Check(sFAbs(TriangleArea(idx)-4.0f)<1e-4f,L"with the right area");
    CheckTriangles(idx,L"and nothing degenerate");
  }

  sPrintF(L"\n%d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
