// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_player_runtime.h"
#include "../character_skill_gate.hpp"

// The capability, its effects and the SDK action host share one installed
// lifetime. A catalogue row alone never implements activation.
class CSOLScenarioControlledSkill : public CSOLScenarioDamageSkill
{
public:
    virtual bool Installed(csol::scenario::ConfigError& error) = 0;
    virtual bool Rebuild(csol::scenario::ConfigError& error) = 0;
    virtual void CancelPendingEffects() = 0;
    virtual std::uint64_t RetirementBuffIdentity() const { return 0; }
    virtual bool RetireEffects(csol::scenario::ConfigError&) { return true; }
    virtual bool Disable(csol::scenario::ConfigError& error) = 0;
    virtual bool GateSnapshot(float now, csol::skillgate::Snapshot& output,
        csol::scenario::ConfigError& error) = 0;
    virtual bool IncomingDamage(std::shared_ptr<CCSOLScenarioRuntime> runtime,
        const EHANDLE& source, float& damage, csol::scenario::ConfigError& error) = 0;
    // Original rules v1B0 -> active skill v2C, after scenario scaling and
    // before protection, shields, armor, and the later v1B8 notification.
    virtual bool BeforeProtectionDamage(std::shared_ptr<CCSOLScenarioRuntime> runtime,
        const EHANDLE& source, float& damage, csol::scenario::ConfigError& error) = 0;
    virtual bool TryActivate(std::shared_ptr<CCSOLScenarioRuntime> runtime,
        bool& activated, csol::scenario::ConfigError& error) = 0;
    virtual bool Update(std::shared_ptr<CCSOLScenarioRuntime> runtime,
        csol::scenario::ConfigError& error) = 0;
};

// Concrete initial-state/damage callbacks. Unknown subtypes return null; the
// caller must retain its explicit unsupported-path diagnostic. Activation
// effects have a separate implementation and are not inferred from ret stubs.
std::shared_ptr<CSOLScenarioActiveSkill> CSOL_CreateScenarioActiveSkill(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const csol::scenario::ClassModDefinition& definition,
    float now, csol::scenario::ConfigError& error);
