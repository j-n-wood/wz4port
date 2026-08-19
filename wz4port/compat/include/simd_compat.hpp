/****************************************************************************/
/***                                                                      ***/
/***   SSE2 -> NEON compatibility shim — wz4port                          ***/
/***                                                                      ***/
/****************************************************************************/
//
// wz4frlib/wz3_bitmap_code.cpp (the whole texture generator) includes
// <emmintrin.h> and uses 338 SSE2 intrinsic calls across 43 distinct
// intrinsics — all SSE2 *integer* operations, e.g. _mm_mulhi_epi16,
// _mm_adds_epi16, _mm_shufflelo_epi16, _mm_packs_epi32.
//
// Apple clang on arm64 does not provide <emmintrin.h>. sse2neon covers the
// complete SSE2 integer set, so the shim is a drop-in.
//
// Not needed until phase 4 (texture library). Include this instead of
// <emmintrin.h>.
//
// Vendored: wz4port/third_party/sse2neon.h, pinned to tag v1.9.1
// (https://github.com/DLTcollab/sse2neon, MIT). Pinned rather than tracking
// master so that the SSE2-vs-NEON bit-parity check in phase 4 is comparing
// against a fixed translation, and so Linux and macOS builds agree.
//
// Coverage is verified by wz4port/tests/simd_parity.cpp, which checks all 43
// intrinsics against independent scalar models — including the saturating,
// rounding and sign-propagating cases where a translation could plausibly
// differ without being obviously wrong.

#ifndef FILE_WZ4PORT_SIMD_COMPAT_HPP
#define FILE_WZ4PORT_SIMD_COMPAT_HPP

#if defined(__aarch64__) || defined(_M_ARM64)

  #define SSE2NEON_SUPPRESS_WARNINGS 1
  #include "sse2neon.h"
  #define WZ4PORT_SIMD_NEON 1

#else

  #include <emmintrin.h>
  #define WZ4PORT_SIMD_NEON 0

#endif

/****************************************************************************/

#endif  // FILE_WZ4PORT_SIMD_COMPAT_HPP
