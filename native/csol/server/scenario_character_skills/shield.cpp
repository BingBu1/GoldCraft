// SPDX-License-Identifier: GPL-3.0-or-later
#include "shield.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace csol::scenario::skills
{
int SentryShieldCapacity(int difficulty) noexcept
{
    // 106F0428 switch and 100445C0..100447A0 float32 constructors.
    constexpr int values[] = {800, 800, 1500, 2000, 2500, 2700, 2700};
    return difficulty >= 1 && difficulty <= 7 ? values[difficulty - 1] : 2700;
}

bool EnergyShield::Install(int amount, float duration, float now, ConfigError& error,
    Callback callback)
{
    const float expires = now + duration;
    const float total = m_energy + static_cast<float>(amount);
    if (!std::isfinite(now) || !std::isfinite(duration) || !std::isfinite(expires) ||
        !std::isfinite(total))
    { error.message = "character energy shield installation is invalid"; return false; }
    // 1079919D skips identity lookup for -1: each activation adds an entry.
    m_entries.push_back({amount, expires, callback});
    m_energy = total;
    return true;
}

void EnergyShield::Update(float now, std::vector<Retired>* retired)
{
    // 107994E9 uses strict >, so an equal deadline remains in the display
    // total until the following update, but Absorb already rejects it.
    std::erase_if(m_entries, [now, retired](const Entry& entry)
    {
        if (!(now > entry.expires)) return false;
        if (retired && entry.callback.kind) retired->push_back({entry.callback, false});
        return true;
    });
    m_energy = 0;
    for (const auto& entry : m_entries) m_energy += static_cast<float>(entry.amount);
}

bool EnergyShield::Absorb(float& damage, float now, bool& absorbed, ConfigError& error,
    std::vector<Retired>* retired)
{
    absorbed = false;
    if (!std::isfinite(now) || !std::isfinite(damage))
    { error.message = "character energy shield damage is invalid"; return false; }
    auto selected = m_entries.end();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        if (it->expires > now && it->amount > 0 &&
            (selected == m_entries.end() || it->expires < selected->expires)) selected = it;
    if (selected == m_entries.end()) return true;
    const float available = static_cast<float>(selected->amount);
    const float remaining = available - damage;
    const double truncated = std::trunc(static_cast<double>(remaining));
    if (!std::isfinite(remaining) || truncated < std::numeric_limits<int>::min() ||
        truncated > std::numeric_limits<int>::max())
    { error.message = "character energy shield capacity is out of range"; return false; }
    m_energy -= std::min(available, damage);
    selected->amount = static_cast<int>(truncated);
    damage = 0;
    absorbed = true;
    // The breaking hit does not leak its excess into health or a second
    // shield. 1079975F zeroes damage before removing a depleted entry.
    if (selected->amount <= 0)
    {
        if (retired && selected->callback.kind) retired->push_back({selected->callback, true});
        m_entries.erase(selected);
    }
    return true;
}
}
