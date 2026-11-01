// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

class CCSOLScenarioRuntime;
class CSOLMonsterSpawnCall;
struct CSOLMonsterSpawnState;
struct CSOLOrdinaryMonsterState;
namespace csol::scenario { struct ConfigError; }

inline constexpr const char* CSOLMonsterTrapModel = "models/zombitrap.mdl";
inline constexpr const char* CSOLMonsterDisarmModel = "models/w_hiddenTail.mdl";
inline constexpr const char* CSOLMonsterDisarmTrail = "sprites/ef_lockongun_trail.spr";

struct CSOLMonsterSkillGuard
{
    bool (*valid)(const void*){};
    const void* context{};
    bool Current() const { return valid && valid(context); }
};

// Original mode15 skill8: a rejected target/trace or exhausted entity pool
// still consumes the skill cooldown. Host cancellation is a separate result.
bool CSOL_UseMonsterDisarm(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CSOLMonsterSkillGuard guard, csol::scenario::ConfigError& error);

bool CSOL_CreateMonsterDisarm(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& direction, bool& created, csol::scenario::ConfigError& error);
bool CSOL_CreateMonsterTrap(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, csol::scenario::ConfigError& error);
