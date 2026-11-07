// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <random>

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

// Original weapon-attribute probability uses a fresh uint32 random-device
// seed, then the first two MT19937 words. The caller obtains a seed only when
// kind5 > 2^-23; this helper neither samples entropy nor alters damage.
double WeaponAttributeRandom(std::uint32_t seed);
// Parts10/11 continue the same engine for every record of one kind. The
// caller creates a fresh engine only when that kind has a nonempty list.
double WeaponAttributeRandom(std::mt19937& engine);
}
