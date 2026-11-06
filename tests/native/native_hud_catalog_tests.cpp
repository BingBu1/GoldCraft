// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the actual pinned parser and resolver without loading a game DLL.
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
    unsigned module;
    mh_gamesymbol_kind_t kind;
    DWORD rva, size;
};
constexpr std::array expected{
    Expected{"GoldCraftNativeHud_State", 0, MH_GAMESYMBOL_KIND_GLOBAL, 0x117b48, 0},
    Expected{"GoldCraftNativeHud_VidInit", 0, MH_GAMESYMBOL_KIND_FUNCTION, 0x47790, 16},
    Expected{"GoldCraftNativeHud_DeleteArray", 0, MH_GAMESYMBOL_KIND_FUNCTION, 0xb4919, 5},
    Expected{"GoldCraftNativeHud_GetSurface", 0, MH_GAMESYMBOL_KIND_FUNCTION, 0x78320, 6},
    Expected{"GoldCraftNativeHud_SourceFree", 1, MH_GAMESYMBOL_KIND_FUNCTION, 0x1da3e0, 6},
    Expected{"GoldCraftNativeHud_HostFrame", 1, MH_GAMESYMBOL_KIND_FUNCTION, 0x1d28b0, 0x283},
};
constexpr std::array<std::uint64_t, 2> crcs{0x124d6034a6b28b40, 0x6ef7192cd8254e2d};
constexpr std::array<DWORD, 2> sizes{0x153000, 0x14aa000};

bool inspect_snapshot(const char* directory, unsigned module) {
    const auto filename = module ? "goldcraft-precache-hl-10210.json" : "cs-client-10210-goldcraft.json";
    std::ifstream stream(std::filesystem::path(directory) / filename);
    const std::string text{std::istreambuf_iterator<char>(stream), {}};
    rapidjson::Document snapshot;
    snapshot.Parse(text.c_str());
    if (snapshot.HasParseError() || !snapshot.IsObject() || !snapshot.HasMember("records")) return false;
    for (const auto& item : expected) {
        if (item.module != module) continue;
        unsigned found{};
        for (const auto& record : snapshot["records"].GetArray()) {
            if (std::string(record["symbolName"].GetString()) != item.name) continue;
            ++found;
            check(std::string(record["module"].GetString()) == (module ? "engine" : "client"), "exact module tag");
            check(std::string(record["platform"].GetString()) == "windows", "Windows record");
            check(std::string(record["kind"].GetString()) ==
                  (item.kind == MH_GAMESYMBOL_KIND_GLOBAL ? "global" : "function"), "exact kind tag");
            if (item.kind != MH_GAMESYMBOL_KIND_GLOBAL) continue;
            const auto& payload = record["payload"];
            GameSymbolRecord normalized;
            std::string error;
            check(NormalizeGlobal(payload, normalized, error), "actual parser accepts complete gHUD payload");
            check(normalized.rva == 0x117b48 && normalized.signatureRva == 0x47790 &&
                  normalized.instructionOffset == 0 && normalized.operandOffset == 1 &&
                  normalized.instructionLength == 5, "gHUD reference describes native mov ecx operand");
            for (const char* field : {"gv_rva", "gv_va", "gv_sig", "gv_sig_va",
                    "gv_inst_offset", "gv_inst_disp", "gv_inst_length"}) {
                rapidjson::Document missing;
                missing.CopyFrom(payload, missing.GetAllocator());
                missing.RemoveMember(field);
                GameSymbolRecord rejected;
                error.clear();
                check(!NormalizeGlobal(missing, rejected, error), "actual parser rejects each missing gHUD field");
                check(error == "global payload is missing a required gv_* field", "precise missing-field diagnostic");
            }
        }
        check(found == 1, "required native HUD record appears exactly once");
    }
    return true;
}
}

int main(int argc, char** argv) {
    // Catalog directories, exact client/hw files, then their modified identities.
    if (argc != 7) return 2;
    check(inspect_snapshot(argv[1], 0), "read client snapshot");
    check(inspect_snapshot(argv[2], 1), "read engine snapshot");
    const char* roots[]{argv[1], argv[2]};
    check(GameData::Initialize(roots, 2), "actual loader accepts generated indexes and snapshots");
    check(GameData::GetDiagnostics().empty(), "catalog loader reports no rejected records");
    if (failures) return 1;

    std::array<void*, 2> bases{}, foreign{};
    for (unsigned module = 0; module != 2; ++module) {
        bases[module] = VirtualAlloc(nullptr, sizes[module], MEM_RESERVE, PAGE_NOACCESS);
        foreign[module] = VirtualAlloc(nullptr, sizes[module], MEM_RESERVE, PAGE_NOACCESS);
        if (!bases[module] || !foreign[module]) return 3;
        GameData::RegisterModuleFileSource(bases[module], argv[3 + module], sizes[module]);
        GameData::RegisterModuleFileSource(foreign[module], argv[5 + module], sizes[module]);
        std::uint64_t crc{};
        check(MH_GetModuleCRC64(bases[module], &crc) == MH_GAMESYMBOL_OK && crc == crcs[module],
              "actual module hashing matches original file CRC64");
    }
    for (const auto& item : expected) {
        mh_gamesymbol_t symbol{};
        symbol.cbSize = sizeof(symbol);
        check(GameData::QueryByCRC64(crcs[item.module], item.name, &symbol) == MH_GAMESYMBOL_OK,
              "exact identity query finds symbol");
        check(symbol.kind == item.kind && symbol.rva == item.rva && symbol.symbolSize == item.size,
              "query retains exact kind RVA and size");
        check(GameData::QueryByCRC64(crcs[item.module] ^ 1, item.name, &symbol) == MH_GAMESYMBOL_MODULE_NOT_FOUND,
              "foreign CRC cannot resolve native HUD symbol");
        void* address{};
        check(MH_ResolveGameSymbol(bases[item.module], item.name, item.kind, &address) == MH_GAMESYMBOL_OK &&
              address == static_cast<BYTE*>(bases[item.module]) + item.rva,
              "actual ResolveGameSymbol produces caller module base plus verified RVA");
        const auto wrong_kind = item.kind == MH_GAMESYMBOL_KIND_GLOBAL ? MH_GAMESYMBOL_KIND_FUNCTION : MH_GAMESYMBOL_KIND_GLOBAL;
        check(MH_ResolveGameSymbol(bases[item.module], item.name, wrong_kind, &address) == MH_GAMESYMBOL_KIND_MISMATCH && !address,
              "wrong kind fails without an address");
        check(MH_ResolveGameSymbol(bases[1 - item.module], item.name, item.kind, &address) == MH_GAMESYMBOL_SYMBOL_NOT_FOUND && !address,
              "other original module cannot resolve symbol");
        check(MH_ResolveGameSymbol(foreign[item.module], item.name, item.kind, &address) == MH_GAMESYMBOL_MODULE_NOT_FOUND && !address,
              "modified binary identity fails without an address");
    }
    // A representative existing symbol from each generator remains available.
    for (const auto& item : std::array{
            Expected{"UTIL_SharedRandomFloat", 0, MH_GAMESYMBOL_KIND_FUNCTION, 0x47c40, 0},
            Expected{"GoldCraft_CL_ClearState", 1, MH_GAMESYMBOL_KIND_FUNCTION, 0x1a18d0, 0}}) {
        void* address{};
        check(MH_ResolveGameSymbol(bases[item.module], item.name, item.kind, &address) == MH_GAMESYMBOL_OK &&
              address == static_cast<BYTE*>(bases[item.module]) + item.rva, "preexisting symbol preserved");
    }
    GameData::ResetModuleIdentities();
    for (const auto& group : {bases, foreign})
        for (auto* base : group) VirtualFree(base, 0, MEM_RELEASE);
    std::cout << "native HUD actual catalog parser/resolver: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
