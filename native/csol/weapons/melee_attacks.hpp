// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace csol::melee {

// Original CKnife variants. These numbers are not network weapon IDs.
enum class Variant : std::uint8_t { SkullAxe = 10, DragonSword = 16 };
enum class Button : std::uint8_t { Primary, Secondary };
enum class Contact : std::uint8_t { None, World, Damageable };

struct Damage {
    float primaryFull{};
    float primaryFast{};
    float secondary{};
};

struct Timers {
    float primary{};
    float secondary{};
    float idle{};
};

struct Attack {
    Variant variant{};
    Button button{};
    int animation{};
    float hitDelay{};
    float distance{};
    float fullAngle{};
    Timers initial{};
};

struct Completion {
    // Relative to the actual delayed callback, using WeaponTimeBase semantics.
    Timers remaining{};
    int animation{-1};
    int dragonEffect{-1};
    bool playerAttackAnimation{};
};

// The owning CKnife increments its primary ordinal only on a primary attack.
// Holster, deploy and prediction ownership remain in the SDK adapter.
Attack BeginAttack(Variant variant, Button button, std::uint32_t primaryOrdinal);
Completion CompleteAttack(const Attack& attack, Contact contact);
float BaseDamage(const Damage& damage, Button button, bool fastPrimary);

} // namespace csol::melee
