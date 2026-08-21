/****************************************************************************/
/***                                                                      ***/
/***   Wz4Mesh::LoadXSI, headless — wz4port                               ***/
/***                                                                      ***/
/****************************************************************************/
//
// The mesh library's Import operator can read three formats, and two of them
// port cleanly: wz4_mesh_obj.cpp and wz4_mesh_lwo.cpp include wz4_mtrl2_ops.hpp
// but use nothing from it, so a guard on the include is enough (patch 11).
//
// wz4_mesh_xsi.cpp is different in kind. Across 2,142 lines it BUILDS materials
// rather than mentioning them: it constructs SimpleMtrl objects, reads
// mtrl->Tex[i], creates Texture2D objects, and passes sMTRL_* render state. That
// is the render library, not an incidental include, so the file needs materials
// to exist for real — which is out of scope here and blocked on the asc shader
// compiler either way.
//
// So this supplies the one symbol the Import operator needs to link, and the
// operator reports the failure through the channel it already uses for a
// missing or unreadable file. The alternative — guarding the XSI branch out of
// the .ops — would need a second upstream patch to say the same thing less
// clearly, and would leave the format silently unmentioned rather than refused.
//
// See wz4port/patches/11-mesh-ops-headless.md and docs/08-phase-geometry.md.

#include "wz4frlib/wz4_mesh.hpp"

/****************************************************************************/

sBool Wz4Mesh::LoadXSI(const sChar *file,sBool forceanim,sBool forcergb)
{
  sPrintF(L"wz4port: XSI import is not available in this build (%s)\n",file);
  return sFALSE;
}

/****************************************************************************/
