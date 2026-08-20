/****************************************************************************/
/***                                                                      ***/
/***   POSIX/glibc compatibility shim — wz4port                           ***/
/***                                                                      ***/
/****************************************************************************/
//
// Force-included into every translation unit (see wz4port/CMakeLists.txt).
//
// altona/main/base/system_linux.cpp was written against glibc and uses a
// number of GNU extensions and Large File Support aliases that macOS does
// not provide. On macOS the plain functions are already 64-bit — off_t is
// 64-bit unconditionally — so the *64 aliases map straight through.
//
// This exists so that system_linux.cpp can be compiled in place, unmodified,
// on macOS. Copying the file (2,274 lines) would have duplicated upstream
// and guaranteed drift. The remaining incompatibilities are genuine type
// errors that no macro can fix; those are a four-line upstream patch,
// recorded in wz4port/patches/01-pthread-t-casts.md.
//
// On Linux this header is inert.

#ifndef FILE_WZ4PORT_POSIX_COMPAT_H
#define FILE_WZ4PORT_POSIX_COMPAT_H

#ifdef __APPLE__

/****************************************************************************/
/***   Large File Support aliases                                         ***/
/****************************************************************************/

// macOS has no *64 variants because the base functions are already 64-bit.

#define lseek64      lseek
#define mmap64       mmap
#define ftruncate64  ftruncate
#define stat64       stat
#define fstat64      fstat
#define lstat64      lstat
#define off64_t      off_t

// Not defined on macOS; only meaningful to glibc.
#ifndef O_LARGEFILE
#define O_LARGEFILE  0
#endif

/****************************************************************************/
/***   GNU extensions                                                     ***/
/****************************************************************************/

#include <alloca.h>
#include <string.h>

// GNU strdupa: stack-allocated strdup. Must stay a macro — the alloca()
// storage has to belong to the caller's frame.
#ifndef strdupa
#define strdupa(s)                                        \
  (__extension__({                                        \
    const char *_wz4_s = (s);                             \
    size_t _wz4_n = strlen(_wz4_s) + 1;                   \
    char *_wz4_d = (char *)alloca(_wz4_n);                \
    (char *)memcpy(_wz4_d, _wz4_s, _wz4_n);               \
  }))
#endif

// GNU pthread_yield; the POSIX spelling is sched_yield.
#include <sched.h>
#ifndef pthread_yield
#define pthread_yield  sched_yield
#endif

#endif  // __APPLE__

/****************************************************************************/
/***   MSVC-isms                                                          ***/
/****************************************************************************/

// __assume(false) marks an unreachable default: case. MSVC-only; clang spells
// the same promise __builtin_assume. Used once, at
// wz4frlib/wz3_bitmap_code.cpp:637, in the inner loop of the Perlin generator,
// where it lets the compiler drop a bounds check.
//
// A macro here rather than a patch: the meaning is identical on both compilers
// and there is nothing to explain at the call site.
#ifndef __assume
#define __assume(x)  __builtin_assume(x)
#endif

// MSVC's forced inline. Altona spells this sINLINE and only maps it to
// __forceinline on the MSVC branch (base/types.hpp:292); wz4frlib/genvector.cpp
// writes the raw keyword instead. `inline` is the honest translation — clang's
// always_inline would change codegen, not just naming.
#ifndef __forceinline
#define __forceinline  inline
#endif

// The Windows calling convention, meaningless on both of this port's targets.
// wz3_bitmap_code.cpp writes the raw keyword on six functions rather than
// Altona's own sSTDCALL, which base/types.hpp:573 already defines empty for
// POSIX. Dropping it changes nothing about how the code runs.
#if !defined(_WIN32) && !defined(__stdcall)
#define __stdcall
#endif

/****************************************************************************/

#endif  // FILE_WZ4PORT_POSIX_COMPAT_H
