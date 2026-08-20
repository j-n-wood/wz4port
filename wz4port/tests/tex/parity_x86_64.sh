#!/bin/sh
# SSE2-versus-NEON bit parity, stage 4.4's sharpest correctness signal.
#
#   sh wz4port/tests/tex/parity_x86_64.sh
#
# The mechanism is the goldens themselves and nothing else. They are checked in,
# they were produced by the arm64 NEON path, and every case compares against them
# byte for byte plus a checksum over all 16 bits. So building an x86-64 slice —
# where wz4port/compat/include/simd_compat.hpp resolves to the REAL <emmintrin.h>
# rather than sse2neon — and running the same suite is a direct test that the
# sse2neon translation is faithful. No separate comparison harness is needed.
#
# On Apple silicon the x86-64 binaries run under Rosetta 2. Worth stating plainly:
# the code path compiled is genuine SSE2, which is the thing being verified, but
# the instructions are executed by Rosetta's translation rather than by Intel
# silicon. Running this on a real x86-64 Linux box remains worth doing once.
#
# What this found the first time it was run, on 87 cases:
#
#   - Eight operators diverged, every one of them float-touching. The cause was
#     FMA contraction, not sse2neon: clang fuses a*b+c into a single FMA where the
#     target has one, and an FMA rounds once where two operations round twice.
#     -ffp-contract=off in CMakeLists.txt fixes it and the two agree exactly.
#   - Seven of those eight had BYTE-IDENTICAL images and differed only in the
#     locked checksum, because the divergence was below 8-bit output precision.
#     An image-only golden would have reported full parity.
#   - A real bug in our own compat header: `#define stat64 stat` collides with the
#     x86-64 macOS SDK's own `struct stat64`, which arm64 does not declare.

set -e

root=$(cd "$(dirname "$0")/../../.." && pwd)
build="$root/wz4port/build-x64"

if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
  if ! /usr/bin/pgrep -q oahd; then
    echo "Rosetta 2 does not appear to be running; x86-64 binaries will not execute."
    echo "Install it with: softwareupdate --install-rosetta"
    exit 1
  fi
  arch_args="-DCMAKE_OSX_ARCHITECTURES=x86_64"
else
  arch_args=""
fi

echo "==> configuring $build"
cmake -S "$root/wz4port" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug $arch_args

echo "==> building"
ninja -C "$build"

echo "==> confirming the binary really is x86-64 and not the host architecture"
file "$build/wz4gen" | grep -q x86_64 || {
  echo "wz4gen is not x86_64 — the parity check would compare NEON against NEON"
  exit 1
}

echo "==> running the suite against the arm64-generated goldens"
ctest --test-dir "$build" --output-on-failure

echo
echo "SSE2 and NEON agree, byte for byte, over every locked case."
