# "These two runs must render differently."
#
# The gate shape phase 7 needed twice. A control that changes what is on screen
# is proved by rendering with and without it and requiring the images to DIFFER;
# nothing weaker establishes it:
#
#   a report line alone passes if the setting is plumbed through and never acted
#     on — it prints what the code BELIEVES;
#   a single screenshot passes if the control did nothing, because a posed mesh
#     and a rest mesh, or a mesh with and without an overlay, are each equally
#     plausible in isolation;
#   a golden is a false-failure generator, since the image depends on the
#     display's DPI (editor_shot.cmake explains that at length).
#
# Comparing two images produced by the SAME binary in the SAME run sidesteps the
# DPI problem entirely: whatever it is, both shots share it, so any difference is
# the thing under test.
#
#   cmake -DWZ4ED=<exe> -DDOC=<file.wz4t> -DMETA=<dir> -DSELECT=<store>
#         -DOUTDIR=<dir> -DNAME=<slug> "-DARGSA=-time;0" "-DARGSB=-time;50"
#         [-DREQUIRE=<regex each run's output must match>]
#         [-DWHAT=<what the difference proves, for the failure message>]
#         -P editor_differs.cmake

foreach(_var WZ4ED DOC SELECT OUTDIR NAME)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "editor_differs.cmake: -D${_var}= is required")
  endif()
endforeach()

if(NOT DEFINED WHAT)
  set(WHAT "the two runs render identically")
endif()

file(MAKE_DIRECTORY "${OUTDIR}")
set(_a "${OUTDIR}/${NAME}_a.png")
set(_b "${OUTDIR}/${NAME}_b.png")
file(REMOVE "${_a}" "${_b}")

set(_shots "${_a}" "${_b}")
set(_i 0)
foreach(_args "${ARGSA}" "${ARGSB}")
  list(GET _shots ${_i} _out)
  math(EXPR _i "${_i}+1")

  execute_process(
    COMMAND "${WZ4ED}" "${DOC}" -select "${SELECT}" -meta "${META}"
            ${_args} -shot "${_out}"
    WORKING_DIRECTORY "${OUTDIR}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _o
    ERROR_VARIABLE _e
    TIMEOUT 120)
  message("[${_args}] ${_o}${_e}")

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "wz4ed exited ${_rc} for <${_args}>")
  endif()
  if("${_o}${_e}" MATCHES "FATAL ERROR")
    message(FATAL_ERROR "wz4ed printed a fatal error for <${_args}>")
  endif()
  if(DEFINED REQUIRE AND NOT "${_o}" MATCHES "${REQUIRE}")
    message(FATAL_ERROR
      "the viewer's report did not match \"${REQUIRE}\" for <${_args}>.\n"
      "Without that the two images could differ for an unrelated reason and "
      "still pass.")
  endif()
  if(NOT EXISTS "${_out}")
    message(FATAL_ERROR "no screenshot for <${_args}>")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${_a}" "${_b}"
  RESULT_VARIABLE _same
  OUTPUT_QUIET ERROR_QUIET)

if(_same EQUAL 0)
  message(FATAL_ERROR
    "the two renders are IDENTICAL.\n"
    "  [${ARGSA}] ${_a}\n"
    "  [${ARGSB}] ${_b}\n"
    "${WHAT}")
endif()

message("ok: [${ARGSA}] and [${ARGSB}] render differently")
