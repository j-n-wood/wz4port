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
// So this supplies the smallest surface that compiles, and nothing more.
//
// WHAT THIS DELIBERATELY DOES NOT DO
//
// It does not implement Serialize. Wz4Mesh::Serialize is unreachable in this
// build and arguably in the whole dump — nothing calls it, wObject declares no
// virtual Serialize, and .wz4 documents store operators rather than evaluated
// meshes. A faithful stub would also be impractical: the real
// SimpleMtrl::Serialize_ does s.OnceRef() on three Texture2D handles
// (wz4_mtrl2.cpp:779), which would drag the texture object type and the render
// library in behind it.
//
// Leaving Serialize alone means the inherited Wz4Mtrl::Serialize applies, and
// that already does exactly the right thing: sFatal("no serialize for this
// material type yet"). If the dead path ever comes alive it stops loudly instead
// of silently misreading a stream. A stub that "worked" by reading nothing would
// desync everything after it.
//
// See wz4port/patches/10-mesh-headless.md and docs/08-phase-geometry.md.

#ifndef FILE_WZ4PORT_MTRL_HEADLESS_HPP
#define FILE_WZ4PORT_MTRL_HEADLESS_HPP

#include "base/types.hpp"
#include "base/graphics.hpp"
#include "wz4lib/doc_core.hpp"

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

  // Serialize is NOT overridden. See the note at the top of this file.

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
  SimpleMtrl() { Flags = 0; Blend = 0; Extras = 0; }

  void SetMtrl(sInt flags=0,sU32 blend=0,sInt extras=0)
  {
    Flags = flags;
    Blend = blend;
    Extras = extras;
  }

  // Texture assignment is accepted and dropped: the parameter type is the render
  // library's texture object, which this build does not have. Taking void * lets
  // the one call site (Wz4Mesh::ConvertFrom) compile without this header needing
  // to know what a Texture2D is.
  void SetTex(sInt stage,void *tex,sInt tflags=0) {}

  sInt Flags;
  sU32 Blend;
  sInt Extras;
};

/****************************************************************************/

#endif  // FILE_WZ4PORT_MTRL_HEADLESS_HPP
