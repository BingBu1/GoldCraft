# Standalone Asio: headers only, pinned independently of Boost and game assets.
if(NOT TARGET goldcraft_asio)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(GOLDCRAFT_ASIO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    execute_process(COMMAND "${Python3_EXECUTABLE}" -X utf8 -B
        "${GOLDCRAFT_ASIO_ROOT}/tools/Prepare-Asio.py"
        OUTPUT_VARIABLE GOLDCRAFT_ASIO_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    add_library(goldcraft_asio INTERFACE)
    target_include_directories(goldcraft_asio SYSTEM INTERFACE "${GOLDCRAFT_ASIO_INCLUDE}")
    target_include_directories(goldcraft_asio INTERFACE "${GOLDCRAFT_ASIO_ROOT}/native/include")
    target_compile_definitions(goldcraft_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED
        _WIN32_WINNT=0x0A00)
    target_compile_features(goldcraft_asio INTERFACE cxx_std_20)
    target_link_libraries(goldcraft_asio INTERFACE ws2_32 mswsock winhttp)
endif()
