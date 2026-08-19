#############################################################################
##                                                                         ##
##  wz4port — portable Werkkzeug4 generators for macOS and Linux           ##
##                                                                         ##
##  Builds a narrow subset of Altona plus our own libraries and tools.     ##
##  Upstream sources under altona_wz4/ are compiled in place and are not   ##
##  modified; see wz4port/README.md and wz4port/patches/.                  ##
##                                                                         ##
#############################################################################

cmake_minimum_required(VERSION 3.20)
project(wz4port CXX C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Repository layout -------------------------------------------------------

get_filename_component(FR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(ALTONA_MAIN  "${FR_ROOT}/altona_wz4/altona/main")
set(ALTONA_TOOLS "${FR_ROOT}/altona_wz4/altona/tools")
set(WZ4_DIR      "${FR_ROOT}/altona_wz4/wz4")
set(COMPAT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/compat")

#############################################################################
##  Altona compile settings                                                ##
#############################################################################

# Altona is pre-C++11, exception-free, RTTI-free, and assumes a 2-byte
# wchar_t (sChar). -fshort-wchar delivers that and keeps L"..." literals
# working; it is safe here because the only three libc wide-char call sites
# in the tree are replaced with Altona's own UTF-8 converters (see
# docs/03-phase-toolchain.md, stage 1.4).
#
# The platform is left as sPLAT_LINUX on macOS. Altona's "LINUX" means
# "POSIX desktop", and 21 guards across the tree spell that as
# (sPLAT_WINDOWS || sPLAT_LINUX). Introducing a separate sPLAT_APPLE would
# require revisiting every one of them for no behavioural gain; the genuine
# macOS/Linux differences are handled per-file with __APPLE__.

add_library(altona_flags INTERFACE)

target_compile_options(altona_flags INTERFACE
  -fno-exceptions
  -fno-rtti
  -fshort-wchar
  -fno-strict-aliasing
  -Wno-invalid-offsetof
  -Wno-writable-strings
  # Altona predates C++11 user-defined literals and writes  "..."sTXT(x)
  # with no separating space (e.g. types.hpp:1445). Harmless, but C++11
  # makes it an error by default.
  -Wno-reserved-user-defined-literal
  -Wno-unused-value
  -Wno-parentheses
  -Wno-logical-op-parentheses
  -Wno-dangling-else
  -Wno-switch
)

# system_linux.cpp is compiled in place on macOS. This shim supplies the
# glibc extensions and Large File Support aliases it expects; it is inert on
# Linux. See wz4port/compat/include/wz4port_posix_compat.h.
target_compile_options(altona_flags INTERFACE
  -include "${COMPAT_DIR}/include/wz4port_posix_compat.h"
)

target_compile_definitions(altona_flags INTERFACE
  sCONFIG_RENDER_BLANK
  sCONFIG_OPTION_SHELL
  $<$<CONFIG:Debug>:sCONFIG_BUILD_DEBUG>
  $<$<NOT:$<CONFIG:Debug>>:sCONFIG_BUILD_RELEASE>
)

# compat/include must precede everything: it is how upstream's
# #include "../altona_config.hpp" finds wz4port/compat/altona_config.hpp
# without altona_config.hpp ever being written into altona_wz4/.
target_include_directories(altona_flags INTERFACE
  "${COMPAT_DIR}/include"
  "${ALTONA_MAIN}"
  "${FR_ROOT}/altona_wz4/altona"
)

#############################################################################
##  altona_base — shell subset (no window system, no graphics API)         ##
#############################################################################

# This is the set upstream's own base/Makefile.linux.boot builds, which is
# exactly what the host tools need. Excluded on purpose:
#   sound.cpp, devices_win.cpp, windows.cpp  — Windows-only bodies
#   windows_xlib.cpp                         — needs X11
#   graphics_dx9/dx11/ogl2/ogles2.cpp        — real renderers

add_library(altona_base STATIC
  "${ALTONA_MAIN}/base/types.cpp"
  "${ALTONA_MAIN}/base/types2.cpp"
  "${ALTONA_MAIN}/base/serialize.cpp"
  "${ALTONA_MAIN}/base/math.cpp"
  "${ALTONA_MAIN}/base/system.cpp"
  "${ALTONA_MAIN}/base/input2.cpp"
  "${ALTONA_MAIN}/base/system_linux.cpp"
  "${ALTONA_MAIN}/base/graphics.cpp"
  "${ALTONA_MAIN}/base/graphics_blank.cpp"
)

target_link_libraries(altona_base PUBLIC altona_flags)

if(APPLE)
  target_link_libraries(altona_base PUBLIC "-framework CoreFoundation")
else()
  target_link_libraries(altona_base PUBLIC pthread rt dl)
endif()

#############################################################################
##  altona_util — the slice the host tools need                            ##
#############################################################################

add_library(altona_util STATIC
  "${ALTONA_MAIN}/util/scanner.cpp"
  "${ALTONA_MAIN}/util/scanconfig.cpp"
)
target_link_libraries(altona_util PUBLIC altona_base)

#############################################################################
##  wz4ops — the .ops code generator (host tool)                           ##
#############################################################################

add_executable(wz4ops
  "${ALTONA_TOOLS}/wz4ops/main.cpp"
  "${ALTONA_TOOLS}/wz4ops/doc.cpp"
  "${ALTONA_TOOLS}/wz4ops/parse.cpp"
  "${ALTONA_TOOLS}/wz4ops/output.cpp"
  "${ALTONA_TOOLS}/wz4ops/wikitext.cpp"
)
target_include_directories(wz4ops PRIVATE "${ALTONA_TOOLS}")
target_link_libraries(wz4ops PRIVATE altona_util)

#############################################################################
##  .ops code generation                                                   ##
#############################################################################

set(WZ4_GEN_DIR "${CMAKE_BINARY_DIR}/generated")

# wz4_add_ops(<out_var> <subdir> <path/to/name.ops>)
#
# Two things to know about wz4ops:
#
#  1. It derives its output paths from the input path by swapping the
#     extension, so it always writes next to its input. We therefore copy
#     the .ops into the build tree first and generate there, which keeps
#     altona_wz4/ clean.
#
#  2. It also derives the registration function names from the input path
#     with the extension stripped — passing "a/b/basic_ops.ops" yields
#     "AddTypes_a/b/basic_ops", which does not compile. It must be invoked
#     with a BARE FILENAME from the file's own directory so the names come
#     out as AddTypes_basic_ops / AddOps_basic_ops, which is what the
#     sREGOPS macro (wz4lib/doc_core.hpp:70-74) expands to.

function(wz4_add_ops out_var subdir ops_path)
  get_filename_component(_stem "${ops_path}" NAME_WE)
  set(_dir "${WZ4_GEN_DIR}/${subdir}")
  set(_cpp "${_dir}/${_stem}.cpp")
  set(_hpp "${_dir}/${_stem}.hpp")

  add_custom_command(
    OUTPUT  "${_cpp}" "${_hpp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ops_path}" "${_dir}/${_stem}.ops"
    COMMAND wz4ops "${_stem}.ops"
    WORKING_DIRECTORY "${_dir}"
    DEPENDS wz4ops "${ops_path}"
    COMMENT "wz4ops ${subdir}/${_stem}.ops"
    VERBATIM
  )

  set(${out_var} "${_cpp}" "${_hpp}" PARENT_SCOPE)
endfunction()

# Generated headers are included as "wz4frlib/x_ops.hpp" / "wz4lib/x_ops.hpp",
# so the generated root goes on the include path.
target_include_directories(altona_flags INTERFACE "${WZ4_GEN_DIR}")

wz4_add_ops(GEN_BASIC  wz4lib   "${ALTONA_MAIN}/wz4lib/basic_ops.ops")
wz4_add_ops(GEN_BITMAP wz4frlib "${WZ4_DIR}/wz4frlib/wz3_bitmap_ops.ops")

# Phase 1 gate: proves the generator runs natively and produces the
# registration entry points the runtime expects. The generated sources are
# not compiled until phase 2, when the headless op runtime exists.
add_custom_target(wz4ops_gate ALL DEPENDS ${GEN_BASIC} ${GEN_BITMAP})

#############################################################################
##  headless_core_gate — phase 2 stage 2.1                                 ##
#############################################################################

# Compiles a translation unit that includes wz4lib/doc_core.hpp and nothing
# else, with tests/gui_poison.h force-included ahead of it. The poison header
# makes any identifier from the real gui/ or util/shaders.hpp a compile error,
# so this target fails the moment the document model reacquires a GUI
# dependency. See wz4port/patches/04-doc-headless-split.md.
#
# OBJECT, not an executable: wz4lib/doc.cpp is not built until stage 2.4, so
# there is nothing to link against yet. Everything the gate checks is checked
# by the compiler.

add_library(headless_core_gate OBJECT
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless_core.cpp"
)
target_link_libraries(headless_core_gate PRIVATE altona_flags)
# SHELL: keeps the flag and its argument together. Without it CMake sees two
# bare "-include" tokens on this target's command line, deduplicates them, and
# orphans altona_flags' wz4port_posix_compat.h path.
target_compile_options(headless_core_gate PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)

#############################################################################
##  SIMD parity test                                                       ##
#############################################################################

# Standalone on purpose: no Altona, no shared flags. wz3_bitmap_code.cpp's
# 43 SSE2 intrinsics are checked against scalar models so that the arm64
# sse2neon translation is verified before the texture library depends on it.
# Runs on both architectures; both must pass.

add_executable(simd_parity "${CMAKE_CURRENT_SOURCE_DIR}/tests/simd_parity.cpp")
target_include_directories(simd_parity PRIVATE
  "${COMPAT_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

enable_testing()
add_test(NAME simd_parity COMMAND simd_parity)
