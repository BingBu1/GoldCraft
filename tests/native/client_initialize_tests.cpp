// Verify forwarding against the real matched CS client, including its callback
// beyond the SDK table and the Director message that previously called null.
#include <metahook.h>
#include <event_api.h>
#include <bcrypt.h>
#include "../../native/client/client_initialize.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using FilteredCommand = int (__cdecl*)(const char*);
struct RuntimeEngine { cl_enginefunc_t known{}; FilteredCommand filtered{}; };
static_assert(sizeof(cl_enginefunc_t) == 536 && offsetof(RuntimeEngine, filtered) == 536);
int calls = 0;
int __cdecl filtered_command(const char* command) {
    assert(std::strcmp(command, "goldcraft_initialize_probe\n") == 0);
    ++calls;
    return 1;
}
}

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
    const bool legacy = argc == 3;
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file), {}};
    std::array<unsigned char, 32> digest{};
    assert(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        const_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()), digest.data(), ULONG(digest.size())) == 0);
    constexpr std::array<unsigned char,32> expected{
        0xb4,0x34,0xb1,0xc0,0x9b,0x10,0xb0,0x11,0xbe,0x6c,0x42,0xe4,0x15,0x49,0x55,0xda,
        0x0c,0x3c,0xa4,0x6e,0x95,0x74,0x69,0x88,0xeb,0xea,0x1a,0xa3,0x16,0xa2,0xa9,0xf2};
    assert(digest == expected);
    const auto module = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    assert(module);
    const auto base = reinterpret_cast<unsigned char*>(module);
    const auto initialize = reinterpret_cast<INITIALIZE_FUNC>(GetProcAddress(module, "Initialize"));
    const auto director = reinterpret_cast<void (__cdecl*)(int, void*)>(GetProcAddress(module, "HUD_DirectorMessage"));
    assert(initialize && director);
    // Hash above pins this copy loop and global; never write these runtime RVAs.
    assert(!std::memcmp(base + 0x47837, "\xb9\x87\0\0\0", 5));
    auto* captured = reinterpret_cast<RuntimeEngine*>(base + 0x117920);
    event_api_t original{}, proxy{}, nested{};
    RuntimeEngine engine;
    engine.known.pEventAPI = &original;
    engine.filtered = filtered_command;
    if (legacy) {
        RuntimeEngine truncated;
        truncated.known = engine.known; // Reproduce the old SDK-sized clone.
        assert(initialize(&truncated.known, 6) == 0);
        assert(captured->filtered == nullptr);
        std::cout << "Old SDK-sized clone loses the native Director callback\n";
        FreeLibrary(module);
        return 0;
    }
    const auto before = engine;
    // Version6 returns immediately AFTER the real540-byte copy, avoiding game
    // initialization. Then execute the unchanged native Director text branch.
    assert(goldcraft::client::initialize_with_events(&engine.known, 6, &proxy, initialize) == 0);
    assert(captured->known.pEventAPI == &proxy && captured->filtered == filtered_command);
    assert(!std::memcmp(&engine, &before, sizeof(engine)));
    char message[] = "\x0a" "goldcraft_initialize_probe\n";
    director(sizeof(message), message);
    assert(calls == 1);

    for (const int result : {0, 1}) {
        const auto returned = goldcraft::client::initialize_with_events(&engine.known, 7, &proxy,
            [&](cl_enginefunc_t* forwarded, int version) {
                assert(forwarded == &engine.known && version == 7 && forwarded->pEventAPI == &proxy);
                assert(engine.filtered == filtered_command);
                assert(goldcraft::client::initialize_with_events(forwarded, version, &nested,
                    [&](cl_enginefunc_t* inner, int) { assert(inner->pEventAPI == &nested); return 9; }) == 9);
                assert(forwarded->pEventAPI == &proxy);
                return result;
            });
        assert(returned == result && !std::memcmp(&engine, &before, sizeof(engine)));
    }
    bool caught = false;
    try {
        goldcraft::client::initialize_with_events(&engine.known, 7, &proxy,
            [](cl_enginefunc_t*, int) -> int { throw std::runtime_error("initialize failure"); });
    } catch (const std::runtime_error&) { caught = true; }
    assert(caught && !std::memcmp(&engine, &before, sizeof(engine)));
    FreeLibrary(module);
    std::cout << "Native540-byte Initialize + Director callback, nested/failure restoration passed\n";
}
