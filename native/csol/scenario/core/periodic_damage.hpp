// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <span>

namespace csol::scenario
{
struct PeriodicDamage
{
    float amount{}, interval{}, radius{};
    bool enabled{};
};

//107D2160 selects one producer per group, including group1. The first live
// row is a candidate even with zero interval; subsequent positive producers
// compete by damage/interval. Equal rates retain the earlier row.
template<class Buff>
bool SelectPeriodicDamage(std::span<const Buff> group, float now, PeriodicDamage& output)
{
    #pragma clang fp contract(off)
    if (!std::isfinite(now)) return false;
    const Buff* best{};
    for (const auto& buff : group)
    {
        if (!std::isfinite(buff.expires)) return false;
        if (buff.expires > 0 && now >= buff.expires) continue;
        if (!std::isfinite(buff.periodicDamage) || !std::isfinite(buff.periodicDamageInterval) ||
            !std::isfinite(buff.periodicDamageRadius)) return false;
        if (!best) { best = &buff; continue; }
        if (!(buff.periodicDamage > 0 && buff.periodicDamageInterval > 0)) continue;
        if (!(best->periodicDamage > 0 && best->periodicDamageInterval > 0) ||
            buff.periodicDamage / buff.periodicDamageInterval > best->periodicDamage / best->periodicDamageInterval)
            best = &buff;
    }
    output = {};
    if (best && best->periodicDamage > 0)
        output = {best->periodicDamage, best->periodicDamageInterval, best->periodicDamageRadius, true};
    return true;
}
}
