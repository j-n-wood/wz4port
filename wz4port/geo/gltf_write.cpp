/****************************************************************************/
/***                                                                      ***/
/***   glTF 2.0 export for Wz4Mesh — wz4port, phase 8                      ***/
/***                                                                      ***/
/****************************************************************************/

#include "base/types.hpp"
#include "base/types2.hpp"
#include "base/system.hpp"
#include "base/math.hpp"

#include "util/image.hpp"       // sImage, for the PNG encode

#include "gltf_write.hpp"
#include "mesh_check.hpp"       // wMeshMeasure — the preconditions, already written
#include "json_write.hpp"       // wJsonWriter, moved here from opsmeta in 8.1
#include "wz4_mtrl_headless.hpp"          // SimpleMtrl — phase 9
#include "wz4frlib/wz3_bitmap_code.hpp"   // GenBitmap

// Defined in altona/main/util/image.cpp, which compiles
// STB_IMAGE_WRITE_IMPLEMENTATION. The vendored copy declares it WITHOUT `static`
// (stb_image_write.h:426), so it has external linkage and is callable here —
// which is what makes embedding a PNG in a .glb need no upstream change and no
// temporary file. sImage::SavePNG already goes through it and then writes the
// bytes to disk; this needs the bytes.
//
// The buffer it returns is released with delete[], not free: this vendored copy
// was adapted to new/delete, and sImage::SavePNG frees it that way too
// (image.cpp:2762).
unsigned char *stbi_write_png_to_mem(unsigned char *pixels,int stride_bytes,
  int x,int y,int n,int *out_len);

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
//   UVs        pass through, and this IS verified rather than assumed. glTF's
//              texture origin is top-left with v running down. Wz4 agrees, by
//              three in-tree code paths with no flip between them:
//
//                Select(Wz4Mesh,GenBitmap) samples bmp->Data[XSize*v + u]
//                  (wz4_mesh_ops.ops:1699), so v = 0 is bitmap row 0;
//                GenBitmap::CopyTo is a linear copy into sImage
//                  (wz3_bitmap_code.cpp:420), no row flip;
//                sImage::SavePNG swizzles BGRA to RGBA and nothing else
//                  (image.cpp:2744), and PNG row 0 is the TOP.
//
//              So v = 0 is the top of the image, which is what glTF wants. No
//              flip on export.

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

/****************************************************************************/
/***   colour space, decided by measurement                                ***/
/****************************************************************************/
//
// glTF is specific and the two halves differ, which is the whole trap here:
//
//   baseColorTexture  is sRGB-ENCODED. The loader linearises it.
//   baseColorFactor   is LINEAR. It is used as-is.
//
// THE TEXTURE NEEDS NO CONVERSION. GenBitmap stores whatever the author
// authored, in the encoding they authored it in: GetColor64 scales an 8-bit
// component into a 15-bit range and applies no transfer function at all
// (wz3_bitmap_code.cpp:213-221), and CopyTo narrows back by a plain shift. A
// colour picked as mid-grey is stored as mid-grey and displays as mid-grey — it
// is display-referred, which is what sRGB-encoded means. So the PNG carries the
// authored values unchanged and glTF reads them correctly.
//
// THE FACTOR DOES. The material's Colour comes from the editor's colour picker,
// so it is display-referred for the same reason, while baseColorFactor is
// specified linear. Passing it through would make every material visibly too
// bright — and would do so consistently enough to look deliberate.
static sF32 SrgbToLinear(sInt v255)
{
  const sF32 c = sClamp(sF32(v255)/255.0f,0.0f,1.0f);
  return (c<=0.04045f) ? (c/12.92f) : sPow((c+0.055f)/1.055f,2.4f);
}

/****************************************************************************/
/***   a GenBitmap as PNG bytes                                            ***/
/****************************************************************************/

static sBool EncodePng(BitmapBase *bmp,sArray<sU8> &out)
{
  out.Clear();
  if(!bmp)
    return 0;

  // CopyTo narrows 16 bits per channel to 8 and sizes the image itself. The
  // texture is 8-bit in glTF regardless, so nothing is lost that survives.
  sImage img;
  bmp->CopyTo(&img);
  if(img.SizeX<=0 || img.SizeY<=0)
    return 0;

  // The same BGRA-to-RGBA swizzle sImage::SavePNG does (image.cpp:2751-2757).
  // Done here rather than by calling SavePNG because that writes a file and this
  // needs the bytes — for a .glb they go in the BIN chunk.
  const sInt count = img.SizeX*img.SizeY;
  sU8 *rgba = new sU8[count*4];
  const sU8 *src = (const sU8 *)img.Data;
  for(sInt i=0;i<count;i++)
  {
    rgba[i*4+0] = src[i*4+2];
    rgba[i*4+1] = src[i*4+1];
    rgba[i*4+2] = src[i*4+0];
    rgba[i*4+3] = src[i*4+3];
  }

  int len = 0;
  unsigned char *png = stbi_write_png_to_mem(rgba,img.SizeX*4,img.SizeX,
    img.SizeY,4,&len);
  delete[] rgba;

  if(!png || len<=0)
  {
    delete[] png;
    return 0;
  }
  sCopyMem(out.AddMany(len),png,len);
  delete[] png;                 // new/delete, not free — see the declaration
  return 1;
}

/****************************************************************************/

wGltfStats::wGltfStats()
{
  Verts = Tris = Prims = UnusedVerts = Degenerate = 0;
  Materials = Textures = 0;
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
  GLTF_REPEAT         = 10497,
  GLTF_CLAMP_TO_EDGE  = 33071,
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

sBool wWriteGltf(sTextBuffer &json,sArray<sU8> &bin,
  sArray<wGltfSidecar> &sidecars,Wz4Mesh *mesh,
  const sChar *binuri,wGltfStats *stats)
{
  json.Clear();
  bin.Clear();
  sidecars.Clear();

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

  /*--- materials, textures and images, one material per primitive ---*/
  //
  // Phase 9.3. Before this, every primitive shared one default grey material.
  // Now each takes its cluster's, which is where SetMaterial put it.
  //
  // Deduplicated BY POINTER, not by contents: SetMaterial shares one material
  // object across the clusters that use it and refcounts it, so pointer identity
  // is exactly the "same material" relation the document already maintains —
  // tests/material.cpp asserts a Transform preserves it. Comparing contents
  // instead would merge two materials a user deliberately kept distinct.

  struct wMat { SimpleMtrl *M; sInt Image; sInt Sampler; };
  sArray<wMat> mats;
  sArray<BitmapBase *> images;        // distinct bitmaps, in emission order
  sArray<sInt> matofprim;

  for(sInt p=0;p<primview.GetCount();p++)
  {
    const sInt c = primcluster[p];
    SimpleMtrl *m = 0;
    if(c<mesh->Clusters.GetCount())
      m = (SimpleMtrl *)mesh->Clusters[c]->Mtrl;

    sInt mi = -1;
    for(sInt i=0;i<mats.GetCount();i++)
      if(mats[i].M==m)
        { mi = i; break; }

    if(mi<0)
    {
      wMat nm;
      nm.M = m;
      nm.Image = -1;
      // glTF's sampler default is REPEAT when the field is absent, and REPEAT is
      // what this port's geometry needs: the Cube's UVs run 0..4, one full tile
      // per face in a continuous band, so CLAMP_TO_EDGE would smear the
      // texture's edge column across three faces of every cube. A sampler is
      // emitted only to say CLAMP, never to restate the default.
      nm.Sampler = (m && !m->Wrap) ? 1 : 0;

      BitmapBase *tex = m ? m->GetBitmap(0) : 0;
      if(tex)
      {
        sInt ii = -1;
        for(sInt i=0;i<images.GetCount();i++)
          if(images[i]==tex)
            { ii = i; break; }
        if(ii<0)
        {
          ii = images.GetCount();
          images.AddTail(tex);
        }
        nm.Image = ii;
      }
      mi = mats.GetCount();
      mats.AddTail(nm);
    }
    matofprim.AddTail(mi);
  }

  // Encode each distinct texture once. For a .glb the bytes go into the BIN
  // chunk behind their own bufferView; for a .gltf they become sidecar files,
  // because JSON has nowhere to put them.
  // Every view up to here has exactly one accessor; the image views appended
  // below have NONE, because an image is raw bytes rather than typed elements.
  // The accessor loop stops at this mark, and because images are appended last
  // the bufferView indices still line up with the accessor indices before it.
  const sInt accessorviews = views.GetCount();

  sArray<sInt> imageview;             // bufferView per image, .glb only
  for(sInt i=0;i<images.GetCount();i++)
  {
    sArray<sU8> png;
    if(!EncodePng(images[i],png))
    {
      sPrintF(L"gltf: could not encode texture %d as PNG\n",i);
      return 0;
    }

    if(binuri)
    {
      // Named after the OUTPUT file, not the mesh. `binuri` is the sidecar
      // buffer's name — the output's stem plus ".bin" — so stripping that gives
      // the stem, and the texture becomes "<stem>_tex0.png" beside its own
      // .gltf and .bin.
      //
      // The mesh's own name was the first attempt and it collides: most meshes
      // have none, so every export in a directory wrote "mesh_tex0.png" over the
      // last one and every .gltf pointed at whichever finished last.
      sString<256> stem(binuri);
      const sInt dot = sFindLastChar(stem,'.');
      if(dot>=0)
        stem[dot] = 0;

      wGltfSidecar sc;
      sc.Name.PrintF(L"%s_tex%d.png",(const sChar *)stem,i);
      sc.Len = png.GetCount();
      sc.Png = new sU8[sc.Len];
      sCopyMem(sc.Png,&png[0],sc.Len);
      sidecars.AddTail(sc);
      imageview.AddTail(-1);
    }
    else
    {
      wView v;
      v.Offset = bin.GetCount();
      sCopyMem(bin.AddMany(png.GetCount()),&png[0],png.GetCount());
      v.Length = bin.GetCount()-v.Offset;

      // An image bufferView carries NO target — targets are for vertex and index
      // data, and a validator rejects one here. Padded to 4 because the next
      // view has to start aligned; PNG length is arbitrary.
      v.Target = 0;
      v.Count = 0; v.ComponentType = 0; v.Type = 0;
      Pad4(bin,0);
      imageview.AddTail(views.GetCount());
      views.AddTail(v);
    }
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
          w.Int(L"material",matofprim[p]);
          w.Int(L"mode",GLTF_TRIANGLES);
        w.EndObject();
      }
      w.EndArray();
    w.EndObject();
  w.EndArray();

  // One material per primitive, from its cluster. A cluster with no material —
  // every mesh that never met SetMaterial — keeps the grey default this used to
  // emit for everything, because a primitive with no material renders untextured
  // white in some viewers and black in others.
  w.BeginArray(L"materials");
  for(sInt i=0;i<mats.GetCount();i++)
  {
    SimpleMtrl *m = mats[i].M;
    w.BeginObject();
      if(m && !m->Name.IsEmpty())
        w.Str(L"name",m->Name);
      else
        w.Str(L"name",L"wz4_default");

      w.BeginObject(L"pbrMetallicRoughness");
        w.BeginArray(L"baseColorFactor");
        if(m)
        {
          // sRGB to linear: the factor is specified linear and the colour was
          // picked on a display. Alpha is NOT transformed — it is not a colour
          // and has no transfer function.
          w.Float(SrgbToLinear((m->Colour>>16)&0xff));
          w.Float(SrgbToLinear((m->Colour>> 8)&0xff));
          w.Float(SrgbToLinear((m->Colour    )&0xff));
          w.Float(sClamp(sF32((m->Colour>>24)&0xff)/255.0f,0.0f,1.0f));
        }
        else
        {
          w.Float(0.8f); w.Float(0.8f); w.Float(0.8f); w.Float(1.0f);
        }
        w.EndArray();

        if(mats[i].Image>=0)
        {
          w.BeginObject(L"baseColorTexture");
            w.Int(L"index",mats[i].Image);
            w.Int(L"texCoord",0);
          w.EndObject();
        }

        // Flat and unshiny. This phase carries a diffuse colour and nothing
        // else, and claiming a metalness or a roughness the material does not
        // have would be inventing data.
        w.Float(L"metallicFactor",0.0f);
        w.Float(L"roughnessFactor",1.0f);
      w.EndObject();
      w.Bool(L"doubleSided",1);   // open meshes are legal here; see meshview.cpp
    w.EndObject();
  }
  w.EndArray();

  // textures, samplers and images — emitted only when something is textured, so
  // an untextured mesh's JSON is exactly what it was before this phase.
  if(images.GetCount())
  {
    w.BeginArray(L"textures");
    for(sInt i=0;i<images.GetCount();i++)
    {
      // One texture per image here, because nothing yet varies the sampler per
      // use. sampler 0 is REPEAT and sampler 1 is CLAMP; whether either is
      // emitted at all is decided below.
      w.BeginObject();
        w.Int(L"source",i);
        w.Int(L"sampler",0);
      w.EndObject();
    }
    w.EndArray();

    // REPEAT is glTF's default when a sampler omits wrapS/wrapT, but it is
    // stated explicitly rather than left out. The Cube's UVs run 0..4 and a
    // reader that guessed differently would smear the texture's edge column
    // across three faces of every cube — this is not a default worth relying on
    // silently.
    w.BeginArray(L"samplers");
      w.BeginObject();
        w.Int(L"wrapS",GLTF_REPEAT);
        w.Int(L"wrapT",GLTF_REPEAT);
      w.EndObject();
    w.EndArray();

    w.BeginArray(L"images");
    for(sInt i=0;i<images.GetCount();i++)
    {
      w.BeginObject();
        if(binuri)
        {
          w.Str(L"uri",sidecars[i].Name);
        }
        else
        {
          w.Int(L"bufferView",imageview[i]);
          w.Str(L"mimeType",L"image/png");
        }
      w.EndObject();
    }
    w.EndArray();
  }

  w.BeginArray(L"accessors");
  for(sInt i=0;i<accessorviews;i++)
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
      // An image's view has no target: targets describe vertex and index data,
      // and a validator rejects one on a view holding PNG bytes.
      if(v.Target)
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
    stats->Materials = mats.GetCount();
    stats->Textures = images.GetCount();
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

void wFreeGltfSidecars(sArray<wGltfSidecar> &sidecars)
{
  for(sInt i=0;i<sidecars.GetCount();i++)
  {
    delete[] sidecars[i].Png;
    sidecars[i].Png = 0;
    sidecars[i].Len = 0;
  }
  sidecars.Clear();
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
  sArray<wGltfSidecar> sidecars;
  if(!wWriteGltf(json,bin,sidecars,mesh,glb ? 0 : (const sChar *)binuri,stats))
  {
    wFreeGltfSidecars(sidecars);
    return 0;
  }

  if(glb)
  {
    // A .glb has its textures inside it, so there is nothing beside it to write.
    wFreeGltfSidecars(sidecars);
    return WriteGlb(path,json,bin);
  }

  if(!WriteJsonFile(path,json))
  {
    wFreeGltfSidecars(sidecars);
    return 0;
  }

  // The texture PNGs, beside the .gltf and named by the uri the JSON already
  // states. Written into the .gltf's OWN directory rather than the working one:
  // a uri is resolved relative to the file that names it, so anywhere else and
  // the pair would only load from one particular cwd.
  {
    sString<1024> dir(path);
    sInt cut = -1;
    for(sInt i=0;dir[i];i++)
      if(dir[i]=='/' || dir[i]=='\\')
        cut = i;
    dir[cut+1] = 0;

    for(sInt i=0;i<sidecars.GetCount();i++)
    {
      sString<1024> p(dir);
      p.Add(sidecars[i].Name);
      if(!sSaveFile(p,sidecars[i].Png,sidecars[i].Len))
      {
        sPrintF(L"gltf: could not write texture <%s>\n",(const sChar *)p);
        wFreeGltfSidecars(sidecars);
        return 0;
      }
    }
  }
  wFreeGltfSidecars(sidecars);

  if(!sSaveFile(binpath,bin.GetCount() ? &bin[0] : (const sU8 *)"",bin.GetCount()))
  {
    sPrintF(L"gltf: could not write <%s>\n",(const sChar *)binpath);
    return 0;
  }
  return 1;
}

/****************************************************************************/
