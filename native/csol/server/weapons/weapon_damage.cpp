// SPDX-License-Identifier: GPL-3.0-or-later
#include "weapon_damage.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <cmath>
#include <limits>

namespace
{
constexpr const char* DamageNames[] = {
    "ShootADamage", "ShootAExplosionDamage", "ShootAFullExplosionDamage",
    "FullChargeExplosionDamage", "ShootB1Damage", "ShootB2Damage",
    "ChargeShootADamage", "ChargeShootBExplosionDamage",
    "WingManAttack1Damage", "WingManAttack2Damage", "KillEvasionExplosionDamage"
};
constexpr const char* ModeSuffixes[] = { "", "Zombie", "Scenario" };
constexpr const char* VariantNames[] = { "beamgun", "beamgunle" };
static_assert(std::size(DamageNames) == static_cast<unsigned>(CSOLBeamDamage::Count));
static_assert(std::size(ModeSuffixes) == static_cast<unsigned>(CSOLDamageMode::Count));
static_assert(std::size(VariantNames) == static_cast<unsigned>(CSOLBeamVariant::Count));

std::string_view Name(const rapidjson::Value& value)
{
    return { value.GetString(), value.GetStringLength() };
}

bool Object(const rapidjson::Value& object, const std::string& path, std::string& error)
{
    if (!object.IsObject())
    {
        error = path + ": expected an object";
        return false;
    }
    for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member)
    {
        for (auto other = object.MemberBegin(); other != member; ++other)
        {
            if (Name(member->name) == Name(other->name))
            {
                error = path + ": duplicate member " + std::string(Name(member->name));
                return false;
            }
        }
    }
    return true;
}

bool DamageField(std::string_view name, unsigned& mode, unsigned& attack)
{
    for (mode = 0; mode < std::size(ModeSuffixes); ++mode)
    {
        for (attack = 0; attack < std::size(DamageNames); ++attack)
        {
            if (name == std::string(DamageNames[attack]) + ModeSuffixes[mode])
                return true;
        }
    }
    return false;
}
}

bool CCSOLBeamDamageConfig::Parse(std::string_view json, std::string& error)
{
    error.clear();
    if (json.empty() || json.size() > 64 * 1024)
    {
        error = "damage configuration must contain 1..65536 bytes";
        return false;
    }
    if (json.find('\0') != json.npos)
    {
        error = "JSON contains an embedded NUL byte";
        return false;
    }
    // Accept the UTF-8 BOM produced by common Windows editors.
    if (json.starts_with("\xEF\xBB\xBF")) json.remove_prefix(3);
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (document.HasParseError())
    {
        error = "JSON at byte " + std::to_string(document.GetErrorOffset()) + ": " +
            rapidjson::GetParseError_En(document.GetParseError());
        return false;
    }
    if (!Object(document, "root", error)) return false;
    for (const auto& member : document.GetObject())
    {
        const auto name = Name(member.name);
        if (name != "version" && name != "defaults" && name != "overrides")
        {
            error = "root: unknown member " + std::string(name);
            return false;
        }
    }
    if (!document.HasMember("version") || !document["version"].IsInt() || document["version"].GetInt() != 1)
    {
        error = "version: expected 1";
        return false;
    }
    if (!document.HasMember("defaults"))
    {
        error = "defaults: required";
        return false;
    }

    CCSOLBeamDamageConfig candidate;
    auto read = [&](const rapidjson::Value& object, ModeSet& values, bool complete, const std::string& path)
    {
        if (!Object(object, path, error)) return false;
        for (const auto& member : object.GetObject())
        {
            unsigned mode, attack;
            const auto name = Name(member.name);
            const auto field = path + "." + std::string(name);
            if (!DamageField(name, mode, attack))
            {
                error = field + ": unknown damage field";
                return false;
            }
            if (!member.value.IsNumber())
            {
                error = field + ": expected a number";
                return false;
            }
            const double value = member.value.GetDouble();
            if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<float>::max())
            {
                error = field + ": expected a finite, nonnegative float";
                return false;
            }
            values[mode][attack] = static_cast<float>(value);
        }
        if (complete)
        {
            for (const auto* suffix : ModeSuffixes)
            {
                for (const auto* damage : DamageNames)
                {
                    const auto name = std::string(damage) + suffix;
                    if (!object.HasMember(name.c_str()))
                    {
                        error = path + "." + name + ": required";
                        return false;
                    }
                }
            }
        }
        return true;
    };
    if (!read(document["defaults"], candidate.m_values[0], true, "defaults")) return false;
    candidate.m_values[1] = candidate.m_values[0];
    if (document.HasMember("overrides"))
    {
        const auto& overrides = document["overrides"];
        if (!Object(overrides, "overrides", error)) return false;
        for (const auto& member : overrides.GetObject())
        {
            const auto name = Name(member.name);
            unsigned variant = 0;
            while (variant < std::size(VariantNames) && name != VariantNames[variant]) ++variant;
            if (variant == std::size(VariantNames))
            {
                error = "overrides: unknown weapon " + std::string(name);
                return false;
            }
            if (!read(member.value, candidate.m_values[variant], false, "overrides." + std::string(name))) return false;
        }
    }
    candidate.m_loaded = true;
    *this = candidate;
    return true;
}

bool CCSOLBeamDamageConfig::IsLoaded() const
{
    return m_loaded;
}

float CCSOLBeamDamageConfig::Get(CSOLBeamVariant weapon, CSOLDamageMode mode, CSOLBeamDamage attack) const
{
    const auto w = static_cast<unsigned>(weapon);
    const auto m = static_cast<unsigned>(mode);
    const auto a = static_cast<unsigned>(attack);
    if (!m_loaded || w >= m_values.size() || m >= std::size(ModeSuffixes) || a >= std::size(DamageNames)) return 0;
    return m_values[w][m][a];
}
