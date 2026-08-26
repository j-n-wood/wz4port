# The stage 8.4 gate: the editor's File -> Export glTF actually exports.
#
# Runs wz4ed with -export, which drives the SAME Editor::ExportGltf the menu item
# calls, then validates the result with gltf_roundtrip -check — the same checker
# the phase 8.3 gate uses, rather than a second one written to agree with it.
#
# Two properties this has that a screenshot does not:
#
#   a screenshot of a menu item shows that the item EXISTS, which is not the
#     claim being made — the same reasoning that produced -wire, -time and
#     -nobones for the other editor controls;
#   the checker is independent of the writer (it parses with wz4t/json.hpp while
#     the writer emits through wz4t/json_write.hpp), so a file that is merely
#     self-consistent does not pass.
#
# It also covers the .glb container end to end, which no golden does: a single
# binary blob is not a reviewable diff, so the goldens are .gltf and this is the
# only test that reads a .glb the editor itself produced.
#
#   cmake -DWZ4ED=<exe> -DCHECKER=<exe> -DDOC=<file.wz4t> -DMETA=<dir>
#         -DSELECT=<store> -DOUT=<file.glb> -P editor_export.cmake

foreach(_var WZ4ED CHECKER DOC SELECT OUT)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "editor_export.cmake: -D${_var}= is required")
  endif()
endforeach()

get_filename_component(_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")
file(REMOVE "${OUT}")

execute_process(
  COMMAND "${WZ4ED}" "${DOC}" -select "${SELECT}" -meta "${META}" -export "${OUT}"
  WORKING_DIRECTORY "${_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _o
  ERROR_VARIABLE _e
  TIMEOUT 120)
message("${_o}${_e}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "wz4ed -export exited ${_rc}")
endif()
if("${_o}${_e}" MATCHES "FATAL ERROR")
  message(FATAL_ERROR "wz4ed printed a fatal error")
endif()

# The status line the editor prints is what a user would read, so it is asserted
# rather than merely produced — an export of zero vertices would otherwise pass
# every check below by writing a structurally valid empty file.
if(NOT "${_o}" MATCHES "exported [1-9][0-9]* vertices, [1-9][0-9]* triangles")
  message(FATAL_ERROR
    "wz4ed did not report a non-empty export for <${SELECT}>.\n"
    "The menu item and this switch share one function, so this is the menu "
    "item's own report.")
endif()

if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "wz4ed reported success but <${OUT}> does not exist")
endif()

execute_process(
  COMMAND "${CHECKER}" "${_dir}" -check "${OUT}"
  WORKING_DIRECTORY "${_dir}"
  RESULT_VARIABLE _crc
  OUTPUT_VARIABLE _co
  ERROR_VARIABLE _ce
  TIMEOUT 120)
message("${_co}${_ce}")

if(NOT _crc EQUAL 0)
  message(FATAL_ERROR
    "the editor wrote <${OUT}> but the glTF checker REJECTS it.\n"
    "The CLI path is covered by the goldens, so a failure here and not there "
    "means the editor's export differs from wz4gen's — most likely the mesh it "
    "handed over, not the writer.")
endif()

message("ok: the editor exported <${SELECT}> and the file validates")
