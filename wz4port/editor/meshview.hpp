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
//   picking, gizmos, handles
//
// Skeletal animation WAS on that list until phase 7, which added CPU skinning per
// frame, a scrubber and a skeleton overlay. A viewer that had grown half a
// material system first would have been in the way of it.

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
  bool ShowBones;           // stage 7.5; like the scrubber, only offered on a rig
  bool ShowTexture;         // stage 9.4; only offered when a material has one

  // --- the timeline, stage 7.4 ---------------------------------------------
  //
  // Only meaningful when the shown mesh carries a skeleton, which almost none
  // do: every generator produces an unrigged mesh and Deform destroys the rig it
  // builds unless told to keep it. So the scrubber appears only when there is
  // something to scrub.
  sF32 Time;                // 0..1, the range every channel here is built for
  bool Playing;
  bool Loop;
  sF32 Fps;                 // playback rate; the pose is continuous, not stepped

  // --- what was last uploaded, for reporting and for the gate ---------------

  sInt Verts;               // vertices in the GL buffer
  sInt Tris;                // triangles, AFTER quads are split
  sInt Quads;               // quads in the source mesh, before splitting
  sVector31 Lo,Hi;          // the source mesh's bounds, at the REST pose
  sBool Empty;              // an evaluated mesh with nothing in it is not a fault
  sInt Joints;              // 0 when the mesh has no skeleton — the usual case
  sInt Bones;               // joints WITH a parent; 0 on every rig this build can
                            // make, since only the wz3 importer sets Parent

  wMeshView();
  ~wMeshView();

  // Builds the GL buffers. A quad becomes two triangles here and nowhere else —
  // the mesh keeps its quads, which is what distinguishes this from the
  // Triangulate operator. Safe to call with 0 to release everything.
  //
  // Does NOT frame the view: Fit() is separate, because an animated mesh
  // re-uploads its vertices every frame and framing on every upload would reset
  // the camera sixty times a second. Callers frame on operator CHANGE instead.
  void Upload(Wz4Mesh *mesh);

  // Re-skins at the current Time and re-uploads the vertex buffer. A no-op when
  // the mesh has no skeleton or the time has not moved. Called per frame.
  void UpdatePose();

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

  // Borrowed, never owned — the document's cache owns the evaluated object, and
  // releasing it broke the next frame in stage 6.4 (A58). Refreshed whenever the
  // operator or revision changes, which is the same moment the cache could have
  // replaced it.
  Wz4Mesh *Source;
  sF32 PosedTime;           // the Time the vertex buffer currently reflects
  sBool Posed;              // has a pose ever been uploaded?

  sU32 Program;
  sU32 LineProgram;
  sU32 Vao,Vbo,Ibo;
  sU32 LineVao,LineVbo;
  sInt LineVerts;

  // The skeleton is a THIRD line section but gets its own buffer, because it is
  // the only one that moves: the grid and the box are built once per mesh and
  // uploaded GL_STATIC_DRAW, while the bones are rebuilt at every pose. Appending
  // them to LineVbo would mean re-uploading the static geometry sixty times a
  // second, and would break the offset arithmetic in Draw, which locates the two
  // existing sections by counting back 24 vertices from the end.
  sU32 BoneVao,BoneVbo;
  sInt BoneVerts;

  // The cluster's texture, uploaded once per operator change rather than per
  // frame. Tex is the GL name; TexSource is the bitmap it came from, so the
  // upload can be skipped when nothing changed — and cleared when the mesh has
  // no material, so a previous operator's texture cannot linger on this one.
  sU32 Tex;
  const void *TexSource;
  sU32 Fbo,ColorTex,DepthBuf;
  sInt FboW,FboH;

  sBool EnsureShaders();
  sBool EnsureFbo(sInt w,sInt h);
  void BuildLines();        // grid and bounding box, as one line buffer
  void BuildBones();        // the posed skeleton; rebuilt with every pose
  void UploadTexture(Wz4Mesh *mesh);   // cluster 0's base colour map, if any
  void RefreshVertices();   // (re)builds the interleaved VBO from Source
  void Release();

  // Scratch, reused across frames rather than reallocated per frame. Members of
  // a heap-allocated object, so no Altona container ever sits at file scope
  // (A59).
  sArray<sMatrix34> BoneMat;
  sArray<sMatrix34> BaseMat;
  sArray<sF32> VertexScratch;
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_MESHVIEW_HPP
