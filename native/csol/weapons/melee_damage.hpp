// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "melee_attacks.hpp"
#include <string_view>

namespace csol::melee {

struct DamageConfig {
    Damage base{};
    float zombieGroupMultiplier{};
    float modes11And13Multiplier{};
    float scenarioGroupMultiplier{};
    float mode18Multiplier{};
};

// Parse transactionally: a malformed reload does not replace valid settings.
bool ParseDamageConfig(std::string_view json, DamageConfig& destination);
// mode is the original CSO game-mode number, not the bridge's csol_game_mode.
float DamageMultiplier(const DamageConfig& config, int originalMode);
float AttackDamage(const DamageConfig& config, int originalMode, Button button, bool fastPrimary);

} // namespace csol::melee
