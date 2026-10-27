// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_monster_death.h"
#include "scenario_monster_models.h"
#include "../scenario/monster_death.hpp"

CCSOLMonsterDeathStages::CCSOLMonsterDeathStages(CCSOLScenarioMonster& monster,
    SessionCheck sessionCheck, const void* sessionContext)
    : m_sessionCheck(sessionCheck), m_sessionContext(sessionContext)
{
    if (m_sessionCheck && !m_sessionCheck(m_sessionContext)) return;
    auto* pool = monster.Pool();
    // The original death gate uses deadflag, not health or solid. A lethal
    // hit has usually already reduced health to zero before entering here.
    if (!pool || pool->Busy() || !pool->Borrowed(monster) || !monster.Type() ||
        monster.pev->deadflag == DEAD_DEAD) return;
    const auto lease = pool->Lease(monster);
    if (!lease) return;
    m_entity = &monster;
    m_pool = pool;
    m_lease = lease;
    m_stage = Stage::Captured;
}

CCSOLScenarioMonster* CCSOLMonsterDeathStages::Current() const
{
    // Engine callbacks in animation/sound can release the entire edict array.
    // The synchronous owner supplies its generation check, before any EHANDLE.
    if (m_sessionCheck && !m_sessionCheck(m_sessionContext)) return nullptr;
    auto* base = m_entity.Get<CBaseEntity>();
    auto* monster = base ? dynamic_cast<CCSOLScenarioMonster*>(base) : nullptr;
    // Shutdown detaches the entity before destroying the owner. Never read a
    // cached owner pointer until the live entity still identifies it. A round
    // restart clears Type even though Free alone intentionally retains it.
    if (!monster || !m_pool || monster->Pool() != m_pool || !monster->Type()) return nullptr;
    if (m_pool->Busy() || m_pool->State() != CSOLMonsterPoolState::Ready) return nullptr;
    return m_pool->Lease(*monster) == m_lease ? monster : nullptr;
}

bool CCSOLMonsterDeathStages::Animate(CCSOLMonsterAnimation& animation,
    const csol::scenario::MonsterSequences& sequences, int savedHitgroup)
{
    if (!BeginAnimation()) return false;
    return FinishAnimation(animation,
        csol::scenario::SelectOrdinaryMonsterDeathSequence(&sequences, savedHitgroup));
}

bool CCSOLMonsterDeathStages::BeginAnimation()
{
    if (m_stage != Stage::Captured) return false;
    m_stage = Stage::Invalid; // No recursive entry during ResetSequenceInfo.
    auto* monster = Current();
    if (!monster) return false;
    monster->pev->health = 0;
    monster->pev->movetype = MOVETYPE_TOSS;
    monster->pev->flags &= ~FL_ONGROUND;
    m_stage = Stage::SelectingAnimation;
    return true;
}

bool CCSOLMonsterDeathStages::FinishAnimation(CCSOLMonsterAnimation& animation, int sequence)
{
    if (m_stage != Stage::SelectingAnimation) return false;
    m_stage = Stage::Invalid;
    auto* monster = Current();
    if (!monster) return false;
    animation.SetSequence(*monster, sequence, true);
    if (!Current()) return false;
    m_stage = Stage::Animated;
    return true;
}

bool CCSOLMonsterDeathStages::ReturnPointAndSound(csol::scenario::SpawnPoints& points,
    const CCSOLMonsterModels& models, bool suppressSound)
{
    if (m_stage != Stage::Animated) return false;
    m_stage = Stage::Invalid; // Quota may never be returned twice by a retry.
    auto* monster = Current();
    if (!monster || !monster->Type()) return false;
    if (monster->Type()->priority != 2)
        points.ReturnPointQuota(monster->pev->team, monster->SpawnPointId());
    if (!suppressSound)
    {
        const auto* appearance = csol::scenario::FindMonsterAppearance(monster->Type()->appearance);
        if (!appearance || !models.EmitDeathSound(*monster, *appearance) || !Current()) return false;
    }
    m_stage = Stage::PointReturned;
    return true;
}

bool CCSOLMonsterDeathStages::MarkDead()
{
    if (m_stage != Stage::PointReturned) return false;
    m_stage = Stage::Invalid;
    auto* monster = Current();
    if (!monster) return false;
    monster->pev->takedamage = DAMAGE_NO;
    monster->pev->deadflag = DEAD_DEAD;
    monster->pev->solid = SOLID_NOT;
    m_stage = Stage::Dead;
    return true;
}

bool CCSOLMonsterDeathStages::ScheduleReturn(float adjustedSeconds, bool shortDeath,
    int scenarioType, std::int16_t slotSkill, int mapId)
{
    if (m_stage != Stage::Dead) return false;
    m_stage = Stage::Invalid;
    auto* monster = Current();
    if (!monster || !monster->Type() || !gpGlobals) return false;
    const auto* type = monster->Type();
    const csol::scenario::MonsterDeathTiming timing{
        gpGlobals->time, adjustedSeconds, shortDeath, scenarioType, slotSkill, type->priority, mapId};
    float deadline{};
    if (!csol::scenario::ResolveMonsterDeathDeadline(timing, deadline)) return false;
    // Do not call Pool::BeginDeath: beforeDeath can have returned this same
    // lease already. The original outer path still installs its late Think.
    monster->pev->nextthink = deadline;
    monster->SetThink(&CCSOLScenarioMonster::FreeThink);
    m_stage = Stage::Scheduled;
    return true;
}
