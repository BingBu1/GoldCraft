// SPDX-License-Identifier: GPL-3.0-or-later
#include "skull_balrog_damage.hpp"

#include <rapidjson/document.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace csol::skull_balrog
{
bool ParseDamage(std::string_view json, std::span<const std::string_view> fields,
    std::span<float> values, std::string& error)
{
    error.clear();
    if (fields.empty() || fields.size() > 32 || fields.size() != values.size())
    {
        error = "invalid damage field layout";
        return false;
    }
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (fields[i].empty() || fields[i] == "version" || fields[i].find('\0') != fields[i].npos ||
            std::find(fields.begin(), fields.begin() + i, fields[i]) != fields.begin() + i)
        {
            error = "invalid damage field name";
            return false;
        }
    }
    if (json.empty() || json.size() > 65536 || json.find('\0') != json.npos)
    {
        error = "damage configuration has an invalid length or contains NUL";
        return false;
    }
    rapidjson::Document document;
    document.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (document.HasParseError() || !document.IsObject() || document.MemberCount() != fields.size() + 1)
    {
        error = "invalid damage configuration object";
        return false;
    }
    std::array<float, 32> next{};
    std::array<bool, 32> seen{};
    bool versionSeen = false;
    for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member)
    {
        const std::string_view name(member->name.GetString(), member->name.GetStringLength());
        if (name == "version")
        {
            if (versionSeen || !member->value.IsInt() || member->value.GetInt() != 1)
            {
                error = "unsupported or duplicate damage configuration version";
                return false;
            }
            versionSeen = true;
            continue;
        }
        const auto found = std::find(fields.begin(), fields.end(), name);
        if (found == fields.end())
        {
            error = "unknown damage property: " + std::string(name);
            return false;
        }
        const auto index = static_cast<std::size_t>(found - fields.begin());
        if (seen[index] || !member->value.IsNumber())
        {
            error = "duplicate or nonnumeric damage property: " + std::string(name);
            return false;
        }
        const double value = member->value.GetDouble();
        if (!std::isfinite(value) || value < 0 || value > 1000000)
        {
            error = "damage property outside [0, 1000000]: " + std::string(name);
            return false;
        }
        seen[index] = true;
        next[index] = static_cast<float>(value);
    }
    if (!versionSeen || std::find(seen.begin(), seen.begin() + fields.size(), false) != seen.begin() + fields.size())
    {
        error = "missing damage configuration property";
        return false;
    }
    std::copy_n(next.begin(), fields.size(), values.begin());
    return true;
}
}
