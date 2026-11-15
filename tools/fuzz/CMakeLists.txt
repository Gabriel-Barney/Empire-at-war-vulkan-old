# libFuzzer targets for the readers that take mod-supplied bytes (docs/BUILD.md, "Fuzzing"). Configured only with
# -DALAMO_FUZZ=ON by build_fuzz.cmd (clang, ASan + libFuzzer instrumentation on every target in that tree). Not ctest
# entries: a fuzz run is open-ended; tools/fuzz/run_fuzz.py runs them for a time budget.
set(_alamo_fuzz_targets)
function(alamo_add_fuzzer name)
  add_executable(${name} targets/${name}.cpp)
  target_link_libraries(${name} PRIVATE ${ARGN} alamo_engine_flags)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  set(_alamo_fuzz_targets ${_alamo_fuzz_targets} ${name} PARENT_SCOPE)
endfunction()

if(TARGET alamo_data)
  alamo_add_fuzzer(fuzz_xml alamo_data)
endif()
alamo_add_fuzzer(fuzz_meg alamo_meg)
if(TARGET alamo_assets)
  alamo_add_fuzzer(fuzz_alo alamo_assets)
  alamo_add_fuzzer(fuzz_ala alamo_assets)
  alamo_add_fuzzer(fuzz_texture alamo_assets)
endif()
add_custom_target(alamo_fuzzers DEPENDS ${_alamo_fuzz_targets})
