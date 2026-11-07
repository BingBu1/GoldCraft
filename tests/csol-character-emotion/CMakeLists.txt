cmake_minimum_required(VERSION 3.20)
project(csol_character_emotion LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_executable(csol_character_emotion main.cpp
    ../../native/csol/server/scenario_character_skills/host_actions.cpp
    ../../native/csol/scenario/core/config.cpp
    ../../native/csol/scenario/core/spawn_points.cpp)
target_compile_features(csol_character_emotion PRIVATE cxx_std_20)
target_compile_options(csol_character_emotion PRIVATE /utf-8 /W4)
target_include_directories(csol_character_emotion PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include")
