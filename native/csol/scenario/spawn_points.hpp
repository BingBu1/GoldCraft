// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace csol::scenario
{
// The original signed extensions matter: negative IDs overlap the team bits.
// Express the shift as unsigned arithmetic, not a left shift of a negative int.
constexpr std::int32_t PointKey(int team, int id)
{
    const auto low = static_cast<std::uint32_t>(id) & 0xffffu;
    const auto high = static_cast<std::uint32_t>(team) & 0xffu;
    const auto signedId = (low & 0x8000u) ? (low | 0xffff0000u) : low;
    const auto signedTeam = (high & 0x80u) ? (high | 0xffffff00u) : high;
    return std::bit_cast<std::int32_t>(signedId | (signedTeam << 16));
}

struct SpawnPoint
{
    std::array<float, 3> origin{};
    std::array<float, 3> angles{};
    int team{};
    int id{};
    std::size_t entityIndex{};
};

struct MonsterPoint
{
    SpawnPoint point;
    int maxSpawn{};
    std::uint32_t consumed{};
};

class SpawnPoints
{
public:
    // Call in engine entity traversal order. Monster duplicates replace the
    // record and its counter; player duplicates retain every position.
    // Entity registration separately applies Spawn defaults and skips a
    // playerspawn whose full-width ID equals -1 before the key is narrowed.
    void RegisterMonsterPoint(const SpawnPoint& point, int maxSpawn);
    void AppendPlayerPoint(const SpawnPoint& point);
    const MonsterPoint* FindMonsterPoint(int team, int id) const;
    const std::vector<SpawnPoint>* FindPlayerPoints(int team, int id) const;

    // This is an attempted-spawn quota, not a count of living monsters. The
    // caller must perform the original priority-2 bypass before consuming it.
    // As in MP, an absent point passes this quota check; FindMonsterPoint must
    // independently succeed before spawning anything.
    bool ConsumePointQuota(int team, int id);
    // Called by the scenario death/eviction path, not by delayed pool Free.
    // The caller applies mode/priority gates and prevents duplicate death work.
    // Original code decrements an existing point even at zero or maxSpawn=0;
    // preserve dword wrap rather than clamping or making this call idempotent.
    void ReturnPointQuota(int team, int id);
    // Round activation resets attempted-spawn quotas without losing map points.
    void ResetMonsterQuotas();
    void Clear();
    const auto& Monsters() const { return monsters_; }
    const auto& Players() const { return players_; }

private:
    std::map<std::int32_t, MonsterPoint> monsters_;
    std::map<std::int32_t, std::vector<SpawnPoint>> players_;
};
}
