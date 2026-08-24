/****************************************************************************/
/***                                                                      ***/
/***   OpenGL 3.3 entry points for the editor — wz4port                    ***/
/***                                                                      ***/
/****************************************************************************/
//
// Stage 6.4. The editor's context is GL 3.3 core (main.cpp:919), and its GL use
// up to now was glReadPixels and glPixelStorei for the screenshot — both GL 1.1,
// which every platform's system header declares. Shaders, vertex arrays and
// framebuffers are not, and on Linux nothing past 1.1 is even linkable without
// asking the driver for a function pointer.
//
// This does NOT add a loader. ImGui already vendors one —
// backends/imgui_impl_opengl3_loader.h — whose implementation is behind
// `#ifdef IMGL3W_IMPL`, defined only in imgui_impl_opengl3.cpp:185, while its
// declarations are unconditional: the glXxx names become macros over
// `imgl3wProcs.gl.Xxx`, and that union has external linkage.
//
//     $ nm -g libimgui.a | grep imgl3wProcs
//     0000000000021770 S _imgl3wProcs
//
// So including the header WITHOUT that define binds to the single copy already
// compiled into libimgui.a, which ImGui_ImplOpenGL3_Init has already
// initialised. One loader, one initialisation, no per-platform #if, and the
// mechanism is upstream-tested wherever ImGui is.
//
// TWO ALTERNATIVES REJECTED
//
//   <OpenGL/gl3.h>  — needs nothing on macOS, which is the primary target, but
//                     leaves Linux requiring a loader anyway. It solves half the
//                     problem and hides the other half.
//   a hand-written glfwGetProcAddress table — about 45 function pointers of
//                     boilerplate, of which the Linux half could not be tested
//                     here. On a project whose rule is verify-don't-assume,
//                     writing untested platform code is the wrong trade.
//
// ORDERING. This must be included BEFORE any GL type or function is named, and
// the push_macro/pop_macro dance is here for the same reason as imgui_wz4.hpp:
// Altona's `#define new` mangles any header declaring an operator new. The
// loader is pure C and declares none, but wrapping it costs nothing and means
// the rule for "third-party header in the editor" has no exceptions to remember.

#ifndef FILE_WZ4PORT_EDITOR_GL_WZ4_HPP
#define FILE_WZ4PORT_EDITOR_GL_WZ4_HPP

#include "base/types.hpp"       // sBool

#pragma push_macro("new")
#undef new

// Deliberately without IMGL3W_IMPL — see above. Defining it here would compile a
// second copy of the loader and duplicate imgl3wProcs at link time.
#include "backends/imgui_impl_opengl3_loader.h"

#pragma pop_macro("new")

/****************************************************************************/
/***   the eleven entry points the loader does not carry                   ***/
/****************************************************************************/
//
// The plan for 6.4 assumed the vendored loader covered GL 3.3. It does not: it
// is GENERATED, stripped to the symbols ImGui itself references, and the header
// says as much — "You may need to regenerate imgui_impl_opengl3_loader.h to add
// new symbols" (imgui_impl_opengl3.cpp:181).
//
// Measured, the split is clean and mostly in our favour. ImGui draws with
// shaders, vertex arrays and buffers, so all of that is present:
//
//   present   glCreateShader, glShaderSource, glCompileShader, glCreateProgram,
//             glLinkProgram, glUseProgram, glGetUniformLocation,
//             glUniformMatrix4fv, glGenVertexArrays, glBindVertexArray,
//             glGenBuffers, glBufferData, glVertexAttribPointer,
//             glDrawElements, glDrawArrays, glPolygonMode, ...
//
//   ABSENT    the entire framebuffer and renderbuffer family, and glUniform3fv
//             — ImGui renders to whatever target it is given and has no use for
//             either.
//
// So this supplements rather than replaces: eleven pointers fetched through
// glfwGetProcAddress, which is the same mechanism the loader uses and works on
// both target platforms. Extending the vendored header instead was rejected —
// it is generated, so an edit becomes a fork that the next ImGui bump silently
// reverts.
//
// They carry the real GL names, which is safe precisely because the loader does
// not define them. Include this header before any system GL header in the same
// translation unit; meshview.cpp is the only consumer and includes nothing else.

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER              0x8D40
#define GL_RENDERBUFFER             0x8D41
#define GL_COLOR_ATTACHMENT0        0x8CE0
#define GL_DEPTH_ATTACHMENT         0x8D00
#define GL_DEPTH_COMPONENT24        0x81A6
#define GL_FRAMEBUFFER_COMPLETE     0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8                    0x8058
#endif

// The loader's enum list is stripped the same way its function list is, so these
// are absent too — all GL 1.1 constants that ImGui simply never names.
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW              0x88E4
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT         0x00000100
#endif
#ifndef GL_LESS
#define GL_LESS                     0x0201
#endif
#ifndef GL_LINES
#define GL_LINES                    0x0001
#endif
#ifndef GL_LINE
#define GL_LINE                     0x1B01
#endif
#ifndef GL_FILL
#define GL_FILL                     0x1B02
#endif

extern void (*glGenFramebuffers)(int,unsigned int *);
extern void (*glBindFramebuffer)(unsigned int,unsigned int);
extern void (*glDeleteFramebuffers)(int,const unsigned int *);
extern void (*glFramebufferTexture2D)(unsigned int,unsigned int,unsigned int,
                                      unsigned int,int);
extern void (*glGenRenderbuffers)(int,unsigned int *);
extern void (*glBindRenderbuffer)(unsigned int,unsigned int);
extern void (*glDeleteRenderbuffers)(int,const unsigned int *);
extern void (*glRenderbufferStorage)(unsigned int,unsigned int,int,int);
extern void (*glFramebufferRenderbuffer)(unsigned int,unsigned int,
                                         unsigned int,unsigned int);
extern unsigned int (*glCheckFramebufferStatus)(unsigned int);
extern void (*glUniform3fv)(int,int,const float *);

// GL 1.1, and absent for the same reason: ImGui draws indexed triangles and
// never sets a depth function, so it references neither.
extern void (*glDepthFunc)(unsigned int);
extern void (*glDrawArrays)(unsigned int,int,int);

// Call once, after the GL context exists. Returns 0 and says which symbol was
// missing if the driver does not have one — a null pointer call would be a
// segfault with no explanation, and this whole file is here because a stripped
// loader failed quietly enough to be mistaken for a plan that worked.
sBool wGlLoadExtras();

/****************************************************************************/

#endif  // FILE_WZ4PORT_EDITOR_GL_WZ4_HPP
