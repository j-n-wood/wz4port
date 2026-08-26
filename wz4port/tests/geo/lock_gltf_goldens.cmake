# Re-locks the glTF goldens. Phase 8.3.
#
# A SEPARATE, DELIBERATE STEP, exactly as lock_obj_goldens.cmake is for the OBJ
# suite. A runner that re-locks whatever it finds is not a test, it is a rubber
# stamp — and the whole value of these files is that a change has to be looked at
# by someone before it becomes the new baseline.
#
# So this is never run by ctest. It is invoked by hand:
#
#   cmake --build wz4port/build --target lock_gltf_goldens
#
# and then `git diff wz4port/tests/geo/golden/gltf/` is the thing to read. Half
# of it is legible: the `.gltf` is JSON, so an accessor, a primitive or a changed
# count shows up directly. The other half is not — the `.bin` is raw floats and
# git will call it binary. When only the buffer moves, the diff tells you THAT
# the geometry changed but not how, and the readable account is what wz4gen
# prints: counts, bounds and the checksum.
#
# Writing the .gltf writes its .bin sidecar, so only one path is passed.
#
# Invoked as:
#   cmake -DWZ4GEN=<exe> -DDOC=<file.wz4t> -DOP=<storename> -DMETA=<dir>
#         -DGOLDEN=<file.gltf> -DWORKDIR=<dir> -P lock_gltf_goldens.cmake

foreach(_var WZ4GEN DOC OP GOLDEN)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "lock_gltf_goldens.cmake: -D${_var}= is required")
  endif()
endforeach()

get_filename_component(_gdir "${GOLDEN}" DIRECTORY)
file(MAKE_DIRECTORY "${_gdir}")

if(NOT DEFINED WORKDIR OR WORKDIR STREQUAL "")
  set(WORKDIR "${_gdir}")
endif()
file(MAKE_DIRECTORY "${WORKDIR}")

execute_process(
  COMMAND "${WZ4GEN}" render "${DOC}" -op "${OP}" -meta "${META}" -out "${GOLDEN}"
  WORKING_DIRECTORY "${WORKDIR}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "wz4gen render exited ${_rc}\n${_out}${_err}")
endif()

message("locked ${OP} -> ${GOLDEN} (+ .bin)")
