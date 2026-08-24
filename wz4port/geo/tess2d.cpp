/****************************************************************************/
/***                                                                      ***/
/***   tess2d — polygon tessellation for Text3D and Path3D, stage 6.6      ***/
/***                                                                      ***/
/****************************************************************************/

#include "tess2d.hpp"
#include "base/system.hpp"

/****************************************************************************/

wTess2D::wTess2D()
{
  ContourStart = 0;
  InContour = 0;
  Error = L"";
}

wTess2D::~wTess2D()
{
}

void wTess2D::Begin()
{
  Points.Clear();
  Contours.Clear();
  ContourStart = 0;
  InContour = 0;
  Error = L"";
}

void wTess2D::BeginContour()
{
  ContourStart = Points.GetCount();
  InContour = 1;
}

void wTess2D::AddVertex(sF32 x,sF32 y,sInt tag)
{
  if(!InContour)
    return;

  // Consecutive duplicates are dropped here rather than in the ear clipper.
  // Glyph outlines contain them — a contour whose last point repeats its first,
  // and on-curve points that coincide with a control point — and a zero-length
  // edge makes every cross product involving it zero, which reads as "this ear
  // is degenerate" and stalls the clip.
  if(Points.GetCount()>ContourStart)
  {
    const wTessVertex &prev = Points[Points.GetCount()-1];
    if(sFAbs(prev.X-x)<1e-7f && sFAbs(prev.Y-y)<1e-7f)
      return;
  }

  wTessVertex *v = Points.AddMany(1);
  v->X = x;
  v->Y = y;
  v->Tag = tag;
}

void wTess2D::EndContour()
{
  InContour = 0;

  sInt count = Points.GetCount()-ContourStart;

  // And the closing duplicate: an outline usually states the first point again
  // at the end. Dropped for the same reason.
  if(count>1)
  {
    const wTessVertex &a = Points[ContourStart];
    const wTessVertex &b = Points[Points.GetCount()-1];
    if(sFAbs(a.X-b.X)<1e-7f && sFAbs(a.Y-b.Y)<1e-7f)
    {
      Points.Resize(Points.GetCount()-1);
      count--;
    }
  }

  if(count<3)
  {
    // Not an error: a glyph like a space has no contours worth keeping, and a
    // path can legally contain a degenerate one. Dropped silently.
    Points.Resize(ContourStart);
    return;
  }

  Contour *c = Contours.AddMany(1);
  c->First = ContourStart;
  c->Count = count;
  c->Depth = 0;
  c->Hole = 0;

  sF32 area = 0;
  c->MinX = c->MaxX = Points[ContourStart].X;
  c->MinY = c->MaxY = Points[ContourStart].Y;
  for(sInt i=0;i<count;i++)
  {
    const wTessVertex &p = Points[ContourStart+i];
    const wTessVertex &q = Points[ContourStart+((i+1)%count)];
    area += p.X*q.Y - q.X*p.Y;
    c->MinX = sMin(c->MinX,p.X); c->MaxX = sMax(c->MaxX,p.X);
    c->MinY = sMin(c->MinY,p.Y); c->MaxY = sMax(c->MaxY,p.Y);
  }
  c->Area = area*0.5f;
}

/****************************************************************************/

// Is p strictly inside the contour? Crossing number, which is the even-odd rule
// and so is orientation-agnostic — see the header for why that matters.

static sBool PointInContour(const sArray<wTessVertex> &pts,sInt first,sInt count,
  sF32 px,sF32 py)
{
  sBool in = 0;
  for(sInt i=0,j=count-1;i<count;j=i++)
  {
    const wTessVertex &a = pts[first+i];
    const wTessVertex &b = pts[first+j];
    if((a.Y>py)!=(b.Y>py))
    {
      const sF32 t = (py-a.Y)/(b.Y-a.Y);
      if(px < a.X + t*(b.X-a.X))
        in = !in;
    }
  }
  return in;
}

static sF32 Cross(const wTessVertex &a,const wTessVertex &b,const wTessVertex &c)
{
  return (b.X-a.X)*(c.Y-a.Y) - (b.Y-a.Y)*(c.X-a.X);
}

static sBool PointInTriangle(const wTessVertex &a,const wTessVertex &b,
  const wTessVertex &c,const wTessVertex &p)
{
  // Strictly inside or on an edge. Shared vertices are excluded by the caller,
  // which compares indices — comparing positions here would reject a legitimate
  // ear whenever two distinct vertices coincide, which glyph outlines do.
  const sF32 d1 = Cross(a,b,p);
  const sF32 d2 = Cross(b,c,p);
  const sF32 d3 = Cross(c,a,p);
  const sBool neg = (d1<0) || (d2<0) || (d3<0);
  const sBool pos = (d1>0) || (d2>0) || (d3>0);
  return !(neg && pos);
}

/****************************************************************************/

// Ear clipping on one ring. The ring is counter-clockwise on entry; the caller
// guarantees that.

sInt wTess2D::ClipRing(sArray<sInt> &ring,sArray<sInt> &indices)
{
  sInt tris = 0;

  // Bounded, not `while(ring.GetCount()>=3)`. If no ear can be found the loop
  // would otherwise spin forever, and a hang is a far worse failure than a
  // reported one — this is a tessellator being fed outlines from a font file we
  // do not control.
  sInt guard = ring.GetCount()*ring.GetCount() + 16;

  while(ring.GetCount()>=3 && guard-->0)
  {
    const sInt n = ring.GetCount();
    sBool clipped = 0;

    for(sInt i=0;i<n;i++)
    {
      const sInt i0 = ring[(i+n-1)%n];
      const sInt i1 = ring[i];
      const sInt i2 = ring[(i+1)%n];

      const wTessVertex &a = Points[i0];
      const wTessVertex &b = Points[i1];
      const wTessVertex &c = Points[i2];

      // Convex in a counter-clockwise ring means a positive cross product.
      // Exactly zero is a collinear ear: clipping it produces a degenerate
      // triangle, so it is skipped here and removed below if nothing else works.
      if(Cross(a,b,c)<=0)
        continue;

      sBool contains = 0;
      for(sInt k=0;k<n && !contains;k++)
      {
        const sInt ik = ring[k];
        if(ik==i0 || ik==i1 || ik==i2)
          continue;                     // by INDEX — see PointInTriangle
        if(PointInTriangle(a,b,c,Points[ik]))
          contains = 1;
      }
      if(contains)
        continue;

      indices.AddTail(a.Tag);
      indices.AddTail(b.Tag);
      indices.AddTail(c.Tag);
      tris++;

      ring.RemAtOrder(i);
      clipped = 1;
      break;
    }

    if(!clipped)
    {
      // No ear found. Almost always a collinear run: drop one such vertex and
      // try again, which removes area from nothing and lets the clip proceed.
      sInt dropped = -1;
      const sInt m = ring.GetCount();
      for(sInt i=0;i<m;i++)
      {
        const wTessVertex &a = Points[ring[(i+m-1)%m]];
        const wTessVertex &b = Points[ring[i]];
        const wTessVertex &c = Points[ring[(i+1)%m]];
        if(sFAbs(Cross(a,b,c))<1e-9f)
        {
          dropped = i;
          break;
        }
      }
      if(dropped<0)
      {
        Error = L"no ear found — the contour is probably self-intersecting, "
                L"which this tessellator does not handle";
        return -1;
      }
      ring.RemAtOrder(dropped);
    }
  }

  if(guard<=0)
  {
    Error = L"ear clipping did not terminate";
    return -1;
  }
  return tris;
}

/****************************************************************************/

sInt wTess2D::End(sArray<sInt> &indices)
{
  Error = L"";
  if(Contours.GetCount()==0)
    return 0;

  // --- nesting depth, and so which contours are holes -----------------------

  // O(n^2) in the contour count, which for a glyph is at most a handful. The
  // bounding-box test short-circuits the common case.
  for(sInt i=0;i<Contours.GetCount();i++)
  {
    Contour &ci = Contours[i];
    const wTessVertex &p = Points[ci.First];

    for(sInt j=0;j<Contours.GetCount();j++)
    {
      if(i==j)
        continue;
      const Contour &cj = Contours[j];
      if(p.X<cj.MinX || p.X>cj.MaxX || p.Y<cj.MinY || p.Y>cj.MaxY)
        continue;
      if(PointInContour(Points,cj.First,cj.Count,p.X,p.Y))
        ci.Depth++;
    }
    ci.Hole = (ci.Depth & 1) != 0;
  }

  sInt total = 0;

  // --- one ring per solid contour, with its immediate holes bridged in -------

  for(sInt i=0;i<Contours.GetCount();i++)
  {
    const Contour &solid = Contours[i];
    if(solid.Hole)
      continue;

    // The ring, counter-clockwise. A negative signed area means clockwise, so
    // the traversal is reversed rather than the data.
    sArray<sInt> ring;
    if(solid.Area>=0)
    {
      for(sInt k=0;k<solid.Count;k++)
        ring.AddTail(solid.First+k);
    }
    else
    {
      for(sInt k=solid.Count-1;k>=0;k--)
        ring.AddTail(solid.First+k);
    }

    // Bridge each hole exactly one level deeper that lies inside this one. Two
    // levels deeper is a solid island within a hole and gets its own ring, which
    // is what makes a glyph like a nested 'O' inside an 'O' work.
    for(sInt j=0;j<Contours.GetCount();j++)
    {
      const Contour &hole = Contours[j];
      if(!hole.Hole || hole.Depth!=solid.Depth+1)
        continue;
      const wTessVertex &hp = Points[hole.First];
      if(!PointInContour(Points,solid.First,solid.Count,hp.X,hp.Y))
        continue;

      // Bridge at the hole's rightmost vertex, joined to the nearest ring
      // vertex to its right. The rightmost point of a hole is guaranteed
      // visible from outside along +x, which is what makes this choice safe
      // rather than merely convenient.
      sInt hstart = 0;
      for(sInt k=1;k<hole.Count;k++)
        if(Points[hole.First+k].X > Points[hole.First+hstart].X)
          hstart = k;

      const wTessVertex &h = Points[hole.First+hstart];

      sInt best = -1;
      sF32 bestd = 0;
      for(sInt k=0;k<ring.GetCount();k++)
      {
        const wTessVertex &r = Points[ring[k]];
        if(r.X < h.X)
          continue;
        const sF32 dx = r.X-h.X, dy = r.Y-h.Y;
        const sF32 d = dx*dx + dy*dy;
        if(best<0 || d<bestd)
        {
          best = k;
          bestd = d;
        }
      }
      if(best<0)
      {
        // Nothing to the right at all. Fall back to the nearest vertex in any
        // direction: less robust, but better than dropping the hole silently.
        for(sInt k=0;k<ring.GetCount();k++)
        {
          const wTessVertex &r = Points[ring[k]];
          const sF32 dx = r.X-h.X, dy = r.Y-h.Y;
          const sF32 d = dx*dx + dy*dy;
          if(best<0 || d<bestd)
          {
            best = k;
            bestd = d;
          }
        }
      }
      if(best<0)
        continue;

      // Splice: ...ring[best], hole (clockwise, so opposite the ring), back to
      // hole start, then ring[best] again and the rest of the ring. The two
      // duplicated vertices are the bridge's two coincident edges.
      sArray<sInt> merged;
      for(sInt k=0;k<=best;k++)
        merged.AddTail(ring[k]);

      if(hole.Area<0)
      {
        for(sInt k=0;k<hole.Count;k++)
          merged.AddTail(hole.First+((hstart+k)%hole.Count));
      }
      else
      {
        for(sInt k=0;k<hole.Count;k++)
          merged.AddTail(hole.First+((hstart-k+hole.Count*2)%hole.Count));
      }
      merged.AddTail(hole.First+hstart);

      for(sInt k=best;k<ring.GetCount();k++)
        merged.AddTail(ring[k]);

      ring.Clear();
      ring.Add(merged);
    }

    const sInt n = ClipRing(ring,indices);
    if(n<0)
      return -1;
    total += n;
  }

  return total;
}

/****************************************************************************/
