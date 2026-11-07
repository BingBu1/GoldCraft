// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "melee_attacks.hpp"

namespace csol::melee {
// Reserved bridge identities, not original CSO weapon IDs. CSO uses weapon
// 29 with knifeType 16/10; distinct bridge entities share the native knife slot.
inline constexpr int DragonSwordId = 1022;
inline constexpr int SkullAxeId = 1023;
inline constexpr int DragonSwordItemId = 483;
inline constexpr int SkullAxeItemId = 363;
inline constexpr char EffectMessage[] = "CSOMelee";
inline constexpr int EffectBytes = 9;
constexpr bool IsWeapon(int id) { return id == DragonSwordId || id == SkullAxeId; }
constexpr Variant WeaponVariant(int id)
{ return id == DragonSwordId ? Variant::DragonSword : Variant::SkullAxe; }
constexpr int WeaponId(Variant variant)
{ return variant == Variant::DragonSword ? DragonSwordId : SkullAxeId; }
}
