# Renders one operator of a .wz4t document to a PNG and checks that the result
# is a real PNG file, not just a successful-looking exit code.
#
# `wz4gen render` reports its own errors, but sImage::SavePNG returning true is
# not by itself evidence that anything usable reached the disk: a zero-byte or
# truncated file would still pass a PASS_REGULAR_EXPRESSION on the tool's
# output. So this checks the artefact — it exists, it is not implausibly small,
# and it starts with the PNG signature.
#
# Invoked as:
#   cmake -DWZ4GEN=<exe> -DDOC=<file.wz4t> -DOP=<storename> -DMETA=<dir>
#         -DOUT=<file.png> [-DEXPECT=<regex on the tool's stdout>]
#         -P render_png.cmake

foreach(_var WZ4GEN DOC OP OUT)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "render_png.cmake: -D${_var}= is required")
  endif()
endforeach()

# Remove any output from a previous run first, so a tool that writes nothing at
# all cannot pass on a stale file. The directory has to exist: sImage::SavePNG
# does not create one, it just fails, so this is the runner's job.
file(REMOVE "${OUT}")
get_filename_component(_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

execute_process(
  COMMAND "${WZ4GEN}" render "${DOC}" -op "${OP}" -meta "${META}" -out "${OUT}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)
message("${_out}${_err}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "wz4gen render exited ${_rc}")
endif()

if(DEFINED EXPECT AND NOT "${_out}" MATCHES "${EXPECT}")
  message(FATAL_ERROR "output did not match expected pattern <${EXPECT}>")
endif()

# REJECT is the negative form, and the reason it exists is `renders blank`: a
# bitmap whose alpha is zero everywhere saves as a valid PNG of the right size
# that displays as plain white. Every check above passes on it and a reviewer
# cannot tell it from a white image. So the default for every case is "must not
# be blank", and a case that means to be blank has to say so.
if(DEFINED REJECT AND NOT REJECT STREQUAL "" AND "${_out}" MATCHES "${REJECT}")
  message(FATAL_ERROR "output matched forbidden pattern <${REJECT}>")
endif()

if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "no file was written to <${OUT}>")
endif()

file(SIZE "${OUT}" _bytes)
if(_bytes LESS 256)
  message(FATAL_ERROR "<${OUT}> is only ${_bytes} bytes")
endif()

file(READ "${OUT}" _magic LIMIT 8 HEX)
if(NOT _magic STREQUAL "89504e470d0a1a0a")
  message(FATAL_ERROR "<${OUT}> is not a PNG (first 8 bytes ${_magic})")
endif()

message("ok: ${OP} -> ${OUT}, ${_bytes} bytes, PNG signature present")
