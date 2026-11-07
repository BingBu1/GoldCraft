cmake_minimum_required(VERSION 3.24)
project(GoldCraftCSOLScenario LANGUAGES CXX)

add_library(csol_scenario_config STATIC
    core/config.cpp core/spawn_points.cpp core/waves.cpp core/controller.cpp core/schedule.cpp core/resources.cpp core/map_resources.cpp monsters/monster_types.cpp
    core/trigger_rules.cpp core/chaos_routes.cpp core/trigger_program.cpp core/trigger_wave_actions.cpp core/trigger_round.cpp core/map_catalog.cpp core/wave_events.cpp
    core/message_table.cpp core/message_resource.cpp monsters/monster_attributes.cpp monsters/monster_appearances.cpp monsters/monster_sequences.cpp
    monsters/monster_damage.cpp monsters/monster_death.cpp core/round_settings.cpp core/death_rules.cpp core/drop_items.cpp core/difficulty.cpp monsters/difficulty_monsters.cpp core/navigation.cpp core/wall_damage.cpp core/upgrade_tables.cpp core/upgrade_progress.cpp
    core/equipment.cpp core/equipment_resources.cpp monsters/monster_rewards.cpp monsters/golden.cpp core/damage_display_modes.cpp core/feature3.cpp)
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
