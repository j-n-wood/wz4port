# Compares an exported glTF against a reviewed golden — BOTH halves of it.
#
# The pair is deliberate. The `.gltf` is JSON and diffs cleanly, so a structural
# change shows as a readable diff; the `.bin` is where every coordinate actually
# lives, and a change there is invisible in the JSON. Comparing only the JSON
# would pass a writer that emitted the right accessors over the wrong vertices —
# which is precisely the failure the OBJ goldens exist to catch in their own
# format (see 08-phase-geometry.md 6.3b), one level less visible.
#
# This is also why the exporter emits `.gltf` + `.bin` for tests rather than
# `.glb`: a single binary container would make every failure a byte offset
# instead of a diff.
#
# The optional Khronos validator runs here too when it is available. It is the
# only check in the suite that tests conformance to the SPECIFICATION rather than
# to our own reading of it — the round-trip in tests/gltf_roundtrip.cpp and this
# golden both ultimately encode what we believe glTF requires. It is absent by
# default and its absence is not a failure; see 10-phase-gltf.md 8.3.
#
# Invoked as:
#   cmake -DWZ4GEN=<exe> -DDOC=<file.wz4t> -DOP=<storename> -DMETA=<dir>
#         -DOUT=<file.gltf> -DGOLDEN=<file.gltf> [-DWORKDIR=<dir>]
#         [-DVALIDATOR=<exe>] -P golden_gltf.cmake

foreach(_var WZ4GEN DOC OP OUT GOLDEN)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "golden_gltf.cmake: -D${_var}= is required")
  endif()
endforeach()

string(REGEX REPLACE "\\.gltf$" ".bin" _outbin "${OUT}")
string(REGEX REPLACE "\\.gltf$" ".bin" _goldenbin "${GOLDEN}")

foreach(_g "${GOLDEN}" "${_goldenbin}")
  if(NOT EXISTS "${_g}")
    message(FATAL_ERROR
      "no golden for ${OP} at <${_g}>.\n"
      "A new case needs its golden reviewed and locked — see 10-phase-gltf.md "
      "8.3. Do not create it by copying the current output without reading it.")
  endif()
endforeach()

file(REMOVE "${OUT}" "${_outbin}")
get_filename_component(_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

if(NOT DEFINED WORKDIR OR WORKDIR STREQUAL "")
  set(WORKDIR "${_dir}")
endif()
file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(
  COMMAND "${WZ4GEN}" render "${DOC}" -op "${OP}" -meta "${META}" -out "${OUT}"
  WORKING_DIRECTORY "${WORKDIR}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)
message("${_out}${_err}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "wz4gen render exited ${_rc}")
endif()
foreach(_f "${OUT}" "${_outbin}")
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "no file was written to <${_f}>")
  endif()
endforeach()

# The JSON first: when both differ, the readable one is the more useful message.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${GOLDEN}"
  RESULT_VARIABLE _jsondiff OUTPUT_QUIET ERROR_QUIET)
if(NOT _jsondiff EQUAL 0)
  message(FATAL_ERROR
    "${OP} does not match its golden glTF JSON.\n"
    "  rendered: ${OUT}\n"
    "  golden:   ${GOLDEN}\n"
    "Both are text — diff them. If the change is intended, read the diff and "
    "re-lock deliberately: cmake --build . --target lock_gltf_goldens")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${_outbin}" "${_goldenbin}"
  RESULT_VARIABLE _bindiff OUTPUT_QUIET ERROR_QUIET)
if(NOT _bindiff EQUAL 0)
  message(FATAL_ERROR
    "${OP}'s glTF JSON matches but its BUFFER does not.\n"
    "  rendered: ${_outbin}\n"
    "  golden:   ${_goldenbin}\n"
    "The structure is unchanged and the coordinates are not, so this is a change "
    "in the geometry itself — positions, normals, tangents or UVs. That is what "
    "this half of the golden exists to catch, and it is invisible in the JSON.\n"
    "If it is intended: cmake --build . --target lock_gltf_goldens")
endif()

if(DEFINED VALIDATOR AND NOT VALIDATOR STREQUAL "" AND EXISTS "${VALIDATOR}")
  execute_process(
    COMMAND "${VALIDATOR}" -a "${OUT}"
    WORKING_DIRECTORY "${_dir}"
    RESULT_VARIABLE _vrc OUTPUT_VARIABLE _vout ERROR_VARIABLE _verr)
  message("${_vout}${_verr}")
  if(NOT _vrc EQUAL 0)
    message(FATAL_ERROR
      "${OP} matches its goldens but the Khronos validator REJECTS it.\n"
      "The goldens record what this writer does, not that it is correct — that "
      "is the whole reason this check exists. Fix the writer, then re-lock.")
  endif()
  message("ok: ${OP} matches its goldens and passes the Khronos validator")
else()
  message("ok: ${OP} matches its golden glTF and buffer"
          " (Khronos validator not installed — conformance unchecked)")
endif()
