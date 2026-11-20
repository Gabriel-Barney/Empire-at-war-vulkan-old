# S1 - game object model: GameObjectType database (every XML element kept, schema from
# docs/re/xml_field_tables.md, field coverage report), typed/merged type view (OBJECT_TYPES.md),
# entity ids + SoA object store (OBJECT_RUNTIME.md).
add_library(alamo_objects STATIC
  src/field_schema.cpp
  src/object_types.cpp
  src/coverage.cpp
  src/behaviors.cpp
  src/type_fields.cpp
  src/enums.cpp
  src/game_object_types.cpp
  src/object_store.cpp
  # mcx0 t02: tag ledger (generated/tag_ledger.inc from tools/gen_tag_ledger.py; read by the load report).
  src/tag_ledger.cpp
)
add_library(alamo::objects ALIAS alamo_objects)
target_include_directories(alamo_objects PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(alamo_objects PUBLIC alamo_data alamo_core alamo_engine_flags)

add_executable(alamo_objects_tests tests/test_objects.cpp)
target_link_libraries(alamo_objects_tests PRIVATE alamo_objects Threads::Threads alamo_test_support)
# Coverage reports for real data go to <build dir>/logs/objects_coverage_<game>.txt (per build tree, so
# two builds' ctests running at once never share the file; the test prints the path).
target_compile_definitions(alamo_objects_tests PRIVATE "ALAMO_OBJECTS_LOG_DIR=\"${CMAKE_BINARY_DIR}/logs\"")
# Real installs keep their XML in MEGs: mount them through the VFS, as alamo_headless does.
if(TARGET alamo_vfs)
  target_link_libraries(alamo_objects_tests PRIVATE alamo_vfs)
  target_compile_definitions(alamo_objects_tests PRIVATE ALAMO_OBJECTS_HAVE_VFS=1)
endif()
# The ModelAnimType converter is injected (GameObjectTypes::build's AnimTypeLookup): alamo_anim links
# alamo_objects, so only the TEST links alamo_anim, to check the real table. alamo_anim is configured
# after this folder, hence the generator expressions.
target_link_libraries(alamo_objects_tests PRIVATE $<TARGET_NAME_IF_EXISTS:alamo_anim>)
target_compile_definitions(alamo_objects_tests PRIVATE $<$<TARGET_EXISTS:alamo_anim>:ALAMO_OBJECTS_HAVE_ANIM=1>)
add_test(NAME alamo_objects_tests COMMAND alamo_objects_tests)

# mcx0 t02 / t03: the tag ledger's lookup, and the ledger lint (tools/lint/check_tag_ledger.py: tag_ledger.inc and the
# Lua binding table's effect column match the code). build_agent.cmd and build_gate.cmd run the lint as well.
add_executable(alamo_objects_tag_ledger_tests tests/test_tag_ledger.cpp)
target_link_libraries(alamo_objects_tag_ledger_tests PRIVATE alamo_objects alamo_test_support)
add_test(NAME alamo_objects_tag_ledger_tests COMMAND alamo_objects_tag_ledger_tests)
set_tests_properties(alamo_objects_tag_ledger_tests PROPERTIES TIMEOUT 60)
find_program(ALAMO_LINT_PYTHON NAMES python PATHS ${PROJECT_SOURCE_DIR}/toolchain/venv/Scripts NO_DEFAULT_PATH)
find_program(ALAMO_LINT_PYTHON NAMES python python3)
if(ALAMO_LINT_PYTHON)
  add_test(NAME alamo_objects_tag_ledger_lint
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_tag_ledger.py)
  add_test(NAME alamo_objects_tag_ledger_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_tag_ledger.py --selftest)
  set_tests_properties(alamo_objects_tag_ledger_lint alamo_objects_tag_ledger_lint_selftest PROPERTIES TIMEOUT 120)
  # ref6 t01: string tag reads in sim code may only go down (tools/lint/tag_reads_baseline.txt; read through type_tags.hpp)
  add_test(NAME alamo_objects_tag_reads_lint COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_tag_reads.py)
  add_test(NAME alamo_objects_tag_reads_lint_selftest
           COMMAND ${ALAMO_LINT_PYTHON} -I ${PROJECT_SOURCE_DIR}/tools/lint/check_tag_reads.py --selftest)
  set_tests_properties(alamo_objects_tag_reads_lint alamo_objects_tag_reads_lint_selftest PROPERTIES TIMEOUT 60)
endif()
