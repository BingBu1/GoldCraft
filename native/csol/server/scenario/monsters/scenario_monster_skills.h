// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

class CCSOLScenarioRuntime;
class CCSOLScenarioMessages;
class CSOLMonsterSpawnCall;
struct CSOLMonsterSpawnState;
struct CSOLOrdinaryMonsterState;
namespace csol::scenario { struct ConfigError; }

inline constexpr const char* CSOLMonsterTrapModel = "models/zombitrap.mdl";
inline constexpr const char* CSOLMonsterDisarmModel = "models/w_hiddenTail.mdl";
inline constexpr const char* CSOLMonsterDisarmTrail = "sprites/ef_lockongun_trail.spr";
// MP1155D720 precache order for the boomer Free explosion effects.
inline constexpr const char* CSOLBoomerPoisonModel = "models/ef_poison03.mdl";
inline constexpr const char* CSOLBoomerModel = "models/ef_boomer.mdl";
inline constexpr const char* CSOLBoomerSprite = "sprites/spr_boomer.spr";
inline constexpr const char* CSOLBoomerExplosionSprite = "sprites/ef_boomer_ex.spr";
inline constexpr const char* CSOLBoomerEffectAssets[]{CSOLBoomerPoisonModel, CSOLBoomerModel,
    CSOLBoomerSprite, CSOLBoomerExplosionSprite};

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

// Original mode15 skills 9 (113EC3E0 dash wind-up), 10 (113EFE80 piercing
// strike) and 12 (113EDC60 leap). Like skill 8, every helper return still
// writes the 113F1021 active/retry clocks; false means host cancellation.
bool CSOL_UseMonsterDash(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, bool suppressed, CSOLMonsterSkillGuard guard);
bool CSOL_UseMonsterPierce(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, bool suppressed, CSOLMonsterSkillGuard guard);
bool CSOL_UseMonsterLeap(CSOLMonsterSpawnCall& call, CSOLOrdinaryMonsterState& state,
    const CSOLMonsterSpawnState& spawn, bool suppressed, CSOLMonsterSkillGuard guard,
    csol::scenario::ConfigError& error);

// MP1155CFC0: CSO private TE138/137x3/137/(on ground) TE133 effects, then the
// 1155D030 damage part: ScreenShake, DMG_EXPLOSION and a direct velocity
// push for living players in a radius/0.25-height cylinder, then Classify16
// walls when host activeSkill is 13.
bool CSOL_ApplyMonsterFreeExplosion(CSOLMonsterSpawnCall& call, float damage, float radius,
    float force, const CCSOLScenarioMessages& messages, CSOLMonsterSkillGuard guard,
    csol::scenario::ConfigError& error);

bool CSOL_CreateMonsterDisarm(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, const Vector& origin,
    const Vector& direction, bool& created, csol::scenario::ConfigError& error);
bool CSOL_CreateMonsterTrap(CSOLMonsterSpawnCall& call,
    const std::shared_ptr<CCSOLScenarioRuntime>& runtime, csol::scenario::ConfigError& error);
