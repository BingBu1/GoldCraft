if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ReGameDLL_CS/regamedll/pm_shared/pm_shared.cpp")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(fluid_pm_source "${CMAKE_CURRENT_SOURCE_DIR}/external/ReGameDLL_CS/regamedll/pm_shared/pm_shared.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${fluid_pm_source}" tests/native/extract_fluid_movement.py)
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/extract_fluid_movement.py"
        "${fluid_pm_source}" "${CMAKE_CURRENT_BINARY_DIR}/fluid_movement_actual.inc"
        COMMAND_ERROR_IS_FATAL ANY)
    add_executable(goldcraft_fluid_movement_tests tests/native/fluid_movement_tests.cpp)
    target_link_libraries(goldcraft_fluid_movement_tests PRIVATE goldcraft_common)
    target_include_directories(goldcraft_fluid_movement_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
        external/ReGameDLL_CS/regamedll/pm_shared external/ReGameDLL_CS/regamedll/common
        external/ReGameDLL_CS/regamedll/engine)
    target_compile_definitions(goldcraft_fluid_movement_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(goldcraft_fluid_movement_tests PRIVATE /W3 /utf-8 /UNDEBUG)
    add_test(NAME goldcraft_fluid_movement COMMAND goldcraft_fluid_movement_tests)
endif()
