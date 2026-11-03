cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(CSOLScenarioSurvivalTests LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT DEFINED SURVIVAL_GENERATED_DIR)
    set(SURVIVAL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
endif()
foreach(name actual_timeout.inc actual_lostcity_points.inc)
    if(NOT EXISTS "${SURVIVAL_GENERATED_DIR}/${name}")
        message(FATAL_ERROR "Run prepare.py first; missing ${SURVIVAL_GENERATED_DIR}/${name}")
    endif()
endforeach()
set(SCENARIO ../../native/csol/scenario)
add_executable(csol_survival_core main.cpp
    ${SCENARIO}/config.cpp ${SCENARIO}/spawn_points.cpp ${SCENARIO}/waves.cpp
    ${SCENARIO}/controller.cpp ${SCENARIO}/schedule.cpp ${SCENARIO}/resources.cpp
    ${SCENARIO}/monster_types.cpp ${SCENARIO}/trigger_rules.cpp
    ${SCENARIO}/trigger_program.cpp ${SCENARIO}/trigger_wave_actions.cpp
    ${SCENARIO}/trigger_round.cpp ${SCENARIO}/round_settings.cpp
    ${SCENARIO}/boss_skills.cpp ${SCENARIO}/upgrade_tables.cpp)
target_compile_features(csol_survival_core PRIVATE cxx_std_20)
target_compile_options(csol_survival_core PRIVATE /utf-8 /W4)
add_executable(csol_survival_timeout timeout.cpp)
target_compile_features(csol_survival_timeout PRIVATE cxx_std_20)
target_compile_options(csol_survival_timeout PRIVATE /utf-8 /W4)
target_include_directories(csol_survival_timeout PRIVATE "${SURVIVAL_GENERATED_DIR}")
add_executable(csol_survival_inventory inventory.cpp ${SCENARIO}/config.cpp
    ${SCENARIO}/monster_types.cpp ${SCENARIO}/monster_appearances.cpp ${SCENARIO}/spawn_points.cpp)
target_compile_features(csol_survival_inventory PRIVATE cxx_std_20)
target_compile_options(csol_survival_inventory PRIVATE /utf-8 /W4)
target_include_directories(csol_survival_inventory PRIVATE ../../native/csol)

set(REGA ../../external/ReGameDLL_CS/regamedll)
add_executable(csol_survival_map map.cpp boundaries.cpp
    ../csol-scenario-point-manager/engine_fixture.cpp
    ../csol-scenario-entities/engine_stubs.cpp ../csol-scenario-player-scan/player_stubs.cpp
    ../csol-scenario-frame/rules_stubs.cpp
    ../../native/csol/server/scenario_map.cpp ../../native/csol/server/scenario_frame.cpp
    ../../native/csol/server/scenario_spawn_points.cpp ../../native/csol/server/scenario_point_manager.cpp
    ../../native/csol/server/scenario_trigger.cpp ../../native/csol/server/scenario_trigger_manager.cpp
    ../../native/csol/server/scenario_player_scan.cpp
    ${SCENARIO}/config.cpp ${SCENARIO}/spawn_points.cpp ${SCENARIO}/waves.cpp
    ${SCENARIO}/controller.cpp ${SCENARIO}/schedule.cpp ${SCENARIO}/resources.cpp
    ${SCENARIO}/monster_types.cpp ${SCENARIO}/trigger_rules.cpp ${SCENARIO}/trigger_program.cpp
    ${SCENARIO}/trigger_wave_actions.cpp ${SCENARIO}/trigger_round.cpp
    ${SCENARIO}/map_catalog.cpp ${SCENARIO}/map_resources.cpp ${SCENARIO}/death_rules.cpp
    ${SCENARIO}/difficulty.cpp ${SCENARIO}/difficulty_monsters.cpp
    ${SCENARIO}/boss_skills.cpp ${SCENARIO}/upgrade_tables.cpp)
target_compile_features(csol_survival_map PRIVATE cxx_std_20)
target_compile_options(csol_survival_map PRIVATE /utf-8 /W4)
target_compile_definitions(csol_survival_map PRIVATE REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES
    REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST
    BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS)
target_include_directories(csol_survival_map PRIVATE "${REGA}" "${REGA}/hookers" "${REGA}/common"
    ../../native/csol/server "${SURVIVAL_GENERATED_DIR}"
    "${REGA}/dlls" "${REGA}/engine" "${REGA}/public" "${REGA}/public/regamedll"
    "${REGA}/pm_shared" "${REGA}/regamedll" "${REGA}/testsuite")
