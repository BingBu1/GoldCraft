// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "melee_attacks.hpp"
#include <algorithm>

namespace csol::melee {
// The stock weapon delta transmits m_iWeaponState (7 unsigned bits), fuser1
// (signed22, scale1000) and fuser2 (signed22, scale128). It does not transmit
// iuser2/iuser3. Keep serial/animation/parity inside actual wire fields.
struct Prediction {
    std::uint32_t primaryOrdinal{};
    std::uint16_t serial{};
    int pendingAnimation{};
    float pendingTime{-1};
};
template<class Data> Prediction ReadPrediction(const Data& data)
{
    return {std::uint32_t((data.m_iWeaponState >> 4) & 1), std::uint16_t(data.fuser2 * 8),
        data.m_iWeaponState & 15, data.fuser1 / 8};
}
template<class Data> void WritePrediction(const Prediction& state, Data& data)
{
    data.m_iWeaponState = int((state.primaryOrdinal & 1) << 4) | state.pendingAnimation;
    data.fuser2 = state.serial / 8.f;
    data.fuser1 = state.pendingTime * 8;
}
inline Attack PendingAttack(Variant variant, int animation)
{
    const Button button = (variant == Variant::DragonSword ? animation == 4 : animation == 2)
        ? Button::Secondary : Button::Primary;
    return BeginAttack(variant, button, variant == Variant::DragonSword && animation == 2 ? 1 : 0);
}
}
