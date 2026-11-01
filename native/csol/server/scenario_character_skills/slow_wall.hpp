// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "control.hpp"
#include <memory>

class CCSOLScenarioPlayerRuntime;
class CCSOLScenarioSession;
struct CSOLScenarioPlayerRuntimeState;

namespace csol::scenario::skills
{
bool StartSlowWall17(const std::shared_ptr<CCSOLScenarioPlayerRuntime>& players,
    const std::shared_ptr<CSOLScenarioPlayerRuntimeState>& state,
    const std::shared_ptr<CCSOLScenarioSession>& session,
    const ActivationState& timers, ConfigError& error);
}
