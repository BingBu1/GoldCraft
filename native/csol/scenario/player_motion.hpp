// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <span>

namespace csol::scenario
{
struct PlayerMotionStatus
{
    int id{};
    float until{}, speed{}, gravity{-9999.0f};
    int tag{-1}, group{-1};
};

// Player v1D0 uses the same base ring as monsters, but ResetMaxSpeed has a
// distinct selection/StatBuff path. In particular, an expired middle entry
// participates until it reaches an end, and equality is not expiration.
class PlayerMotionRing
{
public:
    bool Add(PlayerMotionStatus value)
    {
        const auto old = std::find_if(m_entries.begin(), m_entries.end(), [&](const auto& entry)
        { return entry.id == value.id && ((entry.tag <= 0 && value.tag <= 0) || entry.tag == value.tag); });
        if (old == m_entries.end()) { m_entries.push_back(value); return true; }
        if (!(value.until > old->until)) return false;
        *old = value;
        return true;
    }
    void ExpireEnds(float now)
    {
        while (!m_entries.empty())
        {
            if (now > m_entries.front().until) m_entries.pop_front();
            else if (now > m_entries.back().until) m_entries.pop_back();
            else break;
        }
    }
    void Remove(int id)
    {
        // Original explicit removal marks records, then calls ResetMaxSpeed.
        for (auto& entry : m_entries) if (entry.id == id) entry.until = -1;
    }
    void Clear() { m_entries.clear(); }
    bool Empty() const { return m_entries.empty(); }
    bool Contains(int id) const
    { return std::any_of(m_entries.begin(), m_entries.end(), [id](const auto& entry) { return entry.id == id; }); }
    bool AnyActive(float now) const
    { return std::any_of(m_entries.begin(), m_entries.end(), [now](const auto& entry) { return entry.until > now; }); }
    bool HasExpiredEnd(float now) const
    { return !m_entries.empty() && (now > m_entries.front().until || now > m_entries.back().until); }
    PlayerMotionStatus Select(float base) const
    {
        PlayerMotionStatus selected{0, 0, base, 1.0f, -1, -1};
        // Mode15 rules.v400 is the verified true/no-modification callback.
        for (const auto& entry : m_entries)
            if (entry.group > selected.group ||
                (entry.group == selected.group && selected.speed >= entry.speed)) selected = entry;
        return selected;
    }
private:
    std::deque<PlayerMotionStatus> m_entries;
};

// StatBuff +A8 is a classification threshold, not damage protection (+mask)
// or knockback immunity (+AC). This query does not evaluate conditions.
template<class Buff>
bool PlayerSlowImmune(std::span<const Buff> buffs, float now, int classification)
{
    return std::any_of(buffs.begin(), buffs.end(), [&](const auto& buff)
    { return buff.Active(now) && buff.slowImmunity >= classification && buff.slowImmunity > -1; });
}

// MP107D03A0: group1 multiplies each entry; other groups choose highest B0,
// then largest C0 at equal priority. Neither the ratio nor the result clamps.
template<class Buff>
bool PlayerMotionRatio(std::span<const Buff> buffs, float now, int priority, float& ratio,
    int Buff::* priorityField, float Buff::* resistanceField)
{
    if (!std::isfinite(now) || !std::isfinite(ratio)) return false;
    const auto value = [=](const Buff& buff, int minimum)
    { return buff.*priorityField >= minimum ? buff.*resistanceField : 0.0f; };
    for (std::size_t first = 0; first < buffs.size();)
    {
        const int group = buffs[first].group;
        const Buff* selected{};
        auto next = first;
        for (; next < buffs.size() && buffs[next].group == group; ++next)
        {
            const auto& buff = buffs[next];
            if (!std::isfinite(buff.expires) || !std::isfinite(buff.*resistanceField)) return false;
            if (!buff.Active(now)) continue;
            if (group == 1) ratio *= 1.0f - value(buff, priority);
            else if (!selected || buff.*priorityField > selected->*priorityField ||
                (buff.*priorityField == selected->*priorityField && value(buff, -1) > value(*selected, -1))) selected = &buff;
        }
        if (group != 1 && selected) ratio *= 1.0f - value(*selected, priority);
        if (!std::isfinite(ratio)) return false;
        first = next;
    }
    return true;
}

template<class Buff>
bool PlayerSlowRatio(std::span<const Buff> buffs, float now, int priority, float& ratio)
{ return PlayerMotionRatio(buffs, now, priority, ratio, &Buff::slowPriority, &Buff::slowResistance); }

// MP107CF9F0 is the same aggregation over independent +B8/+C4 fields.
template<class Buff>
bool PlayerGravityRatio(std::span<const Buff> buffs, float now, int priority, float& ratio)
{ return PlayerMotionRatio(buffs, now, priority, ratio, &Buff::gravityPriority, &Buff::gravityResistance); }

inline float PlayerStatusSpeed(float base, float cap, float ratio)
{
    if (cap < 1) return cap;
    if (ratio < 1) return base - (base - cap) * ratio;
    if (ratio > 1) return cap * (1.0f - (ratio - 1.0f) * ((base - cap) / base));
    return cap;
}
}
