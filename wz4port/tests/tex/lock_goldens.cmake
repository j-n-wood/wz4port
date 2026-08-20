# Locks the current renders as the goldens.
#
# Run deliberately, never as part of a build:
#
#   cmake -DBUILD=<builddir> -P wz4port/tests/tex/lock_goldens.cmake
#
# For every case in <build>/tex-cases.txt it renders straight into
# tests/tex/golden/ and writes the tool's report line beside the image as a .txt.
# Both are checked by golden_png.cmake: the image is what a person reviewed, and
# the report carries a checksum over all 16 bits of every pixel, which the 8-bit
# PNG cannot represent.
#
# The whole value of a golden is that somebody looked at it. Re-locking to turn a
# red suite green throws that away, and is the process-level failure that
# 06-phase-texture.md warns about. When a case legitimately changes: review the
# new image, re-lock, and say in the commit what changed and why.

if(NOT DEFINED BUILD)
  message(FATAL_ERROR "lock_goldens.cmake: -DBUILD=<builddir> is required")
endif()

get_filename_component(_here "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
set(_golden "${_here}/golden")
set(_listfile "${BUILD}/tex-cases.txt")

if(NOT EXISTS "${_listfile}")
  message(FATAL_ERROR "no case list at <${_listfile}> — configure the build first")
endif()

set(_wz4gen "${BUILD}/wz4gen")
if(NOT EXISTS "${_wz4gen}")
  message(FATAL_ERROR "no wz4gen at <${_wz4gen}> — build first")
endif()

file(STRINGS "${_listfile}" _cases)
file(MAKE_DIRECTORY "${_golden}")

set(_n 0)
foreach(_case ${_cases})
  if("${_case}" STREQUAL "")
    continue()
  endif()
  string(REPLACE "|" ";" _parts "${_case}")
  list(GET _parts 0 _doc)
  list(GET _parts 1 _op)

  set(_png "${_golden}/${_doc}_${_op}.png")
  set(_txt "${_golden}/${_doc}_${_op}.txt")

  # cwd pinned to the build dir: the Export operator writes a relative path and
  # would otherwise drop files wherever this script was invoked from.
  execute_process(
    COMMAND "${_wz4gen}" render "${_here}/${_doc}.wz4t" -op "${_op}"
            -meta "${BUILD}/meta" -out "${_png}"
    WORKING_DIRECTORY "${BUILD}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

  if(NOT _rc EQUAL 0)
    message("${_out}${_err}")
    message(FATAL_ERROR "could not render ${_doc}/${_op}; nothing further locked")
  endif()

  string(REGEX MATCH "[0-9]+ x [0-9]+, [^\n]*checksum [0-9a-f]+" _report "${_out}")
  if(NOT _report)
    message(FATAL_ERROR "no report line for ${_doc}/${_op}")
  endif()
  file(WRITE "${_txt}" "${_report}\n")

  math(EXPR _n "${_n} + 1")
endforeach()

message(STATUS "locked ${_n} golden(s) into ${_golden}")
message(STATUS "run ctest to see them checked")
