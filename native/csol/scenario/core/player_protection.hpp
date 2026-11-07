// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <span>

namespace csol::scenario
{
//114F46CD..4707 runs after the rule/v2C callback and its nonpositive
// damage rejection. Both mask queries cover every live protection row.
template<class Buff>
bool PlayerProtectionDamage(std::span<const Buff> buffs, float now,
    int damageType, float& damage)
{
    bool any{}, allMask1 = true, allMask4 = true;
    for (const auto& buff : buffs)
    {
        if (!buff.protection || !buff.Active(now)) continue;
        any = true;
        allMask1 &= (buff.mask & 1) != 0;
        allMask4 &= (buff.mask & 4) != 0;
    }
    if (!any || (allMask4 && (damageType & 0x01000040))) return true;
    if (!allMask1) return false;
    // Mask1 continues through dodge/modules and the late hit callback with
    // zero damage. It is not an unconditional early rejection.
    damage = 0;
    return true;
}
}
