// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../scenario/core/config.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace csol::scenario::skills
{
// Original feature21's anonymous (-1) energy entries. These are not armor or
// invulnerability buffs: one eligible entry absorbs an entire incoming hit.
class EnergyShield
{
public:
    struct Callback
    {
        int kind;
        std::uint64_t identity;
        Callback(int value = 0, std::uint64_t id = 0) : kind(value), identity(id) {}
    };
    struct Retired { Callback callback; bool broken{}; };
    bool Install(int amount, float duration, float now, ConfigError& error,
        Callback callback = {});
    bool Absorb(float& damage, float now, bool& absorbed, ConfigError& error,
        std::vector<Retired>* retired = nullptr);
    void Update(float now, std::vector<Retired>* retired = nullptr);
    float Energy() const noexcept { return m_energy; }
    std::size_t Size() const noexcept { return m_entries.size(); }
private:
    struct Entry { int amount; float expires; Callback callback; };
    std::vector<Entry> m_entries;
    float m_energy{};
};

int SentryShieldCapacity(int difficulty) noexcept;
}
