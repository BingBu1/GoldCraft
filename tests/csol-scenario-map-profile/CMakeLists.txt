cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(CSOLScenarioMapProfileTests LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(SCENARIO ../../native/csol/scenario)
add_executable(csol_scenario_map_profile_tests main.cpp
    ${SCENARIO}/core/config.cpp ${SCENARIO}/core/spawn_points.cpp ${SCENARIO}/core/resources.cpp
    ${SCENARIO}/core/map_resources.cpp ${SCENARIO}/monsters/monster_types.cpp ${SCENARIO}/core/trigger_rules.cpp
    ${SCENARIO}/core/chaos_routes.cpp ${SCENARIO}/core/map_catalog.cpp ${SCENARIO}/bosses/boss_skills.cpp
    ${SCENARIO}/core/upgrade_tables.cpp ${SCENARIO}/core/difficulty.cpp ${SCENARIO}/monsters/difficulty_monsters.cpp
    ${SCENARIO}/monsters/monster_attributes.cpp)
target_compile_features(csol_scenario_map_profile_tests PRIVATE cxx_std_20)
target_include_directories(csol_scenario_map_profile_tests PRIVATE ${SCENARIO})
if(MSVC)
    target_compile_options(csol_scenario_map_profile_tests PRIVATE /utf-8 /W4)
endif()
enable_testing()
add_test(NAME csol_scenario_map_profile COMMAND csol_scenario_map_profile_tests)
