// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

namespace csol::bsp
{
// CSOL HW Mod_LoadClipnodes 3787E920, ordinary scenario profile. The BSP
// stores expanded clip planes, but does not store the hull dimensions.
inline constexpr std::array<float, 3> ScenarioLargeMins{-40, -40, -90};
inline constexpr std::array<float, 3> ScenarioLargeMaxs{40, 40, 90};
struct HullBounds { std::array<float, 3> mins, maxs; };
inline constexpr HullBounds ScenarioLargeHull{ScenarioLargeMins, ScenarioLargeMaxs};
inline constexpr HullBounds AllOutLargeHull{{-80, -80, -110}, {80, 80, 110}};
inline constexpr HullBounds MultiBossLargeHull{{-72, -72, -72}, {72, 72, 72}};

namespace detail
{
enum class Kind { End, Open, Close, Word, Invalid };
struct Token { Kind kind; std::string_view text; };
inline Token Next(std::string_view& input)
{
    for (;;)
    {
        const auto first = input.find_first_not_of(" \t\r\n\v\f");
        if (first == input.npos) { input = {}; return {Kind::End, {}}; }
        input.remove_prefix(first);
        if (!input.starts_with("//")) break;
        const auto end = input.find('\n');
        if (end == input.npos) { input = {}; return {Kind::End, {}}; }
        input.remove_prefix(end + 1);
    }
    const char first = input.front();
    if (first == '{' || first == '}')
    {
        input.remove_prefix(1);
        return {first == '{' ? Kind::Open : Kind::Close, {}};
    }
    if (first == '"')
    {
        input.remove_prefix(1);
        const auto end = input.find('"');
        if (end == input.npos) return {Kind::Invalid, {}};
        const auto word = input.substr(0, end);
        input.remove_prefix(end + 1);
        return {Kind::Word, word};
    }
    const auto end = input.find_first_of(" \t\r\n\v\f{}\"");
    const auto word = input.substr(0, end);
    input.remove_prefix(word.size());
    return {Kind::Word, word};
}
}

inline bool MapEquals(std::string_view actual, std::string_view expected)
{
    if (actual.size() != expected.size()) return false;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        const char c = actual[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != expected[i]) return false;
    }
    return true;
}

// Exact filenames from original maplist IDs 219/220/221/275 and 265.
// These override the default in mode15. Original mode18/mode30 use other
// profiles and are not selected by this scenario-entity importer.
inline HullBounds ScenarioHullForMap(std::string_view modelName)
{
    const auto slash = modelName.find_last_of("/\\");
    if (slash != modelName.npos) modelName.remove_prefix(slash + 1);
    if (MapEquals(modelName, "zs_allout1.bsp") || MapEquals(modelName, "zs_allout2.bsp") ||
        MapEquals(modelName, "zs_allout3.bsp") || MapEquals(modelName, "zs_electron2.bsp"))
        return AllOutLargeHull;
    if (MapEquals(modelName, "zs_multiboss.bsp")) return MultiBossLargeHull;
    return ScenarioLargeHull;
}

// Detect the imported scenario entity schema, not a filename prefix or the
// current game mode. Spawn classes must be actual classname values;
// incidental text in messages/comments cannot change a stock map's hulls.
inline bool HasScenarioEntities(std::string_view entities)
{
    bool players = false, monsters = false, triggeredSpawns = false, safeZones = false;
    for (;;)
    {
        const auto open = detail::Next(entities);
        if (open.kind == detail::Kind::End) return (players && monsters) || (triggeredSpawns && safeZones);
        if (open.kind != detail::Kind::Open) return false;
        std::string_view classname;
        for (;;)
        {
            const auto key = detail::Next(entities);
            if (key.kind == detail::Kind::Close) break;
            if (key.kind != detail::Kind::Word) return false;
            const auto value = detail::Next(entities);
            if (value.kind != detail::Kind::Word) return false;
            if (key.text == "classname") classname = value.text;
        }
        // Early survival maps use the original standard-player-spawn fallback.
        players |= classname == "playerspawn" || classname == "info_player_start";
        monsters |= classname == "zombiespawn";
        // Later scenario maps (including allout1 and multiboss) use these
        // paired spawn/safe-zone entities instead of the first-season schema.
        triggeredSpawns |= classname == "zbstrigger_spawn";
        safeZones |= classname == "zs_safezone";
    }
}

inline bool IsScenarioLargeHull(const float* mins, const float* maxs)
{
    const auto equal = [&](const HullBounds& bounds)
    {
        return mins[0] == bounds.mins[0] && mins[1] == bounds.mins[1] && mins[2] == bounds.mins[2] &&
            maxs[0] == bounds.maxs[0] && maxs[1] == bounds.maxs[1] && maxs[2] == bounds.maxs[2];
    };
    return equal(ScenarioLargeHull) || equal(AllOutLargeHull) || equal(MultiBossLargeHull);
}
}
