// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace csol::scenario
{
struct MonsterArmorDamage
{
    float healthDamage{};
    float remainingArmor{};
};

// Separate original stages. These helpers never subtract health or grant
// rewards. GameRules, player/weapon modifiers and damage context still run
// between/after stages; this is not a complete monster TakeDamage policy.
bool ResolveMonsterTraceDamage(float input, int traceHitgroup, float& output);

// The caller supplies a weapon ID only for a valid attacking player whose
// inflictor is that player and whose active weapon handle is valid. Otherwise
// use 0. This is the ordinary zombie behavior, not Oberon's override.
bool ResolveOrdinaryMonsterDamage(float input, int hitgroup, int directPlayerWeapon,
    float& output);
bool ResolveMonsterArmorDamage(float input, float armor, std::uint32_t damageBits,
    MonsterArmorDamage& output);
}
