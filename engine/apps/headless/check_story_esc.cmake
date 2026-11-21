# ctest driver of alamo_headless_story_esc (tst2 t02, b0143; ai_run.hpp --story-esc-at). -D EXE, TRACE, PY (may be empty), READER.
# The UM01 Esc run of retail (docs/re/STORY_T04_KESSEL_RUN.md §1, run 20261108-234224): Underworld campaign, seed 0, the
# linked Kessel land mission, Esc after battle frame 128 while the intro cinematic plays. Retail calls Story_Handle_Esc
# between ticks; the mission's handler starts IntroCineCleanup, whose spawns (MILLENNIUM_FALCON_LANDING, URAI_FEN, ...) appear
# at frame 129 and whose despawns (the UMP_CINE_* objects) at 130. Hands-off the same objects come only at 5361 / 7542.
# 1. The run must report that the story took the key: "pressed at battle frame 128, ..., 1 Story_Handle_Esc calls".
# 2. The battle's tactical trace (tools/parity/story_battle.py --json) must show the cleanup within one story service of
#    the Esc: spawns in 129..136, the mouse droid's removal in 130..137. [port] The port services the plot on its own
#    phase (128, 135 here; retail 129), so its cleanup lands at 135 / 136: filed as a story service cadence difference.
file(REMOVE "${TRACE}")
get_filename_component(_dir "${TRACE}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")
execute_process(COMMAND "${EXE}" --economy --galactic-ai --story --battles fight --campaign Full_Story_Campaign_Underworld
                        --ai-type Underworld=AI_Player_Underworld --human Underworld --seed 0 --ticks 200 --workers 4 --quiet
                        --story-esc-at 128 --tactical-trace-out "${TRACE}"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "alamo_headless --story-esc-at run failed (exit ${_rc})\n${_out}\n${_err}")
endif()
if(NOT EXISTS "${TRACE}")
  message(STATUS "no trace written (ALAMO_FOC_DATA unset): skipped")
  return()
endif()
if(NOT _out MATCHES "story esc: pressed at battle frame 128, applied at host frame [0-9]+, 1 Story_Handle_Esc calls")
  string(REGEX MATCH "story esc:[^\n]*" _line "${_out}")
  message(FATAL_ERROR "the story did not take the Esc after battle frame 128: '${_line}'")
endif()
if(NOT PY)
  message(STATUS "no python found for story_battle.py: the trace check is skipped")
  return()
endif()
set(_json "${TRACE}.json")
execute_process(COMMAND "${PY}" -I "${READER}" "${TRACE}" --json "${_json}" RESULT_VARIABLE _rc OUTPUT_QUIET)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "story_battle.py failed on ${TRACE} (exit ${_rc})")
endif()
file(READ "${_json}" _j)

# The first frame at which an entry of `list_key` (spawned_after_frame1 / removed) has type `type`; -1 when none.
function(first_frame list_key type out)
  set(_f -1)
  string(JSON _n ERROR_VARIABLE _e LENGTH "${_j}" retail ${list_key})
  if(NOT _e AND _n GREATER 0)
    math(EXPR _last "${_n} - 1")
    foreach(_i RANGE ${_last})
      string(JSON _t GET "${_j}" retail ${list_key} ${_i} type)
      if(_t STREQUAL "${type}")
        string(JSON _f GET "${_j}" retail ${list_key} ${_i} frame)
        break()
      endif()
    endforeach()
  endif()
  set(${out} ${_f} PARENT_SCOPE)
endfunction()

set(_bad "")
foreach(_t MILLENNIUM_FALCON_LANDING URAI_FEN UMP_HANSOLO UMP_CHEWBACCA)
  first_frame(spawned_after_frame1 ${_t} _f)
  message(STATUS "spawn ${_t}: battle frame ${_f} (retail 129)")
  if(_f LESS 129 OR _f GREATER 136)
    string(APPEND _bad " spawn ${_t} at ${_f};")
  endif()
endforeach()
first_frame(removed UMP_CINE_MOUSEDROID _f)
message(STATUS "remove UMP_CINE_MOUSEDROID: battle frame ${_f} (retail 130)")
if(_f LESS 130 OR _f GREATER 137)
  string(APPEND _bad " remove UMP_CINE_MOUSEDROID at ${_f};")
endif()
if(_bad)
  message(FATAL_ERROR "IntroCineCleanup not within one story service of the Esc:${_bad}")
endif()
