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
  # Determinism across architectures, not performance. clang defaults to
  # -ffp-contract=fast and will fuse a*b+c into a single FMA where the target
  # has one — arm64 always does, this x86-64 target does not — and an FMA
  # rounds once where the separate operations round twice. That is enough to
  # move a float by one ULP, which the texture engine then quantises into a
  # different 16-bit sample.
  #
  # Found by building an x86-64 slice and comparing it against arm64 goldens
  # (stage 4.4): eight operators diverged, every one of them float-touching —
  # Perlin's and GlowRect's sFPow gamma tables, Unwrap's coordinate maths, and
  # all of the lighting. With contraction off the two architectures agree
  # exactly. See docs/06-phase-texture.md.
  -ffp-contract=off
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
##  opsmeta — .ops -> metadata JSON (host tool, ours)                      ##
#############################################################################

# Links wz4ops' PARSER only — parse.cpp and doc.cpp, which contain no
# references into output.cpp or wikitext.cpp. The C++ emitter is deliberately
# absent: anything that determines memory layout stays on wz4ops' single code
# path, and the JSON is our format to evolve. See docs/architecture.md A11.

add_executable(opsmeta
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/opsmeta/main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/opsmeta/emit.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/opsmeta/json.cpp"
  "${ALTONA_TOOLS}/wz4ops/doc.cpp"
  "${ALTONA_TOOLS}/wz4ops/parse.cpp"
)
# tools/wz4ops so that #include "doc.hpp" finds the parse tree, and our own
# directory for opsmeta.hpp / json.hpp.
target_include_directories(opsmeta PRIVATE
  "${ALTONA_TOOLS}/wz4ops"
  "${CMAKE_CURRENT_SOURCE_DIR}/tools/opsmeta"
)
target_link_libraries(opsmeta PRIVATE altona_util)

#############################################################################
##  .ops code generation                                                   ##
#############################################################################

set(WZ4_GEN_DIR    "${CMAKE_BINARY_DIR}/generated")
set(WZ4_GENHL_DIR  "${CMAKE_BINARY_DIR}/generated-headless")

# wz4_add_ops(<out_var> <subdir> <path/to/name.ops> [HEADLESS])
#
# HEADLESS passes -headless, which omits everything needing the gui or the
# scripting engine and generates into WZ4_GENHL_DIR instead. See
# wz4port/patches/05-wz4ops-headless.md.
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

  if("HEADLESS" IN_LIST ARGN)
    set(_dir "${WZ4_GENHL_DIR}/${subdir}")
    # The switch must follow the filename: Altona's shell parser treats the
    # token after a -switch as that switch's first parameter, so
    # "wz4ops -headless x.ops" leaves sGetShellParameter(0,0) empty.
    set(_args "${_stem}.ops" -headless)
    set(_what "wz4ops -headless")
  else()
    set(_dir "${WZ4_GEN_DIR}/${subdir}")
    set(_args "${_stem}.ops")
    set(_what "wz4ops")
  endif()

  set(_cpp "${_dir}/${_stem}.cpp")
  set(_hpp "${_dir}/${_stem}.hpp")

  add_custom_command(
    OUTPUT  "${_cpp}" "${_hpp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ops_path}" "${_dir}/${_stem}.ops"
    COMMAND wz4ops ${_args}
    WORKING_DIRECTORY "${_dir}"
    DEPENDS wz4ops "${ops_path}"
    COMMENT "${_what} ${subdir}/${_stem}.ops"
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
# registration entry points the runtime expects. Kept alongside the headless
# output because it is what proves -headless is inert when it is off: the two
# generated trees must differ only in what the flag suppresses.
add_custom_target(wz4ops_gate ALL DEPENDS ${GEN_BASIC} ${GEN_BITMAP})

#############################################################################
##  opsmeta_gate — phase 2 stage 2.3                                       ##
#############################################################################

set(WZ4_META_DIR "${CMAKE_BINARY_DIR}/meta")

# wz4_add_meta(<out_var> <subdir> <path/to/name.ops>)
#
# opsmeta shares wz4ops' SetNames(), so it has the same requirement: run it
# from the file's own directory with a bare filename. See wz4_add_ops above.

function(wz4_add_meta out_var subdir ops_path)
  get_filename_component(_stem "${ops_path}" NAME_WE)
  set(_dir "${WZ4_META_DIR}/${subdir}")
  set(_json "${_dir}/${_stem}.json")

  add_custom_command(
    OUTPUT  "${_json}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${ops_path}" "${_dir}/${_stem}.ops"
    COMMAND opsmeta "${_stem}.ops"
    WORKING_DIRECTORY "${_dir}"
    DEPENDS opsmeta "${ops_path}"
    COMMENT "opsmeta ${subdir}/${_stem}.ops"
    VERBATIM
  )

  set(${out_var} "${_json}" PARENT_SCOPE)
endfunction()

wz4_add_meta(META_BASIC  wz4lib   "${ALTONA_MAIN}/wz4lib/basic_ops.ops")
wz4_add_meta(META_BITMAP wz4frlib "${WZ4_DIR}/wz4frlib/wz3_bitmap_ops.ops")

# The whole corpus, not just the two gate modules.
#
# This is coverage, not a deliverable: most of these modules are out of scope
# (materials, sequencer, audio) and nothing consumes their metadata. It earns
# its place because opsmeta validates offsets and resolves every conditional
# symbol as it goes, so running it over all 33 modules checks roughly 3,400
# parameters instead of the ~450 in the two gate files. It found a crash on its
# first run that the gate modules never triggered — condition trees nest deeper
# than a fixed-depth JSON writer allowed.
#
# GLOB is normally the wrong tool because CMake will not reconfigure when a new
# file appears. Here upstream is a frozen 2014 dump, so the file list cannot
# change without someone editing this project on purpose.

file(GLOB WZ4_ALL_OPS
  "${ALTONA_MAIN}/wz4lib/*.ops"
  "${WZ4_DIR}/wz4frlib/*.ops"
)

set(META_ALL "")
foreach(_ops ${WZ4_ALL_OPS})
  get_filename_component(_stem "${_ops}" NAME_WE)
  if(NOT _stem STREQUAL "basic_ops" AND NOT _stem STREQUAL "wz3_bitmap_ops")
    wz4_add_meta(_meta corpus "${_ops}")
    list(APPEND META_ALL "${_meta}")
  endif()
endforeach()

list(LENGTH WZ4_ALL_OPS _opscount)
message(STATUS "opsmeta corpus: ${_opscount} .ops modules")

add_custom_target(opsmeta_gate ALL DEPENDS ${META_BASIC} ${META_BITMAP} ${META_ALL})

#############################################################################
##  headless_ops_gate — phase 2 stage 2.2                                  ##
#############################################################################

wz4_add_ops(GENHL_BASIC  wz4lib   "${ALTONA_MAIN}/wz4lib/basic_ops.ops"      HEADLESS)
wz4_add_ops(GENHL_BITMAP wz4frlib "${WZ4_DIR}/wz4frlib/wz3_bitmap_ops.ops"   HEADLESS)

# Compiles both headless operator modules with the GUI poisoned. This is the
# stage 2.2 gate, and it is the only thing that proves -headless produces code
# that actually builds without a window system.
#
# OBJECT, not a library to link: the operator bodies reference the wz4frlib
# implementation classes, which arrive in phase 4. Compile-only until then.

add_library(headless_ops_gate OBJECT
  "${GENHL_BASIC}"
  "${GENHL_BITMAP}"
)
target_link_libraries(headless_ops_gate PRIVATE altona_flags)

# WZ4_GENHL_DIR must precede altona_flags' WZ4_GEN_DIR so that
# "wz4lib/basic_ops.hpp" resolves to the headless one. Target-level
# directories are emitted before those inherited from linked libraries, so
# this is the right order — and if it ever were not, the poison header below
# would catch it, because the non-headless basic_ops.hpp includes doc.hpp.
target_include_directories(headless_ops_gate PRIVATE
  "${WZ4_GENHL_DIR}"
  "${WZ4_DIR}"
)
target_compile_options(headless_ops_gate PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)

#############################################################################
##  libwz4core — phase 2 stage 2.4                                         ##
#############################################################################

# The Werkkzeug operator runtime, GUI-free: documents, pages, operators, the
# type system, the connection builder and the executive.
#
# sCONFIG_OPTION_SHELL (already set for every target here) makes
# base/types.hpp:605 define sCOMMANDLINE, which is what compiles out the
# painting half of doc.cpp and basic.cpp. See wz4port/patches/06.
#
# script.cpp is NOT optional. wExecutive::Execute drives ScriptContext directly
# (doc.cpp:4050-4200) — that was the phase's last open question, and the answer
# is that the scripting path is part of the executive. It costs nothing
# platform-wise: script.cpp has no GUI or graphics dependency at all.
#
# gui/theme.cpp and gui/palette.cpp are upstream files carrying document data
# that used to be stranded in the gui library; the extractions are patches
# 04 and 06. Neither pulls a window system.

add_library(wz4core STATIC
  "${ALTONA_MAIN}/wz4lib/doc.cpp"
  "${ALTONA_MAIN}/wz4lib/build.cpp"
  "${ALTONA_MAIN}/wz4lib/basic.cpp"
  "${ALTONA_MAIN}/wz4lib/script.cpp"
  "${ALTONA_MAIN}/gui/theme.cpp"
  "${ALTONA_MAIN}/gui/palette.cpp"
  "${ALTONA_MAIN}/util/image.cpp"
  "${ALTONA_MAIN}/util/stb_image.c"
  "${COMPAT_DIR}/altona_missing.cpp"
  "${GENHL_BASIC}"
)
target_include_directories(wz4core PUBLIC "${WZ4_GENHL_DIR}")
target_link_libraries(wz4core PUBLIC altona_util)

# The same tripwire the compile-only gates use. libwz4core is the thing that
# most needs it: it is the layer everything above will link against.
target_compile_options(wz4core PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)

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
##  wz4tex — the texture library (phase 4)                                 ##
#############################################################################

# wz3_bitmap_code.cpp is the whole 3,578-line pixel engine: no graphics API, no
# GUI, no Win32, and one external dependency — <emmintrin.h>, which goes through
# compat/include/simd_compat.hpp and lands on sse2neon here. genvector.cpp is
# the scanline rasteriser behind the Vector operator.
#
# GenBitmap::Text is stubbed: it needs Altona's sFont2D, which has GDI and X11
# backends and none for macOS. Define WZ4PORT_HAVE_SFONT2D=1 once
# compat/font_freetype.cpp exists (stage 4.5).

#############################################################################
##  the font backend — stage 4.5                                           ##
#############################################################################

# GenBitmap::Text is the one texture operator that needs an OS font. Altona
# declares sFont2D and its 2D software drawing layer in base/windows.hpp but
# implements them only for GDI and X11, so wz4port supplies them on FreeType in
# compat/font_freetype.cpp. Nothing in Altona's font layer is patched — sFont2D
# keeps its state behind an opaque pointer, which is the seam that makes an
# out-of-tree implementation possible.
#
# FreeType is optional. Without it Text keeps the stub from 4.1, which leaves the
# bitmap untouched so a graph containing a Text operator still evaluates; the
# other 33 operators are unaffected either way.

find_package(Freetype)

set(WZ4_FONT_SOURCES "")

if(FREETYPE_FOUND)
  # "Found" is not good enough — it has to LINK for the target architecture.
  #
  # Homebrew's libfreetype on Apple silicon is arm64-only, so find_package
  # happily returns it when cross-compiling the x86-64 slice for the SIMD parity
  # check and the link then fails with a wall of undefined FT_ symbols. Compiling
  # and linking a two-line program is the only check that can tell the
  # difference, and it is the same lesson as the golden runner: assert the thing
  # you actually need, not a proxy for it.
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_INCLUDES ${FREETYPE_INCLUDE_DIRS})
  set(CMAKE_REQUIRED_LIBRARIES ${FREETYPE_LIBRARIES})
  check_cxx_source_compiles("
    #include <ft2build.h>
    #include FT_FREETYPE_H
    int main() { FT_Library lib; return FT_Init_FreeType(&lib); }
  " WZ4_FREETYPE_LINKS)
  unset(CMAKE_REQUIRED_INCLUDES)
  unset(CMAKE_REQUIRED_LIBRARIES)
endif()

if(WZ4_FREETYPE_LINKS)
  set(WZ4_FONT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/compat/font_freetype.cpp")
  message(STATUS "FreeType links: GenBitmap.Text is enabled")
elseif(FREETYPE_FOUND)
  message(STATUS "FreeType found but does not link for this target"
                 " (wrong architecture?): GenBitmap.Text stays stubbed")
else()
  message(STATUS "FreeType not found: GenBitmap.Text stays stubbed")
endif()

add_library(wz4tex STATIC
  "${WZ4_DIR}/wz4frlib/wz3_bitmap_code.cpp"
  "${WZ4_DIR}/wz4frlib/genvector.cpp"
  "${GENHL_BITMAP}"
  ${WZ4_FONT_SOURCES}
)
target_include_directories(wz4tex PUBLIC
  "${WZ4_GENHL_DIR}"
  "${WZ4_DIR}"
  "${WZ4_DIR}/wz4frlib"           # genvector.hpp is included unqualified
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
target_link_libraries(wz4tex PUBLIC wz4core)
target_compile_options(wz4tex PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)

if(WZ4_FONT_SOURCES)
  # WZ4PORT_HAVE_SFONT2D switches on the real body of GenBitmap::Text, which was
  # stubbed in 4.1. PUBLIC because the flag has to agree between the library and
  # anything that includes wz3_bitmap_code.hpp.
  target_compile_definitions(wz4tex PUBLIC WZ4PORT_HAVE_SFONT2D=1)
  target_include_directories(wz4tex PRIVATE ${FREETYPE_INCLUDE_DIRS})
  target_link_libraries(wz4tex PUBLIC ${FREETYPE_LIBRARIES})
endif()

#############################################################################
##  wz4gen — headless CLI (phase 3)                                        ##
#############################################################################

# wz4t — our text format and the runtime side of the metadata.
#
# The phase plan called these wz4port/libwz4core/wz4t_*.cpp. They are a separate
# target instead: `wz4core` is upstream sources compiled in place, and mixing
# our code into it would blur the isolation seam. Nothing else changes.

add_library(wz4t STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/wz4t/json.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/wz4t/meta.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/wz4t/wz4t_read.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/wz4t/wz4t_write.cpp"
)
target_include_directories(wz4t PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/wz4t")
target_link_libraries(wz4t PUBLIC wz4core)
target_compile_options(wz4t PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)

add_executable(wz4gen "${CMAKE_CURRENT_SOURCE_DIR}/tools/wz4gen/main.cpp")
target_link_libraries(wz4gen PRIVATE wz4t wz4tex)
target_compile_options(wz4gen PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
# So the tool works with no arguments from a normal build.
target_compile_definitions(wz4gen PRIVATE
  "WZ4GEN_META_DIR=L\"${WZ4_META_DIR}\""
)

#############################################################################
##  wz4ed — the editor (phase 5)                                           ##
#############################################################################

# Dear ImGui + GLFW + OpenGL 3.3 core, both vendored and pinned — see
# third_party/VENDORED.md for the versions and why GLFW is built from source
# while FreeType is a system package.
#
# The editor brings its own window and uses wz4lib purely as a headless library.
# Nothing of Altona's GUI is compiled, which is what phases 2 and 3 were for.

set(WZ4_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(WZ4_GLFW_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw")

if(EXISTS "${WZ4_IMGUI_DIR}/imgui.cpp" AND EXISTS "${WZ4_GLFW_DIR}/CMakeLists.txt")

  find_package(OpenGL)

  if(NOT OPENGL_FOUND)
    message(STATUS "OpenGL not found: the editor is not built")
  else()
    # GLFW's own options, set before add_subdirectory so its examples, tests and
    # docs directories are never referenced — they are not vendored.
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory("${WZ4_GLFW_DIR}" "${CMAKE_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)

    add_library(imgui STATIC
      "${WZ4_IMGUI_DIR}/imgui.cpp"
      "${WZ4_IMGUI_DIR}/imgui_draw.cpp"
      "${WZ4_IMGUI_DIR}/imgui_tables.cpp"
      "${WZ4_IMGUI_DIR}/imgui_widgets.cpp"
      "${WZ4_IMGUI_DIR}/imgui_demo.cpp"
      "${WZ4_IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
      "${WZ4_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
    )
    target_include_directories(imgui PUBLIC
      "${WZ4_IMGUI_DIR}"
      "${WZ4_IMGUI_DIR}/backends"
    )
    target_link_libraries(imgui PUBLIC glfw ${OPENGL_LIBRARIES})

    # ImGui is third-party C++ and does NOT get altona_flags. In particular it
    # must not be built with -fshort-wchar: it is compiled against the system
    # headers and its own wchar_t use has to match the platform's, not Altona's.
    # That is also why every string crossing between the two goes through an
    # explicit converter in editor/main.cpp rather than being assigned across.
    target_compile_options(imgui PRIVATE -Wno-unused-function)

    add_executable(wz4ed
      "${CMAKE_CURRENT_SOURCE_DIR}/editor/main.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/editor/canvas.cpp"
    )
    target_link_libraries(wz4ed PRIVATE wz4t wz4tex imgui)
    target_compile_options(wz4ed PRIVATE
      "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
    )
    target_compile_definitions(wz4ed PRIVATE
      "WZ4ED_META_DIR=L\"${WZ4_META_DIR}\""
    )
    message(STATUS "editor: wz4ed will be built")

    # GUI tests need a graphical session. On by default because the common case
    # is a developer at a desk; turn off for ssh or headless CI.
    option(WZ4_GUI_TESTS "Run tests that open a window" ON)

    if(WZ4_GUI_TESTS)
      add_test(NAME wz4ed_shell COMMAND "${CMAKE_COMMAND}"
        "-DWZ4ED=$<TARGET_FILE:wz4ed>"
        "-DDOC=${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/ops_gen.wz4t"
        "-DMETA=${WZ4_META_DIR}"
        "-DSELECT=bricks"
        "-DOUT=${CMAKE_BINARY_DIR}/editor-shot/shell.png"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_shot.cmake")
      set_tests_properties(wz4ed_shell PROPERTIES DEPENDS opsmeta_gate)

      # One window at a time: GLFW and the platform frameworks are not built for
      # several of these racing each other, and a flaky GUI test is worse than
      # none.
      set_tests_properties(wz4ed_shell PROPERTIES RUN_SERIAL TRUE)
    endif()
  endif()

else()
  message(STATUS "third_party/imgui or third_party/glfw missing:"
                 " the editor is not built")
endif()

# Every .wz4 document in the tree must load. This is the cheapest available
# check that the serialisation surgery of phase 2 preserved the format —
# stages 2.1 and 2.4 both had to work to keep wEditOptions and wDocOptions
# byte-compatible, and nothing else verifies that.
#
# wz4gen sets the error code on a failed load AND on a load that yields zero
# operators, so an empty read cannot pass silently.
#
# The plan named five documents; there are six. screens4/test.wz4 is included
# because it is the only one using Call/Input subroutines heavily.

set(WZ4_DOCS
  "demos/example/example.wz4"
  "demos/the_cube/fr-062_party.wz4"
  "demos/the_cube/fr-062_texts_kb.wz4"
  "demos/easterparty/teaser1.wz4"
  "demos/easterparty/teaser2.wz4"
  "wz4/screens4/test.wz4"
)

foreach(_doc ${WZ4_DOCS})
  get_filename_component(_name "${_doc}" NAME_WE)
  add_test(NAME load_${_name}
    COMMAND wz4gen list "${FR_ROOT}/altona_wz4/${_doc}")

  # And that a load/save/reload by THIS build does not damage the document.
  # Without wOp::ForeignClass every operator whose module we have not
  # registered would be rewritten as UnknownOp and lost for good — see
  # wz4port/patches/07.
  add_test(NAME identity_${_name}
    COMMAND wz4gen identity
      "${FR_ROOT}/altona_wz4/${_doc}"
      "${CMAKE_BINARY_DIR}/roundtrip/${_name}.wz4")
endforeach()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/roundtrip")

# Read the whole metadata back and check it hangs together from the consumer
# side — offsets inside their declared budget, `continues` landing on a word its
# owner declares, every choice value fitting its widget's mask. opsmeta
# validates what it writes; this validates what can be read, which is a
# different question, and it is what proves the schema is actually usable.
add_test(NAME checkmeta COMMAND wz4gen checkmeta -meta "${WZ4_META_DIR}")
set_tests_properties(checkmeta PROPERTIES DEPENDS opsmeta_gate)

#############################################################################
##  wz4t_read — the stage 3.1b gate                                        ##
#############################################################################

add_executable(wz4t_read "${CMAKE_CURRENT_SOURCE_DIR}/tests/wz4t_read.cpp")
target_link_libraries(wz4t_read PRIVATE wz4t)
target_compile_options(wz4t_read PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
add_test(NAME wz4t_read COMMAND wz4t_read
  "${WZ4_META_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/three_ops.wz4t")
set_tests_properties(wz4t_read PROPERTIES DEPENDS opsmeta_gate)

#############################################################################
##  wz4t_round — the stage 3.2 gate                                        ##
#############################################################################

add_executable(wz4t_round "${CMAKE_CURRENT_SOURCE_DIR}/tests/wz4t_round.cpp")
target_link_libraries(wz4t_round PRIVATE wz4t wz4tex)
target_compile_options(wz4t_round PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
foreach(_case three_ops values)
  add_test(NAME wz4t_round_${_case} COMMAND wz4t_round
    "${WZ4_META_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/${_case}.wz4t")
  set_tests_properties(wz4t_round_${_case} PROPERTIES DEPENDS opsmeta_gate)
endforeach()

# And the texture cases: smoke is the only one with array rows, and chain is
# the only one that assigns a flags word positionally (`Flags = "-", rectangle`).
foreach(_case smoke chain)
  add_test(NAME wz4t_round_tex_${_case} COMMAND wz4t_round
    "${WZ4_META_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/${_case}.wz4t")
  set_tests_properties(wz4t_round_tex_${_case} PROPERTIES DEPENDS opsmeta_gate)
endforeach()

#############################################################################
##  wz4t_docround — the phase 3 gate                                       ##
#############################################################################

# .wz4 -> .wz4t -> .wz4 over every bundled document, checking identity,
# geometry, store names, parameters (for the registered subgraph) and the
# re-derived connection lists.

add_executable(wz4t_docround "${CMAKE_CURRENT_SOURCE_DIR}/tests/wz4t_docround.cpp")
target_link_libraries(wz4t_docround PRIVATE wz4t)
target_compile_options(wz4t_docround PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)

foreach(_doc ${WZ4_DOCS})
  get_filename_component(_name "${_doc}" NAME_WE)
  add_test(NAME docround_${_name} COMMAND wz4t_docround
    "${WZ4_META_DIR}"
    "${FR_ROOT}/altona_wz4/${_doc}"
    "${CMAKE_BINARY_DIR}/roundtrip/${_name}.wz4t"
    "${CMAKE_BINARY_DIR}/roundtrip/${_name}_back.wz4")
  set_tests_properties(docround_${_name} PROPERTIES DEPENDS opsmeta_gate)
endforeach()

#############################################################################
##  tex_smoke — the stage 4.1 gate                                         ##
#############################################################################

# libwz4tex compiles, links, and actually runs: four generators/filters
# evaluate to a non-empty bitmap of the requested size.
#
# The metric is "uniform or structured", not "how many pixels are non-zero":
# alpha is 0xffff almost everywhere, so every pixel is non-zero regardless of
# whether the operator did anything.

# Flat is a solid colour, so "uniform" is the correct answer for it and the
# wrong answer for everything else. Both directions are asserted: an operator
# that silently produced a flat fill would otherwise look like it worked.
foreach(_op noise cells ramp blurred)
  add_test(NAME tex_smoke_${_op} COMMAND wz4gen render
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/smoke.wz4t"
    -op ${_op} -meta "${WZ4_META_DIR}")
  set_tests_properties(tex_smoke_${_op} PROPERTIES
    DEPENDS opsmeta_gate
    PASS_REGULAR_EXPRESSION "64 x 64, structured")
endforeach()

add_test(NAME tex_smoke_flat COMMAND wz4gen render
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/smoke.wz4t"
  -op flat -meta "${WZ4_META_DIR}")
set_tests_properties(tex_smoke_flat PROPERTIES
  DEPENDS opsmeta_gate
  PASS_REGULAR_EXPRESSION "64 x 64, uniform")

#############################################################################
##  tex_chain — the stage 4.2 gate                                         ##
#############################################################################

# A three-operator chain rendered to PNG. Every step of tests/tex/chain.wz4t is
# named, so each operator is rendered and checked on its own: Flat must be
# uniform, and the three that follow it must be structured at the size Flat
# asked for. An operator that ran but changed nothing therefore fails here
# rather than being hidden by the rest of the chain.
#
# golden_png.cmake checks the artefact on disk, not just the exit code — see the
# comment at the top of it.

set(WZ4_TEX_PNG_DIR "${CMAKE_BINARY_DIR}/tex-png")

# The Import cases read relative paths, and the runner pins the tool's working
# directory to WZ4_TEX_PNG_DIR, so the checked-in test data has to be there.
# Copied at configure time rather than referenced in place, so that nothing the
# suite writes can land in the source tree.
file(GLOB WZ4_TEX_DATA "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/data/*.png"
                       "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/data/*.jpg")
foreach(_f ${WZ4_TEX_DATA})
  get_filename_component(_n "${_f}" NAME)
  configure_file("${_f}" "${WZ4_TEX_PNG_DIR}/${_n}" COPYONLY)
endforeach()

# The cases themselves are declared below, with the rest of the texture suite,
# because they use the same wz4_tex_case helper and so get goldens too.

#############################################################################
##  tex_ops — stage 4.3, one case per texture operator                     ##
#############################################################################

# wz4_tex_case(<doc> <op> <expect> [reject])
#
# Each named operator in tests/tex/*.wz4t is one case. It renders to
# build/tex-png/, and three things are checked:
#
#   EXPECT   the tool's own report must match — size, uniform/structured. Catches
#            an operator that ran but returned 1x1, or a flat fill where
#            structure was expected.
#   REJECT   the report must NOT match this. Defaults to "renders blank", the
#            assertion that earned its keep in 4.3: a zero-alpha bitmap writes a
#            valid PNG of the right size that displays as plain white, and every
#            positive check passes on it. A case meant to be blank opts out by
#            passing its own reject value.
#   golden   the image must match tests/tex/golden/ byte for byte, and so must
#            the report line, checksum included. That is stage 4.4.
#
# EXPECT and REJECT are kept alongside the golden rather than replaced by it,
# because they say WHY a case failed. A golden mismatch on its own tells you only
# that something moved.
#
# Every case is also recorded into build/tex-cases.txt, which is what
# lock_goldens.cmake reads. Deriving the list from the PNG filenames instead
# would mean splitting "ops_gen_flat" into a document and an operator, which is
# ambiguous; emitting it here keeps the lock step and the tests in step by
# construction.
set_property(GLOBAL PROPERTY WZ4_TEX_CASES "")

# wz4_tex_case_nolock(<doc> <op> <expect> [reject])
#
# Same checks, minus the golden. For cases whose output legitimately depends on
# something the repository does not ship — currently only GenBitmap.Text, whose
# Font parameter is a family name and so resolves to whatever font the machine
# happens to have. A byte-exact golden there would fail on a different OS or
# after a font update, which is a false-failure generator rather than a test.
#
# These are also left out of tex-cases.txt, so the lock step cannot pick them up
# by accident.
function(wz4_tex_case_nolock doc op expect)
  set(_reject "renders blank")
  if(ARGC GREATER 3)
    set(_reject "${ARGV3}")
  endif()
  add_test(NAME tex_${doc}_${op} COMMAND "${CMAKE_COMMAND}"
    "-DWZ4GEN=$<TARGET_FILE:wz4gen>"
    "-DDOC=${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/${doc}.wz4t"
    "-DMETA=${WZ4_META_DIR}"
    "-DOP=${op}"
    "-DOUT=${WZ4_TEX_PNG_DIR}/${doc}_${op}.png"
    "-DEXPECT=${expect}"
    "-DREJECT=${_reject}"
    "-DWORKDIR=${WZ4_TEX_PNG_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/golden_png.cmake")
  set_tests_properties(tex_${doc}_${op} PROPERTIES DEPENDS opsmeta_gate)
endfunction()

function(wz4_tex_case doc op expect)
  set(_reject "renders blank")
  if(ARGC GREATER 3)
    set(_reject "${ARGV3}")
  endif()
  add_test(NAME tex_${doc}_${op} COMMAND "${CMAKE_COMMAND}"
    "-DWZ4GEN=$<TARGET_FILE:wz4gen>"
    "-DDOC=${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/${doc}.wz4t"
    "-DMETA=${WZ4_META_DIR}"
    "-DOP=${op}"
    "-DOUT=${WZ4_TEX_PNG_DIR}/${doc}_${op}.png"
    "-DGOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/golden/${doc}_${op}.png"
    "-DDIFF=${WZ4_TEX_PNG_DIR}/${doc}_${op}.diff.png"
    "-DEXPECT=${expect}"
    "-DREJECT=${_reject}"
    "-DWORKDIR=${WZ4_TEX_PNG_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/golden_png.cmake")
  set_tests_properties(tex_${doc}_${op} PROPERTIES DEPENDS opsmeta_gate)
  set_property(GLOBAL APPEND PROPERTY WZ4_TEX_CASES "${doc}|${op}")
endfunction()

# The stage 4.2 chain: Flat -> GlowRect -> Twirl -> Blur at 256x256, every step
# named so each operator can be checked on its own rather than inferred from the
# end of the chain.
wz4_tex_case(chain field "256 x 256, uniform")
foreach(_op rect twisted twisted_soft)
  wz4_tex_case(chain ${_op} "256 x 256, structured")
endforeach()

# The shared source bitmaps are cases in their own right. Without them a
# reviewer has nothing to compare a filtered result against, and the subtle
# operators — ColorBalance especially — cannot be judged at all: "is the dark end
# bluer than the source" is not answerable if the source was never rendered.
wz4_tex_case(ops_color src_ramp   "128 x 128, structured")
wz4_tex_case(ops_color src_gray   "128 x 128, structured")
wz4_tex_case(ops_merge src_ramp   "128 x 128, structured")
wz4_tex_case(ops_merge src_cells  "128 x 128, structured")
wz4_tex_case(ops_filter src_bricks "128 x 128, structured")
wz4_tex_case(ops_warp src_bricks   "128 x 128, structured")
wz4_tex_case(ops_light src_height  "128 x 128, structured")
wz4_tex_case(ops_light src_white   "128 x 128, uniform")
wz4_tex_case(ops_io src_bricks     "128 x 128, structured")

# Generators. Flat is the one case that must be uniform; if any other generator
# comes out uniform it did nothing.
wz4_tex_case(ops_gen flat "128 x 128, uniform")
foreach(_op
    perlin perlin_abs perlin_sin
    cell_inner cell_outer cell_color
    gradient_step gradient_linear gradient_smooth
    glowrect_ellipse glowrect_rect dots bricks vector atlas)
  wz4_tex_case(ops_gen ${_op} "128 x 128, structured")
endforeach()

# Colour operators. All pointwise, all fed from one shared ramp.
foreach(_op
    color_mul color_add color_gray color_scale
    color_invert_opaque color_sub_opaque
    range_adjust range_range range_invert
    hscb bitcrusher colorbalance premul
    mask_mix mask_mul mask_add)
  wz4_tex_case(ops_color ${_op} "128 x 128, structured")
endforeach()

# Mask's `sub` is the fourth operator that destroys alpha, and the one that
# showed the blank check was too narrow: it leaves alpha at 0x0001 rather than
# 0x0000, which is just as invisible but is not zero.
wz4_tex_case(ops_color mask_sub "structured, alpha 0001..0001" "")

# Two operators zero the alpha channel, and both do it as upstream's own
# behaviour rather than as a port bug:
#
#   Color invert — xors every channel with 0x7fff, alpha included, so an opaque
#                  input becomes fully transparent (wz3_bitmap_code.cpp:919).
#   Color sub    — subtracts the Color's alpha from the input's, and the case
#                  subtracts an opaque grey, so alpha goes to zero.
#
# Both are asserted, alpha range and all, rather than excused. Discovered by the
# blank-render check above: they were written expecting visible output.
wz4_tex_case(ops_color color_invert "structured, alpha 0000..0000" "")
wz4_tex_case(ops_color color_sub    "structured, alpha 0000..0000" "")

# Convolution and resampling, over a hard-edged brick wall.
foreach(_op blur sharpen)
  wz4_tex_case(ops_filter ${_op} "128 x 128, structured")
endforeach()

# Downsample changes the size, which is itself the assertion: a Downsample that
# silently passed its input through would still be "structured" at 128x128.
foreach(_op downsample_point downsample_box)
  wz4_tex_case(ops_filter ${_op} "32 x 32, structured")
endforeach()

# Warps and resamples, same brick-wall source.
foreach(_op
    rotate_bilinear rotate_nearest rotatemul twirl
    unwrap_polar2normal unwrap_normal2polar unwrap_rect2normal
    bulge distort)
  wz4_tex_case(ops_warp ${_op} "128 x 128, structured")
endforeach()

# Normal generation and lighting.
foreach(_op
    normals_2d normals_3d normals_tangent2d normals_tangent3d normals_sharp
    light_spot light_point
    bump_spot bump_point bump_directional)
  wz4_tex_case(ops_light ${_op} "128 x 128, structured")
endforeach()

# A directional light on a FLAT surface is uniform by definition — every pixel
# has the same normal. Asserted as uniform rather than excused, and the reason
# bump_directional above is a separate case: with a normal map it must vary.
wz4_tex_case(ops_light light_directional "128 x 128, uniform")

# Merge, all twelve blend modes over one shared input pair.
foreach(_op
    merge_add merge_mul merge_diff merge_alpha merge_brightness
    merge_hardlight merge_over merge_addsmooth merge_min merge_max
    merge_premul merge_sub_opaque)
  wz4_tex_case(ops_merge ${_op} "128 x 128, structured")
endforeach()

# Merge's `sub` subtracts alpha as well, so with two opaque inputs it renders
# blank — the third operator found to do this, after Color's sub and invert.
wz4_tex_case(ops_merge merge_sub "structured, alpha 0000..0000" "")

# Conversion and file output. Export returns the result of its own save, so this
# case fails if the file cannot be written.
foreach(_op export makewz3bitmap)
  wz4_tex_case(ops_io ${_op} "128 x 128, structured")
endforeach()

#############################################################################
##  stage 4.5 — fonts and image import                                     ##
#############################################################################

# Import reads the checked-in test data copied into WZ4_TEX_PNG_DIR above. These
# ARE golden-locked: stb_image's PNG and JPEG decoders are deterministic, and the
# data is committed, so nothing here depends on the machine.
wz4_tex_case(ops_import import_png "128 x 128, structured")
wz4_tex_case(ops_import import_jpg "128 x 128, structured")

# Four 32x32 frames packed into one atlas.
wz4_tex_case(ops_import import_anim "64 x 64, structured")

# Text is the one case that cannot be golden-locked: its Font parameter is a
# family name, so the glyphs come from whatever font the machine has. Asserted
# structurally instead — see ops_text.wz4t for what that does and does not catch.
if(WZ4_FONT_SOURCES)
  foreach(_op text text_lines text_styled)
    wz4_tex_case_nolock(ops_text ${_op} "128 x 128, structured")
  endforeach()
endif()

# Hand the case list to lock_goldens.cmake. One "doc|op" per line, in
# declaration order.
get_property(_cases GLOBAL PROPERTY WZ4_TEX_CASES)
string(REPLACE ";" "\n" _caselist "${_cases}")
file(WRITE "${CMAKE_BINARY_DIR}/tex-cases.txt" "${_caselist}\n")
list(LENGTH _cases _ncases)
message(STATUS "texture suite: ${_ncases} cases")

#############################################################################
##  merge_identity — brightness and hardlight must stay identical          ##
#############################################################################

# Merge's `brightness` and `hardlight` modes are the same algorithm written
# twice (wz3_bitmap_code.cpp:995 and :1120): the mask is built with
# _mm_srli_epi16 in one and _mm_and_si128 in the other, and everything after is
# identical. So their outputs must be byte-identical.
#
# That is worth a test rather than a comment, because the two paths reach the
# same answer through DIFFERENT intrinsics. If sse2neon mistranslated either of
# those two, or a future change touched one kernel and not the other, they would
# diverge — and nothing else in the suite would notice, since both would still
# match their own goldens only if both were still correct.

add_test(NAME tex_merge_identity COMMAND "${CMAKE_COMMAND}"
  "-DWZ4GEN=$<TARGET_FILE:wz4gen>"
  "-DA=${WZ4_TEX_PNG_DIR}/ops_merge_merge_brightness.png"
  "-DB=${WZ4_TEX_PNG_DIR}/ops_merge_merge_hardlight.png"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/tex/same_png.cmake")
set_tests_properties(tex_merge_identity PROPERTIES
  DEPENDS "tex_ops_merge_merge_brightness;tex_ops_merge_merge_hardlight")

#############################################################################
##  core_connect — the phase 2 gate                                        ##
#############################################################################

# Links libwz4core, registers the basic module, builds a document and lets
# Connect() derive the graph from block geometry. This is the first target that
# LINKS the runtime rather than just compiling it, so it is what proves the
# headless story end to end.

add_executable(core_connect "${CMAKE_CURRENT_SOURCE_DIR}/tests/core_connect.cpp")
target_link_libraries(core_connect PRIVATE wz4core)
target_compile_options(core_connect PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
add_test(NAME core_connect COMMAND core_connect)

#############################################################################
##  canvas_rules — the stage 5.2 gate that a screenshot cannot cover       ##
#############################################################################

# The canvas calls wPage::CheckDest and CheckMove directly rather than
# reimplementing them, so this drives exactly the code a drag drives, with the
# same inputs. It also checks the part that makes the model what it is: that a
# legal move REWIRES the graph, because the geometry is the graph.
#
# No window, so it runs everywhere — including the headless x86-64 parity build.

add_executable(canvas_rules "${CMAKE_CURRENT_SOURCE_DIR}/tests/canvas_rules.cpp")
target_link_libraries(canvas_rules PRIVATE wz4core)
target_compile_options(canvas_rules PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
add_test(NAME canvas_rules COMMAND canvas_rules)

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
