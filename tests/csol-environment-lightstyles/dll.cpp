// SPDX-License-Identifier: GPL-3.0-or-later
#include <Windows.h>
#define EXT_FUNC
#include <interface.h>
#include "goldcraft/lightstyle_api.hpp"
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto module = LoadLibraryExA(argv[1], nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) { std::printf("LoadLibrary error=%lu\n", GetLastError()); return 1; }
    const auto factory = reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"));
    auto* service = factory ? static_cast<goldcraft::IMapLightStyleService*>(factory(goldcraft::MapLightStyleServiceVersion, nullptr)) : nullptr;
    unsigned checks = 1, failures = service ? 0 : 1;
    if (service)
    {
        const auto check = [&](bool value) { ++checks; if (!value) ++failures; };
        goldcraft::MapLightStyleStatus status;
        check(!service->Status(status));
        check(!status.known && !status.applied && !status.owner && !status.baseline[0]);
        check(!service->Acquire());
        check(!service->Set(0, 'c'));
        check(!service->Release(0));
    }
    std::printf("actualEngineChecks=%u failures=%u\n", checks, failures);
    FreeLibrary(module);
    return failures ? 1 : 0;
}
