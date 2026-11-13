// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

class CBasePlayer;
class Vector;
class CCSOLScenarioRuntime;
class CCSOLScenarioSession;
class CCSOLScenarioPlayerRuntime;
class CSOLScenarioEntityAccess;
class CCSOLScenarioMonster;
class CSOLMonsterDamageCall;
struct CSOLScenarioPlayerRuntimeState;
namespace csol::scenario { struct ConfigError; }
struct entvars_s;

// User-authorized host action, separate from CSO's unchanged 32-byte context.
// This is not a weapon ID or the original non-player kick branch.
enum class CSOLMonsterActionKind : std::uint8_t { None, HumanKick };
struct CSOLMonsterActionCredit
{
    CSOLMonsterActionKind kind{CSOLMonsterActionKind::None};
    std::weak_ptr<CCSOLScenarioRuntime> runtime;
    std::weak_ptr<CCSOLScenarioSession> session;
    std::weak_ptr<CCSOLScenarioPlayerRuntime> players;
    std::weak_ptr<const CSOLScenarioEntityAccess> access;
    std::shared_ptr<const CSOLScenarioPlayerRuntimeState> actorState;
    std::uint64_t runtimeGeneration{}, roundGeneration{}, actorGeneration{}, actorLife{}, actorSpawn{};
};

enum class CSOLHumanKickDamageResult { Ignored, Applied, Killed, Cancelled, Failed };

// Call once for each server-selected victim. direction is target-origin minus
// actor-origin (it need not be normalized); actorVelocity is the attack sample.
// The real TakeDamage pipeline owns upgrades/armor/rewards/death. No source
// entity, timer allocation, held-weapon multiplier or weapon callback is used.
CSOLHumanKickDamageResult CSOL_HumanKickMonsterDamage(CCSOLScenarioMonster& target,
    CBasePlayer& actor, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    float baseDamage, const Vector& direction, const Vector& actorVelocity,
    csol::scenario::ConfigError& error,
    bool (*ownerCurrent)(const void*), const void* ownerContext);

// Internal typed-action consumers; ordinary weapon hits retain their old path.
bool CSOL_CaptureHumanKickDamage(CSOLMonsterDamageCall& call);
void CSOL_CommitHumanKickHealth(CSOLMonsterDamageCall& call);
void CSOL_FinishHumanKickDamage(CSOLMonsterDamageCall& call, bool killed);
bool CSOL_HumanKickCreditCurrent(const CSOLMonsterActionCredit& credit, const CBasePlayer* actor);
bool CSOL_HumanKickMonsterStaggered(CCSOLScenarioMonster& target, float now);

// Enter before any SDK/engine callbacks in the actual virtual TakeDamage.
// Nested unrelated damage cannot consume the pending kick's one-use credit.
class CSOLHumanKickDamageEntry
{
public:
    CSOLHumanKickDamageEntry(CCSOLScenarioMonster& target,
        const entvars_s* inflictor, const entvars_s* attacker);
    ~CSOLHumanKickDamageEntry();
    CSOLHumanKickDamageEntry(const CSOLHumanKickDamageEntry&) = delete;
    CSOLHumanKickDamageEntry& operator=(const CSOLHumanKickDamageEntry&) = delete;
private:
    void* m_previous{};
};
