/****************************************************************************/
/***                                                                      ***/
/***   Stage 6.3 Suite A — one case per mesh operator                      ***/
/***                                                                      ***/
/****************************************************************************/
//
// Suite B (`wz4gen sweep`) drives 36 of the 45 operators with the original
// authors' parameter values, but can only check invariants: a demo graph's
// correct output is unknown. This is where VALUES are checked.
//
// The expectation for every case is stated as a derivation, in the Why column,
// and the numbers are chosen so the derivation is possible — small, and distinct
// per axis. Distinct per axis is not decoration: a generator that swapped two
// axes would produce identical counts from equal tesselation values, so equal
// values would make the case unable to fail.
//
// This is the assertion phase 4 could not have. A texture operator's output is
// only checkable by eye, so 4.3 had to review 90 images and 4.4 froze whatever
// they showed. A face count is derivable, and a wrong one is wrong rather than
// merely different.
//
// WHAT A CASE ASSERTS
//
//   the invariant battery       geo/mesh_check.cpp, shared with the sweep
//   face count, tri/quad split  from the derivation in Why
//   bounds                      where the generator or transform fixes them
//   closedness                  ONLY where the operator promises it
//
// Closedness is per-case on purpose. Most of these operators legitimately
// produce open or non-manifold meshes — DeleteFace, SplitAlongPlane, Splitter,
// Chunks — so it cannot be a global invariant. See geo/mesh_check.hpp.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "base/system.hpp"
#include "mesh_check.hpp"
#include "meta.hpp"
#include "wz4t.hpp"

/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)
  {
    sREGOPS(basic,0);
    sREGOPS(wz3_bitmap,0);
    sREGOPS(wz4_anim,0);
    sREGOPS(wz4_mesh,0);
    sREGOPS(animate,0);
    sREGOPS(material,0);
  }
}

/****************************************************************************/

static sInt Failures = 0;
static sInt Checks = 0;

static void Check(sBool cond,const sChar *what)
{
  Checks++;
  if(!cond)
  {
    sPrintF(L"    FAIL  %s\n",what);
    Failures++;
  }
}

/****************************************************************************/

// -1 in a count means "not asserted": a few operators produce a number that is
// genuinely not derivable by hand (Bevel's, for one), and writing down whatever
// it happens to be today would be locking a golden without review — the failure
// 06-phase-texture.md warns about. Those cases assert the battery, the bounds
// and the direction of change instead, and 6.3b locks their checksums after the
// output has been looked at.
#define NOC   (-1)

// Bounds sentinel. A case that does not fix a coordinate leaves it unchecked
// rather than guessing a tolerance.
#define NOB   (-1e30f)

enum wClosedExpect
{
  CL_ANY = -1,          // not asserted
  CL_OPEN = 0,          // must NOT be a closed consistently-wound surface
  CL_CLOSED = 1,        // must be
};

struct wExpect
{
  const sChar *Store;
  sInt Faces,Tris,Quads;
  sInt Closed;
  sF32 LoX,LoY,LoZ,HiX,HiY,HiZ;
  // Bounds tolerance for the axes named in LooseAxis. 0 means every axis uses
  // the tight default, which is enough to pin a derived coordinate.
  //
  // A wider tolerance is for operators whose displacement is BOUNDED but not
  // derivable — Randomize by its Amount, Noise by its Amplify, Displace by its.
  // Bracketing is a real assertion: it fails if the parameter is ignored and it
  // fails if it is applied at the wrong scale, because a loose axis must ALSO
  // have moved.
  //
  // Per-axis, because the two are not interchangeable. Displace moves z only, so
  // asserting x and y EXACTLY is the interesting half of that case; a
  // whole-struct tolerance would have had to give up one or the other.
  sF32 BoundEps;
  sInt LooseAxis;       // bit 0 = x, 1 = y, 2 = z

  sBool MustEvaluate;   // 0 = evaluation is expected to FAIL cleanly
  const sChar *Why;

  // Last, and 0 means "not asserted", so the majority of rows can leave it off
  // entirely. Deliberately optional: a vertex count is a property of how vertex
  // ATTRIBUTES are split and merged rather than of the topology, so for most
  // operators it is reproducible without being derivable. Where it IS derivable
  // it is the strongest check in the table — Facette's four-per-face and Dual's
  // one-per-input-face each pin the whole algorithm.
  //
  // 0 costing nothing as a sentinel: the two cases with genuinely zero vertices,
  // Text3D and Path3D, are already pinned by Faces == 0.
  sInt Verts;
};

struct wCaseFile
{
  const sChar *Path;
  const wExpect *Cases;
  sInt Count;
};

/****************************************************************************/
/***   the cases                                                          ***/
/****************************************************************************/

static const wExpect GenCases[] =
{
  { L"g_cube",   52,0,52, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Cube(2,3,4): 2*(2*3 + 2*4 + 3*4) = 52 quads; Scale 1 centres it on -0.5..0.5" },

  { L"g_grid",   15,0,15, CL_OPEN,   -0.5f,0.0f,-0.5f,  0.5f,0.0f,0.5f, 0,0,1,
    L"Grid(3,5): 3*5 = 15 quads, flat so zero extent on y, Size 1 -> -0.5..0.5" },

  { L"g_sphere", 24,12,12, CL_CLOSED, NOB,-0.5f,NOB,    NOB,0.5f,NOB, 0,0,1,
    L"Sphere(6,4): 6*4 = 24 faces, 2*6 = 12 pole tris, 6*(4-2) = 12 quads. "
    L"y spans the full diameter; x and z do NOT, because with 6 slices no "
    L"vertex lands on the axis — 0.5*cos(30) = 0.433" },

  { L"g_torus",  15,0,15, CL_CLOSED, NOB,-0.21650635f,NOB, NOB,0.21650635f,NOB, 0,0,1,
    L"Torus(5,3): 5*3 = 15 quads, closed at Arc 1. The tube half-height is "
    L"InnerRadius*cos(180/3) = 0.25*0.866 = 0.2165, which is the one bound "
    L"3 segments make exact" },

  { L"g_cylinder", 36,12,24, CL_CLOSED, NOB,-0.5f,NOB,   NOB,0.5f,NOB, 0,0,1,
    L"Cylinder(6,2,top=1): 6*2 = 12 side quads, plus two capped ends of "
    L"6 tris + 6 quads each = 12 tris + 12 quads. Height 1 centred -> y -0.5..0.5" },

  { L"g_disc",   7,7,0, CL_OPEN,     NOB,0.0f,NOB,      NOB,0.0f,NOB, 0,0,1,
    L"Disc(7): a 7-triangle fan, flat, so zero extent on y" },

  { L"g_text3d",  NOC,NOC,NOC, CL_ANY, NOB,0.0f,0.0f,   NOB,NOB,0.1f, 0,0,1,
    L"Text3D \"wz4\" on FreeType outlines, extruded 0.1. Face counts are NOT "
    L"asserted and the checksum is NOLOCK: the geometry comes from whichever "
    L"Arial the host ships. What IS exact is z 0..0.1 — the extrude parameter — "
    L"and y starting at 0, since none of w, z or 4 descends below the baseline. "
    L"Empty before 6.6c" },

  { L"g_text3d_holes", NOC,NOC,NOC, CL_ANY, NOB,NOB,0.0f, NOB,NOB,0.1f, 0,0,1,
    L"\"og 8\" — the demanding case, one character per hazard: `o` is a round "
    L"counter, so a HOLE bounded by curves, exercising conic flattening and "
    L"tess2d's bridging together; `g` descends below the baseline; `8` has TWO "
    L"counters, which is where classifying holes by nesting depth earns its "
    L"keep; and the space is an advance with no contours at all — if the layout "
    L"skipped the advance for empty glyphs, \"og 8\" would be as wide as \"og8\". "
    L"z is the only exact bound, for the same reason as above" },

  { L"g_path3d",  5,2,3, CL_CLOSED,  0.0f,-1.0f,0.0f,   1.0f,0.0f,0.1f, 0,0,1,
    L"Path3D on \"M 0 0 L 1 0 L 1 1 z\" extruded 0.1: a triangular prism, so "
    L"2 triangular caps + 3 quad walls = 5 faces, and the z extent IS the "
    L"extrude depth. y comes out -1..0 rather than 0..1 because SVG's y axis "
    L"points down. Empty before stage 6.6b replaced glu32 with geo/tess2d" },

  { L"g_path3d_hole", 24,16,8, CL_CLOSED, 0.0f,-4.0f,0.0f, 4.0f,0.0f,0.5f, 0,0,1,
    L"a 4x4 square with a 2x2 hole, extruded 0.5 — the case tess2d's hole "
    L"bridging exists for, driven through the real operator. Every number is "
    L"derived: tess2d gives 8 triangles for the ring (4 outer + 4 hole + 2 "
    L"bridge vertices, minus 2), so 8 per cap and 16 for both; the walls are 4 "
    L"quads outside plus 4 in the hole. 24 faces. "
    L"THE FACE COUNT IS WHAT CATCHES A FILLED HOLE, not the closedness check: "
    L"written first with `N` between the contours — which ends the POLYGON, not "
    L"the contour — this came out as two overlapping solid squares, and it still "
    L"tested CLOSED, because two closed shells pair their half-edges whatever "
    L"they overlap. "
    L"WAS 20,12,8 UNTIL PHASE 8, LOCKED ON A DEFECT. Finish2DExtrusionOp's "
    L"triangulation cleanup edge-flipped every cap triangle, because it expects "
    L"GLU's clockwise winding and tess2d emitted counter-clockwise; the result "
    L"overlapped the hole with a reversed copy of itself. The count 20 was then "
    L"rationalised as \"2 of the 8 are zero-area slivers\" — there are no "
    L"slivers, and the arithmetic in ops_gen.wz4t said 24 all along. A count "
    L"that has to be explained rather than derived is a warning" },

  { L"g_import_missing", NOC,NOC,NOC, CL_ANY, NOB,NOB,NOB, NOB,NOB,NOB, 0,0,0,
    L"Import of a file that does not exist must fail CLEANLY — a refusal, "
    L"not a crash and not a silently empty mesh" },
};

/****************************************************************************/

// Every transform preserves topology, so each case below asserts the input's
// face count unchanged. Cube(1,1,1) is 6 quads, Cube(2,2,2) is 24,
// Cube(1,8,1) is 2*(1*8 + 1*1 + 8*1) = 34, Cube(4,4,4) is 2*(16*3) = 96.

static const wExpect TransformCases[] =
{
  { L"t_transform", 6,0,6, CL_CLOSED, 0.0f,0.5f,1.0f, 2.0f,3.5f,5.0f, 0,0,1,
    L"Scale(2,3,4) takes -0.5..0.5 to -1..1, -1.5..1.5, -2..2; then Trans(1,2,3). "
    L"Asserting the composed result also pins the ORDER as scale-then-translate" },

  { L"t_transformex", 6,0,6, CL_CLOSED, 0.0f,0.5f,1.0f, 2.0f,3.5f,5.0f, 0,0,1,
    L"identical numbers to t_transform, and must give an identical answer. "
    L"TransformEx's Flags DEFAULT TO 0x33 = uv0 -> uv0, so left alone it moves "
    L"texture coordinates and the positions do not move at all — measured, the "
    L"default case comes out at -0.5..0.5. The case states pos -> pos" },

  { L"t_transformmatrix", 6,0,6, CL_CLOSED, -1.0f,-1.5f,-2.0f, 1.0f,1.5f,2.0f, 0,0,1,
    L"rows (2,0,0,0),(0,3,0,0),(0,0,4,0): the same scale as t_transform with no "
    L"translation, so -1..1, -1.5..1.5, -2..2" },

  { L"t_transformnonlinear", 96,0,96, CL_CLOSED, NOB,-0.5f,-0.5f, NOB,0.5f,0.5f, 0,0,1,
    L"Cube(4,4,4) is 96 quads. x2 = 0.5 adds a quadratic term on x only, so y "
    L"and z must be UNTOUCHED at -0.5..0.5 — that is the assertion. The x "
    L"extent comes out 0.5625, which is 0.5 + 0.5*0.5^2/2; the formula is not "
    L"stated in the operator, so x is left unasserted rather than guessed" },

  { L"t_center", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"a cube pushed to Trans(5,-7,11) and then Centered must land back exactly "
    L"on -0.5..0.5. The only case in the group whose correct output equals the "
    L"input to the step before it" },

  { L"t_mirror", 6,0,6, CL_CLOSED, -2.5f,-0.5f,-0.5f, -1.5f,0.5f,0.5f, 0,0,1,
    L"the input is a cube translated to x 1.5..2.5. Mirror REFLECTS IN PLACE "
    L"rather than adding a mirrored copy — measured: 6 faces out, not 12 — so "
    L"the result is x -2.5..-1.5 and the face count is unchanged" },

  { L"t_multiply", 18,0,18, CL_OPEN, -0.5f,-0.5f,-0.5f, 2.5f,0.5f,0.5f, 0,0,1,
    L"Count 3 with Trans(1,0,0) each step: 3*6 = 18 quads, unit cubes at "
    L"x 0, 1, 2, so the span is -0.5..2.5. NOT closed, and the reason is worth "
    L"knowing: the copies ABUT exactly, at x = 0.5 and 1.5, so each edge of a "
    L"shared face has four incident faces rather than two. Measured: 32 "
    L"unpaired half-edges and 32 same-direction duplicates, which is the two "
    L"seams' 8 edges seen from both cubes. Disjoint copies would test closed" },

  { L"t_multiplynew", 18,0,18, CL_OPEN, -0.5f,-0.5f,-0.5f, 2.5f,0.5f,0.5f, 0,0,1,
    L"the replacement operator, same numbers, and it must give the same answer "
    L"as t_multiply — which is the point of running both" },

  { L"t_bend", 34,0,34, CL_CLOSED, -0.5f,NOB,NOB, 0.5f,NOB,NOB, 0,0,1,
    L"Cube(1,8,1) is 34 quads. The bend axis is x, so x must be UNTOUCHED at "
    L"-0.5..0.5 while y and z move; their values follow from the bend radius "
    L"and are not stated in the operator, so they are left unasserted" },

  { L"t_deform", 34,0,34, CL_CLOSED, 0.0f,-0.5f,-0.5f, 1.0f,0.5f,0.5f, 0,0,1,
    L"two spline keys from (0,0,0) to (1,0,0) map the bar's length onto the x "
    L"axis over one unit, leaving the cross-section at +-0.5 on y and z. "
    L"WITHOUT array rows Deform is an identity — a case with none would be "
    L"unable to fail" },

  { L"t_normalize", 24,0,24, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Normalize rescales normals and must not move a vertex, so Cube(2,2,2)'s "
    L"24 quads and its exact -0.5..0.5 bounds both have to survive" },

  { L"t_randomize", 24,0,24, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.1f,7,1,
    L"Amount 0.1 bounds the displacement, so every bound must be within 0.1 of "
    L"+-0.5 AND must have moved. Bracketing catches both an ignored parameter "
    L"and one applied at the wrong scale" },

  { L"t_noise", 96,0,96, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.1f,7,1,
    L"Amplify 0.1 on each axis, same bracket as t_randomize, over Cube(4,4,4)" },

  { L"t_transformrange", 34,0,34, CL_CLOSED, -1.0f,-0.5f,-1.0f, 1.0f,0.5f,1.0f, 0,0,1,
    L"ScaleEnd(2,1,2) over an AxialRange covering the whole bar: the widest end "
    L"doubles x and z to +-1 and leaves y at +-0.5" },
};

/****************************************************************************/

// These CHANGE the counts, which is what makes them checkable: a subdivision
// that produced its input's count did nothing, and a triangulation that left
// quads behind did not run.

static const wExpect TopoCases[] =
{
  { L"p_subdivide", 24,0,24, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"one level over 6 quads: each becomes 4, so 24. A smoothing weight of 0 "
    L"keeps the corners, so the bounds must not move" },

  { L"p_triangulate", 12,12,0, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"every quad splits into two triangles: 6 quads -> 12 tris and ZERO quads. "
    L"The quad count is the half that matters — a partial run would leave some" },

  { L"p_invert", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"reversing the winding must leave every count and bound untouched, and the "
    L"surface must STILL pair up as closed. An inversion that broke pairing — "
    L"reversing some faces and not others — would show up here and nowhere else" },

  { L"p_extrude", 5,0,5, CL_OPEN, -0.5f,0.0f,-0.5f, 0.5f,0.25f,0.5f, 0,0,1,
    L"one open quad, all of it selected, so the rim is its four boundary edges: "
    L"1 cap + 4 sides = 5 quads. The rim stays at y = 0 and the cap moves to "
    L"y = Amount = 0.25, which is why the y bounds are 0..0.25 rather than "
    L"0.25..0.25. Gave 1 quad before stage 6.7 fixed the /4 adjacency decode "
    L"(patch 14)" },

  { L"p_extrude_steps", 9,0,9, CL_OPEN, -0.5f,0.0f,-0.5f, 0.5f,0.25f,0.5f, 0,0,1,
    L"the same rim at Steps = 2: 1 cap + 2*4 sides = 9 quads. Sides scale with "
    L"Steps and the cap does not, so this separates `the rim was found` from "
    L"`the rim was walked the right number of times` — a one-step case cannot "
    L"tell whether Steps is read at all. Amount is the TOTAL, so the bounds are "
    L"unchanged from p_extrude" },

  { L"p_extrude_closed", 10,0,10, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.75f,0.5f,0.5f, 0,0,1,
    L"the other rim path, and the one every Extrude in the bundled documents "
    L"uses: one face of a closed cube selected, so the rim is four INTERIOR "
    L"edges between a selected and an unselected face. 6 - 1 selected + 1 cap + "
    L"4 sides = 10 quads, and the +x face moves out to 0.5 + 0.25 = 0.75 while "
    L"nothing else moves. This path decoded correctly even with /4 — which is "
    L"why the defect survived a decade and why the fix changed nothing in the "
    L"corpus. Extruding outward from a closed surface leaves it closed" },

  { L"p_bevel", 26,8,18, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"a bevelled cube is exactly 6 shrunk faces + 12 edge quads + 8 corner "
    L"triangles = 26 faces, 18 quads and 8 tris. Every one of those three "
    L"numbers comes from the cube's own counts, which is why this case is worth "
    L"more than its face total. The face centres do not move, so the bounds hold" },

  { L"p_facette", 24,0,24, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Smoothness 0 gives every face its own vertices, so Cube(2,2,2)'s 24 quads "
    L"survive unchanged while the vertex count becomes 24*4 = 96. The vertex "
    L"count IS the assertion here — the face count alone cannot tell whether "
    L"the split happened", 96 },

  { L"p_crease", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Crease sets edge flags and must not touch geometry at all" },

  { L"p_uncrease", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"and neither must UnCrease" },

  { L"p_dual", 8,8,0, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"the dual of a cube is the OCTAHEDRON, and both halves of that are checked: "
    L"the cube's 8 corners become 8 triangular faces, and its 6 faces become 6 "
    L"vertices. Face centres sit at +-0.5, so the bounds are unchanged", 6 },

  { L"p_splitter", 36,0,36, CL_ANY, -0.6f,-0.6f,-0.6f, 0.6f,0.6f,0.6f, 0,0,1,
    L"6 faces become 36. The bounds are the derivable part: Depth 0.1 pushes "
    L"each split piece out by exactly that, so +-0.5 becomes +-0.6" },

  { L"p_splitalongplane", 10,0,10, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"the plane x = 0 misses the two faces perpendicular to x and cuts the other "
    L"four in half: 6 + 4 = 10 quads. Splitting a closed surface along a plane "
    L"through it leaves it closed, and the bounds cannot change" },

  { L"p_chunks", 24,0,24, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Chunks groups faces into physics chunks and must not alter the mesh: "
    L"Cube(2,2,2)'s 24 quads and its bounds both have to survive intact" },

  { L"p_randomizechunks", 24,0,24, CL_ANY, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.1f,7,1,
    L"Random 0.1 on each axis displaces whole chunks, so the counts hold and "
    L"every bound must be within 0.1 of +-0.5 AND must have moved" },

  { L"p_deleteface_none", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Selection `none` must delete NOTHING. This is the case that catches an "
    L"inverted selection flag, which `all` cannot: deleting everything looks "
    L"the same whether the flag is right or reversed" },

  { L"p_deleteface_all", 0,0,0, CL_ANY, NOB,NOB,NOB, NOB,NOB,NOB, 0,0,1,
    L"and the other direction: `all` must leave nothing at all" },

  { L"p_add", 12,0,12, CL_CLOSED, -0.5f,-0.5f,-0.5f, 2.5f,0.5f,0.5f, 0,0,1,
    L"two cubes 2 units apart: 12 quads spanning -0.5..2.5. A count of 6 would "
    L"mean the second input was never connected, which is the failure this case "
    L"exists for — Add is the only variadic operator and its inputs come from "
    L"page geometry. DISJOINT, unlike t_multiply's abutting copies, so this one "
    L"does test as closed" },
};

/****************************************************************************/

// The awkward seven. Select and SelectGrow change no geometry at all, so they
// are asserted THROUGH DeleteFace: if the selection was never set, nothing gets
// deleted and the face count says so.

static const wExpect AttrCases[] =
{
  { L"a_select_deleted", 0,0,0, CL_ANY, NOB,NOB,NOB, NOB,NOB,NOB, 0,0,1,
    L"Select `all` then DeleteFace `selected` must leave NOTHING. Select writes "
    L"only a per-face float, so this is the only way to observe it: 24 faces "
    L"surviving would mean the flags were never set" },

  { L"a_selectgrow_deleted", 0,0,0, CL_ANY, NOB,NOB,NOB, NOB,NOB,NOB, 0,0,1,
    L"growing an already-total selection must keep it total. This does not prove "
    L"growth — a comparative case belongs with 6.3b's goldens — it proves "
    L"SelectGrow does not DESTROY what it was handed, which is the half that "
    L"would break silently" },

  { L"a_extrudenormal", 6,0,6, CL_CLOSED,
    -0.5577350f,-0.5577350f,-0.5577350f, 0.5577350f,0.5577350f,0.5577350f, 0,0,1,
    L"every vertex moves along its own normal by Amount. A cube's averaged "
    L"corner normal is (+-1,+-1,+-1)/sqrt(3), so each axis gains "
    L"0.1/1.7320508 = 0.0577350 and the extent becomes +-0.5577350 exactly. "
    L"The one operator here whose displacement is fully derivable", 24 },

  { L"a_bakeanim", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"BakeAnim on a mesh with NO skeleton must be a clean no-op. It was a "
    L"SEGFAULT: Wz4Mesh::BakeAnim dereferenced a null Skeleton immediately "
    L"(wz4_mesh.cpp:1754), and every generated mesh has one. Guarded in patch 13 "
    L"— safe to fix, unlike the Extrude defect, because no working document can "
    L"depend on a crash" },

  { L"a_export", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Export writes a file AND passes its input through, so the mesh must come "
    L"out untouched. The file itself is mesh_obj's business, where the reader "
    L"serves as the oracle" },

  { L"a_displace", 96,0,96, CL_ANY, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.2f,4,1,
    L"Cube(4,4,4) is 96 quads, and Displace moves along z only — so x and y are "
    L"asserted EXACTLY at +-0.5 while z is bracketed by Amount 0.2 and required "
    L"to have moved. Measured z -0.4..0.6, which is 0.1 of the 0.2 available. "
    L"The per-axis tolerance exists for this case: a single whole-struct "
    L"tolerance would have had to give up either the exactness or the bracket" },

  { L"a_heal", 6,0,6, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"input 1 is the REFERENCE, not a second body to merge: Heal snaps input 0's "
    L"positions onto it and outputs input 0 alone. So a cube healed against a "
    L"cube offset by 0.001 with PosThreshold 0.01 comes out as ONE cube at "
    L"+-0.5 — 12 faces would mean the two had been added rather than welded" },
};

/****************************************************************************/

// Phase 7. The chain is Cube -> Deform(keep bones) -> AnimateBones -> BakeAnim,
// and the interesting assertions here are the THREE PAIRS that must agree or
// disagree. Counts and bounds alone would not catch any of them.

static const wExpect AnimCases[] =
{
  { L"an_rig", 34,0,34, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"Deform with \"keep bones\": the rig survives and the vertices stay in the "
    L"REST pose, because the bend now lives in the joint channels rather than in "
    L"the geometry. Without that flag Deform calls BakeAnim(0) itself and "
    L"BakeAnim RELEASES the skeleton, so the rig is destroyed the moment it is "
    L"built — which is why nothing in this port could animate before phase 7" },

  { L"an_animated", 34,0,34, CL_CLOSED, -0.5f,-0.5f,-0.5f, 0.5f,0.5f,0.5f, 0,0,1,
    L"after AnimateBones. PAIR 1: its checksum must EQUAL an_rig's — the operator "
    L"replaces channels and must not touch a single vertex. A checksum that moved "
    L"here would mean it had baked something by accident" },

  { L"an_baked_t0", 34,0,34, CL_CLOSED, NOB,NOB,-0.5f, NOB,NOB,0.5f, 0,0,1,
    L"baked at t = 0. The z extent IS asserted and the others are not, and that "
    L"is the derivation: the animation rotates about the z axis, so x and y move "
    L"and z cannot. A rotation leaking into z would be a quaternion or "
    L"matrix-convention error, which is exactly the class of bug that otherwise "
    L"produces plausible-looking output" },

  { L"an_baked_t1", 34,0,34, CL_CLOSED, NOB,NOB,-0.5f, NOB,NOB,0.5f, 0,0,1,
    L"baked at t = 0.5. PAIR 2: its checksum must DIFFER from an_baked_t0's. "
    L"That single inequality is the phase-7 gate — two times, two geometries — "
    L"and it was unreachable before this operator existed, because every channel "
    L"any registered operator could build was a Wz4ChannelConstant" },

  { L"an_rest_baked", 34,0,34, CL_CLOSED, 0.0f,-0.5f,-0.5f, 1.0f,0.5f,0.5f, 0,0,1,
    L"a kept rig with its channels left ALONE, baked at 0. PAIR 3 with an_ref "
    L"below, and the strongest assertion in the file" },

  { L"an_ref", 34,0,34, CL_CLOSED, 0.0f,-0.5f,-0.5f, 1.0f,0.5f,0.5f, 0,0,1,
    L"the same Deform with no flag, which bakes internally at 0. Its checksum "
    L"must equal an_rest_baked's EXACTLY: skinning a preserved rig has to "
    L"reproduce, bit for bit, the bake Deform does for itself. That is the "
    L"rest-pose identity, and it pins the whole BasePose x mata x weights chain "
    L"in one comparison — no locked constant required for it to mean something" },
};

static const wCaseFile CaseFiles[] =
{
  { L"ops_gen.wz4t",       GenCases,       sCOUNTOF(GenCases) },
  { L"ops_transform.wz4t", TransformCases, sCOUNTOF(TransformCases) },
  { L"ops_topo.wz4t",      TopoCases,      sCOUNTOF(TopoCases) },
  { L"ops_attr.wz4t",      AttrCases,      sCOUNTOF(AttrCases) },
  { L"ops_anim.wz4t",      AnimCases,      sCOUNTOF(AnimCases) },
};

/****************************************************************************/
/***   the locked checksums — stage 6.3b                                  ***/
/****************************************************************************/
//
// WHAT THESE ARE FOR, AND WHAT THEY ARE NOT
//
// In phase 4 the golden WAS the correctness statement: a texture operator's
// output can only be judged by eye, so 4.3 reviewed 90 images and 4.4 froze what
// they showed. Nothing else could check them.
//
// Here correctness is already carried by the derivations above — 483 checks whose
// expected values were computed by hand from each operator's own rule. So these
// checksums have a narrower and more honest job: they detect UNINTENDED CHANGE in
// everything the assertions do not reach.
//
// That residue is substantial, which is why they are worth having:
//
//   exact vertex positions       the assertions check bounds, not interiors
//   vertex ORDER                 a reordering leaves every count and bound intact
//   face winding and indices     ditto, as long as the mesh stays closed
//   sub-tolerance drift          bounds are compared at 1e-4; this is bit-exact
//
// The last one is A43 restated: a golden must cover the pipeline's precision, not
// the artefact's. It is also why the OBJ files in golden/ are not the only golden
// — Altona writes `%f` at five decimals, so an OBJ is blind to anything below
// 1e-5, exactly as phase 4's PNGs were blind to the low 8 bits of a 16-bit
// pipeline.
//
// The checksum is FNV-1a over every vertex position AND every face index
// (geo/mesh_check.cpp). Positions alone would not do: Invert, Triangulate and
// Dual all rewire topology without moving a vertex.
//
// TO RE-LOCK, DELIBERATELY:  mesh_ops <casedir> <metadir> -lock
// It prints this block, ready to paste. Read the diff before pasting it — a
// checksum that changed for a reason you cannot state is a regression, not a new
// baseline.

struct wLock
{
  const sChar *Store;
  sU64 Checksum;
};

// A case whose output is not reproducible across machines, and so must not be
// locked. Only the Text3D cases: the geometry comes from whichever Arial the host
// has, and two macOS versions do not ship the same outlines. Same decision phase
// 4.5 took for GenBitmap.Text, and for the same reason — a byte-exact golden on
// a system font is a false-failure generator.
//
// The structural assertions still apply: the battery, the face arity, and the z
// extent, which IS exact because it is the extrude parameter.
//
// -lock preserves this marker rather than overwriting it with today's value.
#define NOLOCK 0xffffffffffffffffULL

// WHAT THE REVIEW FOUND, which is why these are worth reading rather than just
// diffing. The block cross-checks itself in three places, and all three hold
// BIT-EXACTLY — a stronger statement than any of the tolerance-based assertions
// above could make:
//
//   0x9968b939a75dd34d appears SEVEN times, and it is a plain Cube(1,1,1)
//   (confirmed independently: `wz4gen sweep ops_topo.wz4t -v` reports it for all
//   14 Cube operators in the file). The seven are t_center, p_crease, p_uncrease,
//   p_deleteface_none, a_bakeanim, a_export and a_heal — every case asserted to
//   leave a unit cube alone, arriving there through seven unrelated code paths.
//   t_center is the notable one: translate by (5,-7,11) and centre again is exact,
//   not merely within tolerance.
//
//   t_transform == t_transformex. The whole point of stating pos->pos on
//   TransformEx was that it should then agree with Transform; it agrees to the bit.
//
//   t_multiply == t_multiplynew. The old and new operators agree exactly.
//
// Six zeros, all cases whose correct result is nothing: the two empty text
// generators, the failed import, and three delete-everything cases. A zero
// checksum carries no information, but it costs none either — emptiness is
// already pinned by Faces == 0.

static const wLock Locks[] =
{
  { L"g_cube",                0xef0ec24496d56bd5ULL },
  { L"g_grid",                0x4b687ed35d503be9ULL },
  { L"g_sphere",              0x2055f242a31bb6ffULL },
  { L"g_torus",               0x7763506e1284c36eULL },
  { L"g_cylinder",            0x347fa988ddd9c861ULL },
  { L"g_disc",                0xab89217d712098a6ULL },
  { L"g_text3d",              NOLOCK },
  { L"g_text3d_holes",        NOLOCK },
  // Both re-locked in phase 8, when the cap winding was corrected. g_path3d's
  // geometry is unchanged — only the winding, which the checksum covers and the
  // face count cannot. g_path3d_hole's geometry genuinely changed: it is an
  // annulus now rather than a square with a reversed square laid over it.
  { L"g_path3d",              0x58fc1c204f27b40eULL },
  { L"g_path3d_hole",         0x1be2df33c398a015ULL },
  { L"g_import_missing",      0x0000000000000000ULL },
  { L"t_transform",           0x64bc39338e5dd34dULL },
  { L"t_transformex",         0x64bc39338e5dd34dULL },
  { L"t_transformmatrix",     0x12c6b3b1895dd34dULL },
  { L"t_transformnonlinear",  0x2beedd2e683fa40dULL },
  { L"t_center",              0x9968b939a75dd34dULL },
  { L"t_mirror",              0x28cdee0c7c088d5dULL },
  { L"t_multiply",            0x936aefeaf816c34dULL },
  { L"t_multiplynew",         0x936aefeaf816c34dULL },
  { L"t_bend",                0xc7e4b153ad8a0139ULL },
  { L"t_deform",              0x71f2ce517a5cde7dULL },
  { L"t_normalize",           0x0001f83112ec02ddULL },
  { L"t_randomize",           0x7d16a451dffb043eULL },
  { L"t_noise",               0x68027f9c04eb7629ULL },
  { L"t_transformrange",      0x24a9d1a29b7cde7dULL },
  { L"p_subdivide",           0x5a87a543678362c2ULL },
  { L"p_triangulate",         0xfd351fd500edaca9ULL },
  { L"p_invert",              0x140598152a488d5dULL },
  { L"p_extrude",             0xaabfec4d46d8dadfULL },
  { L"p_extrude_steps",       0x3fee3f1f325ef90fULL },
  { L"p_extrude_closed",      0x5951193e4cfbdca7ULL },
  { L"p_bevel",               0xe6a6acb4924b4549ULL },
  { L"p_facette",             0x482dfc4acb3663e5ULL },
  { L"p_crease",              0x9968b939a75dd34dULL },
  { L"p_uncrease",            0x9968b939a75dd34dULL },
  { L"p_dual",                0x3d8d2c6953d3d071ULL },
  { L"p_splitter",            0x66b48ab6303fb985ULL },
  { L"p_splitalongplane",     0x7c3d82ebda9df83dULL },
  { L"p_chunks",              0x20328cf350f5b7f5ULL },
  { L"p_randomizechunks",     0xd0b21af7b7ba1a87ULL },
  { L"p_deleteface_none",     0x9968b939a75dd34dULL },
  { L"p_deleteface_all",      0x0000000000000000ULL },
  { L"p_add",                 0xd21b349de5966085ULL },
  { L"a_select_deleted",      0x0000000000000000ULL },
  { L"a_selectgrow_deleted",  0x0000000000000000ULL },
  { L"a_extrudenormal",       0x717ce8f07a503145ULL },
  { L"a_bakeanim",            0x9968b939a75dd34dULL },
  { L"a_export",              0x9968b939a75dd34dULL },
  { L"a_displace",            0x020b2c4e121a02f8ULL },
  { L"a_heal",                0x9968b939a75dd34dULL },

  // Phase 7. Three PAIRS, and the pairs carry more than the values do:
  //
  //   an_rig == an_animated          AnimateBones replaces channels and moves
  //                                  no vertex
  //   an_baked_t0 != an_baked_t1     THE PHASE GATE — two times, two geometries,
  //                                  unreachable before this operator existed
  //   an_rest_baked == an_ref        the rest-pose identity: skinning a preserved
  //                                  rig reproduces Deform's own internal bake
  //                                  bit for bit
  //
  // And a fourth agreement that fell out for free: 0x71f2ce517a5cde7d is also
  // t_deform's, from ops_transform.wz4t — the same bar deformed along the same
  // straight line, reached through a different case file with a different key
  // count. Corroboration nobody arranged.
  { L"an_rig",                0xd503c483805cde7dULL },
  { L"an_animated",           0xd503c483805cde7dULL },
  { L"an_baked_t0",           0x209ea678bf99b7e7ULL },
  { L"an_baked_t1",           0xce974691c1d02a52ULL },
  { L"an_rest_baked",         0x71f2ce517a5cde7dULL },
  { L"an_ref",                0x71f2ce517a5cde7dULL },
};

/****************************************************************************/

static void CheckBound(sF32 want,sF32 got,const sChar *which,sF32 eps)
{
  if(want==NOB)
    return;
  Checks++;
  // The default is 1e-4 rather than an ULP: these are derived from cos() of an
  // integer angle, and the derivations are written to five places.
  if(eps<=0.0f)
    eps = 1e-4f;
  if(sFAbs(want-got)>eps)
  {
    sPrintF(L"    FAIL  bound %s: want %f +-%f, got %f\n",which,want,eps,got);
    Failures++;
  }
}

// The other half of a bracketed bound: with a wide tolerance, a value that did
// not move at all also passes. Randomize and Noise must MOVE something, so the
// cases that widen the tolerance also require the bound not to be exact.
static void CheckMoved(sF32 from,sF32 got,const sChar *which)
{
  Checks++;
  if(sFAbs(from-got)<1e-6f)
  {
    sPrintF(L"    FAIL  bound %s is still exactly %f — the displacement was "
            L"not applied at all\n",which,from);
    Failures++;
  }
}

// Lock lines are printed as they are produced rather than accumulated. Two
// reasons, both found the hard way:
//
//   - a file-scope sTextBuffer allocates in its CONSTRUCTOR, which runs before
//     Altona registers its memory handlers, and sVERIFY(h) in sAllocMem_ fires
//     immediately (base/types.cpp:4672) — the A47 lifetime hazard from the other
//     end;
//   - sPrint of the accumulated 48-line buffer came out TRUNCATED mid-word, so
//     the block would have been pasted incomplete.
//
// Printing incrementally has neither problem and needs no buffer at all.
static sBool LockMode = 0;

static const wLock *FindLock(const sChar *store)
{
  for(sInt i=0;i<sCOUNTOF(Locks);i++)
    if(Locks[i].Store && sCmpString(Locks[i].Store,store)==0)
      return &Locks[i];
  return 0;
}

static void RunCase(const wExpect &e,wType *meshtype)
{
  wOp *op = Doc->FindStore(e.Store);
  if(!op)
  {
    sPrintF(L"    FAIL  no store called \"%s\"\n",e.Store);
    Failures++;
    return;
  }

  wObject *obj = Doc->CalcOp(op);

  if(!e.MustEvaluate)
  {
    // The point of these cases is that the failure is REPORTED. An operator that
    // quietly returned an empty mesh would look identical downstream to one that
    // worked on an empty input, which is how a missing asset becomes invisible.
    if(LockMode)
    {
      // A case that produces no mesh still gets an entry, so that "one lock per
      // case" holds as an assertion rather than needing an exception list.
      sString<64> name;
      name.PrintF(L"L%q,",e.Store);
      sPrintF(L"  { %-25s 0x0000000000000000ULL },\n",(const sChar *)name);
      if(obj)
        obj->Release();
      return;
    }

    Check(obj==0,L"evaluation fails, as this case requires");
    Check(op->CalcErrorString!=0,L"and says why");
    if(op->CalcErrorString)
      sPrintF(L"          reported: %s\n",op->CalcErrorString);
    if(obj)
      obj->Release();
    return;
  }

  if(!obj)
  {
    sPrintF(L"    FAIL  did not evaluate");
    if(op->CalcErrorString)
      sPrintF(L" (%s)",op->CalcErrorString);
    sPrint(L"\n");
    Failures++;
    return;
  }

  if(!obj->IsType(meshtype))
  {
    sPrintF(L"    FAIL  produced a %s, not a Wz4Mesh\n",
      obj->Type ? obj->Type->Symbol : L"?");
    Failures++;
    obj->Release();
    return;
  }

  Wz4Mesh *mesh = (Wz4Mesh *)obj;
  const wMeshFacts f = wMeshMeasure(mesh);

  // The battery first: a mesh that violates an invariant makes every count
  // below meaningless, and the report names which invariant broke.
  Checks++;
  if(f.Violations())
  {
    wMeshReport(e.Store,f);
    Failures++;
  }

  if(e.Faces!=NOC) Check(f.Faces==e.Faces,L"face count");
  if(e.Tris !=NOC) Check(f.Tris ==e.Tris, L"triangle count");
  if(e.Quads!=NOC) Check(f.Quads==e.Quads,L"quad count");
  if(e.Verts) Check(f.Verts==e.Verts,L"vertex count");

  if(e.Faces!=NOC && f.Faces!=e.Faces)
    sPrintF(L"          faces: want %d, got %d (%d tri, %d quad)\n",
      e.Faces,f.Faces,f.Tris,f.Quads);
  if(e.Verts && f.Verts!=e.Verts)
    sPrintF(L"          vertices: want %d, got %d\n",e.Verts,f.Verts);

  if(f.Verts>0)
  {
    const sF32 ex = (e.LooseAxis & 1) ? e.BoundEps : 0.0f;
    const sF32 ey = (e.LooseAxis & 2) ? e.BoundEps : 0.0f;
    const sF32 ez = (e.LooseAxis & 4) ? e.BoundEps : 0.0f;

    CheckBound(e.LoX,f.Lo.x,L"min x",ex);
    CheckBound(e.HiX,f.Hi.x,L"max x",ex);
    CheckBound(e.LoY,f.Lo.y,L"min y",ey);
    CheckBound(e.HiY,f.Hi.y,L"max y",ey);
    CheckBound(e.LoZ,f.Lo.z,L"min z",ez);
    CheckBound(e.HiZ,f.Hi.z,L"max z",ez);

    // A wide tolerance makes "nothing happened" pass, so a loose axis must also
    // have moved. Only the loose ones: the tight axes are asserted to be exact,
    // which for most of these operators means exactly unchanged.
    if(ex>0.0f)
    {
      if(e.LoX!=NOB) CheckMoved(e.LoX,f.Lo.x,L"min x");
      if(e.HiX!=NOB) CheckMoved(e.HiX,f.Hi.x,L"max x");
    }
    if(ey>0.0f)
    {
      if(e.LoY!=NOB) CheckMoved(e.LoY,f.Lo.y,L"min y");
      if(e.HiY!=NOB) CheckMoved(e.HiY,f.Hi.y,L"max y");
    }
    if(ez>0.0f)
    {
      if(e.LoZ!=NOB) CheckMoved(e.LoZ,f.Lo.z,L"min z");
      if(e.HiZ!=NOB) CheckMoved(e.HiZ,f.Hi.z,L"max z");
    }
  }

  if(e.Closed!=CL_ANY)
  {
    sInt open = 0,rev = 0;
    const sBool closed = wMeshIsClosed(mesh,&open,&rev);
    Checks++;
    if(closed!=(e.Closed==CL_CLOSED))
    {
      sPrintF(L"    FAIL  closedness: want %s, got %s (%d unpaired half-edge(s), "
              L"%d reversed)\n",
        e.Closed==CL_CLOSED ? L"closed" : L"open",
        closed ? L"closed" : L"open",open,rev);
      Failures++;
    }
  }

  // --- the locked checksum --------------------------------------------------

  if(LockMode)
  {
    // The comma goes on the NAME, then the pair is padded as one unit — a
    // `%-24q` alone silently emits a struct initialiser with no comma between
    // its members, which is a compile error 48 lines later.
    sString<64> name;
    name.PrintF(L"L%q,",e.Store);

    // A deliberately unlocked case stays unlocked. Regenerating it with today's
    // measurement would quietly turn a "do not lock this" decision into a lock,
    // which is the one thing a re-lock step must not do on its own.
    const wLock *prev = FindLock(e.Store);
    if(prev && prev->Checksum==NOLOCK)
    {
      sPrintF(L"  { %-25s NOLOCK },\n",(const sChar *)name);
      obj->Release();
      return;
    }

    sPrintF(L"  { %-25s 0x%08x%08xULL },\n",(const sChar *)name,
      sU32(f.Checksum>>32),sU32(f.Checksum));
  }
  else
  {
    const wLock *lock = FindLock(e.Store);

    // An unlocked case is a FAILURE, not a skip. Otherwise a case added later
    // silently has no baseline, and the suite reports full coverage while
    // covering one case less than it says — the same shape as A53.
    Checks++;
    if(!lock)
    {
      sPrintF(L"    FAIL  %s has no locked checksum (run with -lock)\n",e.Store);
      Failures++;
    }
    else if(lock->Checksum==NOLOCK)
    {
      sPrintF(L"          %s: not locked, output depends on the host's font\n",
        e.Store);
    }
    else if(lock->Checksum!=f.Checksum)
    {
      sPrintF(L"    FAIL  checksum: want %08x%08x, got %08x%08x\n",
        sU32(lock->Checksum>>32),sU32(lock->Checksum),
        sU32(f.Checksum>>32),sU32(f.Checksum));
      sPrint(L"          positions, vertex order, winding or face indices changed "
             L"without changing a count or a bound\n");
      Failures++;
    }
  }

  obj->Release();
}

/****************************************************************************/

void sMain()
{
  sPrint(L"mesh_ops: stage 6.3 Suite A\n\n");

  // Both mandatory. A53: an optional path that silently falls back is how a
  // whole suite comes to test the wrong thing while passing.
  const sChar *casedir = sGetShellParameter(0,0);
  const sChar *metadir = sGetShellParameter(0,1);
  if(!casedir || !metadir)
  {
    sPrint(L"usage: mesh_ops <case-directory> <meta-directory>\n");
    sSetErrorCode();
    return;
  }

  wMetaLibrary meta;
  if(!meta.LoadDirectory(metadir))
  {
    sPrintF(L"mesh_ops: no metadata in <%s>\n",metadir);
    sSetErrorCode();
    return;
  }

  // Deliberate, and separate from the run — locking has to be something someone
  // asked for. Phase 4 learned this as lock_goldens.cmake: a runner that
  // re-locks on mismatch is not a test, it is a rubber stamp.
  LockMode = sGetShellSwitch(L"lock");
  if(LockMode)
    sPrint(L"static const wLock Locks[] =\n{\n");

  sInt cases = 0;

  // "Every operator has a case" is the gate, and counting the rows of the table
  // by hand is exactly the kind of proxy this project keeps getting caught by
  // (A39, A53). So the classes actually exercised are collected as the cases run
  // and compared against the live registry at the end.
  //
  // Every operator in a case FILE counts, not only the one a store names: Select
  // is exercised as p_extrude_sel and a_select's helper, and DeleteFace is what
  // makes Select observable at all. An operator that appears nowhere shows up as
  // a named gap rather than as a wrong total.
  sArray<sPoolString> covered;

  for(sInt i=0;i<sCOUNTOF(CaseFiles);i++)
  {
    const wCaseFile &cf = CaseFiles[i];

    sString<1024> path;
    sSPrintF(path,L"%s/%s",casedir,cf.Path);
    if(!LockMode)                 // lock output has to be paste-ready
      sPrintF(L"%s\n",cf.Path);

    // A fresh document per file. wReadWz4t fills the global Doc, and reusing one
    // across files would leave the previous file's pages in place — which would
    // still pass, since stores are looked up by name.
    Doc = new wDocument;

    if(!wReadWz4t(path,meta,0))
    {
      sPrintF(L"    FAIL  could not read <%s>\n",(const sChar *)path);
      Failures++;
      delete Doc;
      Doc = 0;
      continue;
    }
    Doc->Connect();

    wType *meshtype = Doc->FindType(L"Wz4Mesh");
    if(!meshtype)
    {
      sPrint(L"    FAIL  no Wz4Mesh type registered\n");
      Failures++;
      delete Doc;
      Doc = 0;
      continue;
    }

    // Note every mesh operator this file contains, before the cases run — so a
    // case that fails still counts its operator as covered, and the coverage
    // report stays independent of the pass/fail one.
    wPage *page;
    sFORALL(Doc->Pages,page)
    {
      wOp *op;
      sFORALL(page->Ops,op)
      {
        if(!op->Class || op->Class->OutputType!=meshtype)
          continue;
        if(!sFind(covered,op->Class->Name))
          covered.AddTail(op->Class->Name);
      }
    }

    for(sInt k=0;k<cf.Count;k++)
    {
      const wExpect &e = cf.Cases[k];
      const sInt before = Failures;
      RunCase(e,meshtype);
      cases++;

      if(Failures!=before)
      {
        // The derivation is printed only on failure, and that is the whole
        // point of carrying it: a bare "want 52, got 48" is not actionable, and
        // the same line that explains the number also says whether the number
        // or the expectation is what is wrong.
        sPrintF(L"          %s: %s\n",e.Store,e.Why);
      }
    }

    delete Doc;
    Doc = 0;
  }

  if(LockMode)
  {
    sPrint(L"};\n");
    return;                   // nothing is asserted in lock mode
  }

  // --- coverage, against the live registry ----------------------------------

  {
    Doc = new wDocument;
    wType *meshtype = Doc->FindType(L"Wz4Mesh");
    sInt registered = 0,missing = 0;

    for(sInt i=0;i<Doc->Classes.GetCount();i++)
    {
      wClass *cl = Doc->Classes[i];
      if(cl->OutputType!=meshtype)
        continue;
      registered++;
      if(!sFind(covered,cl->Name))
      {
        sPrintF(L"    FAIL  no case exercises %s\n",cl->Name);
        missing++;
        Failures++;
      }
    }

    sPrintF(L"\n%d of %d registered mesh operator(s) exercised\n",
      registered-missing,registered);

    // And the other direction: a case naming an operator the registry does not
    // have would mean the case file drifted from the library.
    Checks++;
    if(covered.GetCount()>registered)
    {
      sPrintF(L"    FAIL  %d case operator(s) are not in the registry\n",
        covered.GetCount()-registered);
      Failures++;
    }

    delete Doc;
    Doc = 0;
  }

  // And the other direction: a lock with no case would mean the block drifted
  // from the table. Counted rather than named, because the block is generated.
  {
    sInt live = 0;
    for(sInt i=0;i<sCOUNTOF(Locks);i++)
      if(Locks[i].Store)
        live++;
    Checks++;
    if(live!=cases)
    {
      sPrintF(L"    FAIL  %d locked checksum(s) for %d case(s)\n",live,cases);
      Failures++;
    }
  }

  sPrintF(L"\n%d case(s), %d check(s), %d failure(s)\n",cases,Checks,Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
