cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(CSOLScenarioMapProfileTests LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(SCENARIO ../../native/csol/scenario)
add_executable(csol_scenario_map_profile_tests main.cpp
    ${SCENARIO}/config.cpp ${SCENARIO}/spawn_points.cpp ${SCENARIO}/resources.cpp
    ${SCENARIO}/map_resources.cpp ${SCENARIO}/monster_types.cpp ${SCENARIO}/trigger_rules.cpp
    ${SCENARIO}/chaos_routes.cpp ${SCENARIO}/map_catalog.cpp ${SCENARIO}/boss_skills.cpp
    ${SCENARIO}/upgrade_tables.cpp ${SCENARIO}/difficulty.cpp ${SCENARIO}/difficulty_monsters.cpp
    ${SCENARIO}/monster_attributes.cpp)
target_compile_features(csol_scenario_map_profile_tests PRIVATE cxx_std_20)
target_include_directories(csol_scenario_map_profile_tests PRIVATE ${SCENARIO})
if(MSVC)
    target_compile_options(csol_scenario_map_profile_tests PRIVATE /utf-8 /W4)
endif()
enable_testing()
add_test(NAME csol_scenario_map_profile COMMAND csol_scenario_map_profile_tests)
