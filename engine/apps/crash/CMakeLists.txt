# [port] alamo_crash: crash minidumps for the port's executables (include/alamo/crash/crash_handler.hpp, docs/BUILD.md
# "Crash dumps"). No sim dependency; linked by alamo_game and alamo_headless, which call alamo::crash::install() first thing.
add_library(alamo_crash STATIC src/crash_handler.cpp)
target_include_directories(alamo_crash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(alamo_crash PRIVATE alamo_engine_flags)
if(WIN32)
  target_link_libraries(alamo_crash PRIVATE dbghelp)
endif()

# Each crash kind in a child process leaves one minidump + note in its own folder under %TEMP%\alamo_crash_tests\<kind>.
add_executable(alamo_crash_tests tests/test_crash.cpp)
target_link_libraries(alamo_crash_tests PRIVATE alamo_crash alamo_test_support)
add_test(NAME alamo_crash_tests COMMAND alamo_crash_tests)
set_tests_properties(alamo_crash_tests PROPERTIES TIMEOUT 120)
