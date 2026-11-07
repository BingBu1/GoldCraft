// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../core/map_resources.hpp"

#include <map>
#include <memory>

namespace csol::scenario
{
struct EliteProperties
{
    std::int32_t difficulty{};
    std::int32_t attackDamage{100}, attackRange{300}, attackInterval{1};
    std::int32_t maxHealth{50000}, maxArmor{20000}, maxSpeed{250};
    // Keep both source values. HW37507820 reads bonusMoney but discards its
    // conversion; killType occupies +2C, and +30 is explicitly zeroed.
    std::int32_t bonusMoney{}, killType{};
};

struct EliteSkillParameters
{
    using Values = std::map<std::string, float, std::less<>>;
    Values base;
    // The original loader visits Params0 through Params9 independently.
    std::map<int, Values> difficulties;
};

struct EliteSettings
{
    std::map<std::int32_t, EliteProperties> properties;
    std::array<std::int32_t, 3> colors{};
    // Original lookup10966030 uses the array index, not the JSON ID or Name.
    std::vector<EliteSkillParameters> skills;
    float Parameter(int skill, int difficulty, std::string_view name) const;
};

// Parse the real maps/ZBS.json section, retaining first equal difficulty and
// parameter keys. A failed read leaves output unchanged. The host rejects
// malformed objects where the original code would dereference invalid data.
bool ReadEliteSettings(std::string_view json, EliteSettings& output, ConfigError& error);

// Original10969880 builds a private type32800, independent of the map's CSV.
// Difficulty must satisfy both the original size gate and an exact map lookup.
// The returned immutable row can be pinned by a factory until every lease ends.
bool MakeEliteMonsterType(const EliteSettings& settings, int difficulty,
    std::shared_ptr<const MonsterType>& output, ConfigError& error);

struct EliteAbilityClock
{
    int id{};
    float nextActivation{}, activeUntil{};
};

using EliteAbilities = std::map<int, EliteAbilityClock>;
// Original10967420. The provider supplies108F56B0's actual qualified-player
// count; this is not the server's connected/alive-player count.
struct EliteRandom
{
    void* context{};
    bool (*integer)(void*, int minimum, int maximum, int& result){};
};
bool SelectEliteAbilities(int difficulty, int qualifiedPlayers, EliteRandom random,
    EliteAbilities& output, ConfigError& error);
bool ApplyEliteInitialModifiers(const EliteSettings& settings, int difficulty,
    const EliteAbilities& abilities, const MonsterType& original,
    std::shared_ptr<const MonsterType>& output, ConfigError& error);
// The original resets deadlines before dispatch, even for passive abilities.
// A failed effect must not be retried by rolling these clocks back.
bool StartEliteAbilityClock(const EliteSettings& settings, int difficulty,
    bool speedy, float now, EliteAbilityClock& ability, float& cooldown,
    ConfigError& error);

class EliteResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool Clear();
    std::shared_ptr<const EliteSettings> Snapshot() const { return m_settings; }
private:
    std::shared_ptr<const EliteSettings> m_settings;
    bool m_busy{};
};
}
