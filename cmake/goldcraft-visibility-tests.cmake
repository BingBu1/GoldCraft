if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        native/server/bridge.cpp external/ReGameDLL_CS/regamedll/dlls/client.cpp tests/native/extract_status_visibility.py)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/extract_status_visibility.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/actual_status_visibility.inc" COMMAND_ERROR_IS_FATAL ANY)
    add_executable(goldcraft_status_visibility_tests tests/native/status_visibility_tests.cpp)
    target_link_libraries(goldcraft_status_visibility_tests PRIVATE goldcraft_common)
    target_include_directories(goldcraft_status_visibility_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_options(goldcraft_status_visibility_tests PRIVATE /W4 /WX /utf-8 /UNDEBUG)
    add_test(NAME goldcraft_status_visibility COMMAND goldcraft_status_visibility_tests)
endif()
