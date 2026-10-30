// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_player_runtime.h"

// Concrete initial-state/damage callbacks. Unknown subtypes return null; the
// caller must retain its explicit unsupported-path diagnostic. Activation
// effects have a separate implementation and are not inferred from ret stubs.
std::shared_ptr<CSOLScenarioActiveSkill> CSOL_CreateScenarioActiveSkill(
    const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const csol::scenario::ClassModDefinition& definition,
    float now, csol::scenario::ConfigError& error);
