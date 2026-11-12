// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include "../../external/MetaHookSv-20261007/MetaHook/src/GameData.cpp"
#include <filesystem>

bool MH_IsInLdrCriticalRegion() { return false; }

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    std::vector<std::string> paths{argv[1]};
    for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1]))
        if (entry.is_regular_file() && entry.path().filename() == "index.json" &&
            entry.path().parent_path() != std::filesystem::path(argv[1]))
            paths.push_back(entry.path().parent_path().string());
    std::sort(paths.begin() + 1, paths.end());
    std::vector<const char*> roots;
    for (const auto& path : paths) roots.push_back(path.c_str());
    if (!GameData::Initialize(roots.data(), roots.size())) return 3;
    struct Expected { const char* name; mh_gamesymbol_kind_t kind; DWORD rva; DWORD operand; DWORD length; };
    const Expected expected[]{
        {"GoldCraftParticle_Observer", MH_GAMESYMBOL_KIND_GLOBAL, 0x110E10, 1, 5},
        {"GoldCraftParticle_RenderAngles", MH_GAMESYMBOL_KIND_GLOBAL, 0x134AF8, 4, 8},
        {"GoldCraftParticle_ChaseAngles", MH_GAMESYMBOL_KIND_GLOBAL, 0x134AEC, 4, 8},
        {"CSOL_ClientCRTRand", MH_GAMESYMBOL_KIND_GLOBAL, 0xBF220, 2, 6},
        {"GoldCraftCof_MouseStage", MH_GAMESYMBOL_KIND_PATCH, 0x3605D, 0, 0},
        {"GoldCraftCof_MouseResume", MH_GAMESYMBOL_KIND_PATCH, 0x362A9, 0, 0},
    };
    unsigned failures{};
    for (const auto& item : expected)
    {
        mh_gamesymbol_t symbol{};
        symbol.cbSize = sizeof(symbol);
        const auto result = MH_QueryGameSymbolByCRC64(0x124D6034A6B28B40ULL, item.name, &symbol);
        const bool valid = result == MH_GAMESYMBOL_OK && symbol.kind == item.kind && symbol.rva == item.rva &&
            (item.kind != MH_GAMESYMBOL_KIND_GLOBAL ||
                (symbol.operandOffset == item.operand && symbol.instructionLength == item.length && symbol.signature.length));
        std::printf("%s: %s status=%d\n", item.name, valid ? "PASS" : "FAIL", int(result));
        failures += !valid;
    }
    std::printf("catalogs=%zu checked=%zu failures=%u\n", roots.size(), std::size(expected), failures);
    if (failures) std::puts(GameData::GetDiagnostics().c_str());
    return failures ? 1 : 0;
}
