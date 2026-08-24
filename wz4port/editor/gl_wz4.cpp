/****************************************************************************/
/***                                                                      ***/
/***   The GL entry points ImGui's stripped loader does not carry           ***/
/***                                                                      ***/
/****************************************************************************/
//
// See gl_wz4.hpp for why these eleven exist and the rest do not.

#include "base/types.hpp"
#include "base/system.hpp"
#include "gl_wz4.hpp"

#include <GLFW/glfw3.h>

/****************************************************************************/

void (*glGenFramebuffers)(int,unsigned int *) = 0;
void (*glBindFramebuffer)(unsigned int,unsigned int) = 0;
void (*glDeleteFramebuffers)(int,const unsigned int *) = 0;
void (*glFramebufferTexture2D)(unsigned int,unsigned int,unsigned int,
                               unsigned int,int) = 0;
void (*glGenRenderbuffers)(int,unsigned int *) = 0;
void (*glBindRenderbuffer)(unsigned int,unsigned int) = 0;
void (*glDeleteRenderbuffers)(int,const unsigned int *) = 0;
void (*glRenderbufferStorage)(unsigned int,unsigned int,int,int) = 0;
void (*glFramebufferRenderbuffer)(unsigned int,unsigned int,unsigned int,
                                  unsigned int) = 0;
unsigned int (*glCheckFramebufferStatus)(unsigned int) = 0;
void (*glUniform3fv)(int,int,const float *) = 0;
void (*glDepthFunc)(unsigned int) = 0;
void (*glDrawArrays)(unsigned int,int,int) = 0;

/****************************************************************************/

// glfwGetProcAddress rather than dlsym or a platform GL header: GLFW already
// owns the context, so it already knows how to resolve against it, and using the
// same route the vendored loader uses keeps this to one mechanism instead of two.

static void *Get(const char *name,sBool &ok)
{
  void *p = (void *)glfwGetProcAddress(name);
  if(!p)
  {
    sChar wide[128];
    wide[0] = 0;
    sCopyStringFromUTF8(wide,name,sCOUNTOF(wide));
    sPrintF(L"gl: %s is not available in this context\n",wide);
    ok = 0;
  }
  return p;
}

// The casts are through void * deliberately. Converting an object pointer to a
// function pointer is not something C++ guarantees, but it is what every GL
// loader in existence does and what glfwGetProcAddress is for; going via the
// typed GLFWglproc would need a cast of the same kind anyway.
#define GET(fn) *(void **)&fn = Get(#fn,ok)

sBool wGlLoadExtras()
{
  sBool ok = 1;

  GET(glGenFramebuffers);
  GET(glBindFramebuffer);
  GET(glDeleteFramebuffers);
  GET(glFramebufferTexture2D);
  GET(glGenRenderbuffers);
  GET(glBindRenderbuffer);
  GET(glDeleteRenderbuffers);
  GET(glRenderbufferStorage);
  GET(glFramebufferRenderbuffer);
  GET(glCheckFramebufferStatus);
  GET(glUniform3fv);
  GET(glDepthFunc);
  GET(glDrawArrays);

  if(!ok)
    sPrint(L"gl: the 3D mesh preview will be unavailable\n");

  return ok;
}

/****************************************************************************/
