# ref3 t05: alamo_game --list-flags --json (game_main.cpp kGameFlags, schema alamo.game_flags) must equal the copy the
# Command Center's tests read (launcher/src/testdata/game_flags.json), line for line (the exe writes CRLF to a file, the checkout has LF). The launcher itself reads the list
# from the exe at run time; this keeps its test data from going stale.
#   cmake -DGAME=<alamo_game.exe> -DEXPECTED=<launcher/src/testdata/game_flags.json> -DOUT=<file> -P list_flags_check.cmake
# After a flag change: copy OUT over EXPECTED (git stores it with LF).
execute_process(COMMAND "${GAME}" --list-flags --json OUTPUT_FILE "${OUT}" RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "alamo_game --list-flags --json exited with ${rc}: ${err}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol "${OUT}" "${EXPECTED}" RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
  message(FATAL_ERROR "the flag list differs from ${EXPECTED}: copy ${OUT} over it (a flag was added, renamed or changed)")
endif()
message(STATUS "flag list: same as ${EXPECTED}")
