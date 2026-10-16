# Included after the GoldCraft target's SDK include directories are set.
target_sources(GoldCraft PRIVATE native/client/steam_async.cpp
    native/client/steam_async_state.cpp native/client/steam_async_transport.cpp)
add_executable(GoldCraftSteamWorker native/client/steam_async_worker.cpp)
add_dependencies(GoldCraft GoldCraftSteamWorker)
target_compile_definitions(GoldCraftSteamWorker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(GoldCraftSteamWorker PRIVATE /W4 /WX /utf-8)
install(TARGETS GoldCraftSteamWorker RUNTIME DESTINATION cstrike/metahook/plugins)
if(BUILD_TESTING)
    add_executable(goldcraft_steam_async_tests tests/native/steam_async_tests.cpp native/client/steam_async_state.cpp)
    add_executable(goldcraft_steam_async_adapter_tests tests/native/steam_async_adapter_tests.cpp
        native/client/steam_async_state.cpp native/client/steam_async_transport.cpp)
    get_target_property(GOLDCRAFT_STEAM_SDK GoldCraft INCLUDE_DIRECTORIES)
    target_include_directories(goldcraft_steam_async_adapter_tests PRIVATE ${GOLDCRAFT_STEAM_SDK})
    add_executable(goldcraft_steam_test_worker tests/native/steam_async_test_worker.cpp)
    add_executable(goldcraft_steam_transport_tests tests/native/steam_async_transport_tests.cpp
        native/client/steam_async_state.cpp native/client/steam_async_transport.cpp)
    add_library(goldcraft_steam_lifetime SHARED tests/native/steam_async_lifetime_dll.cpp
        tests/native/steam_async_lifetime_dll.def native/client/steam_async_state.cpp native/client/steam_async_transport.cpp)
    target_include_directories(goldcraft_steam_lifetime PRIVATE ${GOLDCRAFT_STEAM_SDK})
    add_executable(goldcraft_steam_lifetime_tests tests/native/steam_async_lifetime_tests.cpp)
    add_library(goldcraft_steam_worker_fake_api SHARED tests/native/steam_worker_fake_api.cpp tests/native/steam_worker_fake_api.def)
    add_executable(goldcraft_steam_worker_isolation_tests tests/native/steam_worker_isolation_tests.cpp native/client/steam_async_transport.cpp)
    add_executable(goldcraft_steam_gamedata_tests tests/native/steam_gamedata_tests.cpp)
    target_include_directories(goldcraft_steam_gamedata_tests PRIVATE ${GOLDCRAFT_STEAM_SDK}
        external/MetaHookSv-20261007/MetaHook/include
        external/MetaHookSv-20261007/MetaHook/thirdparty/Chocobo1Hash/src
        external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include)
    target_compile_definitions(goldcraft_steam_gamedata_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(goldcraft_steam_gamedata_tests PRIVATE /W3 /utf-8 /UNDEBUG)
    foreach(target IN ITEMS goldcraft_steam_async_tests goldcraft_steam_async_adapter_tests goldcraft_steam_test_worker goldcraft_steam_transport_tests goldcraft_steam_lifetime goldcraft_steam_lifetime_tests goldcraft_steam_worker_fake_api goldcraft_steam_worker_isolation_tests)
        target_include_directories(${target} PRIVATE native/client)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_options(${target} PRIVATE /W3 /utf-8 /UNDEBUG)
    endforeach()
    add_test(NAME goldcraft_steam_async COMMAND goldcraft_steam_async_tests)
    add_test(NAME goldcraft_steam_async_adapter COMMAND goldcraft_steam_async_adapter_tests)
    add_test(NAME goldcraft_steam_transport COMMAND goldcraft_steam_transport_tests $<TARGET_FILE:goldcraft_steam_test_worker>)
    add_test(NAME goldcraft_steam_lifetime COMMAND goldcraft_steam_lifetime_tests $<TARGET_FILE:goldcraft_steam_lifetime> $<TARGET_FILE:goldcraft_steam_test_worker>)
    set_tests_properties(goldcraft_steam_async goldcraft_steam_async_adapter goldcraft_steam_transport goldcraft_steam_lifetime PROPERTIES TIMEOUT 15)
    add_test(NAME goldcraft_steam_worker_isolation COMMAND goldcraft_steam_worker_isolation_tests $<TARGET_FILE:GoldCraftSteamWorker> $<TARGET_FILE:goldcraft_steam_worker_fake_api> "${CMAKE_CURRENT_BINARY_DIR}/steam-worker-isolation-events.txt")
    set_tests_properties(goldcraft_steam_worker_isolation PROPERTIES TIMEOUT 15)
endif()
