# Fuzzing of the readers that take mod-supplied bytes (docs/BUILD.md, "Fuzzing").
#
# 1. alamo_fuzz_smoke (every tree, mcx2 t08): a seeded, deterministic mutation fuzzer over smoke/fuzz_targets.hpp (TED,
#    DAT, MTD, GUIDialogs.rc, animation clips, particle systems, .lc chunks, WAV, Bink headers), seeded from synthetic
#    inputs, the game (ALAMO_FOC_DATA) and the installed workshop mods. A slow-labelled ctest entry with a short time
#    budget per target; no third-party fuzzer.
# 2. libFuzzer targets (-DALAMO_FUZZ=ON, build_fuzz.cmd: a clang tree with ASan + fuzzer instrumentation on every target):
#    targets/*.cpp plus one fuzz_<name> per smoke target. Not ctest entries: tools/fuzz/run_fuzz.py runs them for a
#    time budget.

# ---- 1. the mutation harness ----
add_executable(alamo_fuzz_smoke smoke/fuzz_smoke.cpp smoke/fuzz_targets.cpp)
target_link_libraries(alamo_fuzz_smoke PRIVATE alamo_core alamo_engine_flags)
set(_alamo_fuzz_deps)
foreach(_pair "TERRAIN=alamo_terrain" "UI=alamo_ui_hud" "ANIM=alamo_anim" "PARTICLES=alamo_render_particles"
              "LUA=alamo_lua_vm" "AUDIO=alamo_audio" "VIDEO=alamo_video" "VFS=alamo_vfs")
  string(REPLACE "=" ";" _kv "${_pair}")
  list(GET _kv 0 _flag)
  list(GET _kv 1 _lib)
  if(TARGET ${_lib})
    target_link_libraries(alamo_fuzz_smoke PRIVATE ${_lib})
    target_compile_definitions(alamo_fuzz_smoke PRIVATE ALAMO_FUZZ_HAVE_${_flag}=1)
    list(APPEND _alamo_fuzz_deps "${_flag}=${_lib}")
  endif()
endforeach()
add_test(NAME alamo_fuzz_smoke
         COMMAND alamo_fuzz_smoke --seconds 6 --out ${CMAKE_CURRENT_BINARY_DIR}/fuzz_smoke)
set_tests_properties(alamo_fuzz_smoke PROPERTIES LABELS slow TIMEOUT 600
                     PASS_REGULAR_EXPRESSION "FUZZ_SMOKE PASS")

# ---- 2. libFuzzer targets ----
if(ALAMO_FUZZ)
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
  if(TARGET alamo_fx2)
    alamo_add_fuzzer(fuzz_fx2 alamo_fx2)
  endif()
  if(TARGET alamo_assets)
    alamo_add_fuzzer(fuzz_alo alamo_assets)
    alamo_add_fuzzer(fuzz_ala alamo_assets)
    alamo_add_fuzzer(fuzz_texture alamo_assets)
  endif()
  # mcx2 t08: one libFuzzer target per smoke target (targets/fuzz_mod_formats.cpp picks it by ALAMO_FUZZ_TARGET).
  foreach(_name ted dat mtd rc clip particle lc wav bink)
    add_executable(fuzz_${_name} targets/fuzz_mod_formats.cpp smoke/fuzz_targets.cpp)
    target_include_directories(fuzz_${_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/smoke)
    target_compile_definitions(fuzz_${_name} PRIVATE ALAMO_FUZZ_TARGET="${_name}")
    target_link_libraries(fuzz_${_name} PRIVATE alamo_core alamo_engine_flags)
    foreach(_dep ${_alamo_fuzz_deps})
      string(REPLACE "=" ";" _kv "${_dep}")
      list(GET _kv 0 _flag)
      list(GET _kv 1 _lib)
      target_link_libraries(fuzz_${_name} PRIVATE ${_lib})
      target_compile_definitions(fuzz_${_name} PRIVATE ALAMO_FUZZ_HAVE_${_flag}=1)
    endforeach()
    target_link_options(fuzz_${_name} PRIVATE -fsanitize=fuzzer)
    list(APPEND _alamo_fuzz_targets fuzz_${_name})
  endforeach()
  add_custom_target(alamo_fuzzers DEPENDS ${_alamo_fuzz_targets})
endif()
