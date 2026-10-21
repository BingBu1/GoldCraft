cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(CSOLSupport LANGUAGES C CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "CSOLSupport requires Windows x86")
endif()
get_filename_component(WORKSPACE "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(METAHOOK_ROOT "${WORKSPACE}/external/MetaHookSv-20261007")
set(SDK "${METAHOOK_ROOT}/MetaHook/include")
add_library(CSOLSupport SHARED client/plugin.cpp client/resources.cpp client/scene.cpp client/assets.cpp client/hands.cpp client/weapons.cpp client/camera.cpp
    model_codec.cpp sprite_codec.cpp model_json.cpp image_codec.cpp events.cpp dependencies.cpp model_source.cpp sequences.cpp
    "${SDK}/HLSDK/common/interface.cpp")
target_compile_features(CSOLSupport PRIVATE cxx_std_20)
target_include_directories(CSOLSupport PRIVATE "${SDK}" "${SDK}/Interface"
    "${SDK}/HLSDK/cl_dll" "${SDK}/HLSDK/common" "${SDK}/HLSDK/engine"
    "${SDK}/HLSDK/pm_shared" "${SDK}/HLSDK/public" "${SDK}/SourceSDK"
    "${SDK}/SourceSDK/tier0" "${SDK}/SourceSDK/tier1"
    "${METAHOOK_ROOT}/MetaHook/thirdparty/capstone_fork/include"
    "${METAHOOK_ROOT}/MetaHook/thirdparty/rapidjson/include"
    "${METAHOOK_ROOT}/thirdparty/glew_fork/include"
    "${METAHOOK_ROOT}/Plugins/Renderer/include/Interface")
target_compile_definitions(CSOLSupport PRIVATE WIN32 _WINDOWS _USRDLL _MBCS NO_MALLOC_OVERRIDE
    NOMINMAX GLEW_STATIC GLEW_NO_GLU _CRT_SECURE_NO_WARNINGS)
target_compile_options(CSOLSupport PRIVATE /utf-8 /W3 /Zi)
target_sources(CSOLSupport PRIVATE "${METAHOOK_ROOT}/thirdparty/glew_fork/src/glew.c")
target_link_libraries(CSOLSupport PRIVATE opengl32)
target_link_options(CSOLSupport PRIVATE /DEBUG /INCREMENTAL:NO /OPT:REF /OPT:ICF)
install(TARGETS CSOLSupport RUNTIME DESTINATION cstrike/metahook/plugins)
add_executable(csol_resources resource_list.cpp dependencies.cpp model_codec.cpp model_json.cpp events.cpp model_source.cpp sequences.cpp)
target_compile_features(csol_resources PRIVATE cxx_std_20)
target_include_directories(csol_resources PRIVATE "${METAHOOK_ROOT}/MetaHook/thirdparty/rapidjson/include")
target_compile_definitions(csol_resources PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(csol_resources PRIVATE /utf-8 /W3)
option(CSOL_BUILD_CODEC_TESTS "Build offline codec/oracle checks" OFF)
if(CSOL_BUILD_CODEC_TESTS)
    add_executable(csol_sequence_tests sequence_tests.cpp sequences.cpp model_codec.cpp)
    target_compile_features(csol_sequence_tests PRIVATE cxx_std_20)
    target_compile_definitions(csol_sequence_tests PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    add_executable(csol_codec_tests codec_tests.cpp model_codec.cpp sprite_codec.cpp model_json.cpp image_codec.cpp events.cpp dependencies.cpp model_source.cpp sequences.cpp)
    target_compile_features(csol_codec_tests PRIVATE cxx_std_20)
    target_include_directories(csol_codec_tests PRIVATE "${METAHOOK_ROOT}/MetaHook/thirdparty/rapidjson/include")
    target_compile_definitions(csol_codec_tests PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(csol_codec_tests PRIVATE /utf-8 /W3)
    target_link_libraries(csol_codec_tests PRIVATE bcrypt)
endif()
