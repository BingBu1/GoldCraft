cmake_minimum_required(VERSION 3.24)
project(GoldCraftCSOLScenario LANGUAGES CXX)

add_library(csol_scenario_config STATIC
    config.cpp spawn_points.cpp waves.cpp controller.cpp schedule.cpp resources.cpp map_resources.cpp monster_types.cpp
    trigger_rules.cpp trigger_program.cpp trigger_wave_actions.cpp trigger_round.cpp map_catalog.cpp wave_events.cpp
    message_table.cpp message_resource.cpp monster_attributes.cpp monster_appearances.cpp monster_sequences.cpp
    monster_damage.cpp monster_death.cpp round_settings.cpp death_rules.cpp drop_items.cpp difficulty.cpp difficulty_monsters.cpp navigation.cpp wall_damage.cpp upgrade_tables.cpp upgrade_progress.cpp
    equipment.cpp equipment_resources.cpp monster_rewards.cpp golden.cpp damage_display_modes.cpp feature3.cpp)
target_compile_features(csol_scenario_config PUBLIC cxx_std_20)
target_include_directories(csol_scenario_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
find_path(CSOL_SCENARIO_RAPIDJSON_INCLUDE_DIR rapidjson/document.h
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../../external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include" REQUIRED)
target_include_directories(csol_scenario_config PRIVATE "${CSOL_SCENARIO_RAPIDJSON_INCLUDE_DIR}")
if(MSVC)
    target_compile_options(csol_scenario_config PRIVATE /utf-8 /W4)
else()
    target_compile_options(csol_scenario_config PRIVATE -Wall -Wextra -Wpedantic)
endif()
