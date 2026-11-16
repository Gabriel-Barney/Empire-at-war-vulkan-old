# ctest driver of alamo_headless_skirmish_plan_retest (skai t15, TacticalAiConfig::plan_retest). -D EXE, LOG.
# The synthetic skirmish (no game data) with --plan-retest and --check-workers: the running-plan contrast re-test
# (docs/re/SKAI_T15_PLAN_RETEST.md) ends at least one plan (the decision line and the stat), and worker counts agree.
# Since skai t18 the re-test is on by default (--plan-retest changes nothing, --no-plan-retest turns it off), so
# alamo_headless_skirmish_ai_synthetic checks the same run against its golden; this test keeps the explicit flag.
file(REMOVE "${LOG}")
get_filename_component(_dir "${LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")
execute_process(COMMAND "${EXE}" --skirmish-ai synthetic --plan-retest --check-workers --quiet --ai-log "${LOG}"
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "alamo_headless --skirmish-ai synthetic --plan-retest run failed (exit ${_rc})")
endif()
if(NOT EXISTS "${LOG}")
  message(FATAL_ERROR "no AI log written: ${LOG}")
endif()
file(READ "${LOG}" _t)
foreach(_need "contrast re-test failed: main space" "plan invalid" "running-plan contrast re-test failed")
  string(FIND "${_t}" "${_need}" _at)
  if(_at LESS 0)
    message(FATAL_ERROR "AI log ${LOG} has no '${_need}'")
  endif()
endforeach()
