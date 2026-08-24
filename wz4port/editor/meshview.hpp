/****************************************************************************/
/***                                                                      ***/
/***   meshview — a 3D viewer for Wz4Mesh, stage 6.4                       ***/
/***                                                                      ***/
/****************************************************************************/
//
// A viewer, not an engine. It draws a Wz4Mesh with one shader, one light and an
// orbit camera, and it deliberately knows nothing about Werkkzeug materials,
// render passes, instancing or the sequencer — all of which are out of scope per
// CLAUDE.md and are where the remaining hard platform blockers live.
//
// It renders into its own framebuffer and hands the texture to ImGui. Drawing
// straight into the ImGui pass would mean saving and restoring ImGui's GL state
// around every mesh; an FBO isolates the two completely and resizes with the
// pane.
//
// WHAT IT DOES NOT DO, ON PURPOSE
//
//   textures, normal maps, anything from a material
//   shadows, reflections, post-processing
//   skeletal animation — a mesh with a skeleton draws in its rest pose
//   picking, gizmos, handles
//
// Phase 7 is where animation arrives; a viewer that grew half a material system
// first would be in the way of it.

#ifndef FILE_WZ4PORT_EDITOR_MESHVIEW_HPP
#define FILE_WZ4PORT_EDITOR_MESHVIEW_HPP

#include "wz4lib/doc_core.hpp"
#include "wz4frlib/wz4_mesh.hpp"

/****************************************************************************/

struct wMeshView
{
  // --- what the user can change ---------------------------------------------

  sF32 Yaw;                 // radians, orbit about the mesh's centre
  sF32 Pitch;
  sF32 Distance;            // in units of the mesh's bounding radius

  // Plain bool, not sBool: ImGui::Checkbox takes a bool * and sBool is an int.
  // wPreview::Tile is declared the same way for the same reason.
  bool Wireframe;
  bool ShowGrid;
  bool ShowBBox;

  // --- what was last uploaded, for reporting and for the gate ---------------

  sInt Verts;               // vertices in the GL buffer
  sInt Tris;                // triangles, AFTER quads are split
  sInt Quads;               // quads in the source mesh, before splitting
  sVector31 Lo,Hi;          // the source mesh's bounds
  sBool Empty;              // an evaluated mesh with nothing in it is not a fault

  wMeshView();
  ~wMeshView();

  // Builds the GL buffers. A quad becomes two triangles here and nowhere else —
  // the mesh keeps its quads, which is what distinguishes this from the
  // Triangulate operator. Safe to call with 0 to release everything.
  void Upload(Wz4Mesh *mesh);

  // Renders at the given pixel size and returns the colour texture, or 0 if
  // there is nothing to show. Size is in PIXELS, not points: on a retina display
  // the two differ by the framebuffer scale and a points-sized target would be
  // visibly soft.
  sU32 Draw(sInt w,sInt h);

  // Frames the whole mesh. Called by Upload, and by the editor's reset control.
  void Fit();

  // One line, for the status bar and for the screenshot runner to assert. The
  // report is the gate: a screenshot alone cannot tell an empty pane from a
  // working one (A39, and editor_shot.cmake says so in as many words).
  void Describe(const sStringDesc &out) const;

  // The editor pane: evaluates the operator, re-uploads only when it changed,
  // draws the toolbar, handles the mouse, and shows the framebuffer. Signature
  // deliberately identical to wPreview::Draw so the two are interchangeable at
  // the call site — which is what makes routing by result type a two-line
  // change rather than a restructure.
  void DrawPane(wOp *op,sInt revision);

private:
  wOp *ShownOp;
  sInt ShownRevision;

  sU32 Program;
  sU32 LineProgram;
  sU32 Vao,Vbo,Ibo;
  sU32 LineVao,LineVbo;
  sInt LineVerts;
  sU32 Fbo,ColorTex,DepthBuf;
  sInt FboW,FboH;

  sBool EnsureShaders();
  sBool EnsureFbo(sInt w,sInt h);
  void BuildLines();        // grid and bounding box, as one line buffer
  void Release();
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_MESHVIEW_HPP
