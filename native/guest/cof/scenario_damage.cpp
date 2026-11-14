// SPDX-License-Identifier: GPL-3.0-or-later
#include "scenario_damage.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>

namespace guest::cof
{
namespace
{
constexpr std::size_t MaxBytes = 64 * 1024;
bool DamageValue(const rapidjson::Value& value, float& result)
{
    if (!value.IsNumber()) return false;
    const double number = value.GetDouble();
    if (!std::isfinite(number) || number < 0 || number > 1'000'000) return false;
    result = static_cast<float>(number);
    return true;
}
}

bool ScenarioDamageConfig::Parse(std::string_view json, std::string& error)
{
    error.clear();
    const auto reject = [&](const char* message) { error = message; return false; };
    if (json.empty() || json.size() > MaxBytes || json.find('\0') != json.npos)
        return reject("CoF scenario damage must contain 1..65536 bytes");

    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject() || document.MemberCount() != 2 ||
        !document.HasMember("version") || !document["version"].IsInt() || document["version"].GetInt() != 1 ||
        !document.HasMember("pistols") || !document["pistols"].IsObject())
        return reject("Invalid CoF scenario damage envelope");

    const auto& pistols = document["pistols"];
    if (pistols.MemberCount() != PistolKeys.size())
        return reject("CoF scenario damage requires every pistol exactly once");
    std::array<ScenarioDamage, PistolKeys.size()> prepared{};
    std::array<bool, PistolKeys.size()> seen{};
    for (auto it = pistols.MemberBegin(); it != pistols.MemberEnd(); ++it)
    {
        const std::string_view key(it->name.GetString(), it->name.GetStringLength());
        const auto found = std::find(PistolKeys.begin(), PistolKeys.end(), key);
        if (found == PistolKeys.end()) return reject("Unknown CoF pistol damage identity");
        const auto index = static_cast<std::size_t>(found - PistolKeys.begin());
        if (seen[index]) return reject("Duplicate CoF pistol damage identity");
        seen[index] = true;
        const auto& entry = it->value;
        if (!entry.IsObject() || (entry.MemberCount() != 2 && entry.MemberCount() != 3) ||
            !entry.HasMember("shot") || !entry.HasMember("melee") ||
            !DamageValue(entry["shot"], prepared[index].shot) ||
            !DamageValue(entry["melee"], prepared[index].melee))
            return reject("Each CoF pistol requires finite shot and melee damage in 0..1000000");
        if (entry.MemberCount() == 3)
        {
            if (index != static_cast<std::size_t>(Pistol::Revolver) || !entry.HasMember("penetration") ||
                !entry["penetration"].IsInt() || entry["penetration"].GetInt() < 0 ||
                entry["penetration"].GetInt() > 32)
                return reject("Only Revolver supports an explicit CS penetration count in 0..32");
            prepared[index].penetration = entry["penetration"].GetInt();
        }
    }

    values_ = prepared;
    loaded_ = true;
    return true;
}

const ScenarioDamage* ScenarioDamageConfig::Find(Pistol weapon) const noexcept
{
    const auto index = static_cast<std::size_t>(weapon);
    return loaded_ && index < values_.size() ? &values_[index] : nullptr;
}
}
