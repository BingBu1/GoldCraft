// SPDX-License-Identifier: GPL-3.0-or-later
#include "spawn_points.hpp"

namespace csol::scenario
{
void SpawnPoints::RegisterMonsterPoint(const SpawnPoint& point, int maxSpawn)
{
    monsters_[PointKey(point.team, point.id)] = {point, maxSpawn, 0};
}

void SpawnPoints::AppendPlayerPoint(const SpawnPoint& point)
{
    players_[PointKey(point.team, point.id)].push_back(point);
}

const MonsterPoint* SpawnPoints::FindMonsterPoint(int team, int id) const
{
    const auto it = monsters_.find(PointKey(team, id));
    return it == monsters_.end() ? nullptr : &it->second;
}

const std::vector<SpawnPoint>* SpawnPoints::FindPlayerPoints(int team, int id) const
{
    const auto it = players_.find(PointKey(team, id));
    return it == players_.end() ? nullptr : &it->second;
}

bool SpawnPoints::ConsumePointQuota(int team, int id)
{
    const auto it = monsters_.find(PointKey(team, id));
    if (it == monsters_.end()) return true;
    auto& point = it->second;
    if (point.maxSpawn == 0) return true;
    // MP compares signed dwords; preserve the signed interpretation explicitly.
    if (std::bit_cast<std::int32_t>(point.consumed) >= point.maxSpawn) return false;
    ++point.consumed;
    return true;
}

void SpawnPoints::ReturnPointQuota(int team, int id)
{
    const auto it = monsters_.find(PointKey(team, id));
    if (it != monsters_.end()) --it->second.consumed;
}

void SpawnPoints::ResetMonsterQuotas()
{
    for (auto& [key, point] : monsters_) point.consumed = 0;
}

void SpawnPoints::Clear()
{
    monsters_.clear();
    players_.clear();
}
}
