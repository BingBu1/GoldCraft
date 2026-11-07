// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "control.hpp"
#include "damage.hpp"
#include "../scenario/core/scenario_death_requests.h"
#include <memory>
#include <optional>

class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioSession;
class CSOLMonsterDamageCall;
class Vector;
struct CSOLScenarioPlayerRuntimeState;

std::optional<CSOLMonsterDeathContext> CSOL_ConsumeCharacterSkillDamageContext(
    CCSOLScenarioMonster& target, entvars_t* inflictor, entvars_t* attacker, int bits);

namespace csol::scenario::skills
{
// The skill has already selected this target. Preserve its exact damage bits
// and context, without a second sphere/team/range test. A target disappearing
// in its own damage callback does not cancel the caster's remaining effects.
bool ApplyCharacterTargetDamage(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, const EHANDLE& target,
    float damage, int bits, const CSOLMonsterDeathContext& context, ConfigError& error);
// Original1152EFE0 sphere enumeration with1152FDF0(false, true), fixed
// damage and the same per-hit context used by character area attacks.
bool ApplyFlatCharacterAreaDamage(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const Vector& origin, float radius, float damage, int bits, ConfigError& error);
bool StartCharacterAreaDamage(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error);
bool ApplyLoveBombDamage(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    DamageState& effect, DamageHost& recovery, CSOLMonsterDamageCall& call,
    float damage, ConfigError& error);
bool ApplyDragonBreathDamage(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    DamageState& effect, CSOLMonsterDamageCall& call, ConfigError& error);
}
