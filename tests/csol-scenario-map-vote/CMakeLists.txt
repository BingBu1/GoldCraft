cmake_minimum_required(VERSION 3.24)
project(CSOLScenarioMapVoteTests LANGUAGES CXX)
add_executable(csol_scenario_map_vote main.cpp ../../native/csol/scenario/map_vote.cpp)
target_compile_features(csol_scenario_map_vote PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(csol_scenario_map_vote PRIVATE /W4 /utf-8)
else()
  target_compile_options(csol_scenario_map_vote PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME authoritative_map_vote COMMAND csol_scenario_map_vote)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/actual_server.inc"
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/extract_server.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../native/csol/server/scenario_map_vote.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/actual_server.inc"
  DEPENDS extract_server.py extract_client.py ../../native/csol/server/scenario_map_vote.cpp VERBATIM)
add_executable(csol_map_vote_server server.cpp "${CMAKE_CURRENT_BINARY_DIR}/actual_server.inc"
  ../../native/csol/scenario/map_vote.cpp)
target_compile_features(csol_map_vote_server PRIVATE cxx_std_20)
target_include_directories(csol_map_vote_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if(MSVC)
  target_compile_options(csol_map_vote_server PRIVATE /W4 /utf-8)
else()
  target_compile_options(csol_map_vote_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME map_vote_controlled_server COMMAND csol_map_vote_server)
set(CSOL_MAP_VOTE_RESOURCES "" CACHE PATH "Exported original map-vote resources")
if(CSOL_MAP_VOTE_RESOURCES)
  find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include" REQUIRED)
  add_executable(csol_map_vote_presentation presentation.cpp
    ../../native/csol/map_vote_presentation.cpp ../../native/csol/scenario/map_vote.cpp ../../native/csol/image_codec.cpp)
  target_compile_features(csol_map_vote_presentation PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(csol_map_vote_presentation PRIVATE /utf-8)
  endif()
  target_include_directories(csol_map_vote_presentation PRIVATE "${RAPIDJSON_INCLUDE_DIR}")
  add_test(NAME map_vote_original_assets COMMAND csol_map_vote_presentation "${CSOL_MAP_VOTE_RESOURCES}")
  if(WIN32)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/actual_client.inc"
      COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/extract_client.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../native/csol/client/scenario_map_vote.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/actual_client.inc"
      DEPENDS extract_client.py ../../native/csol/client/scenario_map_vote.cpp VERBATIM)
    add_executable(csol_map_vote_client client.cpp "${CMAKE_CURRENT_BINARY_DIR}/actual_client.inc"
      ../../native/csol/map_vote_presentation.cpp ../../native/csol/scenario/map_vote.cpp)
    target_compile_features(csol_map_vote_client PRIVATE cxx_std_20)
    if(MSVC)
      target_compile_options(csol_map_vote_client PRIVATE /utf-8)
    endif()
    target_include_directories(csol_map_vote_client PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${RAPIDJSON_INCLUDE_DIR}")
    add_test(NAME map_vote_controlled_ui COMMAND csol_map_vote_client "${CSOL_MAP_VOTE_RESOURCES}/models/csol/mapvote_v1.json")
  endif()
endif()
