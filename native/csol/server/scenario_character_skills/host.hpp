// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "control.hpp"
#include "catalog.hpp"
#include <memory>

class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioSession;
class CSOLScenarioActiveSkill;
class CBasePlayer;
struct CSOLScenarioPlayerRuntimeState;
namespace csol::scenario { struct ClassModDefinition; }

namespace csol::scenario::skills
{
// The effect owner implements the actual original body. Catalog membership
// does not supply an implementation. Callers skip a callback only when its
// SkillTraits entry explicitly proves that original virtual is empty.
class HostEffects
{
public:
    virtual ~HostEffects() = default;
    virtual bool Initialize(ActivationState&, ConfigError&) = 0; // v3C
    virtual bool Lifecycle30(ActivationState&, ConfigError&) = 0;
    virtual bool Lifecycle34(ActivationState&, ConfigError&) = 0;
    virtual bool Lifecycle38(ActivationState&, ConfigError&) = 0;
    // The concrete controller supplies the original notification arguments
    // through its retained operation context, not a guessed private ABI.
    virtual bool Notify28(ActivationState&, ConfigError&) = 0;
    virtual bool Notify2C(ActivationState&, ConfigError&) = 0;
    virtual bool Prepare(ActivationState&, bool& prepared, ConfigError&) = 0;
    virtual bool Start(ActivationState&, ConfigError&) = 0;
    virtual bool Active(ActivationState&, ConfigError&) = 0;
    virtual bool End(ActivationState&, ConfigError&) = 0;
    virtual bool Update(ActivationState&, ConfigError&) = 0;
    virtual bool Voice(ConfigError&) = 0;
    // Trusted rule/StatBuff owners answer these. An absent implementation is
    // not equivalent to FrozenWrath=false or a zero cooldown reduction.
    virtual bool FrozenWrathBlocks(bool& blocked, ConfigError&) = 0;
    virtual bool TargetAvailable(bool& available, ConfigError&) = 0;
    virtual bool CooldownReduction(float& reduction, ConfigError&) = 0;
    virtual bool UiDelay(float& delay, ConfigError&) = 0;
};

std::shared_ptr<ActivationHost> CreateHost(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const SkillTraits& traits, const ClassModDefinition& definition,
    const CSOLScenarioActiveSkill* owner, ActivationState& timers,
    std::shared_ptr<HostEffects> effects, ConfigError& error);
}
