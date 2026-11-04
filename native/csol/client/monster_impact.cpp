// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "monster_impact.hpp"
#include "../monster_impact.hpp"
#include "../scenario_monster_effects.hpp"
#include <event_api.h>
#include <r_efx.h>
#include <bit>
#include <cstdint>
#include <ostream>
#include <sstream>

namespace csol::client {
namespace {
unsigned blood{}, sparks{}, rejected{}, missingSprites{};
void Info()
{
    std::ostringstream report;
    MonsterImpactDiagnostics(report);
    engine.Con_Printf("%s\n", report.str().c_str());
}
}

void InitializeMonsterImpact()
{
    blood = sparks = rejected = missingSprites = 0;
    engine.pfnAddCommand("csol_monster_impact_info", Info);
}

bool ConsumeMonsterImpact(int size, const void* data)
{
    namespace mi = monster_impact;
    namespace wire = monster_effects;
    if (!data || size <= 0) return false;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (bytes[0] != wire::Blood && bytes[0] != wire::Spark) return false;
    const bool metal = bytes[0] == wire::Spark;
    if (size != wire::HitPayloadSize || bytes[1] == 0 || bytes[1] > (metal ? 60 : 15))
    { ++rejected; return true; }
    float position[3];
    for (int i = 0; i < 3; ++i)
    {
        const auto value = static_cast<std::uint16_t>(bytes[2 + i * 2] | (bytes[3 + i * 2] << 8));
        position[i] = static_cast<float>(std::bit_cast<std::int16_t>(value)) * .125f;
    }
    auto* fx = engine.pEfxAPI;
    if (!fx) { ++rejected; return true; }
    if (metal)
    {
        if (!fx->R_SparkEffect) { ++rejected; return true; }
        fx->R_SparkEffect(position, bytes[1], mi::SparkVelocityMin, mi::SparkVelocityMax);
        ++sparks;
    }
    else
    {
        // Always resolve against this server's current precache list.
        auto* ev = engine.pEventAPI;
        const int spray = ev && ev->EV_FindModelIndex ? ev->EV_FindModelIndex(mi::BloodSpray) : 0;
        const int drop = ev && ev->EV_FindModelIndex ? ev->EV_FindModelIndex(mi::BloodDrop) : 0;
        if (spray <= 0 || drop <= 0 || !fx->R_BloodSprite) { ++missingSprites; return true; }
        fx->R_BloodSprite(position, mi::BloodColor, spray, drop, static_cast<float>(bytes[1]));
        ++blood;
    }
    return true;
}

void MonsterImpactDiagnostics(std::ostream& out)
{
    out << "{\"monsterImpact\":{\"authority\":\"server\",\"blood\":" << blood
        << ",\"sparks\":" << sparks << ",\"rejected\":" << rejected
        << ",\"missingSprites\":" << missingSprites << "}}";
}
}
