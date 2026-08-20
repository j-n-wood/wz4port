# Runs the editor for a couple of frames, screenshots it, and checks the result.
#
# This is how a GUI gets a regression test in this port. `wz4ed -shot` renders
# two frames and writes the framebuffer as a PNG, which proves rather more than
# it looks like: GLFW created a window, a GL 3.3 core context came up, ImGui
# built its font atlas and produced draw data, the GL3 backend executed it, the
# document loaded, and the metadata reached the panels.
#
# Not golden-locked, and deliberately. The image depends on the display's DPI —
# the same command here produces 1280x800 or 2560x1600 depending on which screen
# the window opens on — and on the platform's font rasterisation. Same reasoning
# as the Text cases in stage 4.5: a byte-exact golden would be a false-failure
# generator. What is asserted is that a plausible screenshot was produced and
# that the tool reported no error.
#
# REQUIRES A GRAPHICAL SESSION. It fails over ssh or in headless CI, where GLFW
# cannot open a display. Configure with -DWZ4_GUI_TESTS=OFF there.
#
#   cmake -DWZ4ED=<exe> -DDOC=<file.wz4t> -DMETA=<dir> -DOUT=<file.png>
#         [-DSELECT=<storename>] -P editor_shot.cmake

foreach(_var WZ4ED DOC OUT)
  if(NOT DEFINED ${_var})
    message(FATAL_ERROR "editor_shot.cmake: -D${_var}= is required")
  endif()
endforeach()

file(REMOVE "${OUT}")
get_filename_component(_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

set(_args "${DOC}")
if(DEFINED SELECT AND NOT SELECT STREQUAL "")
  list(APPEND _args -select "${SELECT}")
endif()
if(DEFINED META AND NOT META STREQUAL "")
  list(APPEND _args -meta "${META}")
endif()
list(APPEND _args -shot "${OUT}")

# The document comes first: Altona's shell parser treats the token after a
# -switch as that switch's parameter, so a leading switch would swallow it.
execute_process(
  COMMAND "${WZ4ED}" ${_args}
  WORKING_DIRECTORY "${_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  TIMEOUT 120)
message("${_out}${_err}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "wz4ed exited ${_rc}.\n"
    "If this says a window or GL context could not be created, the machine has "
    "no graphical session — configure with -DWZ4_GUI_TESTS=OFF.")
endif()

# Altona's allocator is process-global and its handlers are gone by the time
# static destructors run, so a teardown that unwinds through the GUI frameworks
# printed "FATAL ERROR: pointer ... seems not to belong to any sMemoryHandler"
# on every clean exit. The editor now exits before that. Asserted, because the
# message went to stdout with a zero exit code and so was invisible to any check
# that only looked at the status.
if("${_out}${_err}" MATCHES "FATAL ERROR")
  message(FATAL_ERROR "wz4ed printed a fatal error while exiting cleanly")
endif()

if(NOT EXISTS "${OUT}")
  message(FATAL_ERROR "no screenshot was written to <${OUT}>")
endif()

file(SIZE "${OUT}" _bytes)
if(_bytes LESS 4096)
  message(FATAL_ERROR
    "<${OUT}> is only ${_bytes} bytes — too small to be a rendered UI")
endif()

file(READ "${OUT}" _magic LIMIT 8 HEX)
if(NOT _magic STREQUAL "89504e470d0a1a0a")
  message(FATAL_ERROR "<${OUT}> is not a PNG (first 8 bytes ${_magic})")
endif()

message("ok: the editor started, drew, and screenshotted (${_bytes} bytes)")
