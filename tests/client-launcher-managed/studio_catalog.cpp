// SPDX-License-Identifier: GPL-3.0-or-later
// Query the actual MetaHook resolver against the launcher's installed files.
#include "../../external/MetaHookSv-20261007/MetaHook/src/GameData.cpp"
#include <algorithm>
#include <filesystem>

bool MH_IsInLdrCriticalRegion() { return false; }

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const bool fixed = std::string(argv[2]) == "fixed";
    if (!fixed && std::string(argv[2]) != "issued") return 2;
    const auto game = std::filesystem::absolute(argv[1]);
    const auto root = game / "cstrike/metahook/gamedata";
    std::vector<std::string> names{root.string()};
    for (const auto& file : std::filesystem::recursive_directory_iterator(root))
        if (file.path().filename() == "index.json" && file.path().parent_path() != root)
            names.push_back(file.path().parent_path().string());
    std::sort(names.begin() + 1, names.end());
    std::vector<const char*> roots;
    for (const auto& name : names) roots.push_back(name.c_str());
    if (!GameData::Initialize(roots.data(), roots.size())) return 3;

    // Map the exact x86 files without running engine or client entry points.
    auto engine = LoadLibraryExW((game / "hw.dll").c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    auto client = LoadLibraryExW((game / "cstrike/cl_dlls/client.dll").c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!client || !engine) return 4;
    struct Request {
        HMODULE module;
        const char* name;
        mh_gamesymbol_kind_t kind;
        DWORD rva;
    };
    const Request requests[] = {
        {engine, "CSOL_StudioNativeCall", MH_GAMESYMBOL_KIND_PATCH, 0x240559},
        {engine, "CSOL_StudioReadCall", MH_GAMESYMBOL_KIND_PATCH, 0x24038b},
        {engine, "CSOL_StudioRead", MH_GAMESYMBOL_KIND_FUNCTION, 0x1c80f0},
        {client, "GoldCraftSniperScope_Draw", MH_GAMESYMBOL_KIND_FUNCTION, 0x64f90},
        {client, "GoldCraftCofInput_CreateMove", MH_GAMESYMBOL_KIND_FUNCTION, 0x5b320},
        {client, "GoldCraftCofInput_MaximumSlot", MH_GAMESYMBOL_KIND_GLOBAL, 0x1179d8},
    };
    unsigned failures = 0;
    for (const auto& request : requests) {
        void* address{};
        const auto status = MH_ResolveGameSymbol(request.module, request.name, request.kind, &address);
        const auto base = reinterpret_cast<const BYTE*>(request.module);
        const bool ok = fixed ? status == MH_GAMESYMBOL_OK && address == base + request.rva
                              : status == MH_GAMESYMBOL_SYMBOL_NOT_FOUND && !address;
        std::printf("%s: %s (%s)\n", request.name, MH_GetGameSymbolStatusString(status), ok ? "expected" : "unexpected");
        failures += !ok;
    }
    GameData::ResetModuleIdentities();
    FreeLibrary(client);
    FreeLibrary(engine);
    return failures ? 1 : 0;
}
