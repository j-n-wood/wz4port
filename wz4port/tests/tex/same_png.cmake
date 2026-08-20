# Asserts that two rendered PNGs are byte-identical.
#
# Used for Merge's `brightness` and `hardlight`, which are the same algorithm
# written twice with two different ways of building the same mask. Their equality
# is a property of the code, so it is worth asserting directly: it is a free
# cross-check that two different SSE2 intrinsics were translated consistently.
#
#   cmake -DWZ4GEN=<exe> -DA=<a.png> -DB=<b.png> -P same_png.cmake

foreach(_var A B)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "same_png.cmake: -D${_var}= is required")
  endif()
endforeach()

foreach(_f "${A}" "${B}")
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR
      "<${_f}> has not been rendered. This test depends on the two cases that "
      "produce it; run the full suite rather than this test alone.")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${A}" "${B}"
  RESULT_VARIABLE _differ
  OUTPUT_QUIET ERROR_QUIET)

if(_differ EQUAL 0)
  message("ok: the two renders are byte-identical, as the two kernels require")
  return()
endif()

if(DEFINED WZ4GEN)
  execute_process(
    COMMAND "${WZ4GEN}" diff "${A}" "${B}"
    OUTPUT_VARIABLE _dout ERROR_VARIABLE _derr)
  message("${_dout}${_derr}")
endif()

message(FATAL_ERROR
  "these two renders differ, and they must not.\n"
  "  ${A}\n  ${B}\n"
  "Merge's brightness and hardlight kernels are algorithmically identical, so a "
  "difference here means one of them changed, or an intrinsic used by only one "
  "of them is being translated wrongly.")
