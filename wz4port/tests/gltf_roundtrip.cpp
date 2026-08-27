/****************************************************************************/
/***                                                                      ***/
/***   gltf_roundtrip — the phase 8 gate                                   ***/
/***                                                                      ***/
/****************************************************************************/
//
// THE ORACLE PROBLEM, AND HOW IT IS ANSWERED
//
// tests/mesh_obj.cpp is a real test of SaveOBJ rather than a self-consistent one
// because upstream's LoadOBJ shares no code with it: a writer checked by its own
// reader agrees with itself no matter how wrong both are. There is no LoadGLTF in
// this tree, so that oracle does not come free.
//
// It is reconstructed instead. The writer emits through wz4t/json_write.hpp; this
// test reads back through wz4t/json.hpp — a completely separate parser, written
// for the metadata runtime long before glTF was considered. Nothing below knows
// what the writer intended; it knows only what the glTF specification requires.
// That is the whole reason phase 8 declined to vendor a JSON library: one library
// used for both directions would have collapsed this back into self-consistency.
//
// WHAT A SYMMETRIC MESH CANNOT PROVE
//
// The conversion from Wz4's left-handed space to glTF's right-handed one is a
// mirror on z plus a winding reversal. A cube is symmetric in z, so if the mirror
// were omitted ENTIRELY — no negation, no reversal — every check on a cube would
// still pass and the file would open mirrored in every viewer. So the geometry
// case below is deliberately translated along z first, and the exported bounds
// are required to be the negated, swapped source bounds. That assertion is
// vacuous on a symmetric mesh and decisive on this one.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "wz4frlib/wz3_bitmap_ops.hpp"
#include "wz4frlib/wz4_anim_ops.hpp"
#include "wz4frlib/wz4_mesh_ops.hpp"
#include "wz4frlib/wz4_mesh.hpp"
#include "base/system.hpp"
#include "docedit.hpp"

#include "json.hpp"           // wJsonDoc — the READER, and not the writer's code
#include "gltf_write.hpp"

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

static void Check(sBool cond,const sChar *what)
{
  if(cond)
    sPrintF(L"  ok    %s\n",what);
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

/****************************************************************************/
/***   the checker — glTF in, geometry out, no knowledge of the writer     ***/
/****************************************************************************/

struct wGltfMesh
{
  sArray<sF32> Pos;         // 3 per vertex, as exported
  sArray<sF32> Nrm;         // 3 per vertex
  sArray<sU32> Idx;         // 3 per triangle, all primitives concatenated
  sInt Verts;
  sInt Prims;
  sF32 Min[3],Max[3];       // as DECLARED in the file

  wGltfMesh() { Verts = 0; Prims = 0; }
};

static sF32 GetF32(const sU8 *b,sInt off)
{
  sF32 v; sCopyMem(&v,b+off,4); return v;
}

static sU32 GetU32(const sU8 *b,sInt off)
{
  sU32 v; sCopyMem(&v,b+off,4); return v;
}

static sInt ComponentSize(sInt ct)
{
  switch(ct)
  {
  case 5120: case 5121: return 1;     // BYTE, UNSIGNED_BYTE
  case 5122: case 5123: return 2;     // SHORT, UNSIGNED_SHORT
  case 5125: case 5126: return 4;     // UNSIGNED_INT, FLOAT
  default: return 0;
  }
}

static sInt TypeCount(const sChar *t)
{
  if(sCmpString(t,L"SCALAR")==0) return 1;
  if(sCmpString(t,L"VEC2")==0)   return 2;
  if(sCmpString(t,L"VEC3")==0)   return 3;
  if(sCmpString(t,L"VEC4")==0)   return 4;
  if(sCmpString(t,L"MAT4")==0)   return 16;
  return 0;
}

static sBool Fault(sBool quiet,const sChar *what)
{
  if(!quiet)
    sPrintF(L"    reject: %s\n",what);
  return 0;
}

// Both containers, resolved to the same thing: a parsed JSON tree and a byte
// buffer. .gltf keeps its buffer in a sidecar named by a uri; .glb keeps it in a
// chunk of the same file.
//
// GLB is handled here rather than skipped because otherwise NOTHING would test
// it: the writer emits it, the editor's export defaults to it, and no golden
// covers it — a single binary blob is not a reviewable diff, which is why the
// goldens are .gltf. Without this the most-used output path would be the only
// untested one.
struct wGltfSource
{
  wJsonDoc Doc;
  wJsonValue *Root;
  sU8 *Bin;                 // owned
  sDInt BinSize;
  sChar *Text;              // owned; the widened JSON chunk, GLB only

  wGltfSource() { Root = 0; Bin = 0; BinSize = 0; Text = 0; }
  ~wGltfSource() { delete[] Bin; delete[] Text; }
};

static sU32 ChunkU32(const sU8 *b,sDInt off)
{
  return sU32(b[off]) | (sU32(b[off+1])<<8) | (sU32(b[off+2])<<16)
       | (sU32(b[off+3])<<24);
}

static sBool OpenGlb(const sChar *path,sU8 *raw,sDInt size,wGltfSource &s,
  sBool quiet)
{
  if(size<12)
    return Fault(quiet,L"glb is shorter than its header");
  if(ChunkU32(raw,4)!=2)
    return Fault(quiet,L"glb version is not 2");
  if(sDInt(ChunkU32(raw,8))!=size)
    return Fault(quiet,L"the glb header's total length is not the file's size");

  sDInt at = 12;
  sDInt jsonoff = -1, jsonlen = 0, binoff = -1, binlen = 0;

  while(at+8<=size)
  {
    const sU32 len = ChunkU32(raw,at);
    const sU32 kind = ChunkU32(raw,at+4);
    at += 8;
    if(sDInt(at)+sDInt(len)>size)
      return Fault(quiet,L"a glb chunk runs past the end of the file");
    if(len & 3)
      return Fault(quiet,L"a glb chunk length is not 4-aligned");
    if(kind==0x4E4F534A)      { jsonoff = at; jsonlen = len; }
    else if(kind==0x004E4942) { binoff = at; binlen = len; }
    at += len;
  }
  if(at!=size)
    return Fault(quiet,L"the glb chunks do not tile the file exactly");
  if(jsonoff<0)
    return Fault(quiet,L"the glb has no JSON chunk");

  // The chunk is UTF-8 bytes and sChar is 2 wide here. This writer emits ASCII
  // and refuses anything else, so widening is exact — and a non-ASCII byte is a
  // fault rather than something to guess at.
  s.Text = new sChar[jsonlen+1];
  for(sDInt i=0;i<jsonlen;i++)
  {
    if(raw[jsonoff+i]>127)
      return Fault(quiet,L"non-ASCII in the glb JSON chunk");
    s.Text[i] = sChar(raw[jsonoff+i]);
  }
  s.Text[jsonlen] = 0;

  s.Root = s.Doc.Parse(s.Text);
  if(!s.Root)
    return Fault(quiet,s.Doc.GetError());

  if(binoff>=0)
  {
    s.BinSize = binlen;
    s.Bin = new sU8[binlen ? binlen : 1];
    sCopyMem(s.Bin,raw+binoff,binlen);
  }
  return 1;
}

static sBool OpenGltf(const sChar *path,wGltfSource &s,sBool quiet)
{
  sDInt size = 0;
  sU8 *raw = sLoadFile(path,size);
  if(!raw)
    return Fault(quiet,L"the file is missing");

  // 'glTF' little-endian.
  const sBool glb = (size>=4 && ChunkU32(raw,0)==0x46546C67);
  if(glb)
  {
    const sBool ok = OpenGlb(path,raw,size,s,quiet);
    delete[] raw;
    return ok;
  }
  delete[] raw;

  s.Root = s.Doc.Load(path);
  if(!s.Root)
    return Fault(quiet,s.Doc.GetError());

  const wJsonValue *buffers = s.Root->GetArray(L"buffers");
  if(!buffers || buffers->Items.GetCount()!=1)
    return Fault(quiet,L"expected exactly one buffer");
  sPoolString uri = buffers->Items[0]->GetString(L"uri");
  if(uri.IsEmpty())
    return Fault(quiet,L"a .gltf buffer must name a uri");

  // Resolved relative to the .gltf, as a uri must be.
  sString<1024> binpath(path);
  {
    sInt cut = -1;
    for(sInt i=0;binpath[i];i++)
      if(binpath[i]=='/' || binpath[i]=='\\')
        cut = i;
    binpath[cut+1] = 0;
    binpath.Add(uri);
  }
  s.Bin = sLoadFile(binpath,s.BinSize);
  if(!s.Bin)
    return Fault(quiet,L"the buffer file is missing");
  return 1;
}

// Reads path (.gltf plus its sidecar, or .glb) and validates it against the
// specification. Returns 0 on the first structural fault. `quiet` suppresses the
// reason, for the negative cases where a rejection is the expected result.
static sBool CheckGltf(const sChar *path,wGltfMesh *out,sBool quiet=0)
{
  wGltfSource src;
  if(!OpenGltf(path,src,quiet))
    return 0;

  wJsonValue *root = src.Root;
  if(root->Type!=wJSON_OBJECT)
    return Fault(quiet,L"root is not an object");

  const wJsonValue *asset = root->Member(L"asset");
  if(!asset || sCmpString(asset->GetString(L"version"),L"2.0")!=0)
    return Fault(quiet,L"asset.version is not \"2.0\"");

  const wJsonValue *buffers = root->GetArray(L"buffers");
  const wJsonValue *views = root->GetArray(L"bufferViews");
  const wJsonValue *accs = root->GetArray(L"accessors");
  const wJsonValue *meshes = root->GetArray(L"meshes");
  if(!buffers || !views || !accs || !meshes)
    return Fault(quiet,L"a required top-level array is missing");
  if(buffers->Items.GetCount()!=1)
    return Fault(quiet,L"expected exactly one buffer");

  const sInt declared = buffers->Items[0]->GetInt(L"byteLength",-1);
  const sU8 *bin = src.Bin;
  const sDInt binsize = src.BinSize;

  // For a .gltf this catches a truncated or stale sidecar; for a .glb it catches
  // a chunk length that disagrees with what the JSON claims. Both are silent
  // corruption in a viewer.
  if(!bin || sInt(binsize)!=declared)
    return Fault(quiet,L"buffer.byteLength disagrees with the buffer's size");

  /*--- every bufferView inside the buffer ---*/

  for(sInt i=0;i<views->Items.GetCount();i++)
  {
    const wJsonValue *v = views->Items[i];
    const sInt off = v->GetInt(L"byteOffset",0);
    const sInt len = v->GetInt(L"byteLength",-1);
    if(v->GetInt(L"buffer",-1)!=0 || off<0 || len<0
      || sDInt(off)+sDInt(len)>binsize)
    {      return Fault(quiet,L"a bufferView falls outside its buffer");
    }
  }

  /*--- every accessor inside its bufferView ---*/

  struct wAcc { sInt Off,Count,Comp,Elems; };
  sArray<wAcc> acc;

  for(sInt i=0;i<accs->Items.GetCount();i++)
  {
    const wJsonValue *a = accs->Items[i];
    const sInt vi = a->GetInt(L"bufferView",-1);
    if(vi<0 || vi>=views->Items.GetCount())
    {      return Fault(quiet,L"an accessor names a bufferView that does not exist");
    }

    const sInt comp = ComponentSize(a->GetInt(L"componentType",0));
    const sInt elems = TypeCount(a->GetString(L"type"));
    const sInt count = a->GetInt(L"count",-1);
    if(comp==0 || elems==0 || count<0)
    {      return Fault(quiet,L"an accessor has an unknown componentType, type or count");
    }

    const wJsonValue *v = views->Items[vi];
    const sInt voff = v->GetInt(L"byteOffset",0);
    const sInt vlen = v->GetInt(L"byteLength",0);
    const sInt aoff = a->GetInt(L"byteOffset",0);
    const sInt need = count*comp*elems;

    if(aoff<0 || aoff+need>vlen)
    {      return Fault(quiet,L"an accessor overruns its bufferView");
    }

    // glTF requires an accessor's effective offset to be a multiple of its
    // component size.
    if(((voff+aoff)%comp)!=0)
    {      return Fault(quiet,L"an accessor is misaligned for its component type");
    }

    wAcc r;
    r.Off = voff+aoff; r.Count = count; r.Comp = comp; r.Elems = elems;
    acc.AddTail(r);
  }

  /*--- the mesh, its primitives, and the indices ---*/

  if(meshes->Items.GetCount()!=1)
  {    return Fault(quiet,L"expected exactly one mesh");
  }
  const wJsonValue *prims = meshes->Items[0]->GetArray(L"primitives");
  if(!prims || prims->Items.GetCount()==0)
  {    return Fault(quiet,L"the mesh has no primitives");
  }

  sInt posacc = -1, nrmacc = -1;

  for(sInt p=0;p<prims->Items.GetCount();p++)
  {
    const wJsonValue *pr = prims->Items[p];
    const wJsonValue *at = pr->Member(L"attributes");
    if(!at)
    {      return Fault(quiet,L"a primitive has no attributes");
    }
    const sInt pa = at->GetInt(L"POSITION",-1);
    const sInt na = at->GetInt(L"NORMAL",-1);
    const sInt ia = pr->GetInt(L"indices",-1);
    if(pa<0 || pa>=acc.GetCount() || ia<0 || ia>=acc.GetCount())
    {      return Fault(quiet,L"a primitive names an accessor that does not exist");
    }
    if(pr->GetInt(L"mode",4)!=4)
    {      return Fault(quiet,L"a primitive is not TRIANGLES");
    }
    if(p==0) { posacc = pa; nrmacc = na; }
    else if(pa!=posacc)
    {      return Fault(quiet,L"primitives disagree about POSITION");
    }

    if(acc[ia].Count%3)
    {      return Fault(quiet,L"an index count is not a multiple of 3");
    }

    // THE CHECK THAT MAKES THE REST MEAN SOMETHING: every index must name a
    // vertex that exists. This is the exact off-by-one a broken writer produces.
    for(sInt k=0;k<acc[ia].Count;k++)
    {
      const sU32 idx = GetU32(bin,acc[ia].Off+k*4);
      if(sInt(idx)>=acc[pa].Count)
      {        return Fault(quiet,L"an index is past the end of POSITION");
      }
      out->Idx.AddTail(idx);
    }
    out->Prims++;
  }

  /*--- positions, normals, and the declared bounds ---*/

  const sInt vc = acc[posacc].Count;
  out->Verts = vc;
  for(sInt i=0;i<vc;i++)
    for(sInt k=0;k<3;k++)
      out->Pos.AddTail(GetF32(bin,acc[posacc].Off+i*12+k*4));

  if(nrmacc>=0)
  {
    if(acc[nrmacc].Count!=vc)
    {      return Fault(quiet,L"NORMAL and POSITION disagree about the vertex count");
    }
    for(sInt i=0;i<vc;i++)
      for(sInt k=0;k<3;k++)
        out->Nrm.AddTail(GetF32(bin,acc[nrmacc].Off+i*12+k*4));
  }

  // min/max are REQUIRED on POSITION, and must describe the data. Recomputed
  // here rather than trusted: a writer that takes them from the source mesh
  // before conversion produces a file that is self-consistent everywhere except
  // the one place a viewer uses to frame the camera.
  const wJsonValue *mn = accs->Items[posacc]->GetArray(L"min");
  const wJsonValue *mx = accs->Items[posacc]->GetArray(L"max");
  if(!mn || !mx || mn->Items.GetCount()!=3 || mx->Items.GetCount()!=3)
  {    return Fault(quiet,L"POSITION has no min/max, which the spec requires");
  }
  for(sInt k=0;k<3;k++)
  {
    out->Min[k] = mn->Items[k]->AsFloat();
    out->Max[k] = mx->Items[k]->AsFloat();
  }
  for(sInt k=0;k<3;k++)
  {
    sF32 lo = out->Pos[k], hi = out->Pos[k];
    for(sInt i=1;i<vc;i++)
    {
      const sF32 e = out->Pos[i*3+k];
      lo = sMin(lo,e); hi = sMax(hi,e);
    }
    if(sFAbs(lo-out->Min[k])>1e-5f || sFAbs(hi-out->Max[k])>1e-5f)
    {      return Fault(quiet,L"POSITION min/max do not describe the actual data");
    }
  }

  return 1;    // wGltfSource owns the buffer and frees it
}

/****************************************************************************/
/***   the bytes, before any parser gets to be forgiving about them        ***/
/****************************************************************************/
//
// CheckGltf above cannot catch either of the defects this checks for, and that is
// the point of having it separately.
//
//   a BOM is STRIPPED by sLoadText on the way into wJsonDoc, so a file that
//     violates RFC 8259 parses cleanly here and is rejected by stricter loaders;
//   a trailing NUL sits after the closing brace, so the parse succeeds and the
//     file is nonetheless binary to grep, git and diff.
//
// Both were real: the .gltf writer used sSaveTextUTF8 until phase 8 and emitted
// both. In .wz4t the same defect survived five phases under a comment saying it
// had been fixed (architecture.md A65), because every test there compared whole
// files and none of them read one. So these assertions are byte-level on purpose.
//
// RFC 8259: an implementation MUST NOT add a BOM. glTF: the GLB JSON chunk is
// UTF-8 without one, and is padded to four bytes with SPACES (0x20) — trailing
// NULs there would be a different spec violation with the same cause.

static void CheckBytes(const sChar *path,const sChar *label)
{
  sDInt size = 0;
  sU8 *raw = sLoadFile(path,size);
  if(!raw || size<=0)
  {
    sPrintF(L"  FAIL  %s: cannot read it back\n",label);
    Failures++;
    delete[] raw;
    return;
  }

  const sU8 *json = raw;
  sDInt len = size;
  sBool glb = 0;

  if(size>=20 && ChunkU32(raw,0)==0x46546C67)
  {
    glb = 1;
    len = sDInt(ChunkU32(raw,12));
    json = raw+20;
    Check((len%4)==0,L"the GLB JSON chunk length is 4-aligned");
    Check(sDInt(20)+len<=size,L"and the chunk fits the file");
    if(len>0 && sDInt(20)+len<=size)
      Check(json[len-1]==' ' || json[len-1]=='}' || json[len-1]=='\n',
        L"and is padded with spaces, not NULs");
  }

  if(len>=3)
    Check(!(json[0]==0xef && json[1]==0xbb && json[2]==0xbf),
      glb ? L"the GLB JSON chunk has NO BOM (glTF requires UTF-8 without one)"
          : L"the .gltf has NO BOM (RFC 8259: an implementation must not add one)");

  Check(len>0 && json[0]=='{',L"and starts at the opening brace");

  sInt nuls = 0;
  for(sDInt i=0;i<len;i++)
    if(json[i]==0)
      nuls++;
  Check(nuls==0,
    glb ? L"and contains no NUL byte"
        : L"and contains no NUL, so it is text to grep, git and diff");
  if(nuls)
    sPrintF(L"        %d NUL(s) in %d bytes\n",nuls,sInt(len));

  delete[] raw;
}

/****************************************************************************/
/***   the handedness gate                                                 ***/
/****************************************************************************/
//
// On a closed convex mesh every face points away from the centre. The normal
// implied by the WINDING (right-hand rule, which is what glTF specifies) must
// therefore have a positive dot with the direction from the centre to the face,
// and it must agree with the stored NORMAL.
//
// This is the check that decides whether the z-negation and the winding reversal
// compose correctly. It is not something a screenshot of an unlit mesh can show,
// and a viewer with backface culling off hides it completely.

// `convex` gates the centroid half ONLY. The two checks have very different
// domains and conflating them was a bug:
//
//   NORMAL agreement is valid for ANY orientable mesh — it compares the winding
//     against the normal stored beside it, and needs no assumption about shape;
//   the outward test assumes a CLOSED CONVEX mesh, because only then does "away
//     from the centroid" mean anything. A torus fails it correctly: ten of its
//     thirty faces are on the inner surface and point inward by construction.
//     So do a grid, a disc and an extruded path, which are not closed at all.
//
// Applying both unconditionally made seven of fifteen sample meshes look broken
// when nothing was wrong with any of them.
static void CheckWinding(const wGltfMesh &m,const sChar *label,sBool convex)
{
  sF32 c[3] = { 0,0,0 };
  for(sInt i=0;i<m.Verts;i++)
    for(sInt k=0;k<3;k++)
      c[k] += m.Pos[i*3+k];
  for(sInt k=0;k<3;k++)
    c[k] /= sF32(m.Verts);

  sInt inward = 0, disagree = 0, degenerate = 0;
  const sInt tris = m.Idx.GetCount()/3;

  for(sInt t=0;t<tris;t++)
  {
    const sInt ia = m.Idx[t*3+0], ib = m.Idx[t*3+1], ic = m.Idx[t*3+2];
    sF32 u[3],v[3],n[3],mid[3];
    for(sInt k=0;k<3;k++)
    {
      u[k] = m.Pos[ib*3+k]-m.Pos[ia*3+k];
      v[k] = m.Pos[ic*3+k]-m.Pos[ia*3+k];
      mid[k] = (m.Pos[ia*3+k]+m.Pos[ib*3+k]+m.Pos[ic*3+k])/3.0f - c[k];
    }
    n[0] = u[1]*v[2]-u[2]*v[1];
    n[1] = u[2]*v[0]-u[0]*v[2];
    n[2] = u[0]*v[1]-u[1]*v[0];

    // A zero-area triangle has no orientation, so every claim below is vacuous
    // for it. Counted and skipped rather than silently passed: a mesh that is
    // mostly slivers should say so, not report a clean bill of health.
    const sF32 area2 = n[0]*n[0]+n[1]*n[1]+n[2]*n[2];
    if(area2 <= 1e-16f)
    {
      degenerate++;
      continue;
    }

    if(n[0]*mid[0]+n[1]*mid[1]+n[2]*mid[2] <= 0)
      inward++;

    if(m.Nrm.GetCount())
    {
      for(sInt k=0;k<3;k++)
      {
        const sF32 d = n[0]*m.Nrm[m.Idx[t*3+k]*3+0]
                     + n[1]*m.Nrm[m.Idx[t*3+k]*3+1]
                     + n[2]*m.Nrm[m.Idx[t*3+k]*3+2];
        if(d<=0)
          { disagree++; break; }
      }
    }
  }

  sPrintF(L"  %s: %d triangles, %d inward, %d disagreeing with NORMAL",
    label,tris,inward,disagree);
  if(degenerate)
    sPrintF(L", %d zero-area (skipped)",degenerate);
  sPrint(L"\n");
  if(convex)
    Check(inward==0,L"every face winds outward — the mirror and the winding agree");
  Check(disagree==0,L"and the stored NORMAL agrees with the winding at every corner");
}

/****************************************************************************/
/***   the negative cases                                                  ***/
/****************************************************************************/
//
// Without these, none of the positive assertions above mean anything: a checker
// that accepted every input would pass all of them. Same reasoning, and the same
// shape, as the deliberately-broken OBJ in tests/mesh_obj.cpp.

// A minimal but VALID glTF, so that the broken variants differ from it in
// exactly one way and the rejection can only be blamed on that.
static const sChar *GoodGltf =
L"{\"asset\":{\"version\":\"2.0\"},"
L"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"mode\":4}]}],"
L"\"accessors\":["
L"{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
L"\"min\":[0,0,0],\"max\":[1,1,0]},"
L"{\"bufferView\":1,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"}],"
L"\"bufferViews\":["
L"{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
L"{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
L"\"buffers\":[{\"byteLength\":48,\"uri\":\"neg.bin\"}]}";

// The same file with the index accessor claiming one element more than its
// bufferView holds. Spelled out in full rather than patched from the string
// above, so the difference is visible in a diff instead of in a substring index.
static const sChar *OverrunGltf =
L"{\"asset\":{\"version\":\"2.0\"},"
L"\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"mode\":4}]}],"
L"\"accessors\":["
L"{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
L"\"min\":[0,0,0],\"max\":[1,1,0]},"
L"{\"bufferView\":1,\"componentType\":5125,\"count\":4,\"type\":\"SCALAR\"}],"
L"\"bufferViews\":["
L"{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
L"{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
L"\"buffers\":[{\"byteLength\":48,\"uri\":\"neg.bin\"}]}";

static void WriteNegBin(const sChar *dir,sU32 third)
{
  sString<1024> p(dir); p.Add(L"/neg.bin");
  sU8 b[48];
  const sF32 pos[9] = { 0,0,0, 1,0,0, 0,1,0 };
  for(sInt i=0;i<9;i++)
    sCopyMem(b+i*4,&pos[i],4);
  const sU32 idx[3] = { 0,1,third };
  for(sInt i=0;i<3;i++)
    sCopyMem(b+36+i*4,&idx[i],4);
  sSaveFile(p,b,48);
}

static void Negatives(const sChar *dir)
{
  sString<1024> p(dir); p.Add(L"/neg.gltf");

  // Control: the good file must be ACCEPTED, or the rejections below prove
  // nothing but that the checker dislikes hand-written files.
  WriteNegBin(dir,2);
  sSaveTextUTF8(p,GoodGltf);
  {
    wGltfMesh m;
    Check(CheckGltf(p,&m)!=0,L"the checker accepts a minimal valid glTF");
  }

  // 1. An index one past the end — the exact off-by-one a 1-based writer makes.
  WriteNegBin(dir,3);
  {
    wGltfMesh m;
    Check(CheckGltf(p,&m,1)==0,L"and REJECTS an index one past the end");
  }

  // 2. An accessor overrunning its bufferView, by claiming one element too many.
  WriteNegBin(dir,2);
  {
    sSaveTextUTF8(p,OverrunGltf);
    wGltfMesh m;
    Check(CheckGltf(p,&m,1)==0,L"and REJECTS an accessor that overruns its view");
  }

  // 3. A buffer whose declared length disagrees with the file on disk.
  {
    sSaveTextUTF8(p,GoodGltf);
    sString<1024> b(dir); b.Add(L"/neg.bin");
    sU8 junk[32] = { 0 };
    sSaveFile(b,junk,32);

    wGltfMesh m;
    Check(CheckGltf(p,&m,1)==0,L"and REJECTS a buffer that is not the declared size");
  }
}

/****************************************************************************/

// `Doc` is the framework's own global, declared in wz4lib/doc_core.hpp:787 — not
// ours to shadow.

// Exports one mesh and runs the full battery over the result.
static void Export(Wz4Mesh *mesh,const sChar *dir,const sChar *name,
  sInt expecttris,sBool outward)
{
  sString<1024> path(dir);
  path.Add(L"/"); path.Add(name); path.Add(L".gltf");

  wGltfStats st;
  Check(wWriteGltfFile(path,mesh,&st)!=0,L"the writer reports success");

  // A writer's success return is not evidence (architecture.md A39).
  sDInt size = 0;
  sU8 *bytes = sLoadFile(path,size);
  Check(bytes!=0 && size>0,L"and the file exists and is not empty");
  delete[] bytes;

  CheckBytes(path,name);

  wGltfMesh m;
  Check(CheckGltf(path,&m)!=0,L"and a separate JSON parser validates it");
  if(m.Verts==0)
    return;

  // The same mesh as a .glb, so the container the editor writes by default gets
  // the same byte-level scrutiny. Its JSON chunk has different padding rules and
  // is the one glTF explicitly requires to carry no BOM.
  {
    sString<1024> glbpath(dir);
    glbpath.Add(L"/"); glbpath.Add(name); glbpath.Add(L".glb");

    wGltfStats gst;
    Check(wWriteGltfFile(glbpath,mesh,&gst)!=0,L"the same mesh writes as .glb");
    CheckBytes(glbpath,name);

    wGltfMesh gm;
    Check(CheckGltf(glbpath,&gm)!=0,L"and the .glb validates");
    Check(gm.Verts==m.Verts && gm.Idx.GetCount()==m.Idx.GetCount(),
      L"and both containers describe the same geometry");
  }

  Check(m.Verts==mesh->Vertices.GetCount(),
    L"every vertex reached the buffer, used or not");
  if(expecttris>=0)
    Check(sInt(m.Idx.GetCount()/3)==expecttris,
      L"and the quad fan produced the expected triangle count");

  if(outward)
    CheckWinding(m,name,1);   // the built-in cases are cubes: closed and convex
}

/****************************************************************************/

void sMain()
{
  sPrint(L"gltf_roundtrip: phase 8 gate\n\n");

  // Positional 0. wz4gen's first positional is its COMMAND, which is why its
  // files sit at index 1; there is no command here. Getting this wrong once cost
  // a silent pass in 6.2, because every assertion used the same wrong path.
  const sChar *dir = sGetShellParameter(0,0);
  if(!dir)
  {
    sPrint(L"usage: gltf_roundtrip <outdir> | gltf_roundtrip -check <file>\n");
    sSetErrorCode();
    return;
  }

  // -check <file> validates one existing file and stops. It exists so the 8.4
  // editor gate can use THIS checker rather than a second one written to agree
  // with it — the same reason the export switch drives the menu's own function.
  // Accepts .glb as well as .gltf, which is what the editor writes.
  {
    const sChar *one = sGetShellParameter(L"check",0);
    if(one)
    {
      // -convex additionally requires every face to wind away from the centroid.
      // OPT-IN, because that only means anything on a closed convex mesh: a
      // torus has ten inward-facing triangles by construction, and a grid, a
      // disc or an extruded path are not closed at all. The NORMAL-agreement
      // check below needs no such assumption and always runs.
      const sBool convex = sGetShellSwitch(L"convex");

      CheckBytes(one,L"exported");

      wGltfMesh m;
      Check(CheckGltf(one,&m)!=0,L"the exported file validates");
      if(m.Verts>0)
      {
        sPrintF(L"  %d vertices, %d triangles, %d primitive(s)\n",
          m.Verts,sInt(m.Idx.GetCount()/3),m.Prims);
        CheckWinding(m,L"exported",convex);
      }
      sPrintF(L"\ngltf_roundtrip: %d failure(s)\n",Failures);
      if(Failures)
        sSetErrorCode();
      return;
    }
  }

  Doc = new wDocument;
  wPage *page = Doc->Pages[0];

  wClass *cube = Doc->FindClass(L"Cube",L"Wz4Mesh");
  if(!cube)
  {
    sPrint(L"  FAIL  no Cube class\n");
    Failures++;
  }
  else
  {
    page->Ops.Clear();
    wOp *op = wInsertOp(page,cube,0,0);
    Doc->Connect();
    wObject *obj = Doc->CalcOp(op);

    if(!obj)
    {
      sPrint(L"  FAIL  Cube did not evaluate\n");
      Failures++;
    }
    else
    {
      Wz4Mesh *src = (Wz4Mesh *)obj;

      sPrint(L"\n[cube] a closed convex mesh, 6 quads -> 12 triangles\n");
      Export(src,dir,L"cube",12,1);

      // The z-asymmetric case. The document's object is BORROWED from the cache,
      // so the translation goes onto a copy — mutating a cached object broke the
      // next frame once already (architecture.md A58).
      sPrint(L"\n[shifted] the same cube translated +1 in z, which is the only\n"
             L"          case here that can tell a missing mirror from a present one\n");
      Wz4Mesh shifted;
      shifted.CopyFrom(src);
      sMatrix34 mat;
      mat.Init();
      mat.l.Init(0,0,1);
      shifted.Transform(mat);

      sAABBox box;
      shifted.CalcBBox(box);
      sPrintF(L"          source z range %f..%f\n",box.Min.z,box.Max.z);

      Export(&shifted,dir,L"shifted",12,1);

      sString<1024> sp(dir); sp.Add(L"/shifted.gltf");
      wGltfMesh m;
      if(CheckGltf(sp,&m))
      {
        // The decisive assertion. A mirror on z maps [lo,hi] to [-hi,-lo]; no
        // conversion at all leaves it [lo,hi]. On this mesh those differ.
        Check(sFAbs(m.Min[2]-(-box.Max.z))<1e-5f
           && sFAbs(m.Max[2]-(-box.Min.z))<1e-5f,
          L"the exported z range is the source's negated and swapped");
        sPrintF(L"          exported z range %f..%f\n",m.Min[2],m.Max[2]);
      }

      obj->Release();
    }
  }

  sPrint(L"\n[negative] a checker that accepts everything proves nothing\n");
  Negatives(dir);

  delete Doc;

  sPrintF(L"\ngltf_roundtrip: %d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
