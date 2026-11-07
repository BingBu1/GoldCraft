// SPDX-License-Identifier: GPL-3.0-or-later
#include "monster_death.hpp"

#include <cmath>
#include <cstdlib>

namespace csol::scenario
{
int SelectOrdinaryMonsterDeathSequence(const MonsterSequences* sequences, int hitgroup)
{
    if (!sequences) return 0;
    if (hitgroup == 1) return (*sequences)[MonsterSequence::Head];
    constexpr std::array slots{
        MonsterSequence::Death1, MonsterSequence::Death2, MonsterSequence::Death3,
        MonsterSequence::Gutshot, MonsterSequence::Left, MonsterSequence::Back,
        MonsterSequence::Right, MonsterSequence::Forward};
    return (*sequences)[slots[static_cast<std::size_t>(std::rand() % 8)]];
}

bool ResolveMonsterDeathDeadline(const MonsterDeathTiming& timing, float& deadline)
{
    if (!std::isfinite(timing.now) || !std::isfinite(timing.adjustedSeconds)) return false;
    float result = timing.now + timing.adjustedSeconds;
    if (timing.shortDeath) result = timing.now + 3.0f;
    if (timing.scenarioType == 10) result = timing.now + 10.0f;
    if (timing.skill == 11 || timing.skill == 13) result = timing.now;
    if (timing.priority != 0 && (timing.mapId == 361 || timing.mapId == 236))
        result = timing.now + 5.0f;
    if (!std::isfinite(result)) return false;
    deadline = result;
    return true;
}
}
