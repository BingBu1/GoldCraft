// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "map_resources.hpp"

#include <memory>
#include <optional>

namespace csol::scenario
{
struct BossSkillRow
{
    std::size_t recordIndex{};
    std::int16_t id{}, type{}, ratio{};
    float cooltime{}, animationTime{};
    std::string sequence;
    float damageDelay{}, damage{}, damageCosine{}, damageHeight{}, damageDistance{};
    float forceAttackTime{}, startDistanceMinSquared{}, startDistanceMaxSquared{};
    float startHealthMin{}, startHealthMax{}, approachDistanceSquared{};
    std::int16_t startMessage{};
    float kickSide{}, kickUp{}, rushPower{};
};

struct BossSkillTable
{
    CsvDocument source;
    std::vector<BossSkillRow> rows;
    // Duplicate IDs append rows in physical CSV order. ID is the monster's
    // skill key (1002 for Oberon), not its monster type ID (30200).
    std::map<std::int16_t, std::vector<std::size_t>> groups;
};

bool ReadBossSkills(std::string_view bytes, BossSkillTable& output, ConfigError& error);
bool ReadBossSkillResource(std::span<const std::byte> encoded,
    BossSkillTable& output, ConfigError& error);

struct BossSkillState
{
    std::int16_t sequence{-1};
    float cooldownDeadline{};
};

struct BossSkillPlayer
{
    int slot{}, team{};
    std::uint32_t flags{};
    bool alive{};
    std::array<float, 3> origin{};
};

struct BossSkillSubject
{
    int team{};
    float health{}, maxHealth{};
    std::array<float, 3> origin{};
};

struct BossSkillCandidate
{
    std::size_t row{};
    int playerSlot{};
};

struct BossSkillCandidates
{
    std::vector<BossSkillCandidate> entries;
    std::int32_t total{};
    std::optional<std::size_t> priority;
    bool NeedsRandom() const { return total != 0 && !priority; }
};

struct BossSkillTimer
{
    float duration{}, deadline{}, startedAt{};
    void Start(float interval, float now);
    bool Elapsed(float now) const { return now > deadline; }
};

struct BossSkillClocks
{
    BossSkillTimer target, end, selection, startCheck, force;
};

// One mutable row-state array per loaded map resource, shared by every Boss
// using it. Shared ownership keeps memory alive; callers must still validate
// the map/round/entity lease before every engine callback and state mutation.
class BossSkills
{
public:
    explicit BossSkills(BossSkillTable table);
    const BossSkillTable& Table() const { return table_; }
    const BossSkillState* State(std::size_t row) const;

    // Supply existing client slots in ascending order (including slot 32).
    // No LOS filter; distance is 3D and the original flags test is equality.
    bool Candidates(std::int16_t key, const BossSkillSubject& subject,
        std::span<const BossSkillPlayer> players, float now,
        BossSkillCandidates& output, ConfigError& error) const;
    // The caller consumes exactly one UCRT rand() iff NeedsRandom(). A zero
    // total selects nothing, even if a preceding negative weight cancels 255.
    bool Choose(const BossSkillCandidates& candidates, std::optional<std::int32_t> random,
        std::optional<BossSkillCandidate>& output, ConfigError& error) const;
    bool CommitSelection(std::size_t row, float now, BossSkillClocks& clocks);
    // LookupSequence is lazy while State(row)->sequence == -1. Store only its
    // low 16 bits; a failed lookup (-1) is retried on the next activation.
    bool CacheSequence(std::size_t row, int sequence);
    // Only the original interruption's shared cooldown write. The Boss
    // behavior separately owns target, animation and other clock cleanup.
    bool Interrupt(std::size_t row, float now, std::int16_t hostSequenceState);

private:
    BossSkillTable table_;
    std::vector<BossSkillState> states_;
};

class BossSkillResources
{
public:
    using Current = bool (*)(const void*);
    // Load maps/zbs_sk_<map>.cso through the game filesystem. The original
    // missing-file path leaves an empty table; it never reuses the old map.
    bool Load(std::string_view mapName, ResourceFileApi files,
        Current current, const void* context, ResourceError& error);
    bool Clear();
    std::shared_ptr<BossSkills> Snapshot() const { return skills_; }
private:
    std::shared_ptr<BossSkills> skills_;
    bool loading_{};
};
}
