// Exercise DLL static initialization and exports before a game is launched.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <initializer_list>

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (argc < 2) return 2;
    for (int i = 1; i < argc; ++i) {
        const auto module = LoadLibraryExA(argv[i], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        const auto error = module ? 0 : GetLastError();
        std::printf("%s: loaded=%d win32=%lu\n", argv[i], module != nullptr, error);
        std::fflush(stdout);
        if (!module) return 1;
        using Factory = void*(__cdecl*)(const char*, int*);
        const auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface"));
        if (factory) {
            for (const auto name : {"METAHOOK_PLUGIN_API_VERSION004", "MetaRenderer_API_002"}) {
                int result = -1;
                const auto instance = factory(name, &result);
                std::printf("  %s: instance=%d result=%d\n", name, instance != nullptr, result);
            }
        }
    }
    return 0;
}
