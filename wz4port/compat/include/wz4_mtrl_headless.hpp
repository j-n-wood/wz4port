/****************************************************************************/
/***                                                                      ***/
/***   Materials, headless — wz4port                                      ***/
/***                                                                      ***/
/****************************************************************************/
//
// The mesh library names two material classes, and neither can come from
// upstream in this build:
//
//   wz4frlib/wz4_mtrl2.hpp includes wz4frlib/wz4_mtrl2_shader.hpp, which DOES
//   NOT EXIST IN THE TREE — it is generated from wz4_mtrl2_shader.asc by the asc
//   shader compiler, which this port does not build and which CLAUDE.md lists as
//   out of scope. It also reaches wz4lib/doc.hpp, and so the gui.
//
// Materials themselves are out of scope: nothing here renders. But the mesh
// library still has to COMPILE, and it names materials in three places outside
// the render section it can be split at — cluster construction and destruction,
// the (dead, see below) object serialiser, and the wz3 ChaosMesh conversion.
//
// So this began as the smallest surface that compiles, and nothing more.
//
// PHASE 9 CHANGED THAT, and the file's character with it. It is no longer only a
// stand-in: `SimpleMtrl` now carries a real texture — a `BitmapBase *` per stage,
// refcounted — because that is the one thing a material has to do for geometry to
// be worth exporting. Nothing here renders it; the glTF writer reads it
// (geo/gltf_write.cpp) and the 3D preview may later sample it.
//
// The divergence this creates is worth stating plainly. Headless, `Wz4Mtrl` is
// THIS class; in an unguarded build it is upstream's, and the two now store
// different things rather than merely having the same shape. That is the same
// trade patch 10 took and the same `WZ4PORT_HEADLESS_MTRL` guard covers it, but
// it is wider than it was. Anything reviving the real material system has to
// reconcile the two, and `SetTex` versus `SetBitmap` below is where the seam is.
//
// WHAT THIS DELIBERATELY DOES NOT DO
//
// It does not implement a WORKING Serialize. Wz4Mesh::Serialize is unreachable in
// this build and arguably in the whole dump — nothing calls it, wObject declares
// no virtual Serialize, and .wz4 documents store operators rather than evaluated
// meshes. A faithful stub would also be impractical: the real
// SimpleMtrl::Serialize_ does s.OnceRef() on three Texture2D handles
// (wz4_mtrl2.cpp:779), which would drag the texture object type and the render
// library in behind it.
//
// The two Serialize overloads therefore carry upstream's own base-class bodies,
// verbatim: sFatal("no serialize for this material type yet"). Declaring them is
// not optional — Wz4Mesh::Serialize's cluster loop (wz4_mesh.cpp:486-507) streams
// c.Mtrl through sReader/sWriter, whose templates need the member to exist even
// on a path never taken — and SimpleMtrl inherits them rather than overriding, so
// the dead path stops loudly instead of silently misreading a stream. A stub that
// "worked" by reading nothing would desync everything after it.
//
// See wz4port/patches/10-mesh-headless.md and docs/08-phase-geometry.md.

#ifndef FILE_WZ4PORT_MTRL_HEADLESS_HPP
#define FILE_WZ4PORT_MTRL_HEADLESS_HPP

#include "base/types.hpp"
#include "base/graphics.hpp"
#include "base/serialize.hpp"
#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic.hpp"       // BitmapBase — the texture a material holds here

/****************************************************************************/

// Declared verbatim from wz4frlib/wz4_mtrl2.hpp so that a mesh cluster's
// `Wz4Mtrl *Mtrl` means the same thing either way — same base, same virtuals,
// same signatures. The pure virtuals become trivial bodies, because a concrete
// material has to be constructible here and there is nothing for it to do.
class Wz4Mtrl : public wObject
{
public:
  Wz4Mtrl() { ShellExtrude = 0.0f; }

  virtual void BeforeFrame(sInt lightenv,sInt boxcount=0,const sAABBoxC *boxes=0,
    sInt matcount=0,const sMatrix34CM *mats=0) {}
  virtual void Prepare() {}
  virtual sVertexFormatHandle *GetFormatHandle(sInt flags) { return 0; }
  virtual void Set(sInt flags,sInt index,const sMatrix34CM *mat,
    sInt SkinMatCount,const sMatrix34CM *SkinMats,sInt *SkinMatMap) {}
  virtual sBool SkipPhase(sInt flags,sInt lightenv) { return 1; }

  // Verbatim from wz4_mtrl2.hpp:60-61. See the note at the top of this file for
  // why these stay fatal rather than becoming a stub that reads nothing.
  virtual void Serialize(sReader &stream) { sFatal(L"no serialize for this material type yet"); }
  virtual void Serialize(sWriter &stream) { sFatal(L"no serialize for this material type yet"); }

  sString<64> Name;
  sF32 ShellExtrude;
};

/****************************************************************************/

// The concrete material the mesh library constructs by name. It records the
// flags it is given rather than discarding them, so a mesh still carries its
// material ASSIGNMENT through a copy or a merge — which is what the geometry
// operators care about — while carrying no shader, texture or vertex format.
class SimpleMtrl : public Wz4Mtrl
{
public:
  SimpleMtrl()
  {
    Flags = 0; Blend = 0; Extras = 0;
    for(sInt i=0;i<3;i++) Tex[i] = 0;
    Colour = 0xffcccccc;
    Wrap = 1;
  }

  ~SimpleMtrl()
  {
    for(sInt i=0;i<3;i++) sRelease(Tex[i]);
  }

  void SetMtrl(sInt flags=0,sU32 blend=0,sInt extras=0)
  {
    Flags = flags;
    Blend = blend;
    Extras = extras;
  }

  // Upstream's texture entry point, still a no-op. Its parameter is the RENDER
  // library's Texture2D — a GPU object this build has no way to make — and the
  // two call sites (Wz4Mesh::ConvertFrom and the XSI loader) are both compiled
  // out. It stays for the class's upstream shape, exactly as before.
  void SetTex(sInt stage,void *tex,sInt tflags=0) {}

  // OURS, and deliberately a different function rather than a reinterpretation
  // of SetTex above. The two hold genuinely different things: upstream's Tex[3]
  // are uploaded GPU textures converted FROM a bitmap, while these are the
  // source bitmaps themselves — which is what a generator produces and what a
  // glTF image needs. Overloading one name onto both would make a later phase
  // that revives the renderer have to untangle which was meant.
  //
  // Refcounted: a material outlives the operator that built it, and a bitmap can
  // be shared by several materials.
  void SetBitmap(sInt stage,BitmapBase *bmp)
  {
    if(stage<0 || stage>=3) return;
    if(bmp) bmp->AddRef();
    sRelease(Tex[stage]);
    Tex[stage] = bmp;
  }

  BitmapBase *GetBitmap(sInt stage) const
  {
    return (stage>=0 && stage<3) ? Tex[stage] : 0;
  }

  sInt Flags;
  sU32 Blend;
  sInt Extras;

  // Phase 9. Stage 0 is the diffuse / base colour map; 1 and 2 are reserved so
  // the array matches upstream's Tex[3] and a later phase has somewhere to put
  // normal and specular maps without changing the shape again.
  BitmapBase *Tex[3];
  sU32 Colour;              // base colour, modulating the texture; 0xAARRGGBB
  sInt Wrap;                // 1 = repeat, 0 = clamp. See the note in gltf_write.
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_MTRL_HEADLESS_HPP
