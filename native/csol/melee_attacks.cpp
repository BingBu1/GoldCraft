// SPDX-License-Identifier: GPL-3.0-or-later
#include "melee_attacks.hpp"

namespace csol::melee {

Attack BeginAttack(Variant variant, Button button, std::uint32_t primaryOrdinal)
{
    // MP 39f962f3: CKnifeHelperDragonSword 110B6490/110B6670;
    // CKnifeHelperSkullAxe 110D2540/110D26C0. Base, unenhanced variants.
    if (variant == Variant::DragonSword) {
        if (button == Button::Primary) {
            const bool second = (primaryOrdinal & 1U) != 0;
            return {variant, button, second ? 2 : 1, second ? .3f : .6f,
                70, 30, {1.25f, 1.25f, 1.25f}};
        }
        return {variant, button, 4, .66f, 110, 120, {1.65f, 1.65f, 1.65f}};
    }
    if (button == Button::Primary)
        return {variant, button, 8, 1, 95, 90, {1.4f, 1.4f, 1.4f}};
    return {variant, button, 2, 1, 78, 200, {1.4f, 1.4f, 1.4f}};
}

Completion CompleteAttack(const Attack& attack, Contact contact)
{
    Completion result;
    if (attack.variant == Variant::DragonSword) {
        result.playerAttackAnimation = true;
        if (attack.button == Button::Primary) {
            const float wait = contact == Contact::Damageable ? 1.2f : 1.25f;
            // The original completion always subtracts Param_1 (.6), even
            // when slash2 was scheduled with Param_2 (.3): 110B782F/7D10/7F14.
            result.remaining = {wait - .6f, wait - .6f, wait};
            if (attack.animation == 1) result.dragonEffect = 0;
        } else {
            result.remaining = {1.65f - .66f, 1.65f - .66f, 1.65f};
            result.animation = 5;
            result.dragonEffect = 2;
        }
        return result;
    }

    result.remaining = {1.4f - 1.0f, 1.4f - 1.0f, 1.4f};
    if (attack.button == Button::Primary) {
        // Both flesh and a world strike select slash1; empty space selects
        // stab_miss (110D3132/110D34F2, fraction==1 takes 110D3376).
        result.animation = contact == Contact::None ? 5 : 1;
    }
    return result;
}

float BaseDamage(const Damage& damage, Button button, bool fastPrimary)
{
    if (button == Button::Secondary) return damage.secondary;
    return fastPrimary ? damage.primaryFast : damage.primaryFull;
}

} // namespace csol::melee
