# ref4 t05: alamo_game_open - open_game_data (include/alamo/game/open_game_data.hpp), the one way alamo_game and every
# alamo_headless runner open the game's data. vfs + core only, so alamo_headless's Phase 2 main.cpp (built without
# alamo_sim, hence without alamo_game_core) links it too. Added from engine/apps/CMakeLists.txt before alamo_headless.
add_library(alamo_game_open STATIC open_game_data.cpp)
target_include_directories(alamo_game_open PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(alamo_game_open PUBLIC alamo_vfs alamo_core alamo_engine_flags)
