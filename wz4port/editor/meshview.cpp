/****************************************************************************/
/***                                                                      ***/
/***   meshview — a 3D viewer for Wz4Mesh, stage 6.4                       ***/
/***                                                                      ***/
/****************************************************************************/

#include "gl_wz4.hpp"           // must precede anything naming a GL symbol
#include "imgui_wz4.hpp"
#include "meshview.hpp"
#include "base/system.hpp"

/****************************************************************************/

// Narrow char, not sChar. Altona builds with -fshort-wchar so sChar is 2 bytes,
// and glShaderSource wants bytes — passing a wide literal produces a shader
// source full of NULs and a compiler error with no obvious cause.

static const char *VertexSrc =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "layout(location=1) in vec3 aNormal;\n"
  "uniform mat4 uViewProj;\n"
  "out vec3 vNormal;\n"
  "out vec3 vPos;\n"
  "void main()\n"
  "{\n"
  "  vNormal = aNormal;\n"
  "  vPos = aPos;\n"
  "  gl_Position = uViewProj * vec4(aPos,1.0);\n"
  "}\n";

// Lambert plus a fill term, and a rim so silhouettes read against the
// background. Two-sided: an open mesh seen from behind should be lit, not black,
// because several operators legitimately produce open meshes and a viewer that
// showed them as holes would be reporting a fault that is not there.
static const char *FragmentSrc =
  "#version 330 core\n"
  "in vec3 vNormal;\n"
  "in vec3 vPos;\n"
  "uniform vec3 uLightDir;\n"
  "uniform vec3 uEye;\n"
  "uniform vec3 uColor;\n"
  "out vec4 oColor;\n"
  "void main()\n"
  "{\n"
  "  vec3 n = normalize(vNormal);\n"
  "  vec3 v = normalize(uEye - vPos);\n"
  "  if(dot(n,v) < 0.0) n = -n;\n"
  "  float d = max(dot(n,normalize(uLightDir)),0.0);\n"
  "  float rim = pow(1.0 - max(dot(n,v),0.0),3.0);\n"
  "  vec3 c = uColor * (0.25 + 0.75*d) + vec3(0.10,0.12,0.16)*rim;\n"
  "  oColor = vec4(c,1.0);\n"
  "}\n";

static const char *LineVertexSrc =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "layout(location=1) in vec3 aColor;\n"
  "uniform mat4 uViewProj;\n"
  "out vec3 vColor;\n"
  "void main()\n"
  "{\n"
  "  vColor = aColor;\n"
  "  gl_Position = uViewProj * vec4(aPos,1.0);\n"
  "}\n";

static const char *LineFragmentSrc =
  "#version 330 core\n"
  "in vec3 vColor;\n"
  "out vec4 oColor;\n"
  "void main() { oColor = vec4(vColor,1.0); }\n";

/****************************************************************************/

static sU32 CompileShader(sU32 kind,const char *src,const sChar *what)
{
  sU32 sh = glCreateShader(kind);
  glShaderSource(sh,1,&src,0);
  glCompileShader(sh);

  int ok = 0;
  glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
  if(!ok)
  {
    // Printed, not swallowed. A shader that fails to compile leaves a black
    // pane, which is indistinguishable from an empty mesh — so the reason has
    // to reach stdout, where the screenshot runner can also see it.
    char log[1024];
    log[0] = 0;
    glGetShaderInfoLog(sh,sizeof(log),0,log);
    sPrintF(L"meshview: %s shader failed to compile\n",what);
    sChar wide[1024];
    wide[0] = 0;
    sCopyStringFromUTF8(wide,log,sCOUNTOF(wide));
    sPrintF(L"%s\n",wide);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}

static sU32 LinkProgram(const char *vs,const char *fs,const sChar *what)
{
  sU32 v = CompileShader(GL_VERTEX_SHADER,vs,what);
  if(!v)
    return 0;
  sU32 f = CompileShader(GL_FRAGMENT_SHADER,fs,what);
  if(!f)
  {
    glDeleteShader(v);
    return 0;
  }

  sU32 p = glCreateProgram();
  glAttachShader(p,v);
  glAttachShader(p,f);
  glLinkProgram(p);

  int ok = 0;
  glGetProgramiv(p,GL_LINK_STATUS,&ok);
  glDeleteShader(v);
  glDeleteShader(f);

  if(!ok)
  {
    char log[1024];
    log[0] = 0;
    glGetProgramInfoLog(p,sizeof(log),0,log);
    sPrintF(L"meshview: %s program failed to link\n",what);
    sChar wide[1024];
    wide[0] = 0;
    sCopyStringFromUTF8(wide,log,sCOUNTOF(wide));
    sPrintF(L"%s\n",wide);
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

/****************************************************************************/

wMeshView::wMeshView()
{
  Yaw = 0.6f;
  Pitch = 0.5f;
  Distance = 2.6f;
  Wireframe = 0;
  ShowGrid = 1;
  ShowBBox = 0;

  Verts = Tris = Quads = 0;
  Lo.Init(0,0,0);
  Hi.Init(0,0,0);
  Empty = 1;

  ShownOp = 0;
  ShownRevision = -1;

  Program = LineProgram = 0;
  Vao = Vbo = Ibo = 0;
  LineVao = LineVbo = 0;
  LineVerts = 0;
  Fbo = ColorTex = DepthBuf = 0;
  FboW = FboH = 0;
}

wMeshView::~wMeshView()
{
  Release();
}

void wMeshView::Release()
{
  if(Ibo)      { glDeleteBuffers(1,&Ibo); Ibo = 0; }
  if(Vbo)      { glDeleteBuffers(1,&Vbo); Vbo = 0; }
  if(Vao)      { glDeleteVertexArrays(1,&Vao); Vao = 0; }
  if(LineVbo)  { glDeleteBuffers(1,&LineVbo); LineVbo = 0; }
  if(LineVao)  { glDeleteVertexArrays(1,&LineVao); LineVao = 0; }
  if(ColorTex) { glDeleteTextures(1,&ColorTex); ColorTex = 0; }
  if(DepthBuf) { glDeleteRenderbuffers(1,&DepthBuf); DepthBuf = 0; }
  if(Fbo)      { glDeleteFramebuffers(1,&Fbo); Fbo = 0; }
  if(Program)     { glDeleteProgram(Program); Program = 0; }
  if(LineProgram) { glDeleteProgram(LineProgram); LineProgram = 0; }
  FboW = FboH = 0;
}

sBool wMeshView::EnsureShaders()
{
  if(Program && LineProgram)
    return 1;
  if(!Program)
    Program = LinkProgram(VertexSrc,FragmentSrc,L"mesh");
  if(!LineProgram)
    LineProgram = LinkProgram(LineVertexSrc,LineFragmentSrc,L"line");
  return Program!=0 && LineProgram!=0;
}

/****************************************************************************/

void wMeshView::Upload(Wz4Mesh *mesh)
{
  Verts = Tris = Quads = 0;
  Lo.Init(0,0,0);
  Hi.Init(0,0,0);
  Empty = 1;

  if(!mesh || mesh->Faces.GetCount()==0 || mesh->Vertices.GetCount()==0)
  {
    // Not an error. Text3D and Path3D produce empty meshes by design in this
    // build (patch 12), and a DeleteFace with everything selected is a legal
    // result. The pane says "empty" rather than showing a stale mesh.
    LineVerts = 0;
    return;
  }

  if(!EnsureShaders())
    return;

  const sInt vc = mesh->Vertices.GetCount();

  // Interleaved position + normal. The normal comes straight from the vertex:
  // every generator ends with CalcNormalAndTangents(), so there is no geometry
  // pass to do here.
  sArray<sF32> verts;
  verts.AddMany(vc*6);
  for(sInt i=0;i<vc;i++)
  {
    const Wz4MeshVertex &v = mesh->Vertices[i];
    sF32 *d = &verts[i*6];
    d[0] = v.Pos.x; d[1] = v.Pos.y; d[2] = v.Pos.z;
    d[3] = v.Normal.x; d[4] = v.Normal.y; d[5] = v.Normal.z;

    if(i==0)
    {
      Lo = Hi = v.Pos;
    }
    else
    {
      Lo.x = sMin(Lo.x,v.Pos.x); Hi.x = sMax(Hi.x,v.Pos.x);
      Lo.y = sMin(Lo.y,v.Pos.y); Hi.y = sMax(Hi.y,v.Pos.y);
      Lo.z = sMin(Lo.z,v.Pos.z); Hi.z = sMax(Hi.z,v.Pos.z);
    }
  }

  // Quads become two triangles HERE and nowhere else — the mesh keeps its quads.
  // A face with an out-of-range index is skipped rather than trusted: the
  // invariant battery would catch it in a test, but the viewer must not hand a
  // bad index to the driver.
  sArray<sU32> idx;
  for(sInt i=0;i<mesh->Faces.GetCount();i++)
  {
    const Wz4MeshFace &f = mesh->Faces[i];
    const sInt n = sClamp<sInt>(f.Count,0,4);
    if(n<3)
      continue;

    sBool bad = 0;
    for(sInt j=0;j<n;j++)
      if(f.Vertex[j]<0 || f.Vertex[j]>=vc)
        bad = 1;
    if(bad)
      continue;

    idx.AddTail(sU32(f.Vertex[0]));
    idx.AddTail(sU32(f.Vertex[1]));
    idx.AddTail(sU32(f.Vertex[2]));
    Tris++;

    if(n==4)
    {
      idx.AddTail(sU32(f.Vertex[0]));
      idx.AddTail(sU32(f.Vertex[2]));
      idx.AddTail(sU32(f.Vertex[3]));
      Tris++;
      Quads++;
    }
  }

  if(idx.GetCount()==0)
  {
    LineVerts = 0;
    return;
  }

  Verts = vc;
  Empty = 0;

  if(!Vao) glGenVertexArrays(1,&Vao);
  if(!Vbo) glGenBuffers(1,&Vbo);
  if(!Ibo) glGenBuffers(1,&Ibo);

  glBindVertexArray(Vao);

  glBindBuffer(GL_ARRAY_BUFFER,Vbo);
  glBufferData(GL_ARRAY_BUFFER,verts.GetCount()*sizeof(sF32),&verts[0],
    GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(sF32),(void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(sF32),
    (void *)(3*sizeof(sF32)));

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,Ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.GetCount()*sizeof(sU32),&idx[0],
    GL_STATIC_DRAW);

  glBindVertexArray(0);

  BuildLines();
  Fit();
}

/****************************************************************************/

// Grid on the mesh's own scale, plus its bounding box. Both in one buffer with a
// per-vertex colour, so the whole thing is a single draw.

void wMeshView::BuildLines()
{
  sArray<sF32> v;
  LineVerts = 0;

  const sVector31 c((Lo.x+Hi.x)*0.5f,(Lo.y+Hi.y)*0.5f,(Lo.z+Hi.z)*0.5f);
  const sF32 sx = Hi.x-Lo.x, sy = Hi.y-Lo.y, sz = Hi.z-Lo.z;
  const sF32 radius = sMax(0.001f,0.5f*sSqrt(sx*sx + sy*sy + sz*sz));

  // A grid sized to the mesh rather than a fixed 1-unit one: these operators
  // produce meshes from 0.5 to 32 units across in the bundled documents, and a
  // fixed grid is either invisible or a solid block.
  const sF32 step = sMax(0.01f,radius*0.25f);
  const sInt lines = 8;
  const sF32 ext = step*lines;
  const sF32 y = Lo.y;

  for(sInt i=-lines;i<=lines;i++)
  {
    const sF32 t = i*step;
    const sBool axis = (i==0);
    const sF32 g = axis ? 0.42f : 0.24f;

    v.AddTail(c.x-ext); v.AddTail(y); v.AddTail(c.z+t);
    v.AddTail(g); v.AddTail(g); v.AddTail(g*1.15f);
    v.AddTail(c.x+ext); v.AddTail(y); v.AddTail(c.z+t);
    v.AddTail(g); v.AddTail(g); v.AddTail(g*1.15f);

    v.AddTail(c.x+t); v.AddTail(y); v.AddTail(c.z-ext);
    v.AddTail(g); v.AddTail(g); v.AddTail(g*1.15f);
    v.AddTail(c.x+t); v.AddTail(y); v.AddTail(c.z+ext);
    v.AddTail(g); v.AddTail(g); v.AddTail(g*1.15f);
    LineVerts += 4;
  }

  // The 12 edges of the bounding box.
  const sF32 bx[2] = { Lo.x,Hi.x };
  const sF32 by[2] = { Lo.y,Hi.y };
  const sF32 bz[2] = { Lo.z,Hi.z };
  static const sInt Edges[12][6] =
  {
    {0,0,0, 1,0,0},{1,0,0, 1,1,0},{1,1,0, 0,1,0},{0,1,0, 0,0,0},
    {0,0,1, 1,0,1},{1,0,1, 1,1,1},{1,1,1, 0,1,1},{0,1,1, 0,0,1},
    {0,0,0, 0,0,1},{1,0,0, 1,0,1},{1,1,0, 1,1,1},{0,1,0, 0,1,1},
  };
  for(sInt e=0;e<12;e++)
  {
    for(sInt end=0;end<2;end++)
    {
      const sInt *p = &Edges[e][end*3];
      v.AddTail(bx[p[0]]); v.AddTail(by[p[1]]); v.AddTail(bz[p[2]]);
      v.AddTail(0.85f); v.AddTail(0.55f); v.AddTail(0.20f);
    }
    LineVerts += 2;
  }

  if(!LineVao) glGenVertexArrays(1,&LineVao);
  if(!LineVbo) glGenBuffers(1,&LineVbo);

  glBindVertexArray(LineVao);
  glBindBuffer(GL_ARRAY_BUFFER,LineVbo);
  glBufferData(GL_ARRAY_BUFFER,v.GetCount()*sizeof(sF32),&v[0],GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(sF32),(void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(sF32),
    (void *)(3*sizeof(sF32)));
  glBindVertexArray(0);
}

/****************************************************************************/

void wMeshView::Fit()
{
  Yaw = 0.6f;
  Pitch = 0.5f;
  Distance = 2.6f;
}

/****************************************************************************/

sBool wMeshView::EnsureFbo(sInt w,sInt h)
{
  if(w<1 || h<1)
    return 0;
  if(Fbo && FboW==w && FboH==h)
    return 1;

  if(!Fbo)      glGenFramebuffers(1,&Fbo);
  if(!ColorTex) glGenTextures(1,&ColorTex);
  if(!DepthBuf) glGenRenderbuffers(1,&DepthBuf);

  glBindTexture(GL_TEXTURE_2D,ColorTex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D,0);

  glBindRenderbuffer(GL_RENDERBUFFER,DepthBuf);
  glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,w,h);
  glBindRenderbuffer(GL_RENDERBUFFER,0);

  glBindFramebuffer(GL_FRAMEBUFFER,Fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,
    ColorTex,0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,
    DepthBuf);

  const sU32 status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER,0);

  if(status!=GL_FRAMEBUFFER_COMPLETE)
  {
    sPrintF(L"meshview: framebuffer %d x %d is incomplete (0x%04x)\n",w,h,status);
    return 0;
  }

  FboW = w;
  FboH = h;
  return 1;
}

/****************************************************************************/

sU32 wMeshView::Draw(sInt w,sInt h)
{
  if(Empty || !Program || !EnsureFbo(w,h))
    return 0;

  const sVector31 centre((Lo.x+Hi.x)*0.5f,(Lo.y+Hi.y)*0.5f,(Lo.z+Hi.z)*0.5f);
  const sF32 sx = Hi.x-Lo.x, sy = Hi.y-Lo.y, sz = Hi.z-Lo.z;
  const sF32 radius = sMax(0.001f,0.5f*sSqrt(sx*sx + sy*sy + sz*sz));
  const sF32 dist = radius*Distance;

  // Orbit. Pitch is clamped short of the poles so the up vector never becomes
  // parallel to the view direction, which would make the basis degenerate.
  const sF32 pitch = sClamp(Pitch,-1.5f,1.5f);
  sVector30 eye;
  eye.x = sSin(Yaw)*sCos(pitch);
  eye.y = sSin(pitch);
  eye.z = sCos(Yaw)*sCos(pitch);

  sVector31 eyepos;
  eyepos.x = centre.x + eye.x*dist;
  eyepos.y = centre.y + eye.y*dist;
  eyepos.z = centre.z + eye.z*dist;

  // A look-at and a perspective, written out rather than taken from Altona: its
  // camera helpers live in the render library, which this build does not have,
  // and the matrix convention there is row-vector D3D-style. Being explicit is
  // shorter than reconciling the two.
  sVector30 fwd(centre.x-eyepos.x,centre.y-eyepos.y,centre.z-eyepos.z);
  fwd.Unit();
  sVector30 up(0,1,0);
  sVector30 right; right.Cross(fwd,up); right.Unit();
  up.Cross(right,fwd); up.Unit();

  const sF32 aspect = sF32(w)/sF32(sMax(1,h));
  const sF32 zn = sMax(0.001f,radius*0.05f);
  const sF32 zf = dist + radius*8.0f;
  const sF32 f = 1.0f/sTan(0.5f*0.9f);       // ~51 degrees vertical

  // VIEW AND PROJECTION SEPARATELY, THEN MULTIPLIED. The first version fused
  // them by hand into one 16-float initialiser and got three signs wrong — the
  // w row came out negated, so every vertex had w < 0, was clipped, and the pane
  // rendered black with no error anywhere. The two-step form is longer and the
  // mistake is not available in it.
  //
  // Column-major throughout: element [col*4+row], which is what
  // glUniformMatrix4fv with transpose=GL_FALSE expects.

  // View: world -> eye, with the camera looking down -Z.
  sF32 v[16];
  v[ 0]=right.x; v[ 4]=right.y; v[ 8]=right.z; v[12]=-(right.x*eyepos.x+right.y*eyepos.y+right.z*eyepos.z);
  v[ 1]=up.x;    v[ 5]=up.y;    v[ 9]=up.z;    v[13]=-(up.x*eyepos.x+up.y*eyepos.y+up.z*eyepos.z);
  v[ 2]=-fwd.x;  v[ 6]=-fwd.y;  v[10]=-fwd.z;  v[14]=  fwd.x*eyepos.x+fwd.y*eyepos.y+fwd.z*eyepos.z;
  v[ 3]=0;       v[ 7]=0;       v[11]=0;       v[15]=1;

  // Projection: the standard GL perspective, mapping eye -Z to clip +W.
  sF32 p[16];
  for(sInt i=0;i<16;i++)
    p[i] = 0;
  p[ 0] = f/aspect;
  p[ 5] = f;
  p[10] = -(zf+zn)/(zf-zn);
  p[11] = -1.0f;
  p[14] = -(2.0f*zf*zn)/(zf-zn);

  // m = p * v, in column-major terms m[c*4+r] = sum_k p[k*4+r] * v[c*4+k].
  sF32 m[16];
  for(sInt c=0;c<4;c++)
  {
    for(sInt r=0;r<4;r++)
    {
      sF32 s = 0;
      for(sInt k=0;k<4;k++)
        s += p[k*4+r] * v[c*4+k];
      m[c*4+r] = s;
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER,Fbo);
  glViewport(0,0,w,h);
  glClearColor(0.09f,0.10f,0.12f,1.0f);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDisable(GL_CULL_FACE);            // open meshes are legal, see the shader

  if(ShowGrid || ShowBBox)
  {
    glUseProgram(LineProgram);
    glUniformMatrix4fv(glGetUniformLocation(LineProgram,"uViewProj"),1,
      GL_FALSE,m);
    glBindVertexArray(LineVao);
    // The grid is the first (2*lines+1)*4 vertices, the box the last 24, so
    // either can be drawn without a second buffer.
    const sInt boxverts = 24;
    const sInt gridverts = LineVerts - boxverts;
    if(ShowGrid && gridverts>0)
      glDrawArrays(GL_LINES,0,gridverts);
    if(ShowBBox)
      glDrawArrays(GL_LINES,gridverts,boxverts);
    glBindVertexArray(0);
  }

  glUseProgram(Program);
  glUniformMatrix4fv(glGetUniformLocation(Program,"uViewProj"),1,GL_FALSE,m);

  const sF32 light[3] = { 0.45f,0.75f,0.50f };
  const sF32 eyeu[3] = { eyepos.x,eyepos.y,eyepos.z };
  const sF32 col[3] = { 0.72f,0.74f,0.78f };
  glUniform3fv(glGetUniformLocation(Program,"uLightDir"),1,light);
  glUniform3fv(glGetUniformLocation(Program,"uEye"),1,eyeu);
  glUniform3fv(glGetUniformLocation(Program,"uColor"),1,col);

  if(Wireframe)
    glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);

  glBindVertexArray(Vao);
  glDrawElements(GL_TRIANGLES,Tris*3,GL_UNSIGNED_INT,0);
  glBindVertexArray(0);

  if(Wireframe)
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);

  // Everything ImGui's backend assumes is restored. It sets most of its own
  // state per frame, but not the framebuffer binding or the depth test, and
  // leaving either would show up as the whole UI vanishing.
  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER,0);

  return ColorTex;
}

/****************************************************************************/

// Evaluates the operator and uploads, but only when something changed —
// re-uploading every frame would rebuild the GL buffers 60 times a second for a
// mesh that has not moved. Same caching rule as wPreview.

void wMeshView::DrawPane(wOp *op,sInt revision)
{
  if(op!=ShownOp || revision!=ShownRevision)
  {
    ShownOp = op;
    ShownRevision = revision;

    Wz4Mesh *mesh = 0;
    wObject *obj = op ? Doc->CalcOp(op) : 0;
    wType *meshtype = Doc ? Doc->FindType(L"Wz4Mesh") : 0;
    if(obj && meshtype && obj->IsType(meshtype))
      mesh = (Wz4Mesh *)obj;

    Upload(mesh);

    // NOT released, and that is deliberate. The first version released the
    // object after uploading, reasoning that the GL buffers hold copies — and
    // the NEXT frame's evaluation then came back empty, because the document's
    // cache and this pointer are not independent references. wPreview::Upload
    // does not release either, and it is the tested precedent in this editor.
    //
    // Whether Execute hands out a reference the caller should own is a real
    // question — the weak-op loop in wDocument::CalcOp releases its results —
    // but answering it belongs with the caching model, not with a viewer.

    // Printed on change only, not per frame. This is the line the screenshot
    // runner asserts, and it is why an empty pane cannot pass the gate.
    sString<128> what;
    Describe(what);
    sPrintF(L"%s\n",(const sChar *)what);
  }

  // --- toolbar --------------------------------------------------------------

  if(ImGui::Button("fit"))
    Fit();
  ImGui::SameLine();
  ImGui::Checkbox("wire",&Wireframe);
  ImGui::SameLine();
  ImGui::Checkbox("grid",&ShowGrid);
  ImGui::SameLine();
  ImGui::Checkbox("bbox",&ShowBBox);

  if(Empty)
  {
    ImGui::TextDisabled("no mesh");
    return;
  }

  ImGui::SameLine();
  ImGui::Text("%d v  %d tri",Verts,Tris);

  // --- the viewport ---------------------------------------------------------

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if(avail.x<8.0f || avail.y<8.0f)
    return;

  // Sized in PIXELS, not points. On a retina display the two differ by the
  // framebuffer scale, and a points-sized target renders visibly soft — the
  // same distinction the screenshot path already has to make.
  const sF32 scale = ImGui::GetIO().DisplayFramebufferScale.x>0.0f
    ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
  const sInt pw = sInt(avail.x*scale);
  const sInt ph = sInt(avail.y*scale);

  const sU32 tex = Draw(pw,ph);
  if(!tex)
  {
    ImGui::TextDisabled("no framebuffer");
    return;
  }

  // A GL texture has its origin bottom-left and ImGui expects top-left, so the
  // V coordinate is flipped. Getting this wrong renders the mesh upside down,
  // which looks like a camera bug rather than a UV one.
  ImGui::Image((ImTextureID)(sDInt)tex,avail,ImVec2(0,1),ImVec2(1,0));

  // Orbit and dolly, on the image itself so the toolbar stays clickable.
  if(ImGui::IsItemHovered())
  {
    const ImGuiIO &io = ImGui::GetIO();
    if(ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
      Yaw   -= io.MouseDelta.x*0.01f;
      Pitch += io.MouseDelta.y*0.01f;
      Pitch = sClamp(Pitch,-1.5f,1.5f);
    }
    if(io.MouseWheel!=0.0f)
    {
      // Multiplicative, so a wheel click covers the same visual distance at any
      // zoom level. Clamped so the camera cannot pass through the mesh or
      // retreat until it is a dot.
      Distance *= (io.MouseWheel>0.0f) ? 0.88f : 1.0f/0.88f;
      Distance = sClamp(Distance,0.15f,40.0f);
    }
  }
}

/****************************************************************************/

void wMeshView::Describe(const sStringDesc &out) const
{
  sString<128> buf;
  if(Empty)
  {
    buf.PrintF(L"meshview: empty");
  }
  else
  {
    buf.PrintF(L"meshview: %d vertices, %d triangles uploaded (%d quad(s) split)",
      Verts,Tris,Quads);
  }
  sCopyString(out,buf);
}

/****************************************************************************/
