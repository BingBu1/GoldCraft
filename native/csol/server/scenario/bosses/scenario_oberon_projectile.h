// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../monsters/scenario_monster_factory.h"
#include "../../../scenario/bosses/boss_skills.hpp"
class CCSOLScenarioRuntime;
struct CSOLOberonProjectileState;
class CCSOLOberonProjectile final : public CBaseAnimating
{
public:
    void Spawn() override;
    void OnDestroy() override;
    void FlightThink();
    void BounceTouch(CBaseEntity* other);
    static bool CreateBatch(const std::shared_ptr<CCSOLScenarioRuntime>&,
        CSOLMonsterSpawnCall&, const csol::scenario::BossSkillRow&, csol::scenario::ConfigError&);
private:
    friend struct CSOLOberonProjectileState;
    static bool Create(const std::shared_ptr<CCSOLScenarioRuntime>&,
        CSOLMonsterSpawnCall&, const csol::scenario::BossSkillRow&,
        int targetSlot, int offset, csol::scenario::ConfigError&);
    std::shared_ptr<CSOLOberonProjectileState> m_state;
};
