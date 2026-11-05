// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the pinned host's actual catalog parser, including rejected records.
#include "../../external/MetaHookSv-20261007/MetaHook/src/GameData.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <iterator>

bool MH_IsInLdrCriticalRegion() { return false; }

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const bool valid = std::string(argv[2]) == "valid";
    assert(valid || std::string(argv[2]) == "rejected");
    std::ifstream stream(std::filesystem::path(argv[1]) / "cs-client-10210-goldcraft.json");
    assert(stream);
    const std::string text{std::istreambuf_iterator<char>(stream), {}};
    rapidjson::Document snapshot;
    snapshot.Parse(text.c_str());
    assert(!snapshot.HasParseError());
    const rapidjson::Value* payload = nullptr;
    for (const auto& record : snapshot["records"].GetArray())
        if (std::string(record["symbolName"].GetString()) == "CSOL_ClientCRTRand")
        {
            assert(!payload);
            payload = &record["payload"];
        }
    assert(payload);
    GameSymbolRecord normalized;
    std::string error;
    assert(NormalizeGlobal(*payload, normalized, error) == valid);
    if (valid)
    {
        for (const auto* field : {"gv_rva", "gv_va", "gv_sig", "gv_sig_va",
            "gv_inst_offset", "gv_inst_disp", "gv_inst_length"})
        {
            rapidjson::Document missing;
            missing.CopyFrom(*payload, missing.GetAllocator());
            missing.RemoveMember(field);
            GameSymbolRecord rejected;
            error.clear();
            assert(!NormalizeGlobal(missing, rejected, error));
            assert(error == "global payload is missing a required gv_* field");
        }
    }
    else
        assert(error == "global payload is missing a required gv_* field");

    const char* roots[]{argv[1]};
    assert(GameData::Initialize(roots, 1));
    constexpr std::uint64_t crc = 0x124d6034a6b28b40;
    mh_gamesymbol_t symbol{};
    symbol.cbSize = sizeof(symbol);
    assert((GameData::QueryByCRC64(crc, "CSOL_ClientCRTRand", &symbol) == MH_GAMESYMBOL_OK) == valid);
    if (valid)
    {
        assert(symbol.kind == MH_GAMESYMBOL_KIND_GLOBAL && symbol.rva == 0xbf220);
        assert(symbol.signatureRva == 0x3743a && symbol.instructionOffset == 0);
        assert(symbol.operandOffset == 2 && symbol.instructionLength == 6);
        assert(GameData::QueryByCRC64(crc ^ 1, "CSOL_ClientCRTRand", &symbol) != MH_GAMESYMBOL_OK);
    }
    else
        assert(GameData::GetDiagnostics().find("global payload is missing a required gv_* field") != std::string::npos);
    assert(GameData::QueryByCRC64(crc, "UTIL_SharedRandomFloat", &symbol) == MH_GAMESYMBOL_OK);
    assert(symbol.kind == MH_GAMESYMBOL_KIND_FUNCTION && symbol.rva == 0x47c40);
    std::cout << "Production GameData loader: " << (valid ? "complete rand accepted; seven missing-field negatives rejected"
        : "original incomplete rand rejected; shared float retained") << '\n';
}
