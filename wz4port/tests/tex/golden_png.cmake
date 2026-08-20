# Renders one case and compares it byte-for-byte against its locked golden.
#
# This is the stage 4.4 assertion, and it is deliberately byte-exact rather than
# perceptual. Two reasons:
#
#  - The engine is integer fixed point end to end. There is no floating-point
#    accumulation to excuse a tolerance, so any difference at all is a real
#    behavioural change and worth stopping for.
#  - The goldens are checked in, so running this suite on x86-64 compares the
#    native SSE2 path against arm64 NEON output. A tolerance would let a genuine
#    sse2neon mistranslation through, which is the single most valuable thing
#    this comparison can catch.
#
# On failure it writes an amplified difference image next to the output so the
# discrepancy can be looked at rather than guessed at.
#
# Invoked as:
#   cmake -DWZ4GEN=<exe> -DDOC=<file.wz4t> -DOP=<storename> -DMETA=<dir>
#         -DOUT=<file.png> -DGOLDEN=<file.png> [-DDIFF=<file.png>]
#         -P golden_png.cmake

foreach(_var WZ4GEN DOC OP OUT GOLDEN)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "golden_png.cmake: -D${_var}= is required")
  endif()
endforeach()

if(NOT EXISTS "${GOLDEN}")
  message(FATAL_ERROR
    "no golden for ${OP} at <${GOLDEN}>.\n"
    "A new case needs its golden reviewed and locked — see 06-phase-texture.md "
    "4.4. Do not create it by copying the current output without looking at it.")
endif()

file(REMOVE "${OUT}")
get_filename_component(_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

# WORKING_DIRECTORY is set deliberately. The Export operator writes to whatever
# path its Filename parameter holds, which is relative in a checked-in .wz4t
# because an absolute one would not be portable — so without this it lands in
# whichever directory the caller happened to be in, and littered two copies into
# the repository before this was pinned. Every path passed in above is absolute,
# so moving the cwd is safe.
execute_process(
  COMMAND "${WZ4GEN}" render "${DOC}" -op "${OP}" -meta "${META}" -out "${OUT}"
  WORKING_DIRECTORY "${_dir}"
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

# The report line is locked too, in a .txt beside the .png, and it is checked
# FIRST because it is the stronger of the two.
#
# A PNG golden is blind to any change confined to the low 8 bits of the 16-bit
# pipeline. MakeWz3Bitmap proves the point: it requantises its input through an
# 8-bit sImage, which moves the checksum but leaves the written PNG byte-identical
# to the source. An image-only golden would call that operator a no-op forever.
#
# The report line carries the size, the uniform/structured verdict, the alpha
# range and a checksum over all 16 bits of every pixel.
string(REGEX MATCH "[0-9]+ x [0-9]+, [^\n]*checksum [0-9a-f]+" _report "${_out}")
if(NOT _report)
  message(FATAL_ERROR "could not find a report line in wz4gen's output")
endif()

get_filename_component(_gdir "${GOLDEN}" DIRECTORY)
get_filename_component(_gname "${GOLDEN}" NAME_WE)
set(_gtxt "${_gdir}/${_gname}.txt")

if(NOT EXISTS "${_gtxt}")
  message(FATAL_ERROR "no locked report for ${OP} at <${_gtxt}>")
endif()
file(READ "${_gtxt}" _want)
string(STRIP "${_want}" _want)

if(NOT _report STREQUAL _want)
  message(FATAL_ERROR
    "${OP}'s report does not match its golden.\n"
    "  expected: ${_want}\n"
    "  actual:   ${_report}\n"
    "A changed checksum with an unchanged image means the difference is below "
    "8-bit output precision — real, and invisible in the PNG.")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${GOLDEN}"
  RESULT_VARIABLE _differ
  OUTPUT_QUIET ERROR_QUIET)

if(_differ EQUAL 0)
  message("ok: ${OP} matches its golden, image and checksum")
  return()
endif()

# Different. Produce something a person can look at before failing.
set(_diffmsg "")
if(DEFINED DIFF)
  # `wz4gen diff` exits non-zero when the images differ, which is exactly the
  # case we are in — so its status is ignored and its output is what we want.
  execute_process(
    COMMAND "${WZ4GEN}" diff "${GOLDEN}" "${OUT}" -out "${DIFF}"
    OUTPUT_VARIABLE _dout
    ERROR_VARIABLE _derr)
  set(_diffmsg "\n${_dout}${_derr}  difference image: ${DIFF}")
endif()

message(FATAL_ERROR
  "${OP} does not match its golden.\n"
  "  rendered: ${OUT}\n"
  "  golden:   ${GOLDEN}${_diffmsg}\n"
  "If the change is intended, review the new image and re-lock the golden "
  "deliberately. If it is not, this is exactly what the golden is for.")
