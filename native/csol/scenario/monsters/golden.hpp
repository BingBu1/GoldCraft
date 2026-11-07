// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../core/map_resources.hpp"
#include "monster_attributes.hpp"
#include "golden_parameters.hpp"

#include <memory>
#include <random>
#include <span>

namespace csol::scenario
{
enum class GoldenConfiguration { Unloaded, Missing, Empty, ParseError, Unsupported, Configured };

struct GoldenMonsterInfo
{
    std::string name;
    float probability{}, delay{};
    std::array<std::int32_t, 3> color{};
    std::int32_t attackDamage{}, attackRange{}, attackInterval{};
    std::int32_t health{}, armor{}, speed{}, experience{}, dropItemId{}, bonusMoney{};
};

struct GoldenDifficulty
{
    std::int32_t level{}, probability{}, person{}, dropItemId{}, health{}, armor{}, speed{};
};

struct GoldenSettings
{
    GoldenMonsterInfo monster;
    std::vector<GoldenDifficulty> difficulties;
    // HW37A31FF0: greatest nonnegative level <= requested; LAST equal wins.
    const GoldenDifficulty* Select(int requested) const;
};

// Parses a configured root. Missing/empty/syntax-error optional-file handling
// belongs to GoldenResources. Failed schema reads leave output unchanged.
bool ReadGoldenSettings(std::string_view json, GoldenSettings& output, ConfigError& error);

// Fresh-owner loading only. CSO leaves its initial difficulty/plan/member
// containers empty when this optional file is missing or has no parsed root.
class GoldenResources
{
public:
    bool Load(ResourceFileApi files, ResourceError& error);
    bool EmptyPlan() const;
    GoldenConfiguration State() const { return state_; }
    const std::string& Diagnostic() const { return diagnostic_; }
    std::shared_ptr<const GoldenSettings> Snapshot() const { return settings_; }
private:
    GoldenConfiguration state_{GoldenConfiguration::Unloaded};
    std::string diagnostic_;
    bool loading_{};
    std::shared_ptr<const GoldenSettings> settings_;
};

struct GoldenCandidate
{
    std::int32_t round{}; // Zero-based manager round-vector index, not CSV round label.
    std::int16_t wave{};
    bool operator==(const GoldenCandidate&) const = default;
};

// Explicit host snapshot for each wave referenced by trigger-mode rounds.
// Uses the current trigger record with the same low 16-bit ID as the wave.
// nullopt means no runtime/geometry record exists. Pending rows are those
// with RowClock.next != -1, whether or not the wave is currently active.
struct GoldenWaveStatus
{
    std::uint16_t wave{};
    std::optional<std::int16_t> triggerCooldown;
    bool runtimeHasPendingRow{};
};
bool GoldenCandidateType(const MonsterType& type);
bool CollectGoldenCandidates(const MapTables& tables, int currentRound,
    std::span<const GoldenWaveStatus> statuses, std::vector<GoldenCandidate>& output,
    ConfigError& error);

struct GoldenPlan
{
    GoldenCandidate destination;
    std::int32_t remaining{};
};

struct GoldenGateResult
{
    bool allowed{};
    // Original109796CE touches/creates this wave's runtime even when the
    // following delay check defers it. The adapter must preserve that action.
    bool touchWaveRuntime{};
};

struct GoldenEntityKey
{
    std::uint64_t session{}, lease{};
    int entity{};
    bool operator==(const GoldenEntityKey&) const = default;
};

struct GoldenReplacement
{
    // Only color changes in the private type copy; runtime attributes are
    // independently overridden below. Keep original exp/drop/skill fields.
    std::shared_ptr<const MonsterType> type;
    MonsterAttributes attributes;
    bool overrideHealth{}, overrideArmor{}; // Apply to BOTH current/max pev values only when true.
    std::int16_t runtimeSkill{};
    float renderUntil{}; // now +999, original monster+12D0.
    int renderFx{19}, renderMode{}, renderAmount{25};
};

struct GoldenDeathRecord
{
    std::shared_ptr<const MonsterType> original;
    // Original114D5740 returns pev+2D8. The adapter must supply this identity,
    // not an entity/player index. Stable insertion order is a host convention.
    std::vector<std::int32_t> contributors;
};

// One map/challenge owner, with its own MT19937 (not the global gameplay RNG).
// Methods do not call the engine. The adapter validates its session/lease
// before and after applying a returned replacement or emitting a death event.
class GoldenManager
{
public:
    bool Install(std::shared_ptr<const GoldenSettings> settings, int difficulty,
        GoldenExtraParameters extras, std::uint64_t session, std::uint32_t seed,
        ConfigError& error);
    bool Rebuild(std::span<const GoldenCandidate> candidates, ConfigError& error);
    // Original clock is max(0, gpGlobals.time - manager+12A4). Its origin
    // must come from the scenario owner, not this member's spawn time.
    bool Gate(int round, int wave, float scenarioElapsed,
        std::span<const GoldenCandidate> candidates, GoldenGateResult& result, ConfigError& error);
    bool EndWave(int round, int wave, std::span<const GoldenCandidate> candidates, ConfigError& error);
    bool Convert(GoldenEntityKey key, int round, int wave,
        std::shared_ptr<const MonsterType> original, const MonsterAttributes& attributes,
        float currentHealth, std::int16_t runtimeSkill, float now, std::optional<GoldenReplacement>& result,
        ConfigError& error);
    bool Contains(GoldenEntityKey key) const;
    bool Hit(GoldenEntityKey key, std::int32_t contributor);
    // Decrements completedPlans ONCE per lease, rebuilds, but retains member
    // and type until the common-death tail. Ordinary death does not decrement.
    bool EarlyReclaim(GoldenEntityKey key, std::span<const GoldenCandidate> candidates, ConfigError& error);
    // Begin once, perform optional valid-player event then restore original
    // type, then FinishDeath. Null attacker still restores/removes membership.
    std::optional<GoldenDeathRecord> BeginDeath(GoldenEntityKey key);
    bool FinishDeath(GoldenEntityKey key);
    std::span<const GoldenPlan> Plans() const { return plans_; }
    std::int32_t CompletedPlans() const { return completed_; }
    std::int32_t DeferredPlans() const { return deferred_; }
    std::size_t MemberCount() const { return members_.size(); }
private:
    struct Member
    {
        GoldenEntityKey key;
        std::shared_ptr<const MonsterType> original, replacement;
        std::vector<std::int32_t> contributors;
        bool reclaimed{}, dying{};
    };
    bool Ready(std::span<const GoldenCandidate> candidates, ConfigError& error) const;
    std::optional<GoldenPlan> Choose(std::span<const GoldenCandidate> candidates);
    std::size_t FindPlan(int round, int wave) const;
    std::uint32_t Random(std::uint32_t maximum);
    std::shared_ptr<const GoldenSettings> settings_;
    const GoldenDifficulty* difficulty_{};
    GoldenExtraParameters extras_;
    std::uint64_t session_{};
    std::int32_t count_{}, delay_{}, completed_{}, deferred_{};
    std::mt19937 random_;
    std::vector<GoldenPlan> plans_;
    std::map<int, Member> members_;
};
}
