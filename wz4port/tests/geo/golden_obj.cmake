# Renders one mesh case to OBJ and compares it byte-for-byte against its locked
# golden. Stage 6.3b.
#
# WHY THIS EXISTS ALONGSIDE mesh_ops' CHECKSUMS
#
# mesh_ops locks a checksum over every vertex POSITION and every FACE INDEX. That
# covers most of what an operator can get wrong, and it is bit-exact where an OBJ
# is not — Altona writes `%f` at five decimals, so an OBJ cannot see a change
# below 1e-5. The checksum is the stronger of the two for anything it covers.
#
# But it does not cover NORMALS or UVs, and that is not a hypothetical gap:
#
#   t_normalize's checksum is IDENTICAL to its input's. Normalize only rewrites
#   normals, so the position-and-index checksum is structurally incapable of
#   detecting any change in it — verified, not assumed: `wz4gen sweep
#   ops_transform.wz4t -v` reports 0001f83112ec02dd for the Cube input and for
#   the Normalize output alike.
#
# An OBJ carries `vn` and `vt` lines, so it closes that gap. Hence a small,
# deliberately chosen set of OBJ goldens rather than one per case: they are for
# the operators whose whole job is in the attributes, plus a few small enough to
# read in full.
#
# Same lesson as A43 in the other direction. There, a PNG golden was blind to the
# low 8 bits and needed a checksum beside it. Here the checksum is blind to the
# attributes and needs a file beside it. Neither artefact covers a pipeline on its
# own.
#
# Invoked as:
#   cmake -DWZ4GEN=<exe> -DDOC=<file.wz4t> -DOP=<storename> -DMETA=<dir>
#         -DOUT=<file.obj> -DGOLDEN=<file.obj> -P golden_obj.cmake

foreach(_var WZ4GEN DOC OP OUT GOLDEN)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "golden_obj.cmake: -D${_var}= is required")
  endif()
endforeach()

if(NOT EXISTS "${GOLDEN}")
  message(FATAL_ERROR
    "no golden for ${OP} at <${GOLDEN}>.\n"
    "A new case needs its golden reviewed and locked — see 08-phase-geometry.md "
    "6.3b. Do not create it by copying the current output without reading it.")
endif()

file(REMOVE "${OUT}")
get_filename_component(_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

# Pinned, and the same directory the lock step uses — Export writes relative
# paths and the .wz4t case files contain one. Same reasoning as golden_png.cmake.
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
if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "no file was written to <${OUT}>")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${GOLDEN}"
  RESULT_VARIABLE _differ
  OUTPUT_QUIET ERROR_QUIET)

if(_differ EQUAL 0)
  message("ok: ${OP} matches its golden OBJ, including normals and UVs")
  return()
endif()

message(FATAL_ERROR
  "${OP} does not match its golden OBJ.\n"
  "  rendered: ${OUT}\n"
  "  golden:   ${GOLDEN}\n"
  "Both are text — diff them. If mesh_ops still passes, the difference is in the "
  "normals or the UVs, which is exactly the gap this golden exists to cover.\n"
  "If the change is intended, read the diff and re-lock deliberately: "
  "cmake --build . --target lock_obj_goldens")
