// SPDX-License-Identifier: GPL-3.0-or-later
#include "melee_damage.hpp"
#include <rapidjson/document.h>
#include <cmath>

namespace csol::melee {
namespace {
bool Read(const rapidjson::Value& object, const char* key, float& destination)
{
    const auto member = object.FindMember(key);
    if (member == object.MemberEnd() || !member->value.IsNumber()) return false;
    const double value = member->value.GetDouble();
    if (!std::isfinite(value) || value < 0 || value > 1000000) return false;
    destination = static_cast<float>(value);
    return true;
}
}

bool ParseDamageConfig(std::string_view json, DamageConfig& destination)
{
    if (json.empty() || json.size() > 65536) return false;
    rapidjson::Document doc;
    doc.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (doc.HasParseError() || !doc.IsObject() || doc.MemberCount() != 8 ||
        !doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt() != 1) return false;
    DamageConfig candidate;
    if (!Read(doc, "PriFullDmg", candidate.base.primaryFull) ||
        !Read(doc, "PriFastDmg", candidate.base.primaryFast) ||
        !Read(doc, "SndDmg", candidate.base.secondary) ||
        !Read(doc, "ZombieGroupMultiplier", candidate.zombieGroupMultiplier) ||
        !Read(doc, "Modes11And13Multiplier", candidate.modes11And13Multiplier) ||
        !Read(doc, "ScenarioGroupMultiplier", candidate.scenarioGroupMultiplier) ||
        !Read(doc, "Mode18Multiplier", candidate.mode18Multiplier)) return false;
    destination = candidate;
    return true;
}

float DamageMultiplier(const DamageConfig& config, int originalMode)
{
    // The priority is significant for SkullAxe: 11 and 13 also occur in the
    // broader 10623020 set, but 110D2887 checks them first.
    switch (originalMode) {
    case 11: case 13: return config.modes11And13Multiplier;
    case 8: case 9: case 14: case 20: case 24: case 29: case 30:
    case 32: case 35: case 36: case 41: case 43: case 44: case 45:
    case 46: case 47: case 51: case 54: case 55: case 56: case 57:
    case 58: case 60: return config.zombieGroupMultiplier;
    case 18: return config.mode18Multiplier;
    case 15: case 17: case 48: case 59: return config.scenarioGroupMultiplier;
    default: return 1;
    }
}

float AttackDamage(const DamageConfig& config, int originalMode, Button button, bool fastPrimary)
{
    return BaseDamage(config.base, button, fastPrimary) * DamageMultiplier(config, originalMode);
}

} // namespace csol::melee
