// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

class CBasePlayer;
class CBaseEntity;
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

// Independent player actions, separate from CSO's unchanged 32-byte context
// and from the held weapon. CoF damage adaptation does not add kick motion.
enum class CSOLMonsterActionKind : std::uint8_t { None, HumanKick, GuestCofPushback, GuestCofFlare };
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

enum class CSOLMonsterActionDamageResult { Ignored, Applied, Killed, Cancelled, Failed };
using CSOLHumanKickDamageResult = CSOLMonsterActionDamageResult;

// Call once for each server-selected victim. direction is target-origin minus
// actor-origin (it need not be normalized); actorVelocity is the attack sample.
// The real TakeDamage pipeline owns upgrades/armor/rewards/death. No source
// entity, timer allocation, held-weapon multiplier or weapon callback is used.
CSOLHumanKickDamageResult CSOL_HumanKickMonsterDamage(CCSOLScenarioMonster& target,
    CBasePlayer& actor, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    float baseDamage, const Vector& direction, const Vector& actorVelocity,
    csol::scenario::ConfigError& error,
    bool (*ownerCurrent)(const void*), const void* ownerContext);

// Damage-only host adapter for original CoF100E8B40: actor is both source and
// attacker. Native DMG_CLUB replaces incompatible CoF damage bits. Existing
// CSOL damage/flinch behavior owns the target response; no CoF monster AI or
// HumanKick impulse/stagger is installed (user direction, 2026-11-16).
CSOLMonsterActionDamageResult CSOL_GuestCofPushbackMonsterDamage(CCSOLScenarioMonster& target,
    CBasePlayer& actor, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    float baseDamage, csol::scenario::ConfigError& error,
    bool (*ownerCurrent)(const void*), const void* ownerContext);

// A thrown flare keeps burning if its thrower's player/life registration expires.
void CSOL_GuestCofFlareMonsterDamage(CCSOLScenarioMonster& target, CBaseEntity& flare,
    CBasePlayer* thrower, const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    bool (*ownerCurrent)(const void*), const void* ownerContext);

// Internal typed-action consumers; ordinary weapon hits retain their old path.
bool CSOL_CaptureMonsterActionDamage(CSOLMonsterDamageCall& call);
void CSOL_CommitMonsterActionHealth(CSOLMonsterDamageCall& call);
void CSOL_FinishMonsterActionDamage(CSOLMonsterDamageCall& call, bool killed);
bool CSOL_MonsterActionCreditCurrent(const CSOLMonsterActionCredit& credit, const CBasePlayer* actor);
bool CSOL_HumanKickMonsterStaggered(CCSOLScenarioMonster& target, float now);

// Enter before any SDK/engine callbacks in the actual virtual TakeDamage.
// Nested unrelated damage cannot consume the pending action's one-use credit.
class CSOLMonsterActionDamageEntry
{
public:
    CSOLMonsterActionDamageEntry(CCSOLScenarioMonster& target,
        const entvars_s* inflictor, const entvars_s* attacker);
    ~CSOLMonsterActionDamageEntry();
    CSOLMonsterActionDamageEntry(const CSOLMonsterActionDamageEntry&) = delete;
    CSOLMonsterActionDamageEntry& operator=(const CSOLMonsterActionDamageEntry&) = delete;
private:
    void* m_previous{};
};
