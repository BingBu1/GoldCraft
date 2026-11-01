// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <span>

namespace csol::scenario
{
// Original107CF3A0 and107CEFD0 are independent ordered reductions. Group1
// adds every live value. Other groups select the minimum +114 and maximum
// +118 independently, including negative values and the first candidate.
template<class Buff>
bool PlayerIncomingDamage(std::span<const Buff> buffs, float now, float damage, float& output)
{
    #pragma clang fp contract(off)
    if (!std::isfinite(now) || !std::isfinite(damage)) return false;
    float reduction{}, amplification{};
    for (std::size_t first = 0; first < buffs.size();)
    {
        const auto group = buffs[first].group;
        const Buff* lowest{};
        const Buff* highest{};
        auto next = first;
        for (; next < buffs.size() && buffs[next].group == group; ++next)
        {
            const auto& buff = buffs[next];
            if (!std::isfinite(buff.expires)) return false;
            if (buff.expires > 0 && now >= buff.expires) continue;
            if (!std::isfinite(buff.incomingDamageRatio) ||
                !std::isfinite(buff.incomingDamageAmplificationRatio)) return false;
            if (group == 1)
            {
                reduction += buff.incomingDamageRatio;
                amplification += buff.incomingDamageAmplificationRatio;
            }
            else
            {
                if (!lowest || !(buff.incomingDamageRatio > lowest->incomingDamageRatio)) lowest = &buff;
                if (!highest || !(highest->incomingDamageAmplificationRatio > buff.incomingDamageAmplificationRatio)) highest = &buff;
            }
        }
        if (group != 1 && lowest)
        {
            reduction += lowest->incomingDamageRatio;
            amplification += highest->incomingDamageAmplificationRatio;
        }
        if (!std::isfinite(reduction) || !std::isfinite(amplification)) return false;
        first = next;
    }
    //114F4798..47E8 stores after each float32 multiplication. Do not merge
    // these into one ratio, clamp them, or query StatBuff conditions here.
    const float reduced = (1.0f + reduction) * damage;
    const float scaled = (1.0f + amplification) * reduced;
    if (!std::isfinite(reduced) || !std::isfinite(scaled)) return false;
    output = scaled;
    return true;
}

// Original107CF620: group1 sums every live row; other groups contribute their
// highest live StatBuff+104. Negative values and totals above100 are retained.
// The owner keeps rows sorted by group, as in the original ordered map.
template<class Buff>
bool PlayerDodgeChance(std::span<const Buff> buffs, float now, float& output)
{
    if (!std::isfinite(now)) return false;
    float sum{};
    for (std::size_t first = 0; first < buffs.size();)
    {
        const auto group = buffs[first].group;
        const Buff* best{};
        auto next = first;
        for (; next < buffs.size() && buffs[next].group == group; ++next)
        {
            const auto& buff = buffs[next];
            if (!std::isfinite(buff.expires) || !std::isfinite(buff.dodgeChance)) return false;
            if (buff.expires > 0 && now >= buff.expires) continue;
            if (group == 1) sum += buff.dodgeChance;
            else if (!best || buff.dodgeChance > best->dodgeChance) best = &buff;
        }
        if (group != 1 && best) sum += best->dodgeChance;
        if (!std::isfinite(sum)) return false;
        first = next;
    }
    output = sum;
    return true;
}
}
