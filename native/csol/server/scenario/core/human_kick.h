// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

class CBasePlayer;
class CCSOLScenarioRuntime;
class CSOLScenarioEntityAccess;
namespace csol::scenario { struct ConfigError; }

bool CSOL_PrecacheHumanKick(const std::shared_ptr<const CSOLScenarioEntityAccess>& access,
    csol::scenario::ConfigError& error);
bool CSOL_TryHumanKick(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, csol::scenario::ConfigError& error);
bool CSOL_UpdateHumanKick(const std::shared_ptr<CCSOLScenarioRuntime>& runtime,
    CBasePlayer& player, csol::scenario::ConfigError& error, bool force = false);
void CSOL_ResetHumanKick(CBasePlayer& player, bool restore = true);
bool CSOL_PreserveHumanKickAnimation(CBasePlayer& player, int activity);
// Server-owned configuration. A disabled in-flight action releases its own
// weapon/animation lock at the next player update and publishes Disabled.
void CSOL_SetHumanKickPolicy(bool enabled);
