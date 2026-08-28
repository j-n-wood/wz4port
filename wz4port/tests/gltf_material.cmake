# The stage 9.3 gate: a material assigned in a document reaches the glTF.
#
# Two exports, because they test different halves and neither covers the other:
#
#   mm_moved   a TEXTURED material, and downstream of a Transform, so it also
#              shows the material survived an unrelated operator;
#   mf_flat    an UNTEXTURED one with an asymmetric non-grey colour, which is the
#              only way to see whether baseColorFactor was converted from the
#              display-referred colour a picker gives to the linear one glTF
#              specifies. White passes either way.
#
# Both are .glb, so the texture travels inside the file and the image bufferView
# path is exercised. The .gltf sidecar path is covered by the goldens.
#
#   cmake -DWZ4GEN=<exe> -DCHECKER=<exe> -DDOC=<file.wz4t> -DMETA=<dir>
#         -DOUTDIR=<dir> -P gltf_material.cmake

foreach(_var WZ4GEN CHECKER DOC OUTDIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "gltf_material.cmake: -D${_var}= is required")
  endif()
endforeach()

file(MAKE_DIRECTORY "${OUTDIR}")
set(_tex "${OUTDIR}/mtrl_textured.glb")
set(_flat "${OUTDIR}/mtrl_flat.glb")
file(REMOVE "${_tex}" "${_flat}")

foreach(_pair "mm_moved|${_tex}" "mf_flat|${_flat}")
  string(REPLACE "|" ";" _parts "${_pair}")
  list(GET _parts 0 _store)
  list(GET _parts 1 _out)

  execute_process(
    COMMAND "${WZ4GEN}" render "${DOC}" -op "${_store}" -meta "${META}" -out "${_out}"
    WORKING_DIRECTORY "${OUTDIR}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e TIMEOUT 120)
  message("${_o}${_e}")

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "wz4gen render exited ${_rc} for <${_store}>")
  endif()
  if(NOT EXISTS "${_out}")
    message(FATAL_ERROR "no file written for <${_store}>")
  endif()
endforeach()

# The report has to say a material and a texture were written. Without this the
# checker below would pass on a file that simply had no materials at all — every
# assertion there is conditional on finding one.
if(NOT "${_o}" MATCHES "1 material")
  message(FATAL_ERROR
    "wz4gen did not report a material for <mf_flat>.\n"
    "A mesh whose cluster carries no material exports the grey default and every "
    "material assertion downstream is vacuously satisfied.")
endif()

execute_process(
  COMMAND "${CHECKER}" "${OUTDIR}" -textured "${_tex}" -flat "${_flat}"
  WORKING_DIRECTORY "${OUTDIR}"
  RESULT_VARIABLE _crc OUTPUT_VARIABLE _co ERROR_VARIABLE _ce TIMEOUT 120)
message("${_co}${_ce}")

if(NOT _crc EQUAL 0)
  message(FATAL_ERROR "the material chain did not validate")
endif()

# --- and the .gltf half, where a texture cannot live inside the file ---------
#
# JSON has nowhere to put image bytes, so a textured .gltf writes each texture as
# a PNG beside itself and names it by uri. That path shares no code with the .glb
# one — bufferView versus sidecar file — and no golden covers it, because none of
# the six golden stores is textured. Without this it would be the only untested
# branch of the exporter, which is exactly how the .glb container was missed in
# phase 8.4.

set(_gltf "${OUTDIR}/mtrl_textured.gltf")
file(REMOVE "${_gltf}")

execute_process(
  COMMAND "${WZ4GEN}" render "${DOC}" -op mm_moved -meta "${META}" -out "${_gltf}"
  WORKING_DIRECTORY "${OUTDIR}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _o ERROR_VARIABLE _e TIMEOUT 120)
message("${_o}${_e}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "wz4gen render exited ${_rc} writing .gltf")
endif()

# The uri the JSON names must be a file that exists, next to the .gltf. A uri is
# resolved relative to the file naming it, so a sidecar written to the working
# directory instead would load from exactly one cwd and nowhere else.
file(READ "${_gltf}" _json)
if(NOT _json MATCHES "\"uri\": \"([A-Za-z0-9_.]+\\.png)\"")
  message(FATAL_ERROR
    "the .gltf names no PNG uri — a textured .gltf must write its texture beside "
    "itself, since JSON cannot carry image bytes")
endif()
set(_png "${OUTDIR}/${CMAKE_MATCH_1}")
if(NOT EXISTS "${_png}")
  message(FATAL_ERROR
    "the .gltf names <${CMAKE_MATCH_1}> but no such file was written beside it")
endif()

# And it is a PNG, not an empty file with the right name.
file(SIZE "${_png}" _pngsize)
if(_pngsize LESS 8)
  message(FATAL_ERROR "the sidecar texture is ${_pngsize} bytes")
endif()
file(READ "${_png}" _magic LIMIT 4 HEX)
if(NOT _magic STREQUAL "89504e47")
  message(FATAL_ERROR "the sidecar texture is not a PNG (magic ${_magic})")
endif()

execute_process(
  COMMAND "${CHECKER}" "${OUTDIR}" -textured "${_gltf}"
  WORKING_DIRECTORY "${OUTDIR}"
  RESULT_VARIABLE _crc2 OUTPUT_VARIABLE _co2 ERROR_VARIABLE _ce2 TIMEOUT 120)
message("${_co2}${_ce2}")
if(NOT _crc2 EQUAL 0)
  message(FATAL_ERROR "the .gltf material chain did not validate")
endif()

message("ok: materials export in both containers, with the texture embedded in "
        ".glb and beside the .gltf")
