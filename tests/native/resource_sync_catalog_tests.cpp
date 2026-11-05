// SPDX-License-Identifier: GPL-3.0-or-later
// Run the actual pinned host parser; a JSON schema-only test would not have
// caught the earlier missing gv_* fields rejected during client startup.
#include "../../external/MetaHookSv-20261007/MetaHook/src/GameData.cpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <iterator>

bool MH_IsInLdrCriticalRegion() { return false; }

namespace {
int checks{}, failures{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::cout << "FAIL: " << message << '\n'; }
}
struct Expected {
    const char* name;
    DWORD rva, source, displacement, length;
};
constexpr std::array globals{
    Expected{"GoldCraft_resource_url", 0x1408d50, 0x1a7865, 1, 5},
    Expected{"GoldCraft_client_state", 0x1408de0, 0x1a8892, 2, 7},
    Expected{"GoldCraft_client_uploading", 0x140e87c, 0x1a8889, 2, 7},
    Expected{"GoldCraft_sprite_handles", 0x32e5bc, 0x19cf9e, 1, 5},
    Expected{"GoldCraft_sprite_capacity", 0x32e5c0, 0x19cfab, 2, 6},
};
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream stream(std::filesystem::path(argv[1]) / "goldcraft-precache-hl-10210.json");
    if (!stream) return 3;
    const std::string text{std::istreambuf_iterator<char>(stream), {}};
    rapidjson::Document snapshot;
    snapshot.Parse(text.c_str());
    check(!snapshot.HasParseError(), "built catalog is valid JSON");
    if (snapshot.HasParseError()) return 4;
    for (const auto& expected : globals) {
        unsigned found{};
        for (const auto& record : snapshot["records"].GetArray()) {
            if (std::string(record["symbolName"].GetString()) != expected.name) continue;
            ++found;
            const auto& payload = record["payload"];
            GameSymbolRecord normalized;
            std::string error;
            check(NormalizeGlobal(payload, normalized, error), "complete global accepted by actual parser");
            check(normalized.rva == expected.rva && normalized.signatureRva == expected.source &&
                  normalized.operandOffset == expected.displacement && normalized.instructionLength == expected.length &&
                  normalized.instructionOffset == 0, "native global parser preserves exact consumer fields");
            for (const char* field : {"gv_rva", "gv_va", "gv_sig", "gv_sig_va",
                 "gv_inst_offset", "gv_inst_disp", "gv_inst_length"}) {
                rapidjson::Document missing;
                missing.CopyFrom(payload, missing.GetAllocator());
                missing.RemoveMember(field);
                GameSymbolRecord rejected;
                error.clear();
                check(!NormalizeGlobal(missing, rejected, error), "each missing global field is rejected");
                check(error == "global payload is missing a required gv_* field", "missing field diagnostics remain precise");
            }
        }
        check(found == 1, "each required sync global appears exactly once");
    }
    const char* roots[]{argv[1]};
    check(GameData::Initialize(roots, 1), "real GameData loads generated index and catalog");
    constexpr std::uint64_t crc = 0x6ef7192cd8254e2d;
    for (const auto& expected : globals) {
        mh_gamesymbol_t symbol{};
        symbol.cbSize = sizeof(symbol);
        check(GameData::QueryByCRC64(crc, expected.name, &symbol) == MH_GAMESYMBOL_OK,
              "real catalog query finds sync global");
        check(symbol.kind == MH_GAMESYMBOL_KIND_GLOBAL && symbol.rva == expected.rva &&
              symbol.signatureRva == expected.source && symbol.instructionOffset == 0 &&
              symbol.operandOffset == expected.displacement && symbol.instructionLength == expected.length,
              "queried global has correct exact engine consumer contract");
        check(GameData::QueryByCRC64(crc ^ 1, expected.name, &symbol) != MH_GAMESYMBOL_OK,
              "different binary identity cannot resolve these global addresses");
    }
    for (const auto& [name, rva] : std::array{
        std::pair{"GoldCraft_SPR_Load", 0x19cf80u},
        std::pair{"GoldCraft_Mod_UnloadSpriteTextures", 0x2417b0u},
        std::pair{"GoldCraft_S_StopAllSounds", 0x1fea30u},
        std::pair{"GoldCraft_COM_LoadCacheFile", 0x1b81b0u},
        std::pair{"GoldCraft_CL_ParseResourceList", 0x1a7690u},
        std::pair{"GoldCraft_Cache_Free", 0x238000u}}) {
        mh_gamesymbol_t symbol{};
        symbol.cbSize = sizeof(symbol);
        check(GameData::QueryByCRC64(crc, name, &symbol) == MH_GAMESYMBOL_OK,
              "native function and baseline precache symbol remain queryable");
        check(symbol.kind == MH_GAMESYMBOL_KIND_FUNCTION && symbol.rva == rva,
              "native function has reviewed RVA and kind");
    }
    std::cout << "resource sync actual catalog parser: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
