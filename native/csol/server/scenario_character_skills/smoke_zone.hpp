// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "control.hpp"
#include "../scenario/core/scenario_death_requests.h"
#include <memory>
#include <optional>

class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioSession;
class CCSOLScenarioMonster;
struct CSOLScenarioPlayerRuntimeState;

std::optional<CSOLMonsterDeathContext> CSOL_ConsumeSmokeSkillDamageContext(
    CCSOLScenarioMonster&, entvars_t* inflictor, entvars_t* attacker, int bits);

namespace csol::scenario::skills
{
bool StartSmokeCloakSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ConfigError& error);
}
