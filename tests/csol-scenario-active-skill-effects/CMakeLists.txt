cmake_minimum_required(VERSION 3.20)
project(csol_scenario_active_skill_effects LANGUAGES CXX)
add_executable(csol_scenario_active_skill_effects
    main.cpp
    ../../native/csol/server/scenario_active_skill_effects.cpp)
target_compile_features(csol_scenario_active_skill_effects PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(csol_scenario_active_skill_effects PRIVATE -ffp-contract=off)
endif()
enable_testing()
add_test(NAME csol_scenario_active_skill_effects COMMAND csol_scenario_active_skill_effects)
