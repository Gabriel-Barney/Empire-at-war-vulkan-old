# P3 - audio: SFX / music / speech event databases and player (docs/PHASE4.md P3).
#
# Vendored miniaudio 0.11.25 (approved by Gabriel 2026-10-27): the pristine GitHub tag archive unpacked
# by toolchain\8_miniaudio.cmd into toolchain\miniaudio-0.11.25 (SHA-256 pinned there). Like engine/lua,
# the single implementation file is compiled AS C++ by the engine's compiler (the project enables only
# CXX), in its own target with third-party warnings off (/W0, -w); its include dir is SYSTEM. Our code
# includes the header only through src/ma_include.hpp.
set(ALAMO_MINIAUDIO_DIR "${PROJECT_SOURCE_DIR}/toolchain/miniaudio-0.11.25" CACHE PATH "miniaudio source tree (toolchain\\8_miniaudio.cmd)")
if(NOT EXISTS "${ALAMO_MINIAUDIO_DIR}/miniaudio.h")
  message(WARNING "engine/audio skipped: miniaudio not found in ${ALAMO_MINIAUDIO_DIR} (run toolchain\\8_miniaudio.cmd)")
  return()
endif()
if(NOT TARGET alamo_vfs OR NOT TARGET alamo_data)
  message(WARNING "engine/audio skipped: needs the vfs and data modules (ALAMO_ENGINE_MODULES)")
  return()
endif()

# Copy so the TU has a .cpp name next to nothing else (the tree stays pristine).
set(_ma_cpp "${CMAKE_CURRENT_BINARY_DIR}/miniaudio_impl.cpp")
file(WRITE "${_ma_cpp}.in" "#define MINIAUDIO_IMPLEMENTATION\n#include \"miniaudio.h\"\n")
configure_file("${_ma_cpp}.in" "${_ma_cpp}" COPYONLY)
add_library(alamo_miniaudio STATIC "${_ma_cpp}")
target_include_directories(alamo_miniaudio SYSTEM PUBLIC "${ALAMO_MINIAUDIO_DIR}")
# Only what the game needs: decoders for WAV and MP3, playback. No FLAC, no encoders, no file-path
# helpers on the engine side (all data comes through the VFS as memory).
target_compile_definitions(alamo_miniaudio PUBLIC MA_NO_FLAC MA_NO_ENCODING MA_NO_GENERATION)
target_compile_features(alamo_miniaudio PRIVATE cxx_std_20)
# tst1: vendored, built without alamo_engine_flags (/fp:precise), so the shared PCH (ALAMO_PCH, on without the compiler
# cache) does not fit it: MSVC warned C4651 ('/D_M_FP_PRECISE' specified for precompiled header) on every fresh compile.
set_property(TARGET alamo_miniaudio PROPERTY DISABLE_PRECOMPILE_HEADERS ON)
if(MSVC)
  target_compile_options(alamo_miniaudio PRIVATE /W0)
else()
  target_compile_options(alamo_miniaudio PRIVATE -w)
endif()
find_package(Threads REQUIRED)
target_link_libraries(alamo_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
  target_link_libraries(alamo_miniaudio PUBLIC m)
endif()

add_library(alamo_audio STATIC
  src/audio_db.cpp
  src/sample_source.cpp
  src/decode.cpp
  src/audio_system.cpp
  src/miniaudio_backend.cpp
  src/game_audio_data.cpp
  src/ambience.cpp
  src/conversation.cpp
  src/game_audio.cpp
  src/pcm_stream.cpp
)
add_library(alamo::audio ALIAS alamo_audio)
target_include_directories(alamo_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(alamo_audio PUBLIC alamo_data alamo_vfs alamo_core alamo_engine_flags PRIVATE alamo_miniaudio)
# Lua presentation log -> audio player bridge (only when engine/lua is configured).
if(TARGET alamo_lua)
  target_sources(alamo_audio PRIVATE src/lua_audio.cpp)
  target_link_libraries(alamo_audio PUBLIC alamo_lua)
  target_compile_definitions(alamo_audio PUBLIC ALAMO_AUDIO_HAVE_LUA=1)
endif()

add_executable(alamo_audio_tests tests/test_audio.cpp)
target_link_libraries(alamo_audio_tests PRIVATE alamo_audio alamo_test_support)
# Coverage reports (XML fields, sample resolution, decode sample) go to <project>/logs/audio_*.txt.
target_compile_definitions(alamo_audio_tests PRIVATE "ALAMO_AUDIO_LOG_DIR=\"${PROJECT_SOURCE_DIR}/logs\"")
add_test(NAME alamo_audio_tests COMMAND alamo_audio_tests)
set_tests_properties(alamo_audio_tests PROPERTIES TIMEOUT 300)

# Game-host layer: ambience (weather / map), conversations, GameAudio event hooks, Lua bridge via GameAudio.
add_executable(alamo_audio_game_tests tests/test_game_audio.cpp)
target_link_libraries(alamo_audio_game_tests PRIVATE alamo_audio alamo_test_support)
target_compile_definitions(alamo_audio_game_tests PRIVATE "ALAMO_AUDIO_LOG_DIR=\"${PROJECT_SOURCE_DIR}/logs\"")
add_test(NAME alamo_audio_game_tests COMMAND alamo_audio_game_tests)
set_tests_properties(alamo_audio_game_tests PROPERTIES TIMEOUT 300)

# ums0 t05: shader_files_from_vfs and default_sfx_megs on the VFS layers equal the pre-ums0 code on the reference
# mods (data-driven; skip code 77 without ALAMO_FOC_DATA). Needs the materials module for the shader half.
if(TARGET alamo_materials)
  add_executable(alamo_audio_layer_equivalence_tests tests/test_layer_equivalence.cpp)
  target_link_libraries(alamo_audio_layer_equivalence_tests PRIVATE alamo_audio alamo_materials alamo_test_support)
  add_test(NAME alamo_audio_layer_equivalence_tests COMMAND alamo_audio_layer_equivalence_tests)
  set_tests_properties(alamo_audio_layer_equivalence_tests PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
else()
  message(STATUS "alamo_audio_layer_equivalence_tests not built: needs the materials module")
endif()
