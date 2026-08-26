/****************************************************************************/
/***                                                                      ***/
/***   glTF 2.0 export for Wz4Mesh — wz4port, phase 8                      ***/
/***                                                                      ***/
/****************************************************************************/

#include "base/types.hpp"
#include "base/types2.hpp"
#include "base/system.hpp"
#include "base/math.hpp"

#include "gltf_write.hpp"
#include "mesh_check.hpp"       // wMeshMeasure — the preconditions, already written
#include "json_write.hpp"       // wJsonWriter, moved here from opsmeta in 8.1

/****************************************************************************/
/***   the coordinate conversion, derived once                             ***/
/****************************************************************************/
//
// Wz4 is LEFT-handed Y-up (Altona's projection is D3DXMatrixPerspectiveOffCenterLH,
// math.cpp:1039, and the quaternion-to-matrix form at math.cpp:793 rotates by the
// conjugate). glTF is RIGHT-handed Y-up. One mirror on z converts between them.
//
// A mirror is not a rotation, so it does not act on everything the same way, and
// getting this wrong produces a mesh that looks correct until it is lit. The
// derivation, once, so the code below can just cite it:
//
//   POSITIONS  z -> -z.
//
//   WINDING    for a mirror M, cross(Mb-Ma, Mc-Ma) = det(M) * M * cross(b-a,c-a),
//              and det(M) = -1. So mirroring the corners of a triangle without
//              touching their order gives a face pointing the WRONG WAY. Reversing
//              the order negates it back. Hence indices are emitted reversed.
//
//   NORMALS    z -> -z, the same as positions. Combined with the winding reversal
//              above, the stored normal and the normal implied by the winding
//              agree — which is precisely what tests/gltf_roundtrip.cpp checks,
//              because it is the one thing a screenshot of an unlit mesh cannot
//              show.
//
//   TANGENTS   xyz mirror like positions. The w (BiSign) must FLIP: the bitangent
//              is w*cross(N,T), and cross(MN,MT) = -M*cross(N,T), so preserving
//              M*B requires w -> -w.
//
//   UVs        pass through. glTF's texture origin is top-left with v running
//              down, which is D3D's convention, and Wz4 is a D3D engine. This is
//              the one conversion here that is NOT independently verified in this
//              phase, because materials and textures are out of scope so nothing
//              samples a texture to disagree with it. Flagged rather than hidden.

/****************************************************************************/
/***   little-endian byte emission                                         ***/
/****************************************************************************/
//
// glTF buffers are little-endian by specification. Both targets are, so this is
// a memcpy — but it is asserted rather than assumed, because a silent
// byte-swapped buffer would produce a file that parses cleanly and renders as
// noise, which is the worst failure mode available.

static sBool wLittleEndian()
{
  const sU32 one = 1;
  return *(const sU8 *)&one == 1;
}

static void PutF32(sArray<sU8> &b,sF32 v)
{
  sU8 *d = b.AddMany(4);
  sCopyMem(d,&v,4);
}

static void PutU32(sArray<sU8> &b,sU32 v)
{
  sU8 *d = b.AddMany(4);
  sCopyMem(d,&v,4);
}

static inline void PutPos(sArray<sU8> &b,const sVector31 &v)
{
  PutF32(b,v.x); PutF32(b,v.y); PutF32(b,-v.z);
}

static inline void PutNrm(sArray<sU8> &b,const sVector30 &v)
{
  PutF32(b,v.x); PutF32(b,v.y); PutF32(b,-v.z);
}

// Every element here is 4 bytes wide, so bufferView offsets are 4-aligned by
// construction and glTF's alignment rules are met without padding between them.
// Asserted at the end rather than trusted.
static void Pad4(sArray<sU8> &b,sU8 fill)
{
  while(b.GetCount() & 3)
    *b.AddMany(1) = fill;
}

/****************************************************************************/

wGltfStats::wGltfStats()
{
  Verts = Tris = Prims = UnusedVerts = Degenerate = 0;
  JsonBytes = BinBytes = 0;
}

/****************************************************************************/
/***   the writer                                                          ***/
/****************************************************************************/

// glTF enumerants, spelled out so the emission below reads as the spec does.
enum
{
  GLTF_FLOAT          = 5126,
  GLTF_UNSIGNED_INT   = 5125,
  GLTF_ARRAY_BUFFER   = 34962,
  GLTF_ELEMENT_ARRAY  = 34963,
  GLTF_TRIANGLES      = 4,
};

namespace
{
  // One bufferView plus its accessor. Collected first, emitted second, because
  // glTF is index-referenced and the JSON has to name offsets the binary decides.
  struct wView
  {
    sInt Offset;              // into the binary buffer
    sInt Length;
    sInt Target;              // GLTF_ARRAY_BUFFER or GLTF_ELEMENT_ARRAY
    sInt Count;               // elements, not bytes
    sInt ComponentType;
    const sChar *Type;        // "VEC3", "SCALAR", ...
  };
}

sBool wWriteGltf(sTextBuffer &json,sArray<sU8> &bin,Wz4Mesh *mesh,
  const sChar *binuri,wGltfStats *stats)
{
  json.Clear();
  bin.Clear();

  if(!mesh)
  {
    sPrintF(L"gltf: no mesh\n");
    return 0;
  }

  if(!wLittleEndian())
  {
    sPrintF(L"gltf: this build is big-endian and the writer emits native floats\n");
    return 0;
  }

  // The preconditions, through the measurement code phase 6 already wrote.
  //
  // Deliberately NOT facts.Violations(), which also counts degenerate faces and
  // bad weight sums. A degenerate triangle is perfectly legal glTF and OBJ
  // exports one without complaint, so refusing would make this exporter fail on
  // meshes the existing one handles. Weights are irrelevant while skinning is out
  // of scope. What is left is the set that would produce a structurally invalid
  // file: an out-of-range index, a face arity the format cannot express, or a
  // NaN — which is not even representable in JSON, so the POSITION min/max would
  // be unwritable.
  const wMeshFacts facts = wMeshMeasure(mesh);
  const sInt fatal = facts.OtherArity + facts.BadVertexIndex
                   + facts.BadClusterIndex + facts.NonFinite;
  if(fatal)
  {
    sPrintF(L"gltf: refusing to write a mesh with %d structural violation(s)\n",fatal);
    wMeshReport(L"gltf",facts);
    return 0;
  }

  const sInt vc = mesh->Vertices.GetCount();
  const sInt fc = mesh->Faces.GetCount();
  if(vc<=0 || fc<=0)
  {
    sPrintF(L"gltf: mesh is empty (%d vertices, %d faces)\n",vc,fc);
    return 0;
  }

  // Clusters.GetCount()==0 with every face at cluster 0 is legal and common —
  // mesh_check.cpp:130-140 says so explicitly — so one implicit cluster.
  const sInt ccount = sMax(1,mesh->Clusters.GetCount());

  sArray<wView> views;

  /*--- vertex attributes, one accessor each, shared by every primitive ---*/

  // POSITION. min/max are REQUIRED by the spec on this accessor, and they must
  // describe the data as exported — not CalcBBox, which is taken before the
  // conversion and includes vertices no face references.
  sF32 lo[3],hi[3];
  {
    wView v;
    v.Offset = bin.GetCount();
    v.Target = GLTF_ARRAY_BUFFER;
    v.Count = vc;
    v.ComponentType = GLTF_FLOAT;
    v.Type = L"VEC3";
    for(sInt i=0;i<vc;i++)
    {
      const sVector31 &p = mesh->Vertices[i].Pos;
      const sF32 e[3] = { p.x,p.y,-p.z };
      if(i==0)
      {
        for(sInt k=0;k<3;k++) { lo[k] = hi[k] = e[k]; }
      }
      else
      {
        for(sInt k=0;k<3;k++)
        {
          if(e[k]<lo[k]) lo[k] = e[k];
          if(e[k]>hi[k]) hi[k] = e[k];
        }
      }
      PutF32(bin,e[0]); PutF32(bin,e[1]); PutF32(bin,e[2]);
    }
    v.Length = bin.GetCount()-v.Offset;
    views.AddTail(v);
  }

  // NORMAL
  {
    wView v;
    v.Offset = bin.GetCount();
    v.Target = GLTF_ARRAY_BUFFER;
    v.Count = vc; v.ComponentType = GLTF_FLOAT; v.Type = L"VEC3";
    for(sInt i=0;i<vc;i++)
      PutNrm(bin,mesh->Vertices[i].Normal);
    v.Length = bin.GetCount()-v.Offset;
    views.AddTail(v);
  }

  // TANGENT, VEC4 — xyz plus the handedness sign, which is exactly what
  // Wz4MeshVertex already stores as Tangent + BiSign. The sign FLIPS under the
  // mirror; see the derivation at the top.
  {
    wView v;
    v.Offset = bin.GetCount();
    v.Target = GLTF_ARRAY_BUFFER;
    v.Count = vc; v.ComponentType = GLTF_FLOAT; v.Type = L"VEC4";
    for(sInt i=0;i<vc;i++)
    {
      const Wz4MeshVertex &mv = mesh->Vertices[i];
      PutF32(bin,mv.Tangent.x); PutF32(bin,mv.Tangent.y); PutF32(bin,-mv.Tangent.z);
      PutF32(bin,-mv.BiSign);
    }
    v.Length = bin.GetCount()-v.Offset;
    views.AddTail(v);
  }

  // TEXCOORD_0 and TEXCOORD_1. Both sets exist in Wz4MeshVertex and OBJ carries
  // only the first, which is one of the reasons for this exporter.
  for(sInt set=0;set<2;set++)
  {
    wView v;
    v.Offset = bin.GetCount();
    v.Target = GLTF_ARRAY_BUFFER;
    v.Count = vc; v.ComponentType = GLTF_FLOAT; v.Type = L"VEC2";
    for(sInt i=0;i<vc;i++)
    {
      const Wz4MeshVertex &mv = mesh->Vertices[i];
      PutF32(bin,set ? mv.U1 : mv.U0);
      PutF32(bin,set ? mv.V1 : mv.V0);
    }
    v.Length = bin.GetCount()-v.Offset;
    views.AddTail(v);
  }

  // The primitives below name attribute accessors 0..4 by number, so the count
  // has to match what was just emitted or every primitive points at an index
  // buffer and calls it a position.
  const sInt ATTRVIEWS = 5;     // position, normal, tangent, uv0, uv1
  if(views.GetCount()!=ATTRVIEWS)
  {
    sPrintF(L"gltf: internal error, %d attribute view(s) and not %d\n",
      views.GetCount(),ATTRVIEWS);
    return 0;
  }

  /*--- indices, one accessor per non-empty cluster ---*/

  sArray<sInt> primview;        // view index per emitted primitive
  sArray<sInt> primcluster;
  sInt tris = 0;

  for(sInt c=0;c<ccount;c++)
  {
    const sInt start = bin.GetCount();
    sInt count = 0;

    for(sInt i=0;i<fc;i++)
    {
      const Wz4MeshFace &f = mesh->Faces[i];
      if(f.Cluster!=c)
        continue;

      // The same fan ChargeSolid uses (wz4_mesh.cpp:4716) — (0,i-1,i) — so the
      // triangulation matches what the renderer shows. Emitted REVERSED, which
      // is the winding half of the mirror; see the derivation at the top.
      for(sInt k=2;k<f.Count;k++)
      {
        PutU32(bin,sU32(f.Vertex[k]));
        PutU32(bin,sU32(f.Vertex[k-1]));
        PutU32(bin,sU32(f.Vertex[0]));
        count += 3;
      }
    }

    if(count==0)              // a cluster no face names is not a primitive
      continue;

    wView v;
    v.Offset = start;
    v.Length = bin.GetCount()-start;
    v.Target = GLTF_ELEMENT_ARRAY;
    v.Count = count;
    v.ComponentType = GLTF_UNSIGNED_INT;
    v.Type = L"SCALAR";
    primview.AddTail(views.GetCount());
    primcluster.AddTail(c);
    views.AddTail(v);
    tris += count/3;
  }

  if(primview.GetCount()==0)
  {
    sPrintF(L"gltf: no primitive has any triangles\n");
    return 0;
  }

  // Every element written was 4 bytes wide, so this should already hold. It is
  // checked because an accessor at a misaligned offset is invalid glTF and the
  // failure would surface in someone else's loader, not here.
  if(bin.GetCount() & 3)
  {
    sPrintF(L"gltf: internal error, buffer is %d bytes and not 4-aligned\n",
      bin.GetCount());
    return 0;
  }

  /*--- the JSON ---*/

  wJsonWriter w(json);
  w.BeginObject();

  w.BeginObject(L"asset");
    w.Str(L"version",L"2.0");
    // Fixed, with no version number and no timestamp: this string lands in every
    // golden, and a generator that stamps itself makes every golden drift on
    // every build.
    w.Str(L"generator",L"wz4port glTF writer");
  w.EndObject();

  w.Int(L"scene",0);
  w.BeginArray(L"scenes");
    w.BeginObject();
      w.BeginArray(L"nodes"); w.Int(0); w.EndArray();
    w.EndObject();
  w.EndArray();

  w.BeginArray(L"nodes");
    w.BeginObject();
      w.Int(L"mesh",0);
      if(mesh->Name[0])
        w.Str(L"name",mesh->Name);
    w.EndObject();
  w.EndArray();

  w.BeginArray(L"meshes");
    w.BeginObject();
      if(mesh->Name[0])
        w.Str(L"name",mesh->Name);
      w.BeginArray(L"primitives");
      for(sInt p=0;p<primview.GetCount();p++)
      {
        w.BeginObject();
          w.BeginObject(L"attributes");
            w.Int(L"POSITION",0);
            w.Int(L"NORMAL",1);
            w.Int(L"TANGENT",2);
            w.Int(L"TEXCOORD_0",3);
            w.Int(L"TEXCOORD_1",4);
          w.EndObject();
          w.Int(L"indices",primview[p]);
          w.Int(L"material",0);
          w.Int(L"mode",GLTF_TRIANGLES);
        w.EndObject();
      }
      w.EndArray();
    w.EndObject();
  w.EndArray();

  // One default material. The material system is out of scope and Wz4Mtrl is
  // only forward-declared headless, so there is genuinely nothing to read from
  // the cluster — but a primitive with no material renders untextured white in
  // some viewers and black in others, and a stated default beats that.
  w.BeginArray(L"materials");
    w.BeginObject();
      w.Str(L"name",L"wz4_default");
      w.BeginObject(L"pbrMetallicRoughness");
        w.BeginArray(L"baseColorFactor");
          w.Float(0.8f); w.Float(0.8f); w.Float(0.8f); w.Float(1.0f);
        w.EndArray();
        w.Float(L"metallicFactor",0.0f);
        w.Float(L"roughnessFactor",0.8f);
      w.EndObject();
      w.Bool(L"doubleSided",1);   // open meshes are legal here; see meshview.cpp
    w.EndObject();
  w.EndArray();

  w.BeginArray(L"accessors");
  for(sInt i=0;i<views.GetCount();i++)
  {
    const wView &v = views[i];
    w.BeginObject();
      w.Int(L"bufferView",i);
      w.Int(L"componentType",v.ComponentType);
      w.Int(L"count",v.Count);
      w.Str(L"type",v.Type);
      if(i==0)                  // POSITION: min/max are required
      {
        w.BeginArray(L"min"); w.Float(lo[0]); w.Float(lo[1]); w.Float(lo[2]); w.EndArray();
        w.BeginArray(L"max"); w.Float(hi[0]); w.Float(hi[1]); w.Float(hi[2]); w.EndArray();
      }
    w.EndObject();
  }
  w.EndArray();

  w.BeginArray(L"bufferViews");
  for(sInt i=0;i<views.GetCount();i++)
  {
    const wView &v = views[i];
    w.BeginObject();
      w.Int(L"buffer",0);
      w.Int(L"byteOffset",v.Offset);
      w.Int(L"byteLength",v.Length);
      w.Int(L"target",v.Target);
    w.EndObject();
  }
  w.EndArray();

  w.BeginArray(L"buffers");
    w.BeginObject();
      w.Int(L"byteLength",bin.GetCount());
      if(binuri)                // absent for .glb, where the buffer is a chunk
        w.Str(L"uri",binuri);
    w.EndObject();
  w.EndArray();

  w.EndObject();
  w.Finish();

  if(stats)
  {
    stats->Verts = vc;
    stats->Tris = tris;
    stats->Prims = primview.GetCount();
    stats->UnusedVerts = facts.UnusedVerts;
    stats->Degenerate = facts.Degenerate;
    stats->BinBytes = bin.GetCount();
    stats->JsonBytes = json.GetCount();
  }

  return 1;
}

/****************************************************************************/
/***   files                                                               ***/
/****************************************************************************/

// sChar is 2 bytes here (-fshort-wchar) and a glTF file is bytes, so the JSON is
// narrowed explicitly. Exact rather than lossy, because wJsonWriter escapes
// everything outside 0x20..0x7e as \uXXXX — its output is ASCII by construction.
// A non-ASCII character therefore means the writer changed, not that the input
// was exotic, and it is a refusal rather than a guess.
static sBool NarrowAscii(const sChar *s,sArray<sU8> &out)
{
  for(;*s;s++)
  {
    if(sU32(*s)>127)
      return 0;
    *out.AddMany(1) = sU8(*s);
  }
  return 1;
}

// NOT sSaveTextUTF8, and the reason is worth stating because it is the obvious
// call and it is wrong here: it writes a BOM *and the string's terminating NUL*
// into the file. Both are defects in a glTF:
//
//   RFC 8259 says a JSON implementation MUST NOT add a BOM, and the glTF spec
//     requires the GLB JSON chunk to be UTF-8 WITHOUT one;
//   a NUL is not JSON whitespace, so a strict parser is entitled to reject the
//     file outright;
//   and a NUL makes every tool treat the file as BINARY — `file` reports "data",
//     grep refuses to search it, git will not diff it. That defeats the entire
//     reason the goldens are .gltf rather than .glb, which was that a failure
//     should be readable rather than a byte offset.
//
// Found by trying to grep an exported file for its buffer uri and getting
// nothing back.
static sBool WriteJsonFile(const sChar *path,sTextBuffer &json)
{
  sArray<sU8> bytes;
  if(!NarrowAscii(json.Get(),bytes))
  {
    sPrintF(L"gltf: non-ASCII in the JSON, which this writer does not encode\n");
    return 0;
  }
  if(!sSaveFile(path,bytes.GetCount() ? &bytes[0] : (const sU8 *)"",
    bytes.GetCount()))
  {
    sPrintF(L"gltf: could not write <%s>\n",path);
    return 0;
  }
  return 1;
}

// .glb container: a 12-byte header then length-prefixed chunks. The JSON chunk
// pads with SPACES and the BIN chunk with ZEROS — the spec is specific about
// which, because a parser may hand the JSON chunk straight to a text parser.
static sBool WriteGlb(const sChar *path,sTextBuffer &json,sArray<sU8> &bin)
{
  sArray<sU8> jsonbytes;
  if(!NarrowAscii(json.Get(),jsonbytes))
  {
    sPrintF(L"gltf: non-ASCII in the JSON chunk, which this writer does not"
            L" encode\n");
    return 0;
  }
  Pad4(jsonbytes,' ');
  Pad4(bin,0);

  sArray<sU8> glb;
  const sInt total = 12 + 8 + jsonbytes.GetCount() + 8 + bin.GetCount();

  PutU32(glb,0x46546C67);       // 'glTF'
  PutU32(glb,2);                // version
  PutU32(glb,sU32(total));

  PutU32(glb,sU32(jsonbytes.GetCount()));
  PutU32(glb,0x4E4F534A);       // 'JSON'
  sCopyMem(glb.AddMany(jsonbytes.GetCount()),&jsonbytes[0],jsonbytes.GetCount());

  PutU32(glb,sU32(bin.GetCount()));
  PutU32(glb,0x004E4942);       // 'BIN\0'
  if(bin.GetCount()>0)
    sCopyMem(glb.AddMany(bin.GetCount()),&bin[0],bin.GetCount());

  if(glb.GetCount()!=total)
  {
    sPrintF(L"gltf: internal error, glb is %d bytes and the header says %d\n",
      glb.GetCount(),total);
    return 0;
  }

  if(!sSaveFile(path,&glb[0],glb.GetCount()))
  {
    sPrintF(L"gltf: could not write <%s>\n",path);
    return 0;
  }
  return 1;
}

sBool wWriteGltfFile(const sChar *path,Wz4Mesh *mesh,wGltfStats *stats)
{
  if(!path || !path[0])
  {
    sPrintF(L"gltf: no output path\n");
    return 0;
  }

  const sBool glb = sFindString(path,L".glb")>=0;

  // The sidecar's name is the output's, with the extension replaced, and the uri
  // in the JSON is that name WITHOUT any directory: a glTF uri is resolved
  // relative to the .gltf, so an absolute path would make the pair
  // non-relocatable — and every golden would then carry this machine's build
  // directory in it.
  sString<1024> binpath(path);
  sString<256> binuri;
  if(!glb)
  {
    const sInt dot = sFindLastChar(binpath,'.');
    if(dot>=0)
      binpath[dot] = 0;
    binpath.Add(L".bin");

    const sChar *leaf = binpath;
    for(const sChar *s=binpath;*s;s++)
      if(*s=='/' || *s=='\\')
        leaf = s+1;
    binuri = leaf;
  }

  sTextBuffer json;
  sArray<sU8> bin;
  if(!wWriteGltf(json,bin,mesh,glb ? 0 : (const sChar *)binuri,stats))
    return 0;

  if(glb)
    return WriteGlb(path,json,bin);

  if(!WriteJsonFile(path,json))
    return 0;
  if(!sSaveFile(binpath,bin.GetCount() ? &bin[0] : (const sU8 *)"",bin.GetCount()))
  {
    sPrintF(L"gltf: could not write <%s>\n",(const sChar *)binpath);
    return 0;
  }
  return 1;
}

/****************************************************************************/
