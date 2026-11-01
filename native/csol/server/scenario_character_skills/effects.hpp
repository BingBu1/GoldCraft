// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "control.hpp"
#include "damage.hpp"
#include "area_rules.hpp"
#include <array>
#include <memory>

class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioSession;
class CBaseEntity;
template<typename T> class EntityHandle;
using EHANDLE = EntityHandle<CBaseEntity>;
struct CSOLScenarioPlayerRuntimeState;

namespace csol::scenario::skills
{
struct AuraMember
{
    std::weak_ptr<const CSOLScenarioPlayerRuntimeState> state;
    std::uint64_t life{};
};
struct AuraState
{
    std::array<AuraMember, 32> members{};
    std::size_t count{};
    std::uint32_t effectId{};
    std::uint64_t revision{};
    float nextUpdate{};
    int team{};
    bool initialized{}, active{};
};
enum class AuraStage { Rebuild, RemoveVisual, Retire, Start, End, Update };
bool RunAuraSkill(int subtype, AuraStage stage,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    AuraState& aura, std::uint64_t identity, ConfigError& error);
struct ReflectionState
{
    float accumulated{}, nextRecovery{};
    std::uint32_t effectId{};
};
struct ShieldAttackState
{
    float nextStage{-1};
    int remaining{};
};
bool StartLegacyShieldSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, const ActivationState& timers,
    std::uint64_t identity, ShieldAttackState& attack, ConfigError& error);
bool ActiveShieldAttack30(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ShieldAttackState& attack, ConfigError& error);
bool StartGroupSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const ActivationState& timers, ConfigError& error);
bool StartDrainSupport37(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, std::uint64_t identity, ConfigError& error);
bool StartReflectionSkill36(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, const ActivationState& timers,
    ReflectionState& reflection, ConfigError& error);
bool ActiveReflectionSkill36(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ReflectionState& reflection, ConfigError& error);
struct LotusState
{
    std::array<float, 3> origin{};
    LotusValues values{};
    float nextPulse{};
    std::uint64_t life{};
    bool started{};
};
bool StartLotusSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, LotusState& lotus, std::uint64_t identity, ConfigError& error);
bool ActiveLotusSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, LotusState& lotus, ConfigError& error);
// Availability describes concrete SDK effects, not membership in the
// original factory catalog. Preparing visuals is owned by host_notifications.
bool HasOtherSkillEffects(int subtype) noexcept;
bool PrepareOtherSkillEffects(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, bool& prepared, ConfigError& error);
bool StartOtherSkillEffects(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    ActivationState& timers, DamageState& damageState, ConfigError& error,
    std::uint64_t identity = 0, bool* incomingUpgraded = nullptr);
bool UpgradeIncomingSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ConfigError& error);
bool EndSmokeCloakSkill(int subtype,
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error);
bool EndEscortSkill(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error);
bool EndRallySkill38(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error);
bool EndShieldSkill45(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ConfigError& error);
bool StartGuardSkill(int subtype, const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, ActivationState& timers,
    std::uint64_t identity, float& accumulated, ConfigError& error);
bool EndGuardRecovery52(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session, float accumulated, ConfigError& error);
bool StartDragonShield53(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    std::uint64_t identity, ConfigError& error);
// Call once from the incoming player-damage module stage, before armor.
// `absorbed` supplies original context byte29; the caller owns that context.
bool AbsorbOtherSkillDamage(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const EHANDLE& target, const EHANDLE& attacker, float& damage,
    bool& absorbed, ConfigError& error);
}
