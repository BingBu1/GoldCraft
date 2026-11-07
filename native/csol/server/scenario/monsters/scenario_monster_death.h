// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_monster_animation.h"
#include "scenario_monster_pool.h"

class CCSOLMonsterModels;

// Entity-state stages of a single synchronous death, owned on the death
// handler's stack. Capture BEFORE notification/beforeDeath. The owner keeps the
// original gaps: notification/beforeDeath -> Animate -> ReturnPointAndSound ->
// player money -> MarkDead -> read/adjust duration -> ScheduleReturn ->
// rules/rewards/effects/map/Golden/postDeath.
// This does not implement or silently omit those surrounding owner callbacks.
class CCSOLMonsterDeathStages
{
public:
    using SessionCheck = bool (*)(const void*);
    explicit CCSOLMonsterDeathStages(CCSOLScenarioMonster& monster,
        SessionCheck sessionCheck = nullptr, const void* sessionContext = nullptr);
    CCSOLMonsterDeathStages(const CCSOLMonsterDeathStages&) = delete;
    CCSOLMonsterDeathStages& operator=(const CCSOLMonsterDeathStages&) = delete;

    // Re-resolve after EVERY owner callback. An early pool Free is allowed;
    // another Acquire of the same entity, even with the same type, is not.
    CCSOLScenarioMonster* Current() const;
    bool Animate(CCSOLMonsterAnimation& animation,
        const csol::scenario::MonsterSequences& sequences, int savedHitgroup);
    // The live behavior selects its sequence after observing health/movement
    // changes. Keep this gap explicit for derived behavior callbacks.
    bool BeginAnimation();
    bool FinishAnimation(CCSOLMonsterAnimation& animation, int sequence);
    bool ReturnPointAndSound(csol::scenario::SpawnPoints& points,
        const CCSOLMonsterModels& models, bool suppressSound);
    // Must precede the appearance/behavior duration calls: those callbacks
    // observe DAMAGE_NO, DEAD_DEAD and SOLID_NOT in the original.
    bool MarkDead();
    // slotSkill is the attribute-stage copy, not a later replacement type's
    // skill. The live type supplies the priority used by the map override.
    bool ScheduleReturn(float adjustedSeconds, bool shortDeath, int scenarioType,
        std::int16_t slotSkill, int mapId);
    bool Scheduled() const { return m_stage == Stage::Scheduled; }

private:
    enum class Stage { Invalid, Captured, SelectingAnimation, Animated, PointReturned, Dead, Scheduled };
    EntityHandle<CCSOLScenarioMonster> m_entity;
    CCSOLMonsterPool* m_pool{};
    std::uint64_t m_lease{};
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access;
    SessionCheck m_sessionCheck{};
    const void* m_sessionContext{};
    Stage m_stage{Stage::Invalid};
};
