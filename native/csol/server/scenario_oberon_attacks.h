// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_monster_factory.h"
#include "../scenario/oberon_combat.hpp"

class CCSOLScenarioRuntime;
// The projectile supplies its round/entity guard; an expired creator does
// not cancel a bomb already in flight. Melee supplies the live spawn guard.
struct CSOLOberonAttackGuard
{
    CBaseEntity* (*resolve)(const void*);
    const void* context;
    CBaseEntity* Current() const { return resolve ? resolve(context) : nullptr; }
    CBaseEntity* (*attacker)(const void*){};
    CBaseEntity* Attacker() const { return attacker ? attacker(context) : Current(); }
};
bool CSOL_OberonSector(const std::shared_ptr<CCSOLScenarioRuntime>&,
    CSOLMonsterSpawnCall&, const csol::scenario::BossSkillRow&, csol::scenario::ConfigError&);
bool CSOL_OberonCylinder(const std::shared_ptr<CCSOLScenarioRuntime>&,
    CSOLMonsterSpawnCall&, const csol::scenario::BossSkillRow&, float launchZ,
    bool landing, csol::scenario::ConfigError&);
bool CSOL_OberonBombDamage(const std::shared_ptr<CCSOLScenarioRuntime>&,
    const CSOLOberonAttackGuard&, float damage, float radius,
    float kickSide, float kickUp, csol::scenario::ConfigError&);
