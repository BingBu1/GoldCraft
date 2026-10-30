// SPDX-License-Identifier: GPL-3.0-or-later
// Offline contract fixture; its rules query is supplied by this test process.
#include "weapon_damage_mode.h"
#include "weapon_damage.h"
#include "weapon_damage_file.h"
#include "../cannon.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
bool scenarioRules{};
int assertions{};
void Check(bool condition, const char* message)
{
    ++assertions;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

bool CSOL_UsesScenarioRules() { return scenarioRules; }

// Built from the current production adapter bodies, against the actual SDK.
CSOLDamageMode CSOL_TestBeamMode(float legacyCvar);
float CSOL_TestCannonBase(float legacyCvar, bool zombie, float normal,
    float zombieDamage, float scenario, float escape);

int main(int argc, char** argv)
{
    Check(argc == 2, "supply the authoritative beam JSON path");
    CCSOLBeamDamageConfig config;
    std::string error;
    Check(CSOL_LoadBeamDamageFile(std::filesystem::path(argv[1]), config, error), error.c_str());
    constexpr std::array<std::array<float, 11>, 3> expected{{
        {40, 10, 10, 5, 20, 5, 40, 40, 0, 0, 0},
        {850, 600, 11760, 3000, 3000, 2000, 16800, 18000, 100, 75, 6000},
        {3600, 1800, 19000, 3000, 3000, 2000, 78500, 65000, 250, 75, 16000}
    }};
    constexpr std::array<float, 8> legacyModes{-1, 0, 1, 2, 3, 7, 1.5f, 2.5f};
    // Exercise mode changes repeatedly: no cached scenario selection may leak
    // into ordinary CS/ZP after a map/rules transition.
    for (bool scenario : {false, true, false, true, false})
    {
        scenarioRules = scenario;
        for (float legacy : legacyModes)
        {
            const auto mode = CSOL_TestBeamMode(legacy);
            const unsigned expectedMode = scenario || legacy == 2 ? 2 : legacy == 1 ? 1 : 0;
            Check(static_cast<unsigned>(mode) == expectedMode, "beam mode preserves legacy/actual-rules precedence");
            for (unsigned weapon = 0; weapon != 2; ++weapon)
                for (unsigned attack = 0; attack != 11; ++attack)
                    Check(config.Get(static_cast<CSOLBeamVariant>(weapon), mode,
                        static_cast<CSOLBeamDamage>(attack)) == expected[expectedMode][attack],
                        "all beam attacks use the selected JSON base exactly once");
            for (bool zombie : {false, true})
            {
                const int oldMode = static_cast<int>(legacy);
                const float oldBase = oldMode == 2 ? 2950 : oldMode == 3 ? 650 :
                    oldMode == 1 || zombie ? 1150 : 410;
                const float base = CSOL_TestCannonBase(legacy, zombie, 410, 1150, 2950, 650);
                Check(base == (scenario ? 2950 : oldBase), "cannon preserves CS/ZP/escape/manual semantics");
                Check(CSOL_TestCannonBase(legacy, zombie, 11, 22, 3333, 44) ==
                    (scenario ? 3333 : oldMode == 2 ? 3333 : oldMode == 3 ? 44 : oldMode == 1 || zombie ? 22 : 11),
                    "cannon keeps customized profile values");
            }
        }
    }
    scenarioRules = true;
    Check(CSOL_TestCannonBase(0, false, 410, 1150, 0, 650) == 0,
        "explicit zero scenario damage remains zero");

    // Existing per-variant JSON overrides must take precedence over defaults,
    // including when automatic rules select Scenario instead of a cvar.
    rapidjson::Document document;
    FILE* file = std::fopen(argv[1], "rb");
    Check(file != nullptr, "open config for custom override fixture");
    std::string json;
    for (int ch; (ch = std::fgetc(file)) != EOF;) json += static_cast<char>(ch);
    std::fclose(file);
    document.Parse(json.c_str());
    Check(!document.HasParseError(), "fixture input is JSON");
    document["overrides"]["beamgunle"].AddMember("ShootADamageScenario", 4321, document.GetAllocator());
    document["overrides"]["beamgunle"].AddMember("WingManAttack1DamageScenario", 0, document.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    Check(config.Parse({buffer.GetString(), buffer.GetSize()}, error), "parse operator's custom profile");
    const auto automatic = CSOL_TestBeamMode(0);
    Check(config.Get(CSOLBeamVariant::BeamgunLE, automatic, CSOLBeamDamage::ShootA) == 4321,
        "LE override survives automatic scenario selection");
    Check(config.Get(CSOLBeamVariant::Beamgun, automatic, CSOLBeamDamage::ShootA) == 3600,
        "LE override does not mutate the base variant");
    Check(config.Get(CSOLBeamVariant::BeamgunLE, automatic, CSOLBeamDamage::WingManAttack1) == 0,
        "explicit zero override is not replaced with a default");
    Check(!config.Parse("{}", error), "invalid reload is rejected");
    Check(config.Get(CSOLBeamVariant::BeamgunLE, automatic, CSOLBeamDamage::ShootA) == 4321,
        "invalid reload retains the previous profile");
    scenarioRules = false;
    Check(config.Get(CSOLBeamVariant::BeamgunLE, CSOL_TestBeamMode(0), CSOLBeamDamage::ShootA) == 40,
        "scenario override does not change ordinary damage");
    Check(config.Get(CSOLBeamVariant::BeamgunLE, CSOL_TestBeamMode(1), CSOLBeamDamage::ShootA) == 850,
        "scenario override does not change zombie damage");
    std::printf("PASS %d assertions: real SDK adapters, mode transitions, 22 beam attacks, cannon profiles and JSON overrides\n", assertions);
    return 0;
}
