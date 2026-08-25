# The stage 7.4 gate: scrubbing the timeline changes what is on screen.
#
# Runs the editor TWICE against the same rigged operator at two different times
# and requires the two screenshots to DIFFER. That is the whole claim of a
# scrubber, and nothing weaker establishes it:
#
#   the report line alone would pass if the time were plumbed through and the
#   vertices never re-skinned — it prints what the code BELIEVES;
#   a single screenshot would pass if the mesh were stuck in its rest pose,
#   because a posed mesh and a rest mesh look equally plausible;
#   a golden would be a false-failure generator, since the image depends on the
#   display's DPI (see editor_shot.cmake).
#
# Comparing two images produced by the SAME binary in the SAME run sidesteps all
# of that: whatever the DPI, both shots share it, so any difference is the pose.
#
#   cmake -DWZ4ED=<exe> -DDOC=<file.wz4t> -DMETA=<dir> -DSELECT=<store>
#         -DOUTDIR=<dir> [-DTIMEA=0] [-DTIMEB=50] -P editor_pose.cmake

foreach(_var WZ4ED DOC SELECT OUTDIR)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "editor_pose.cmake: -D${_var}= is required")
  endif()
endforeach()

if(NOT DEFINED TIMEA)
  set(TIMEA 0)
endif()
if(NOT DEFINED TIMEB)
  set(TIMEB 50)
endif()

file(MAKE_DIRECTORY "${OUTDIR}")
set(_a "${OUTDIR}/pose_a.png")
set(_b "${OUTDIR}/pose_b.png")
file(REMOVE "${_a}" "${_b}")

# -time takes a PERCENTAGE: Altona's shell parser has an integer parameter getter
# and no float one, so 0..100 maps to t = 0..1.
foreach(_pair "${TIMEA}|${_a}" "${TIMEB}|${_b}")
  string(REPLACE "|" ";" _parts "${_pair}")
  list(GET _parts 0 _t)
  list(GET _parts 1 _out)

  execute_process(
    COMMAND "${WZ4ED}" "${DOC}" -select "${SELECT}" -meta "${META}"
            -time "${_t}" -shot "${_out}"
    WORKING_DIRECTORY "${OUTDIR}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _o
    ERROR_VARIABLE _e
    TIMEOUT 120)
  message("t=${_t}: ${_o}${_e}")

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "wz4ed exited ${_rc} at t=${_t}")
  endif()
  if("${_o}${_e}" MATCHES "FATAL ERROR")
    message(FATAL_ERROR "wz4ed printed a fatal error at t=${_t}")
  endif()

  # The mesh must have reached the viewer AS A RIG. Without this the two images
  # could differ for some unrelated reason and still pass.
  if(NOT "${_o}" MATCHES "rig [1-9][0-9]* bone\\(s\\) at t =")
    message(FATAL_ERROR
      "the viewer did not report a rig for <${SELECT}> at t=${_t}.\n"
      "A mesh with no skeleton cannot be scrubbed — check that the graph uses "
      "Deform with its \"keep bones\" flag.")
  endif()
  if(NOT EXISTS "${_out}")
    message(FATAL_ERROR "no screenshot at t=${_t}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${_a}" "${_b}"
  RESULT_VARIABLE _same
  OUTPUT_QUIET ERROR_QUIET)

if(_same EQUAL 0)
  message(FATAL_ERROR
    "the two poses are IDENTICAL.\n"
    "  t=${TIMEA}: ${_a}\n"
    "  t=${TIMEB}: ${_b}\n"
    "The timeline reported a rig and a time but the geometry did not move — so "
    "either the pose is not re-skinned per frame, or the channels are constant "
    "(which is what every operator produced before AnimateBones existed).")
endif()

message("ok: t=${TIMEA} and t=${TIMEB} render differently — the scrubber poses the mesh")
