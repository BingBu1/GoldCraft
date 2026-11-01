// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <span>

namespace csol::scenario
{
// 107CFE40 walks StatBuff groups in order. Group1 multiplies every active
// entry; other groups select priority first, then resistance at equal priority.
// The selected entry is subsequently compared with the request's priority.
// Rows are already grouped by the authoritative StatBuff owner.
template<class Buff>
bool PlayerImpulseRatio(std::span<const Buff> buffs, float now, int priority,
    float initial, float& output)
{
    if (!std::isfinite(now) || !std::isfinite(initial)) return false;
    float ratio = initial;
    const auto active = [now](const Buff& buff)
    { return !(buff.expires > 0 && now >= buff.expires); };
    const auto value = [](const Buff& buff, int minimum)
    { return buff.knockbackPriority >= minimum ? buff.knockbackResistance : 0.0f; };
    for (std::size_t first = 0; first < buffs.size();)
    {
        const int group = buffs[first].group;
        const Buff* selected{};
        auto next = first;
        for (; next < buffs.size() && buffs[next].group == group; ++next)
        {
            const auto& buff = buffs[next];
            if (!std::isfinite(buff.expires) || !std::isfinite(buff.knockbackResistance)) return false;
            if (!active(buff)) continue;
            if (group == 1) ratio *= 1.0f - value(buff, priority);
            else if (!selected || buff.knockbackPriority > selected->knockbackPriority ||
                (buff.knockbackPriority == selected->knockbackPriority &&
                 value(buff, -1) > value(*selected, -1))) selected = &buff;
        }
        if (group != 1 && selected) ratio *= 1.0f - value(*selected, priority);
        if (!std::isfinite(ratio)) return false;
        first = next;
    }
    output = ratio;
    return true;
}

// 114DF520 recognizes action constraints, independently of HUD status labels.
constexpr bool ActionBlocksPlayerImpulse(int type) noexcept
{
    return type == 28 || type == 34 || type == 38 || type == 57;
}
}
