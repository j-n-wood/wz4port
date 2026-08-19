/****************************************************************************/
/***                                                                      ***/
/***   SIMD parity test — wz4port                                         ***/
/***                                                                      ***/
/****************************************************************************/
//
// wz4frlib/wz3_bitmap_code.cpp implements the entire Werkkzeug4 texture
// generator with 338 calls to 43 distinct SSE2 integer intrinsics. On arm64
// those are supplied by sse2neon rather than by the CPU, so a semantic
// difference in any one of them would corrupt generated textures in ways
// that still look plausible.
//
// This checks each intrinsic against an independent scalar model, over
// pseudorandom inputs plus the edge values where saturating and rounding
// operations actually differ (0, 1, -1, 0x7fff, 0x8000, 0xffff).
//
// It is deliberately standalone — no Altona, no CMake-managed dependencies —
// so it can be run on x86-64 and arm64 and compared directly.
//
// Build/run:  ninja -C build simd_parity && ./build/simd_parity

#include "simd_compat.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>

/****************************************************************************/
/***   Harness                                                            ***/
/****************************************************************************/

static int g_fail = 0;
static int g_checks = 0;

static void report(const char *name, bool ok, const char *detail = nullptr)
{
  g_checks++;
  if (!ok) {
    g_fail++;
    std::printf("  FAIL  %-22s %s\n", name, detail ? detail : "");
  }
}

// Deterministic LCG so both architectures see identical inputs.
static uint32_t g_seed = 0x12345678u;
static uint16_t next16()
{
  g_seed = g_seed * 1664525u + 1013904223u;
  return (uint16_t)(g_seed >> 16);
}

// Edge values that matter for saturation / rounding / sign handling.
static const uint16_t kEdge[] = {
  0x0000, 0x0001, 0x7fff, 0x8000, 0x8001, 0xffff, 0x00ff, 0xff00,
};
static const int kEdgeN = (int)(sizeof(kEdge) / sizeof(kEdge[0]));

struct V128 { uint16_t u16[8]; };

static V128 to_v(__m128i x) { V128 v; _mm_storeu_si128((__m128i *)v.u16, x); return v; }
static __m128i from_v(const uint16_t *p) { return _mm_loadu_si128((const __m128i *)p); }

static bool same(__m128i got, const uint16_t *want)
{
  V128 g = to_v(got);
  return std::memcmp(g.u16, want, 16) == 0;
}

/****************************************************************************/
/***   Scalar models                                                      ***/
/****************************************************************************/

static int16_t sat_s16(int32_t v)
{
  if (v >  32767) return  32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

static uint16_t sat_u16(int32_t v)
{
  if (v > 65535) return 65535;
  if (v < 0)     return 0;
  return (uint16_t)v;
}

/****************************************************************************/
/***   Per-intrinsic checks                                               ***/
/****************************************************************************/

// One pass over a pair of 8x16-bit vectors, comparing every arithmetic
// intrinsic against its scalar model.
static void check_pair(const uint16_t *A, const uint16_t *B)
{
  __m128i a = from_v(A), b = from_v(B);
  uint16_t w[8];

  // --- addition / subtraction, wrapping and saturating ---------------------

  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(A[i] + B[i]);
  report("_mm_add_epi16", same(_mm_add_epi16(a, b), w));

  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(A[i] - B[i]);
  report("_mm_sub_epi16", same(_mm_sub_epi16(a, b), w));

  for (int i = 0; i < 8; i++)
    w[i] = (uint16_t)sat_s16((int32_t)(int16_t)A[i] + (int16_t)B[i]);
  report("_mm_adds_epi16", same(_mm_adds_epi16(a, b), w));

  for (int i = 0; i < 8; i++)
    w[i] = (uint16_t)sat_s16((int32_t)(int16_t)A[i] - (int16_t)B[i]);
  report("_mm_subs_epi16", same(_mm_subs_epi16(a, b), w));

  for (int i = 0; i < 8; i++) w[i] = sat_u16((int32_t)A[i] + B[i]);
  report("_mm_adds_epu16", same(_mm_adds_epu16(a, b), w));

  for (int i = 0; i < 8; i++) w[i] = sat_u16((int32_t)A[i] - B[i]);
  report("_mm_subs_epu16", same(_mm_subs_epu16(a, b), w));

  // --- rounding average (the classic off-by-one trap) ----------------------

  // Build with -DWZ4PORT_SIMD_MUTATE to confirm the harness is not vacuous:
  // this drops the rounding term, which must make _mm_avg_epu16 fail.
#ifdef WZ4PORT_SIMD_MUTATE
  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(((uint32_t)A[i] + B[i]) >> 1);
#else
  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(((uint32_t)A[i] + B[i] + 1) >> 1);
#endif
  report("_mm_avg_epu16", same(_mm_avg_epu16(a, b), w));

  // --- multiplies ----------------------------------------------------------

  for (int i = 0; i < 8; i++)
    w[i] = (uint16_t)(((int32_t)(int16_t)A[i] * (int16_t)B[i]) & 0xffff);
  report("_mm_mullo_epi16", same(_mm_mullo_epi16(a, b), w));

  for (int i = 0; i < 8; i++)
    w[i] = (uint16_t)((((int32_t)(int16_t)A[i] * (int16_t)B[i]) >> 16) & 0xffff);
  report("_mm_mulhi_epi16", same(_mm_mulhi_epi16(a, b), w));

  for (int i = 0; i < 8; i++)
    w[i] = (uint16_t)((((uint32_t)A[i] * B[i]) >> 16) & 0xffff);
  report("_mm_mulhi_epu16", same(_mm_mulhi_epu16(a, b), w));

  // madd: signed 16x16 products summed in adjacent pairs -> 4x int32
  {
    int32_t want32[4];
    for (int i = 0; i < 4; i++)
      want32[i] = (int32_t)(int16_t)A[2*i]   * (int16_t)B[2*i]
                + (int32_t)(int16_t)A[2*i+1] * (int16_t)B[2*i+1];
    int32_t got32[4];
    _mm_storeu_si128((__m128i *)got32, _mm_madd_epi16(a, b));
    report("_mm_madd_epi16", std::memcmp(got32, want32, 16) == 0);
  }

  // --- min / max / compare (all signed) ------------------------------------

  for (int i = 0; i < 8; i++)
    w[i] = (int16_t)A[i] > (int16_t)B[i] ? A[i] : B[i];
  report("_mm_max_epi16", same(_mm_max_epi16(a, b), w));

  for (int i = 0; i < 8; i++)
    w[i] = (int16_t)A[i] < (int16_t)B[i] ? A[i] : B[i];
  report("_mm_min_epi16", same(_mm_min_epi16(a, b), w));

  for (int i = 0; i < 8; i++)
    w[i] = (int16_t)A[i] > (int16_t)B[i] ? 0xffff : 0x0000;
  report("_mm_cmpgt_epi16", same(_mm_cmpgt_epi16(a, b), w));

  // --- bitwise -------------------------------------------------------------

  for (int i = 0; i < 8; i++) w[i] = A[i] & B[i];
  report("_mm_and_si128", same(_mm_and_si128(a, b), w));

  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(~A[i] & B[i]);
  report("_mm_andnot_si128", same(_mm_andnot_si128(a, b), w));

  for (int i = 0; i < 8; i++) w[i] = A[i] | B[i];
  report("_mm_or_si128", same(_mm_or_si128(a, b), w));

  for (int i = 0; i < 8; i++) w[i] = A[i] ^ B[i];
  report("_mm_xor_si128", same(_mm_xor_si128(a, b), w));

  // --- shifts (16-bit lanes) ----------------------------------------------

  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(A[i] << 3);
  report("_mm_slli_epi16", same(_mm_slli_epi16(a, 3), w));

  for (int i = 0; i < 8; i++) w[i] = (uint16_t)(A[i] >> 3);
  report("_mm_srli_epi16", same(_mm_srli_epi16(a, 3), w));

  // --- 32-bit lane operations ---------------------------------------------

  {
    int32_t a32[4], b32[4], want32[4], got32[4];
    std::memcpy(a32, A, 16);
    std::memcpy(b32, B, 16);

    for (int i = 0; i < 4; i++) want32[i] = a32[i] + b32[i];
    _mm_storeu_si128((__m128i *)got32, _mm_add_epi32(a, b));
    report("_mm_add_epi32", std::memcmp(got32, want32, 16) == 0);

    for (int i = 0; i < 4; i++) want32[i] = a32[i] - b32[i];
    _mm_storeu_si128((__m128i *)got32, _mm_sub_epi32(a, b));
    report("_mm_sub_epi32", std::memcmp(got32, want32, 16) == 0);

    for (int i = 0; i < 4; i++) want32[i] = a32[i] > b32[i] ? -1 : 0;
    _mm_storeu_si128((__m128i *)got32, _mm_cmpgt_epi32(a, b));
    report("_mm_cmpgt_epi32", std::memcmp(got32, want32, 16) == 0);

    for (int i = 0; i < 4; i++) want32[i] = (int32_t)((uint32_t)a32[i] << 5);
    _mm_storeu_si128((__m128i *)got32, _mm_slli_epi32(a, 5));
    report("_mm_slli_epi32", std::memcmp(got32, want32, 16) == 0);

    for (int i = 0; i < 4; i++) want32[i] = (int32_t)((uint32_t)a32[i] >> 5);
    _mm_storeu_si128((__m128i *)got32, _mm_srli_epi32(a, 5));
    report("_mm_srli_epi32", std::memcmp(got32, want32, 16) == 0);

    // arithmetic shift: sign must propagate
    for (int i = 0; i < 4; i++) want32[i] = a32[i] >> 5;
    _mm_storeu_si128((__m128i *)got32, _mm_srai_epi32(a, 5));
    report("_mm_srai_epi32", std::memcmp(got32, want32, 16) == 0);

    // saturating signed pack 32 -> 16: a in low lanes, b in high
    for (int i = 0; i < 4; i++) w[i]     = (uint16_t)sat_s16(a32[i]);
    for (int i = 0; i < 4; i++) w[4 + i] = (uint16_t)sat_s16(b32[i]);
    report("_mm_packs_epi32", same(_mm_packs_epi32(a, b), w));
  }

  // --- 64-bit lane shifts --------------------------------------------------

  {
    uint64_t a64[2], want64[2], got64[2];
    std::memcpy(a64, A, 16);

    for (int i = 0; i < 2; i++) want64[i] = a64[i] << 9;
    _mm_storeu_si128((__m128i *)got64, _mm_slli_epi64(a, 9));
    report("_mm_slli_epi64", std::memcmp(got64, want64, 16) == 0);

    for (int i = 0; i < 2; i++) want64[i] = a64[i] >> 9;
    _mm_storeu_si128((__m128i *)got64, _mm_srli_epi64(a, 9));
    report("_mm_srli_epi64", std::memcmp(got64, want64, 16) == 0);
  }

  // --- interleave ----------------------------------------------------------

  for (int i = 0; i < 4; i++) { w[2*i] = A[i]; w[2*i+1] = B[i]; }
  report("_mm_unpacklo_epi16", same(_mm_unpacklo_epi16(a, b), w));

  for (int i = 0; i < 4; i++) { w[2*i] = A[4+i]; w[2*i+1] = B[4+i]; }
  report("_mm_unpackhi_epi16", same(_mm_unpackhi_epi16(a, b), w));

  // --- shuffles (immediate operands must be compile-time constants) --------

  {
    // _MM_SHUFFLE(3,1,2,0) -> dst32[0]=src32[0], [1]=src32[2], [2]=src32[1], [3]=src32[3]
    int32_t a32[4], want32[4], got32[4];
    std::memcpy(a32, A, 16);
    want32[0] = a32[0]; want32[1] = a32[2]; want32[2] = a32[1]; want32[3] = a32[3];
    _mm_storeu_si128((__m128i *)got32, _mm_shuffle_epi32(a, _MM_SHUFFLE(3,1,2,0)));
    report("_mm_shuffle_epi32", std::memcmp(got32, want32, 16) == 0);
  }

  // low four 16-bit lanes reordered, high four untouched
  w[0] = A[2]; w[1] = A[0]; w[2] = A[3]; w[3] = A[1];
  for (int i = 4; i < 8; i++) w[i] = A[i];
  report("_mm_shufflelo_epi16", same(_mm_shufflelo_epi16(a, _MM_SHUFFLE(1,3,0,2)), w));

  for (int i = 0; i < 4; i++) w[i] = A[i];
  w[4] = A[6]; w[5] = A[4]; w[6] = A[7]; w[7] = A[5];
  report("_mm_shufflehi_epi16", same(_mm_shufflehi_epi16(a, _MM_SHUFFLE(1,3,0,2)), w));
}

// Load / store / set forms, checked once.
static void check_movement()
{
  uint16_t src[8] = { 0x1122, 0x3344, 0x5566, 0x7788,
                      0x99aa, 0xbbcc, 0xddee, 0xff00 };
  uint16_t w[8];

  // aligned load/store round-trip
  {
    alignas(16) uint16_t al[8];
    std::memcpy(al, src, 16);
    __m128i v = _mm_load_si128((const __m128i *)al);
    alignas(16) uint16_t out[8];
    _mm_store_si128((__m128i *)out, v);
    report("_mm_load_si128/_mm_store_si128", std::memcmp(out, src, 16) == 0);
  }

  // loadl: low 64 bits loaded, high 64 zeroed
  {
    std::memcpy(w, src, 8);
    std::memset(w + 4, 0, 8);
    report("_mm_loadl_epi64", same(_mm_loadl_epi64((const __m128i *)src), w));
  }

  // storel: only the low 64 bits are written
  {
    uint16_t out[8];
    std::memset(out, 0xa5, 16);
    _mm_storel_epi64((__m128i *)out, from_v(src));
    bool lo_ok = std::memcmp(out, src, 8) == 0;
    bool hi_untouched = true;
    for (int i = 4; i < 8; i++) if (out[i] != 0xa5a5) hi_untouched = false;
    report("_mm_storel_epi64", lo_ok && hi_untouched);
  }

  report("_mm_setzero_si128", same(_mm_setzero_si128(), (uint16_t[8]){0,0,0,0,0,0,0,0}));

  for (int i = 0; i < 8; i++) w[i] = 0xbeef;
  report("_mm_set1_epi16", same(_mm_set1_epi16((short)0xbeef), w));

  {
    int32_t want32[4] = { 0x0badf00d, 0x0badf00d, 0x0badf00d, 0x0badf00d }, got32[4];
    _mm_storeu_si128((__m128i *)got32, _mm_set1_epi32(0x0badf00d));
    report("_mm_set1_epi32", std::memcmp(got32, want32, 16) == 0);
  }

  // _mm_set_epi16 takes lanes 7..0 (highest first)
  w[0]=0x0007; w[1]=0x0006; w[2]=0x0005; w[3]=0x0004;
  w[4]=0x0003; w[5]=0x0002; w[6]=0x0001; w[7]=0x0000;
  report("_mm_set_epi16", same(_mm_set_epi16(0,1,2,3,4,5,6,7), w));

  // cvtsi32: scalar into low 32 bits, rest zeroed
  {
    int32_t want32[4] = { 0x11223344, 0, 0, 0 }, got32[4];
    _mm_storeu_si128((__m128i *)got32, _mm_cvtsi32_si128(0x11223344));
    report("_mm_cvtsi32_si128", std::memcmp(got32, want32, 16) == 0);
  }
}

/****************************************************************************/

int main()
{
  std::printf("wz4port SIMD parity test\n");
#if WZ4PORT_SIMD_NEON
  std::printf("backend: sse2neon (NEON)\n");
#else
  std::printf("backend: native SSE2\n");
#endif

  check_movement();

  // Edge-value cross product: every combination of the interesting values.
  for (int i = 0; i < kEdgeN; i++) {
    for (int j = 0; j < kEdgeN; j++) {
      uint16_t A[8], B[8];
      for (int k = 0; k < 8; k++) {
        A[k] = kEdge[(i + k) % kEdgeN];
        B[k] = kEdge[(j + k * 3) % kEdgeN];
      }
      check_pair(A, B);
    }
  }

  // Pseudorandom sweep.
  for (int iter = 0; iter < 2000; iter++) {
    uint16_t A[8], B[8];
    for (int k = 0; k < 8; k++) { A[k] = next16(); B[k] = next16(); }
    check_pair(A, B);
  }

  std::printf("%d checks, %d failures\n", g_checks, g_fail);
  if (g_fail == 0) std::printf("PASS\n");
  else             std::printf("FAIL\n");
  return g_fail ? 1 : 0;
}
