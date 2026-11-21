# ctest driver of alamo_headless_eval_uw_infra (tst2 t07, b0441; ai_run.hpp --eval-equations). -D EXE.
# The five Underworld infrastructure equations on Heart of the Maw, seed 0, human Empire, after one tick, against retail's
# frame-0 row of docs/re/A2M_R3_RE_A.md §7.2 (run 20261116-020754): all 20 values must be equal. Values print with %.6g;
# retail's table rounds them (0.914, 0.766, 24.89). Without ALAMO_FOC_DATA the run skips and prints no eval line.
execute_process(COMMAND "${EXE}" --economy --galactic-ai --campaign Sandbox_Heart_Of_The_Maw_Empire --human Empire --seed 0
                        --ticks 1 --workers 4 --quiet --story --eval-player Underworld
                        --eval-equations "Should_Build_Underworld_Barracks,Should_Underworld_Save_For_Desirable_Structures,Should_Save_For_Corruption,Needs_Groundbase_Upgrade,Should_Underworld_Build_Starbase"
                RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "alamo_headless --eval-equations run failed (exit ${_rc})\n${_out}\n${_err}")
endif()
string(REPLACE "\r" "" _out "${_out}")
if(_out MATCHES "ALAMO_FOC_DATA unset: skipped")  # ai_run.cpp's own skip line; any other run without eval lines fails below
  message(STATUS "ALAMO_FOC_DATA unset: skipped")
  return()
endif()
set(_want
  "Felucia: Should_Build_Underworld_Barracks=34.92 Should_Underworld_Save_For_Desirable_Structures=0 Should_Save_For_Corruption=0 Needs_Groundbase_Upgrade=0.88 Should_Underworld_Build_Starbase=17"
  "NalHutta: Should_Build_Underworld_Barracks=0 Should_Underworld_Save_For_Desirable_Structures=1 Should_Save_For_Corruption=0 Needs_Groundbase_Upgrade=0.914286 Should_Underworld_Build_Starbase=34"
  "Shola: Should_Build_Underworld_Barracks=24.8914 Should_Underworld_Save_For_Desirable_Structures=0 Should_Save_For_Corruption=0 Needs_Groundbase_Upgrade=0.765714 Should_Underworld_Build_Starbase=17"
  "The_Maw: Should_Build_Underworld_Barracks=18 Should_Underworld_Save_For_Desirable_Structures=0 Should_Save_For_Corruption=0 Needs_Groundbase_Upgrade=0 Should_Underworld_Build_Starbase=27")
set(_bad "")
foreach(_w IN LISTS _want)
  string(FIND "${_out}" "galactic-ai: eval frame 1 p6 ${_w}\n" _at)
  if(_at EQUAL -1)
    string(APPEND _bad "\n  missing: ${_w}")
  endif()
endforeach()
if(_bad)
  string(REGEX MATCHALL "galactic-ai: eval frame[^\n]*" _got "${_out}")
  string(REPLACE ";" "\n  " _got "${_got}")
  message(FATAL_ERROR "the infrastructure equations differ from retail's frame-0 row:${_bad}\ngot:\n  ${_got}")
endif()
