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
target_link_libraries(wz4gen PRIVATE wz4t)
target_compile_options(wz4gen PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
# So the tool works with no arguments from a normal build.
target_compile_definitions(wz4gen PRIVATE
  "WZ4GEN_META_DIR=L\"${WZ4_META_DIR}\""
)

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
target_link_libraries(wz4t_round PRIVATE wz4t)
target_compile_options(wz4t_round PRIVATE
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_poison.h"
)
foreach(_case three_ops values)
  add_test(NAME wz4t_round_${_case} COMMAND wz4t_round
    "${WZ4_META_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/${_case}.wz4t")
  set_tests_properties(wz4t_round_${_case} PROPERTIES DEPENDS opsmeta_gate)
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
